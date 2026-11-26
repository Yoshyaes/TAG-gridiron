#include "Booth/TagBooth.h"
#include "Audio/TagAudio.h"
#include "Game/TagRecent.h"

#include "Async/Async.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundWaveProcedural.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// the speech service lives for the whole game session (its models take ~45 s to load): one per process
	FProcHandle GBoothService;
	bool GBoothServiceStarted = false;

	FString Utf8(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

	// the commentary volume (Settings), in quarters; read from GameUserSettings the first time it is asked for
	int32 GCommentary = 4;
	bool GCommentaryRead = false;
	const TCHAR* const kCommentaryKey = TEXT("VolumeCommentary");

	FString JsonEsc(const FString& S)
	{
		FString O;
		O.Reserve(S.Len() + 8);
		for (const TCHAR C : S)
		{
			if (C == TEXT('"') || C == TEXT('\\')) { O.AppendChar(TEXT('\\')); O.AppendChar(C); }
			else if (C == TEXT('\n')) O += TEXT("\\n");
			else if (C < 32) O.AppendChar(TEXT(' '));
			else O.AppendChar(C);
		}
		return O;
	}

	/** Where the voice cache (Saved/BoothVoice) and the speech service (Tools/Booth) are. A packaged build has neither
	 *  beside its exe: -TagBoothCache=<dir> / -TagBoothTools=<dir>, or [TAG.Booth] CacheDir= / ToolsDir= in Game.ini
	 *  (a packaged build's Saved/Config/Windows/Game.ini), point at the dev tree's. Absent: the project's own, as always. */
	FString BoothDir(const TCHAR* Flag, const TCHAR* IniKey, const FString& Default)
	{
		FString Dir;
		if (FParse::Value(FCommandLine::Get(), Flag, Dir) && !Dir.IsEmpty()) return FPaths::ConvertRelativePathToFull(Dir);
		if (GConfig && GConfig->GetString(TEXT("TAG.Booth"), IniKey, Dir, GGameIni) && !Dir.IsEmpty()) return FPaths::ConvertRelativePathToFull(Dir);
		return FPaths::ConvertRelativePathToFull(Default);
	}
	FString BoothCacheDir() { return BoothDir(TEXT("TagBoothCache="), TEXT("CacheDir"), FPaths::ProjectSavedDir() / TEXT("BoothVoice")); }
	FString BoothToolsDir() { return BoothDir(TEXT("TagBoothTools="), TEXT("ToolsDir"), FPaths::ProjectDir() / TEXT("Tools/Booth")); }
}

int32 ATagBooth::CommentaryLevel()
{
	if (!GCommentaryRead && GConfig)
	{
		GCommentaryRead = true;
		GConfig->GetInt(TEXT("TAG.Settings"), kCommentaryKey, GCommentary, GGameUserSettingsIni);
		GCommentary = FMath::Clamp(GCommentary, 0, 4);
	}
	return GCommentary;
}

void ATagBooth::StepCommentaryLevel()
{
	GCommentary = (CommentaryLevel() + 4) % 5; // down a quarter a press, round to full again (as the other volumes)
	if (GConfig && !FParse::Param(FCommandLine::Get(), TEXT("TagMenuAudio"))) // (the scripted walk through the menus keeps nothing)
	{
		GConfig->SetInt(TEXT("TAG.Settings"), kCommentaryKey, GCommentary, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	// what the new level sounds like: the settings' click at it
	ATagAudio::Ui(TEXT("UiValue"), 1.f, 0.9f + 0.05f * GCommentary);
}

float ATagBooth::VoiceGain() const
{
	// Master is the sound actor's (it reads the settings as it starts, and the settings screen steps it); with no sound
	// actor in the world (-TagNoAudio) the settings file's own
	int32 Master = 4;
	if (ATagAudio::Get(this)) Master = ATagAudio::Level(0);
	else if (GConfig)
	{
		GConfig->GetInt(TEXT("TAG.Settings"), TEXT("VolumeMaster"), Master, GGameUserSettingsIni);
		Master = FMath::Clamp(Master, 0, 4);
	}
	return (Master / 4.f) * (CommentaryLevel() / 4.f);
}

ATagBooth::ATagBooth()
{
	PrimaryActorTick.bCanEverTick = false; // the director feeds it (Feed), on the presentation's own clock
	Voice = CreateDefaultSubobject<UAudioComponent>(TEXT("BoothVoice"));
	Voice->bAutoActivate = false;
	Voice->bAllowSpatialization = false;
	Voice->bIsUISound = false; // the pause menu holds the voices with the game
	RootComponent = Voice;
}

void ATagBooth::BeginPlay()
{
	Super::BeginPlay();
	// (not in the constructor: the class default object is built at module load in every process, and must not
	// start a speech service or put a caption on the screen)
	const TCHAR* Cmd = FCommandLine::Get();
	bMute = FParse::Param(Cmd, TEXT("TagBoothMute"));
	bLog = FParse::Param(Cmd, TEXT("TagBoothLog"));
	bNoWriter = FParse::Param(Cmd, TEXT("TagNoWriter"));
	const uint32 Seed = static_cast<uint32>(FPlatformTime::Cycles64() & 0x7fffffff) | 1u;
	uint32 Fixed = 0;
	Core = MakeUnique<tag::booth::Booth>(FParse::Value(Cmd, TEXT("TagBoothSeed="), Fixed) ? Fixed : Seed);
	// what the booth said in earlier games (Game/TagRecent.h, saved in GameUserSettings [TAG.Recent] booth.*): the next
	// game picks the wordings heard least recently. -TagBoothNoMemory: every game picks as if it were the first
	Core->enableMemory(!FParse::Param(Cmd, TEXT("TagBoothNoMemory")));
	// -TagBoothRef: the referee announces each flag at his microphone in his own voice (Booth::enableReferee; the speech
	// service's "ref" voice), and the stadium PA's stock referee sample stays quiet (ATagAudio::BoothReferee). Off by default
	Core->enableReferee(FParse::Param(Cmd, TEXT("TagBoothRef")));
	CacheDir = BoothCacheDir();
	ReqDir = CacheDir / TEXT("req");
	IFileManager::Get().MakeDirectory(*ReqDir, true);
	if (!bMute) StartService();
	// the caption: a broadcast closed-caption box, bottom center, above the ticker
	if (GEngine && GEngine->GameViewport)
	{
		const FSlateFontInfo WhoFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);
		const FSlateFontInfo TextFont = FCoreStyle::GetDefaultFontStyle("Regular", 13); // (19 pt over 1100 px covered the play)
		SAssignNew(CaptionRoot, SBox)
			.HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(120.f, 0.f, 120.f, 118.f))
			.Visibility_Lambda([this] { return bCaptions && CaptionAge < 1.6f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(SBox).MaxDesiredWidth(720.f)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.01f, 0.015f, 0.03f, 0.62f))
					.Padding(FMargin(12.f, 5.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[ SAssignNew(CaptionWho, STextBlock).Font(WhoFont).ColorAndOpacity(FLinearColor(0.98f, 0.78f, 0.25f)) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
						[ SAssignNew(CaptionText, STextBlock).Font(TextFont).ColorAndOpacity(FLinearColor(0.97f, 0.97f, 0.98f)).AutoWrapText(true) ]
					]
				]
			];
		GEngine->GameViewport->AddViewportWidgetContent(CaptionRoot.ToSharedRef(), 20);
	}
}

void ATagBooth::Tick(float Dt) { Super::Tick(Dt); }

void ATagBooth::EndPlay(const EEndPlayReason::Type Reason)
{
	if (LiveLines > 0)
		UE_LOG(LogTemp, Display, TEXT("TAG booth live calls %d, voiced %d (%.0f%%), %d of them by the generic stand-in"), LiveLines, LiveVoiced,
			100.0 * LiveVoiced / LiveLines, AltLines);
	Silence();
	if (Core && Core->memoryOn()) tag::Recent::get().flush(); // (what this match said, kept for the next one)
	if (bLiveFlag) IFileManager::Get().Delete(*(CacheDir / TEXT("_live")), false, true, true);
	bLiveFlag = false;
	if (CaptionRoot && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(CaptionRoot.ToSharedRef());
	CaptionRoot.Reset();
	// the service goes with the game, not with the match (the next match uses the voices already loaded)
	if ((Reason == EEndPlayReason::Quit || Reason == EEndPlayReason::EndPlayInEditor) && GBoothServiceStarted && GBoothService.IsValid())
	{
		FPlatformProcess::TerminateProc(GBoothService, true);
		FPlatformProcess::CloseProc(GBoothService);
		GBoothServiceStarted = false;
	}
	Super::EndPlay(Reason);
}

void ATagBooth::Silence()
{
	if (Voice) Voice->Stop();
	if (Core && CurId) Core->finished(CurId);
	if (bPlaying) TellMix(-1, false);
	CurId = 0;
	bPlaying = bWaitingAudio = false;
	DropClipWait();
	FadeT = -1.f;
	CaptionAge = 99.f;
}

bool ATagBooth::IsTalk(tag::booth::Kind K)
{
	// the crowd gives way well under talk; a call of the action (a live call, a result) by its energy
	using KK = tag::booth::Kind;
	return K != KK::Live && K != KK::Result;
}

void ATagBooth::TellMix(int32 Energy, bool bTalk) const
{
	if (ATagAudio* A = ATagAudio::Get(this)) A->BoothLine(Energy, bTalk);
}

bool ATagBooth::HasClip(const FString& Key)
{
	if (Present.Contains(Key)) return true;
	if (!FPaths::FileExists(CacheDir / (Key + TEXT(".wav")))) return false;
	Present.Add(Key);
	return true;
}

FString ATagBooth::LineKey(const tag::booth::Line& L)
{
	// FNV-1a 64 over "voice|energy|say" (booth_tts.py line_key computes the same)
	// ("ref": the referee at his microphone, -TagBoothRef; the two broadcasters' keys are as they always were)
	const char* VoiceName = L.who == tag::booth::Who::PBP ? "pbp" : L.who == tag::booth::Who::Ref ? "ref" : "color";
	const std::string S = std::string(VoiceName) + "|" + std::to_string(L.energy) + "|" + L.say;
	uint64 H = 0xcbf29ce484222325ull;
	for (const unsigned char C : S)
	{
		H ^= C;
		H *= 0x100000001b3ull;
	}
	return FString::Printf(TEXT("%hs%d_%016llx"), VoiceName, L.energy, H);
}

bool ATagBooth::ServiceAlive() const
{
	const FDateTime T = IFileManager::Get().GetTimeStamp(*(CacheDir / TEXT("_alive")));
	return T != FDateTime::MinValue() && (FDateTime::UtcNow() - T).GetTotalSeconds() < 10.0; // (a loaded machine can stall the beat a few seconds)
}

void ATagBooth::WarmService()
{
	// the director calls this at boot: the voices load (~45 s) while the menus are up, not after the kickoff
	// only for a game a person (or a booth test) will watch: the capture and test modes run without the voices
	const TCHAR* Cmd = FCommandLine::Get();
	if (FParse::Param(Cmd, TEXT("TagNoBooth")) || FParse::Param(Cmd, TEXT("TagBoothMute"))) return;
	const bool bTagMode = FCString::Stristr(Cmd, TEXT("-Tag")) != nullptr;
	const bool bWatched = FParse::Param(Cmd, TEXT("TagPlayNow")) || FParse::Param(Cmd, TEXT("TagAutoWatch")) || FParse::Param(Cmd, TEXT("TagBoothLog")) ||
		FCString::Stristr(Cmd, TEXT("-TagSoak=")) != nullptr;
	if (bTagMode && !bWatched) return;
	ATagBooth* Cdo = GetMutableDefault<ATagBooth>();
	Cdo->CacheDir = BoothCacheDir();
	Cdo->ReqDir = Cdo->CacheDir / TEXT("req");
	IFileManager::Get().MakeDirectory(*Cdo->ReqDir, true);
	Cdo->StartService();
}

void ATagBooth::StartService()
{
	const FString Tools = BoothToolsDir();
	const FString Python = Tools / TEXT(".venv/Scripts/python.exe");
	const FString Script = Tools / TEXT("booth_tts.py");
	if (!FPaths::FileExists(Python) || !FPaths::FileExists(Script))
	{
		UE_LOG(LogTemp, Display, TEXT("TAG booth: no speech service (%s missing): captions only. Tools/Booth/setup_booth.bat installs it."), *Python);
		bService = false;
		return;
	}
	bService = true;
	if (ServiceAlive() || GBoothServiceStarted) return;
	// -TagBoothEngine=kokoro: the light voice (a fraction of the GPU; flatter delivery). Turbo by default.
	FString Engine;
	FParse::Value(FCommandLine::Get(), TEXT("TagBoothEngine="), Engine);
	FString EngineArg = Engine.IsEmpty() ? FString() : FString::Printf(TEXT(" --engine %s"), *Engine);
	// The writer (exchanges, break packages) is opt-in only: -TagBoothWriter=claude:<id> (Claude API, billed per call) or an
	// Ollama model (costs the game frames). Fred paused it over cost (2026-11-13): a key being set no longer turns it on.
	FString WriterModel;
	FParse::Value(FCommandLine::Get(), TEXT("TagBoothWriter="), WriterModel);
	if (FParse::Param(FCommandLine::Get(), TEXT("TagNoWriter"))) WriterModel.Reset();
	if (!WriterModel.IsEmpty()) EngineArg += FString::Printf(TEXT(" --writer %s"), *WriterModel);
	const FString Args = FString::Printf(TEXT("\"%s\" --cache \"%s\" --parent %u%s"), *Script, *CacheDir, FPlatformProcess::GetCurrentProcessId(), *EngineArg);
	GBoothService = FPlatformProcess::CreateProc(*Python, *Args, false, true, true, nullptr, 0, *Tools, nullptr);
	GBoothServiceStarted = GBoothService.IsValid();
	UE_LOG(LogTemp, Display, TEXT("TAG booth: speech service %s (%s)"), GBoothServiceStarted ? TEXT("started") : TEXT("failed to start"), *Python);
}

void ATagBooth::DropClipWait()
{
	++ClipSerial; // (a read still out answers with the old serial, and is thrown away)
	bClipLoading = bClipReady = bClipLast = false;
	ClipPollT = 0.f;
	ReadyClip.Pcm.Reset();
}

void ATagBooth::StartClipLoad()
{
	// the stat, the read and the parse happen on a background thread; the samples come back to the game thread
	bClipLoading = true;
	ClipPollT = 0.1f;
	const uint32 Serial = ClipSerial;
	const TWeakObjectPtr<ATagBooth> Weak(this);
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Weak, Serial, Path = CacheDir / (CurKey + TEXT(".wav"))]()
	{
		FClip Clip;
		const bool bOk = LoadClip(Path, Clip);
		AsyncTask(ENamedThreads::GameThread, [Weak, Serial, bOk, Clip = MoveTemp(Clip)]() mutable
		{
			if (ATagBooth* Self = Weak.Get()) Self->ClipLoaded(Serial, bOk, MoveTemp(Clip));
		});
	});
}

void ATagBooth::ClipLoaded(uint32 Serial, bool bOk, FClip&& C)
{
	if (Serial != ClipSerial) return; // for a line since dropped (or already carried by its caption)
	bClipLoading = false;
	if (bOk && bWaitingAudio && CurId)
	{
		ReadyClip = MoveTemp(C);
		bClipReady = true;
	}
}

bool ATagBooth::LoadClip(const FString& Path, FClip& Out)
{
	TArray<uint8> B;
	if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToArray(B, *Path) || B.Num() < 44) return false;
	auto U32 = [&B](int32 At) { return static_cast<uint32>(B[At]) | (static_cast<uint32>(B[At + 1]) << 8) | (static_cast<uint32>(B[At + 2]) << 16) | (static_cast<uint32>(B[At + 3]) << 24); };
	auto U16 = [&B](int32 At) { return static_cast<uint16>(B[At] | (B[At + 1] << 8)); };
	if (FMemory::Memcmp(B.GetData(), "RIFF", 4) != 0 || FMemory::Memcmp(B.GetData() + 8, "WAVE", 4) != 0) return false;
	int32 At = 12, Channels = 1, Bits = 16;
	while (At + 8 <= B.Num())
	{
		const uint32 Len = U32(At + 4);
		if (FMemory::Memcmp(B.GetData() + At, "fmt ", 4) == 0 && At + 24 <= B.Num())
		{
			Channels = U16(At + 10);
			Out.Rate = static_cast<int32>(U32(At + 12));
			Bits = U16(At + 22);
		}
		else if (FMemory::Memcmp(B.GetData() + At, "data", 4) == 0)
		{
			const int32 N = FMath::Min(static_cast<int32>(Len), B.Num() - (At + 8));
			if (Channels != 1 || Bits != 16 || N <= 0) return false;
			Out.Pcm.SetNumUninitialized(N);
			FMemory::Memcpy(Out.Pcm.GetData(), B.GetData() + At + 8, N);
			Out.Secs = static_cast<float>(N / 2) / static_cast<float>(FMath::Max(1, Out.Rate));
			// its loudest sample (the energy's lift stops before that passes the ceiling)
			int32 Loudest = 0;
			const int16* Samples = reinterpret_cast<const int16*>(Out.Pcm.GetData());
			for (int32 i = 0; i < N / 2; ++i) Loudest = FMath::Max(Loudest, FMath::Abs(static_cast<int32>(Samples[i])));
			Out.Peak = static_cast<float>(Loudest) / 32768.f;
			return true;
		}
		At += 8 + static_cast<int32>(Len + (Len & 1));
	}
	return false;
}

void ATagBooth::Request(const tag::booth::Line& L, int32 Prio)
{
	if (!bService || bMute) return;
	const FString Key = LineKey(L);
	// what is already known comes first: a line is looked for on the disk once per priority, and never again once found
	if (Present.Contains(Key)) return;
	const FString AskKey = Key + FString::Printf(TEXT("#%d"), Prio);
	if (Asked.Contains(AskKey)) return;
	if (FPaths::FileExists(CacheDir / (Key + TEXT(".wav")))) { Present.Add(Key); return; }
	Asked.Add(AskKey);
	const FString Json = FString::Printf(TEXT("{\"key\":\"%s\",\"text\":\"%s\",\"voice\":\"%s\",\"energy\":%d,\"prio\":%d,\"t\":%.3f}"), *Key, *JsonEsc(Utf8(L.say)),
		L.who == tag::booth::Who::PBP ? TEXT("pbp") : L.who == tag::booth::Who::Ref ? TEXT("ref") : TEXT("color"), L.energy, Prio, FPlatformTime::Seconds());
	FFileHelper::SaveStringToFile(Json, *(ReqDir / (Key + TEXT(".json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void ATagBooth::Prewarm(const tag::Game& G)
{
	if (!bService || bMute) return;
	// this match's regulars first (their live calls), then the calls that name nobody; both once a match (the
	// cache keeps every line across sessions, so the next game with these teams starts warm)
	if (!bGenericsAsked)
	{
		bGenericsAsked = true;
		// (some 500 lines: which of them have audio already is found on a background thread, not in the kickoff's frame;
		// the ones that do not are then asked for a few a frame, in this order - Feed)
		std::vector<std::pair<std::string, int>> Calls = Core->rosterCalls(G);
		const std::vector<std::pair<std::string, int>> Generic = tag::booth::Booth::genericCalls();
		Calls.insert(Calls.end(), Generic.begin(), Generic.end());
		const TWeakObjectPtr<ATagBooth> Weak(this);
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Weak, Dir = CacheDir, Calls = MoveTemp(Calls)]()
		{
			std::vector<tag::booth::Line> Missing;
			TArray<FString> Have;
			for (const std::pair<std::string, int>& T : Calls)
			{
				tag::booth::Line L;
				L.who = tag::booth::Who::PBP;
				L.energy = T.second;
				L.say = tag::booth::Booth::spoken(T.first);
				const FString Key = LineKey(L);
				if (FPaths::FileExists(Dir / (Key + TEXT(".wav")))) Have.Add(Key);
				else Missing.push_back(std::move(L));
			}
			AsyncTask(ENamedThreads::GameThread, [Weak, Missing = MoveTemp(Missing), Have = MoveTemp(Have)]() mutable
			{
				if (ATagBooth* Self = Weak.Get())
				{
					Self->Present.Append(Have);
					Self->WarmMissing = MoveTemp(Missing);
					Self->WarmAt = 0;
				}
			});
		});
	}
	// what is queued and the live calls this play could need: ahead of the generic ones (the service takes the newest
	// "soon" request first, so the line just queued is made before the plan of the play). Walked only when it has
	// changed: every line of an unchanged forecast has been asked for (or found) already
	const uint64 Stamp = Core->forecastStamp();
	if (bForecastSeen && Stamp == ForecastStamp) return;
	bForecastSeen = true;
	ForecastStamp = Stamp;
	for (const tag::booth::Line& L : Core->forecast()) Request(L, 1);
}

void ATagBooth::ShowCaption(const tag::booth::Line* L)
{
	if (!L || !CaptionText || !CaptionWho) return;
	const tag::booth::Crew& C = Core->crew();
	static const std::string RefName = "Referee";
	const std::string& Who = L->who == tag::booth::Who::PBP ? C.pbpLast : L->who == tag::booth::Who::Ref ? RefName : C.colorLast;
	CaptionWho->SetText(FText::FromString(Utf8(Who).ToUpper()));
	CaptionText->SetText(FText::FromString(Utf8(L->text)));
	CaptionAge = 0.f;
	if (bLog)
		UE_LOG(LogTemp, Display, TEXT("TAG booth %.1f %s %s e%d%s%s | %s"), Core->now(), ANSI_TO_TCHAR(tag::booth::whoName(L->who)), ANSI_TO_TCHAR(tag::booth::kindName(L->kind)),
			L->energy, bPlaying ? TEXT(" [voice]") : TEXT(" [caption]"), bAltLine ? TEXT(" [alt]") : TEXT(""), *Utf8(L->text));
}

void ATagBooth::Feed(float Dt, const tag::Game& G, const tag::booth::View& V)
{
	if (!Core) return;
	// the speech service keeps the GPU to the game while the ball is live (a flag file it checks between jobs)
	const bool bLiveNow = V.stage == tag::booth::View::Stage::Live || V.stage == tag::booth::View::Stage::Kick;
	if (bService && !bMute && bLiveNow != bLiveFlag)
	{
		bLiveFlag = bLiveNow;
		const FString Flag = CacheDir / TEXT("_live");
		if (bLiveNow) FFileHelper::SaveStringToFile(TEXT("1"), *Flag);
		else IFileManager::Get().Delete(*Flag, false, true, true);
	}
	Core->tick(Dt, G, V);
	if (CurId == 0 || bPlaying || bWaitingAudio) CaptionAge += Dt;
	if (bPlaying || (!bWaitingAudio && CurId)) CaptionAge = FMath::Min(CaptionAge, 0.f); // held while he talks
	// the line being said
	if (CurId)
	{
		// a volume changed in the settings (the pause menu's) reaches the line already playing
		if (bPlaying && FadeT < 0.f && Voice)
		{
			const float Gain = VoiceGain();
			if (!FMath::IsNearlyEqual(Gain, GainSet))
			{
				GainSet = Gain;
				Voice->SetVolumeMultiplier(Gain * EnergyGain * ATagAudio::MasterTrim());
			}
		}
		if (FadeT < 0.f && Core->cutCurrent())
		{
			if (bPlaying && Voice) Voice->FadeOut(0.15f, 0.f);
			FadeT = 0.15f;
		}
		if (FadeT >= 0.f)
		{
			FadeT -= Dt;
			if (FadeT <= 0.f)
			{
				if (Voice) Voice->Stop();
				Core->finished(CurId);
				if (bPlaying) TellMix(-1, false);
				CurId = 0;
				bPlaying = bWaitingAudio = false;
				DropClipWait();
				FadeT = -1.f;
				CaptionAge = 0.9f;
			}
		}
		else if (bWaitingAudio)
		{
			WaitT += Dt;
			// how long a line may wait for its voice before the caption carries it: a live call is now or never; a result
			// may come a beat after the whistle; the talk between the plays has the hold behind it
			using K = tag::booth::Kind;
			const K Kd = CurLine.kind;
			const float MaxWait = CurLine.live ? 0.3f : Kd == K::Result ? 3.5f : Kd == K::Setup ? 3.f : Kd == K::Analysis || Kd == K::Read ? 3.5f : 5.f;
			// the wav is looked for off the game thread (StartClipLoad), at once when the line comes up and then ten
			// times a second; its samples arrive here
			if (bClipReady)
			{
				const FClip C = MoveTemp(ReadyClip);
				DropClipWait();
				bWaitingAudio = false;
				// (a wave per line, not one kept: a line cut short leaves samples queued in its wave, and the audio
				// thread may still be reading them when the next line starts)
				Wave = NewObject<USoundWaveProcedural>(this);
				Wave->SetSampleRate(C.Rate);
				Wave->NumChannels = 1;
				Wave->Duration = C.Secs;
				Wave->SoundGroup = SOUNDGROUP_Voice;
				Wave->bLooping = false;
				Wave->QueueAudio(C.Pcm.GetData(), C.Pcm.Num());
				Voice->SetSound(Wave);
				// (the settings' Master x Commentary: the voices went out at full whatever the volume was set to), and the
				// line's own lift: an excited call is louder than calm talk (the broadcast mix; 1 with -TagMixLegacy)
				GainSet = VoiceGain();
				EnergyGain = ATagAudio::VoiceEnergyGain(CurLine.energy, C.Peak, GainSet);
				Voice->SetVolumeMultiplier(GainSet * EnergyGain * ATagAudio::MasterTrim()); // (and the mix's master trim, as every sound has)
				Voice->Play();
				TellMix(CurLine.energy, IsTalk(CurLine.kind));
				if (CurLine.live) { ++LiveVoiced; AltLines += bAltLine ? 1 : 0; }
				if (bLog)
					UE_LOG(LogTemp, Display, TEXT("TAG booth voice gain %.2f x %.2f for e%d (peak %.2f; master %d/4, commentary %d/4) key %s%s"), GainSet, EnergyGain, CurLine.energy, C.Peak,
						ATagAudio::Get(this) ? ATagAudio::Level(0) : -1, CommentaryLevel(), *CurKey,
						*(CurLine.live ? FString::Printf(TEXT(", %.0f ms after the call%s"), 1000.0 * (Core->now() - CurLine.notBefore), bAltLine ? TEXT(" (generic stand-in)") : TEXT("")) : FString()));
				bPlaying = true;
				PlayT = 0.f;
				PlaySecs = C.Secs;
				Core->started(CurId, C.Secs);
				ShowCaption(&CurLine);
			}
			else if (bClipLoading)
			{
				// (a look is out: its answer first, a frame or so)
			}
			else if (WaitT > MaxWait && !bClipLast)
			{
				// the wait has run out: one last look, as there was on this frame when the look was made here
				bClipLast = true;
				StartClipLoad();
			}
			else if (WaitT <= MaxWait)
			{
				ClipPollT -= Dt;
				if (ClipPollT <= 0.f) StartClipLoad();
			}
			else
			{
				// too late for its moment: the caption carries it at the booth's pace (the audio stays on order, for next time)
				DropClipWait();
				bWaitingAudio = false;
				PlayT = 0.f;
				PlaySecs = static_cast<float>(CurLine.estSecs);
				Core->started(CurId, CurLine.estSecs);
				ShowCaption(&CurLine);
			}
		}
		else
		{
			PlayT += Dt;
			if (PlayT >= PlaySecs + 0.03f)
			{
				Core->finished(CurId);
				if (bPlaying) TellMix(-1, false);
				CurId = 0;
				bPlaying = false;
				CaptionAge = 0.f;
			}
		}
	}
	if (!CurId)
		if (const tag::booth::Line* L = Core->next())
		{
			CurLine = *L;
			CurId = L->id;
			CurKey = LineKey(*L);
			bAltLine = false;
			if (L->live) ++LiveLines;
			if (!bMute && bService)
			{
				// a live call whose own audio is not made yet (a matchup never played, a man just signed): its generic
				// stand-in ("Caught!" for a named catch) when that is cached, rather than a wait and a silent caption
				if (L->live && !L->alt.empty() && !HasClip(CurKey))
				{
					tag::booth::Line Alt = *L;
					Alt.text = L->alt;
					Alt.say = tag::booth::Booth::spoken(L->alt);
					Alt.energy = L->altEnergy;
					const FString AltKey = LineKey(Alt);
					if (HasClip(AltKey))
					{
						Request(*L, 0); // (the named call on order for next time)
						CurLine = MoveTemp(Alt);
						CurKey = AltKey;
						bAltLine = true;
					}
				}
				// the clip may already be there (made ahead); if not, ask for it now and give it a moment. A live call that
				// was not made ahead is too late to make now (it goes on order for next time, behind the game's frames)
				Request(CurLine, L->live ? 0 : 2);
				bWaitingAudio = true;
				DropClipWait();
				StartClipLoad(); // (made ahead, it is in hand by the next frame)
				WaitT = L->live ? 0.f : 0.f;
				Core->started(CurId, L->estSecs + (L->live ? 0.3 : 5.0));
			}
			else
			{
				bWaitingAudio = false;
				PlayT = 0.f;
				PlaySecs = static_cast<float>(L->estSecs);
				Core->started(CurId, L->estSecs);
				ShowCaption(&CurLine);
			}
		}
	// keep the service in mind (it may have been closed) and ask ahead
	ServiceCheckT += Dt;
	if (bService && !bMute && ServiceCheckT > 5.0)
	{
		ServiceCheckT = 0;
		if (!ServiceAlive() && !(GBoothServiceStarted && FPlatformProcess::IsProcRunning(GBoothService))) { GBoothServiceStarted = false; StartService(); }
	}
	ForecastT -= Dt;
	if (ForecastT <= 0.f)
	{
		ForecastT = 0.1f;
		Prewarm(G);
	}
	// the match's regulars and the generic calls with no audio yet (Prewarm found them on a background thread): asked
	// for a few a frame, in their order (Request does nothing without the service)
	for (int32 Sent = 0; Sent < 4 && WarmAt < WarmMissing.size(); ++Sent) Request(WarmMissing[WarmAt++], 0);
	if (WarmAt > 0 && WarmAt >= WarmMissing.size())
	{
		WarmMissing.clear();
		WarmAt = 0;
	}
	// the writer (a local Ollama model the speech service runs on the CPU): on once the service says the model is there
	WriterCheckT += Dt;
	if (bService && !bMute && !bNoWriter && WriterCheckT > 5.f)
	{
		WriterCheckT = 0.f;
		const bool bReady = FPaths::FileExists(CacheDir / TEXT("_llm_ready"));
		if (bReady != Core->writerOn())
		{
			Core->enableWriter(bReady);
			UE_LOG(LogTemp, Display, TEXT("TAG booth: writer %s"), bReady ? TEXT("on (Ollama)") : TEXT("off"));
		}
	}
	for (const tag::booth::Booth::WriterJob& J : Core->takeWriterJobs())
	{
		const FString Json = FString::Printf(TEXT("{\"id\":%d,\"kind\":\"%s\",\"system\":\"%s\",\"user\":\"%s\",\"facts\":\"%s\"}"), J.id, *Utf8(J.kind),
			*JsonEsc(Utf8(J.system)), *JsonEsc(Utf8(J.user)), *JsonEsc(Utf8(J.facts)));
		FFileHelper::SaveStringToFile(Json, *(ReqDir / FString::Printf(TEXT("llm_%d.json"), J.id)), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		WriterAsked.Add(J.id, FPlatformTime::Seconds());
	}
	for (auto It = WriterAsked.CreateIterator(); It; ++It)
	{
		const FString Out = CacheDir / FString::Printf(TEXT("llm_%d.txt"), It.Key());
		FString Text;
		if (FPaths::FileExists(Out) && FFileHelper::LoadFileToString(Text, *Out))
		{
			Core->writerDone(It.Key(), TCHAR_TO_UTF8(*Text), FPlatformTime::Seconds() - It.Value());
			IFileManager::Get().Delete(*Out, false, true, true);
			It.RemoveCurrent();
		}
		else if (FPlatformTime::Seconds() - It.Value() > 120.0)
		{
			Core->writerDone(It.Key(), std::string(), 120.0);  // (never came: the booth goes on without it)
			It.RemoveCurrent();
		}
	}
}
