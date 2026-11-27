#include "Audio/TagAudio.h"
#include "Render/Coords.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundAttenuation.h"
#include "Kismet/GameplayStatics.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AudioMixerBlueprintLibrary.h"
#include "AudioMixerDevice.h"
#include "Sound/SoundSubmix.h"
#include "SubmixEffects/AudioMixerSubmixEffectDynamicsProcessor.h"
#include "Game/TagResult.h"
#include "AudioDevice.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"

namespace
{
	TWeakObjectPtr<ATagAudio> GTagAudio;
	using EStage = FTagAudioView::EStage;

	// the mix: how loud each crowd layer sits when it is fully up (the assets are all built to one loudness)
	constexpr float kCalm = 1.0f, kBuzz = 0.95f, kRoar = 1.6f, kNoise = 1.35f, kRain = 0.7f, kWind = 0.6f;
	constexpr float kField = 1.0f, kCrowdShot = 0.95f, kHouse = 0.42f, kPa = 0.62f, kMusic = 0.72f, kUi = 0.8f;

	// the broadcast mix (Research/NextLevel/05 A3; T18: the A1 keeps the booth on top of calm play and lets the crowd over
	// the announcers on the big one): how far the crowd's beds and one-shots, the stadium and the music give way to a line
	constexpr float kDuckTalk = 0.562f, kDuckTalkMusic = 0.398f;  // calm talk, and any line at energy 0-1: -5 / -8 dB
	constexpr float kDuckCall = 0.794f, kDuckCallMusic = 0.631f;  // a call of the action at energy 2: -2 / -4 dB
	constexpr float kBloom = 1.12f;                                // the big call over a roaring building: the crowd +1 dB
	constexpr float kDuckOld = 0.74f, kDuckOldMusic = 0.55f;       // -TagMixLegacy: any voice, the beds only
	// the voice by its energy (every clip is levelled to -19 dBFS RMS by booth_tts.py): -2, 0, +2, +4 dB
	constexpr float kVoiceByEnergy[4] = {0.794f, 1.f, 1.259f, 1.585f};
	// the master (A4; T21: consoles -24 +-2 LUFS integrated, true peak -1 dBTP). The summed mix measured -20.4 to -22.2 LUFS
	// with sample peaks to -1.0 dBFS (D:/TAGAudio/work final, final2, run4): trimmed 3.5 dB, then a limiter. The booth's
	// lift stops 2 dB under the ceiling at the output, so a call over the roar leaves the limiter little to do.
	// (measured 2026-11-25, seed 7 with the booth on - the voice is on ~60% of a game and carries the level - untrimmed:
	// -22.0 / -22.1 LUFS, true peak -2.0 / -4.2 dBTP; the trim the engine's limiter was to apply never acted)
	constexpr float kMasterTrimDb = -2.f;
	constexpr float kLimitDb = -2.5f;
	constexpr float kVoiceCeiling = 0.708f;                        // -3 dBFS
	// a bed with several takes (Tools/Audio/build_crowd.py: three of each) moves to another every 2-4 minutes, over a
	// 5 s equal-power crossfade, only while the building is quiet (05 A9: no 44-64 s cycle to hear)
	constexpr float kSwapMin = 120.f, kSwapMax = 240.f, kSwapSecs = 5.f;

	const FName NCalm("CrowdCalm"), NBuzz("CrowdBuzz"), NRoar("CrowdRoar"), NNoise("CrowdNoise"), NRain("WxRain"), NWind("WxWind");
	const FName NCheerBig("CheerBig"), NCheerMed("CheerMed"), NCheerSmall("CheerSmall"), NGroan("Groan"), NGroanBig("GroanBig"), NBoo("Boo"),
		NGasp("Gasp"), NAwayCheer("AwayCheer"), NKickRise("KickRise"), NKickShout("KickShout"), NChant("ChantDefense");
	const FName NHitLight("HitLight"), NHitMed("HitMed"), NHitHeavy("HitHeavy"), NHitHuge("HitHuge"), NLineCrash("LineCrash"), NBodyFall("BodyFall"),
		NCatch("Catch"), NSlap("Slap"), NSnap("Snap"), NThrow("Throw"), NBounce("Bounce"), NKickPunt("KickPunt"), NKickPlace("KickPlace"), NPost("Post"),
		NStep("Step"), NCut("Cut"), NGrunt("Grunt"), NBallOut("BallOut"), NWhistle("Whistle"), NWhistleLong("WhistleLong"), NDefCall("DefCall");
	const FName NHorn("Horn"), NFireworks("Fireworks"), NHouseMusic("HouseMusic"), NPaThird("PaThirdDown"), NPaNoise("PaMakeNoise");
	const FName NMusMenu("MusMenu"), NMusIntro("MusIntro"), NMusOut("MusBumperOut"), NMusIn("MusBumperIn"), NMusHalf("MusHalftime"), NMusFinal("MusFinal"),
		NMusScore("MusScore");
	const FName NUiWhoosh("UiWhoosh"), NUiPause("UiPause"), NUiOpen("UiOpen"), NUiClose("UiClose"), NUiTitle("UiTitle"), NUiStart("UiStart"), NUiValue("UiValue");
	const FName NScrMain("Main"), NScrLobby("Lobby"), NScrTeams("TeamSelect"), NScrPause("Pause"), NScrControls("Controls");
	FName GScreen; // the screen the menus say is up (they may say so before the sound exists)
	// how far into the menus a screen is: forward is heard as one thing, back as another
	int32 ScreenDepth(FName S) { return S.IsNone() ? -1 : S == NScrMain ? 0 : S == NScrLobby || S == NScrPause ? 1 : S == NScrTeams ? 2 : S == NScrControls ? 4 : 3; }

	bool In(const std::string& S, const char* Sub) { return S.find(Sub) != std::string::npos; }
}

ATagAudio::ATagAudio()
{
	PrimaryActorTick.bCanEverTick = false; // the director feeds it
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

ATagAudio* ATagAudio::Get(const UObject* WorldCtx)
{
	ATagAudio* A = GTagAudio.Get();
	return A && WorldCtx && A->GetWorld() == WorldCtx->GetWorld() ? A : nullptr;
}

void ATagAudio::Ui(const UObject* WorldCtx, FName Bank, float Vol)
{
	if (ATagAudio* A = Get(WorldCtx)) A->PlayNow(FLater{0.f, Bank, FVector::ZeroVector, Vol, 1.f, ECat::Ui});
}

namespace
{
	int32 GLevels[3] = {4, 4, 4};
	const TCHAR* const kLevelKeys[3] = {TEXT("VolumeMaster"), TEXT("VolumeMusic"), TEXT("VolumeStadium")};
}

int32 ATagAudio::Level(int32 Which) { return GLevels[FMath::Clamp(Which, 0, 2)]; }

void ATagAudio::StepLevel(int32 Which)
{
	Which = FMath::Clamp(Which, 0, 2);
	GLevels[Which] = (GLevels[Which] + 4) % 5; // down a quarter a press, round to full again
	if (GConfig && !FParse::Param(FCommandLine::Get(), TEXT("TagMenuAudio"))) // (the scripted walk through the menus keeps nothing)
	{
		GConfig->SetInt(TEXT("TAG.Settings"), kLevelKeys[Which], GLevels[Which], GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	if (ATagAudio* A = GTagAudio.Get())
	{
		A->SetLevels(GLevels[0] / 4.f, GLevels[1] / 4.f, GLevels[2] / 4.f);
		// what the new level sounds like: a click at it (the music is its own example); the stadium's, a crowd
		if (Which != 1) A->PlayNow(FLater{0.f, NUiValue, FVector::ZeroVector, 1.f, 0.9f + 0.05f * GLevels[Which], ECat::Ui});
		if (Which == 2 && !A->Game) A->Crowd(NCheerSmall, 0.7f);
	}
}

void ATagAudio::Ui(const TCHAR* Bank, float Vol, float Pitch)
{
	if (ATagAudio* A = GTagAudio.Get()) A->PlayNow(FLater{0.f, FName(Bank), FVector::ZeroVector, Vol, Pitch, ECat::Ui});
}

// ---------------------------------------------------------------------------------------------- the broadcast mix

bool ATagAudio::LegacyMix()
{
	static const bool bOld = FParse::Param(FCommandLine::Get(), TEXT("TagMixLegacy"));
	return bOld;
}

float ATagAudio::MasterTrim()
{
	static const float Trim = []
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("TagNoLimiter"))) return 1.f;
		float Db = kMasterTrimDb;
		FParse::Value(FCommandLine::Get(), TEXT("TagMixTrimDb="), Db);
		return FMath::Pow(10.f, FMath::Clamp(Db, -24.f, 6.f) / 20.f);
	}();
	return Trim;
}

float ATagAudio::VoiceEnergyGain(int32 Energy, float ClipPeak, float Level)
{
	if (LegacyMix()) return 1.f;
	const float G = kVoiceByEnergy[FMath::Clamp(Energy, 0, 3)];
	if (G <= 1.f) return G;
	// (a lift only as far as the clip's peak stays under the ceiling at the output: never below the level it was built at)
	const float Out = FMath::Max(ClipPeak, 0.01f) * FMath::Max(Level, 0.01f) * MasterTrim();
	return FMath::Clamp(kVoiceCeiling / Out, 1.f, G);
}

void ATagAudio::BoothLine(int32 Energy, bool bTalk)
{
	if (Energy == VoiceEnergy && bTalk == bVoiceTalk) return;
	VoiceEnergy = Energy;
	bVoiceTalk = bTalk;
	if (bLog)
	{
		// the fader the line asks for (TickMix rides it there): what was applied is in the mix test's lines
		const float CrowdG = Energy < 0 ? 1.f : bLegacyMix ? kDuckOld : Energy >= 3 || Excite > 0.8f ? (Excite > 0.5f ? kBloom : 1.f) : Energy == 2 && !bTalk ? kDuckCall : kDuckTalk;
		const float MusicG = Energy < 0 ? 1.f : bLegacyMix ? kDuckOldMusic : Energy >= 3 || Excite > 0.8f ? 1.f : Energy == 2 && !bTalk ? kDuckCallMusic : kDuckTalkMusic;
		UE_LOG(LogTemp, Display, TEXT("TAG mix %.2f line e%d %s -> crowd %+.1f dB, music %+.1f dB (excite %.2f)"), FPlatformTime::Seconds() - T0, Energy,
			Energy < 0 ? TEXT("off") : bTalk ? TEXT("talk") : TEXT("call"), 20.f * FMath::LogX(10.f, CrowdG), 20.f * FMath::LogX(10.f, MusicG), Excite);
	}
}

void ATagAudio::StartLimiter()
{
	// the master's trim to the console loudness target and a limiter under the -1 dBTP ceiling (the engine's dynamics
	// processor on the main submix: peak detection linked across the channels, a fast digital attack, look-ahead so the
	// gain is down before the peak arrives, a hard knee, a slow enough release not to pump the crowd)
	if (FParse::Param(FCommandLine::Get(), TEXT("TagNoLimiter")))
	{
		UE_LOG(LogTemp, Display, TEXT("TAG audio: no master limiter (-TagNoLimiter)"));
		return;
	}
	Limiter = NewObject<USubmixEffectDynamicsProcessorPreset>(this);
	FSubmixEffectDynamicsProcessorSettings S;
	S.DynamicsProcessorType = ESubmixEffectDynamicsProcessorType::Limiter;
	S.PeakMode = ESubmixEffectDynamicsPeakMode::Peak;
	S.LinkMode = ESubmixEffectDynamicsChannelLinkMode::Peak;
	S.InputGainDb = 0.f; // (the trim is in the mix's own gains: SetLevels)
	float LimitDb = kLimitDb;
	FParse::Value(FCommandLine::Get(), TEXT("TagLimitDb="), LimitDb); // (a test of the limiter itself: -20 must hold the recording's peaks there)
	S.ThresholdDb = FMath::Clamp(LimitDb, -60.f, 0.f);
	S.KneeBandwidthDb = 0.f;
	S.LookAheadMsec = 10.f;
	S.AttackTimeMsec = 1.f;
	S.ReleaseTimeMsec = 200.f;
	S.OutputGainDb = 0.f;
	S.bAnalogMode = false;
	S.bBypass = false;
	Limiter->SetSettings(S);
	UAudioMixerBlueprintLibrary::AddMasterSubmixEffect(this, Limiter);
	// (again, now that the effect exists: an instance takes its preset's settings only when told they changed - set before
	// it was made, it ran the engine's defaults, and a -15 dB input gain did nothing to the recording)
	Limiter->SetSettings(S);
	UE_LOG(LogTemp, Display, TEXT("TAG audio: master trim %+.1f dB, limiter at %.1f dBFS%s"), 20.f * FMath::LogX(10.f, MasterTrim()), S.ThresholdDb, bLegacyMix ? TEXT(" (legacy mix)") : TEXT(""));
}

void ATagAudio::Screen(const TCHAR* Id)
{
	GScreen = FName(Id);
	if (ATagAudio* A = GTagAudio.Get(); A && A->Fed > 3) A->ScreenChanged();
}

void ATagAudio::ScreenChanged()
{
	if (ScreenNow == GScreen) return;
	const FName Was = ScreenNow;
	ScreenNow = GScreen;
	if (ScreenNow.IsNone() || (Was.IsNone() && ScreenNow == NScrPause)) return; // (the pause has its own sound)
	if (ScreenNow == NScrMain && !bTitled && !Game)
	{
		// the game arrives: the title's hit, and the playlist comes in as it lands
		bTitled = true;
		MenuHold = 1.3f;
		PlayNow(FLater{0.f, NUiTitle, FVector::ZeroVector, 1.f, 1.f, ECat::Ui});
		return;
	}
	const bool bForward = ScreenDepth(ScreenNow) > ScreenDepth(Was);
	PlayNow(FLater{0.f, bForward ? NUiOpen : NUiClose, FVector::ZeroVector, bForward ? 0.8f : 0.7f, 1.f, ECat::Ui});
}

void ATagAudio::BeginPlay()
{
	Super::BeginPlay();
	GTagAudio = this;
	const TCHAR* Cmd = FCommandLine::Get();
	if (GConfig)
		for (int32 i = 0; i < 3; ++i)
		{
			GConfig->GetInt(TEXT("TAG.Settings"), kLevelKeys[i], GLevels[i], GGameUserSettingsIni);
			GLevels[i] = FMath::Clamp(GLevels[i], 0, 4);
		}
	SetLevels(GLevels[0] / 4.f, GLevels[1] / 4.f, GLevels[2] / 4.f);
	bLog = FParse::Param(Cmd, TEXT("TagAudioLog"));
	bLegacyMix = LegacyMix();
	bBoothRef = FParse::Param(Cmd, TEXT("TagBoothRef")) && !FParse::Param(Cmd, TEXT("TagNoBooth")) && !FParse::Param(Cmd, TEXT("TagBoothMute"));
	bMixTest = FParse::Param(Cmd, TEXT("TagMixTest"));
	StartLimiter();
	Rand.Initialize(static_cast<int32>(FPlatformTime::Cycles64() & 0x7fffffff));
	T0 = FPlatformTime::Seconds();
	FindBanks();
	// the menus' sounds, their playlist and the building's bed behind them: loaded in the background from here (one
	// needed before it lands is loaded on the spot, as before: Pick)
	for (const TPair<FName, FBank>& It : Banks)
		if (It.Key.ToString().StartsWith(TEXT("Ui"))) PreloadBank(It.Key);
	for (const FName B : {NMusMenu, NCalm, NBuzz, NCheerSmall}) PreloadBank(B);
	// the field: heard from the broadcast's field microphones more than from the camera, so distance takes little off
	FieldAtt = NewObject<USoundAttenuation>(this);
	FSoundAttenuationSettings& S = FieldAtt->Attenuation;
	S.bAttenuate = true;
	S.bSpatialize = true;
	S.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	S.AttenuationShape = EAttenuationShape::Sphere;
	S.AttenuationShapeExtents = FVector(2200.f);
	S.FalloffDistance = 14000.f;
	S.dBAttenuationAtMax = -11.f;
	S.NonSpatializedRadiusStart = 1500.f;
	S.NonSpatializedRadiusEnd = 300.f;
	S.StereoSpread = 300.f;
	static const FName LoopBanks[LNum] = {NCalm, NBuzz, NRoar, NNoise, NRain, NWind};
	for (int32 i = 0; i < LNum; ++i)
	{
		Loops[i].Bank = LoopBanks[i];
		Loops[i].C = MakeComp(false);
		Loops[i].C2 = MakeComp(false);
		Loops[i].NextSwap = Rand.FRandRange(kSwapMin, kSwapMax);
	}
	Loops[LRoar].Up = 2.6f;  Loops[LRoar].Down = 0.5f;
	Loops[LNoise].Up = 0.9f; Loops[LNoise].Down = 1.1f;
	Loops[LBuzz].Up = 1.2f;  Loops[LBuzz].Down = 0.5f;
	Loops[LCalm].Up = 0.5f;  Loops[LCalm].Down = 0.3f;
	Decks[0].C = MakeComp(true);
	Decks[1].C = MakeComp(true);
	House.C = MakeComp(false);
	float Rec = 0.f;
	if (FParse::Value(Cmd, TEXT("TagAudioRec="), Rec) && Rec > 0.f)
	{
		RecLeft = Rec; // (it starts with the first frame: Feed)
	}
	// -TagAudioSilent: nothing reaches the speakers (the recording is taken before the main mix's output level): for
	// test runs on a machine somebody is sitting at
	if (FParse::Param(Cmd, TEXT("TagAudioSilent")))
		if (FAudioDevice* AD = GetWorld()->GetAudioDeviceRaw())
		{
			USoundSubmix& Main = static_cast<Audio::FMixerDevice*>(AD)->GetMainSubmixObject();
			Main.SetSubmixOutputVolume(this, 0.f);
			UE_LOG(LogTemp, Display, TEXT("TAG audio: silent run"));
		}
	UE_LOG(LogTemp, Display, TEXT("TAG audio: %d banks"), Banks.Num());
}

void ATagAudio::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bRec)
	{
		bRec = false;
		UAudioMixerBlueprintLibrary::StopRecordingOutput(this, EAudioRecordingExportType::WavFile, TEXT("tag_audio"), FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AudioRec")));
	}
	MatchEnd();
	if (Limiter) UAudioMixerBlueprintLibrary::RemoveMasterSubmixEffect(this, Limiter); // (the next world's sound actor adds its own)
	Limiter = nullptr;
	Shots.Reset();
	for (const TSharedPtr<FStreamableHandle>& H : Preloads)
		if (H.IsValid()) H->CancelHandle(); // (a load still out is dropped; one that landed lets go of its waves)
	Preloads.Reset();
	if (GTagAudio.Get() == this) GTagAudio = nullptr;
	Super::EndPlay(Reason);
}

UAudioComponent* ATagAudio::MakeComp(bool bUi)
{
	UAudioComponent* C = NewObject<UAudioComponent>(this);
	C->bAutoActivate = false;
	C->bAllowSpatialization = false;
	C->bIsUISound = bUi;
	C->bAutoDestroy = false;
	// the beds and the music are never the voice that gives way when a pile of hits and footsteps lands at once
	C->bOverridePriority = true;
	C->Priority = 8.f;
	C->RegisterComponent();
	Comps.Add(C);
	return C;
}

void ATagAudio::FindBanks()
{
	// A run on loose files (the launcher's -game): the folders themselves say what is there - a bank is a folder, a
	// take is a .uasset in it - which costs a directory listing, where making the asset registry read every package
	// under /Game/TAG/Audio right now cost most of a second at launch. (No class check this way: anything in a bank's
	// folder that is not a sound wave is dropped when the bank loads, in Pick / BankLanded.) A cooked build, or a
	// listing that finds nothing, asks the registry as before.
	if (!FPlatformProperties::RequiresCookedData())
	{
		struct FTake { FName Name; FString Package; };
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *(FPaths::ProjectContentDir() / TEXT("TAG/Audio")), TEXT("*.uasset"), true, false);
		TArray<FTake> Takes;
		Takes.Reserve(Files.Num());
		for (const FString& File : Files)
		{
			FString Package;
			if (!FPackageName::TryConvertFilenameToLongPackageName(File, Package)) continue;
			Takes.Add(FTake{FName(*FPackageName::GetShortName(Package)), Package});
		}
		Takes.Sort([](const FTake& A, const FTake& B) { return A.Name.LexicalLess(B.Name); }); // (the registry path's order)
		for (const FTake& Take : Takes)
		{
			const FString Folder = FPaths::GetCleanFilename(FPaths::GetPath(Take.Package));
			Banks.FindOrAdd(FName(*Folder)).Paths.Add(FSoftObjectPath(FTopLevelAssetPath(FName(*Take.Package), Take.Name)));
		}
		if (Banks.Num() > 0) return;
	}
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	AR.ScanPathsSynchronous({TEXT("/Game/TAG/Audio")}, false);
	TArray<FAssetData> Assets;
	AR.GetAssetsByPath(FName(TEXT("/Game/TAG/Audio")), Assets, true);
	Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
	for (const FAssetData& A : Assets)
	{
		if (A.AssetClassPath != USoundWave::StaticClass()->GetClassPathName()) continue;
		const FString Folder = FPaths::GetCleanFilename(A.PackagePath.ToString());
		Banks.FindOrAdd(FName(*Folder)).Paths.Add(A.GetSoftObjectPath());
	}
}

void ATagAudio::PreloadBank(FName Bank)
{
	FBank* B = Banks.Find(Bank);
	if (!B || B->bLoaded || B->bAsync || B->Paths.Num() == 0 || !UAssetManager::IsInitialized()) return;
	B->bAsync = true;
	const TSharedPtr<FStreamableHandle> H = UAssetManager::GetStreamableManager().RequestAsyncLoad(B->Paths,
		FStreamableDelegate::CreateWeakLambda(this, [this, Bank]() { BankLanded(Bank); }));
	if (H.IsValid()) Preloads.Add(H);
}

void ATagAudio::BankLanded(FName Bank)
{
	FBank* B = Banks.Find(Bank);
	if (!B || B->bLoaded) return; // (it was needed before it landed: Pick loaded it there and then)
	B->bLoaded = true;
	for (const FSoftObjectPath& Path : B->Paths)
		if (USoundWave* W = Cast<USoundWave>(Path.ResolveObject()))
		{
			B->Waves.Add(W);
			Loaded.Add(W);
		}
}

void ATagAudio::PreloadMatchBanks()
{
	// what a play needs first, first; then everything else a match can play. The PA's "touchdown" and "first down" are
	// one pair a team (32 of them): MatchStart asks for the home side's
	for (const FName B : {NCalm, NBuzz, NRoar, NNoise, NCheerBig, NCheerMed, NCheerSmall, NGroan, NGasp, NBoo, NHitLight, NHitMed, NHitHeavy, NHitHuge, NLineCrash,
		NBodyFall, NCatch, NSnap, NThrow, NWhistle, NKickPlace, NKickPunt, NStep, NGrunt})
		PreloadBank(B);
	for (const TPair<FName, FBank>& It : Banks)
	{
		const FString Name = It.Key.ToString();
		if (Name.StartsWith(TEXT("PaTouchdown_")) || Name.StartsWith(TEXT("PaFirstDown_"))) continue;
		PreloadBank(It.Key);
	}
}

USoundWave* ATagAudio::Pick(FName Bank)
{
	FBank* B = Banks.Find(Bank);
	if (!B) return nullptr;
	if (!B->bLoaded)
	{
		B->bLoaded = true;
		for (const FSoftObjectPath& Path : B->Paths)
			if (USoundWave* W = Cast<USoundWave>(Path.TryLoad()))
			{
				B->Waves.Add(W);
				Loaded.Add(W);
			}
	}
	const int32 N = B->Waves.Num();
	if (N == 0) return nullptr;
	int32 i = Rand.RandHelper(N);
	if (N > 1 && i == B->Last) i = (i + 1 + Rand.RandHelper(N - 1)) % N;
	B->Last = i;
	return B->Waves[i];
}

void ATagAudio::Note(const TCHAR* Kind, FName Bank, float Vol, const USoundWave* Take)
{
	Counts.FindOrAdd(Bank)++;
	if (Take) TakeUses.FindOrAdd(Bank).FindOrAdd(Take->GetFName())++;
	if (bLog) UE_LOG(LogTemp, Display, TEXT("TAG audio %.2f %s %s %.2f %s"), FPlatformTime::Seconds() - T0, Kind, *Bank.ToString(), Vol, Take ? *Take->GetName() : TEXT(""));
}

void ATagAudio::PlayNow(const FLater& L)
{
	USoundWave* W = Pick(L.Bank);
	if (!W || L.Vol <= 0.f) { if (bLog && !W) UE_LOG(LogTemp, Display, TEXT("TAG audio %.2f (no bank) %s"), FPlatformTime::Seconds() - T0, *L.Bank.ToString()); return; }
	switch (L.Cat)
	{
	case ECat::Field:
		// (one-shots nobody holds on to: played straight on the audio device, with no audio component made and
		// collected for each - the footsteps alone were some thirty a second)
		UGameplayStatics::PlaySoundAtLocation(this, W, L.Pos, FRotator::ZeroRotator, L.Vol * kField * LevelMaster * LevelStadium, L.Pitch, 0.f, FieldAtt);
		Note(TEXT("field"), L.Bank, L.Vol, W);
		break;
	case ECat::Crowd:
	case ECat::Stadium:
	{
		// (not a UI sound: held with the world by the pause menu)
		const float Base = L.Vol * (L.Cat == ECat::Crowd ? kCrowdShot : kPa) * LevelMaster * LevelStadium;
		if (bLegacyMix)
		{
			UGameplayStatics::PlaySound2D(this, W, Base, L.Pitch, 0.f, nullptr, nullptr, false);
			LastShot.Reset();
			LastShotBase = LastShotGain = Base;
		}
		else if (UAudioComponent* C = UGameplayStatics::CreateSound2D(this, W, Base * Duck, L.Pitch, 0.f, nullptr, false, true))
		{
			// the broadcast mix rides these with the beds (a cheer under calm talk gives way like the bed under it): a
			// component kept by a weak handle until it plays out (CreateSound2D makes a UI sound: this one is the world's)
			C->bIsUISound = false;
			C->Play();
			if (Shots.Num() < 32) Shots.Add(FShot{C, Base, Duck});
			LastShot = C;
			LastShotBase = Base;
			LastShotGain = Base * Duck;
		}
		Note(L.Cat == ECat::Crowd ? TEXT("crowd") : TEXT("stadium"), L.Bank, L.Vol, W);
		break;
	}
	case ECat::Ui:
		UGameplayStatics::PlaySound2D(this, W, L.Vol * kUi * LevelMaster, L.Pitch, 0.f, nullptr, nullptr, true);
		Note(TEXT("ui"), L.Bank, L.Vol);
		break;
	}
}

void ATagAudio::Field(FName Bank, const FVector& Pos, float Vol, float Pitch, float Delay)
{
	const FLater L{Delay, Bank, Pos, Vol, Pitch * Jit(0.95f, 1.05f), ECat::Field};
	if (bTaping && Bank != NStep && Bank != NWhistle && Tape.Num() < 400) Tape.Add(FLater{TapeT + FMath::Max(0.f, Delay), Bank, Pos, Vol, L.Pitch, ECat::Field});
	if (Delay <= 0.f) PlayNow(L); else Queue.Add(L);
}

void ATagAudio::Crowd(FName Bank, float Vol, float Delay)
{
	if (Bank == NGroanBig && !Has(NGroanBig)) Bank = NGroan;
	const FLater L{Delay, Bank, FVector::ZeroVector, FMath::Min(Vol, 1.2f), Jit(0.97f, 1.03f), ECat::Crowd};
	if (Delay <= 0.f) PlayNow(L); else Queue.Add(L);
}

void ATagAudio::StadiumSay(FName Bank, float Vol, float Delay)
{
	const FLater L{Delay, Bank, FVector::ZeroVector, Vol, 1.f, ECat::Stadium};
	if (Delay <= 0.f) PlayNow(L); else Queue.Add(L);
}

void ATagAudio::SetLevels(float Master, float MusicLevel, float Stadium)
{
	// (the master trim rides in the mix's own gains: the engine's dynamics processor on the main submix, StartLimiter,
	// was measured doing nothing - a -15 dB input gain left the recording 0.4 LU from no effect at all, 2026-11-25)
	LevelMaster = Master * MasterTrim(); LevelMusic = MusicLevel; LevelStadium = Stadium;
}

// ---------------------------------------------------------------------------------------------- loops and music

void ATagAudio::TickLoops(float Dt, float CrowdGain)
{
	static const float Gain[LNum] = {kCalm, kBuzz, kRoar, kNoise, kRain, kWind};
	for (int32 i = 0; i < LNum; ++i)
	{
		FLoop& L = Loops[i];
		L.Vol = L.Target > L.Vol ? FMath::Min(L.Target, L.Vol + L.Up * Dt) : FMath::Max(L.Target, L.Vol - L.Down * Dt);
		if (!L.bStarted)
		{
			if (L.Target <= 0.f) continue;
			USoundWave* W = Pick(L.Bank);
			if (!W) { L.Target = 0.f; continue; }
			W->bLooping = true;
			W->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
			L.C->SetSound(W);
			L.C->SetVolumeMultiplier(0.001f);
			L.C->Play(Rand.FRandRange(0.f, FMath::Max(0.f, W->Duration - 1.f)));
			L.bStarted = true;
			Note(TEXT("loop"), L.Bank, 1.f);
		}
		// another take of the bed, in over this one at a quiet moment (a building on its feet is not the time)
		const FBank* Bk = Banks.Find(L.Bank);
		if (L.Xf < 0.f && L.C2 && Bk && Bk->Waves.Num() > 1)
		{
			L.NextSwap -= Dt;
			if (L.NextSwap <= 0.f && Excite < 0.35f && Swell < 0.2f && Deflate <= 0.f)
				if (USoundWave* W = Pick(L.Bank))
				{
					W->bLooping = true;
					W->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
					L.C2->SetSound(W);
					L.C2->SetVolumeMultiplier(0.001f);
					L.C2->Play(Rand.FRandRange(0.f, FMath::Max(0.f, W->Duration - 1.f)));
					L.Xf = 0.f;
					Note(TEXT("loop"), L.Bank, 1.f);
				}
		}
		const float G = i >= LRain ? LevelMaster * LevelStadium : CrowdGain;
		const float Vol = FMath::Max(0.001f, L.Vol * Gain[i] * G);
		if (L.Xf >= 0.f)
		{
			L.Xf = FMath::Min(1.f, L.Xf + Dt / kSwapSecs);
			const float A = FMath::Sin(L.Xf * UE_HALF_PI);
			L.C->SetVolumeMultiplier(FMath::Max(0.001f, Vol * FMath::Cos(L.Xf * UE_HALF_PI)));
			L.C2->SetVolumeMultiplier(FMath::Max(0.001f, Vol * A));
			if (L.Xf >= 1.f)
			{
				// the new take is the bed now; the old one's component waits for the next swap
				L.C->Stop();
				Swap(L.C, L.C2);
				L.Xf = -1.f;
				L.NextSwap = Rand.FRandRange(kSwapMin, kSwapMax);
			}
		}
		else L.C->SetVolumeMultiplier(Vol);
	}
}

void ATagAudio::StopLoops()
{
	for (FLoop& L : Loops)
	{
		if (L.C && L.bStarted) L.C->Stop();
		if (L.C2 && L.Xf >= 0.f) L.C2->Stop();
		L.bStarted = false;
		L.Xf = -1.f;
		L.Vol = L.Target = 0.f;
	}
}

void ATagAudio::Music(FName Bank, float Vol, float FadeIn, float FadeOutOld)
{
	USoundWave* W = Pick(Bank);
	FDeck& Old = Decks[Deck];
	if (Old.bOn) { Old.Target = 0.f; Old.Rate = FMath::Max(Old.Vol, 0.05f) / FMath::Max(FadeOutOld, 0.05f); }
	MusicNow = Bank;
	if (!W) return;
	Deck ^= 1;
	FDeck& D = Decks[Deck];
	D.Bank = Bank;
	D.C->Stop();
	W->bLooping = false;
	D.C->SetSound(W);
	D.Vol = FadeIn <= 0.f ? Vol : 0.f;
	D.Target = Vol;
	D.Rate = Vol / FMath::Max(FadeIn, 0.01f);
	D.Left = W->Duration;
	D.bOn = true;
	D.C->SetVolumeMultiplier(FMath::Max(0.001f, D.Vol * kMusic * LevelMaster * LevelMusic));
	D.C->Play();
	Note(TEXT("music"), Bank, Vol);
}

void ATagAudio::MusicStop(float Fade)
{
	FDeck& D = Decks[Deck];
	if (D.bOn) { D.Target = 0.f; D.Rate = FMath::Max(D.Vol, 0.05f) / FMath::Max(Fade, 0.05f); }
	MusicNow = NAME_None;
}

void ATagAudio::HouseStart(FName Bank, float Vol, float LeadIn)
{
	USoundWave* W = Pick(Bank);
	if (!W) return;
	// LeadIn: the piece is a build that must END in that many seconds (the kickoff's rising roar): start inside it
	const float StartAt = LeadIn >= 0.f ? FMath::Clamp(W->Duration - LeadIn, 0.f, FMath::Max(0.f, W->Duration - 0.5f)) : 0.f;
	House.C->Stop();
	W->bLooping = false;
	House.C->SetSound(W);
	House.Bank = Bank;
	House.Vol = 0.f; House.Target = Vol; House.Rate = Vol / 0.5f;
	House.Left = W->Duration - StartAt;
	if (LeadIn >= 0.f) House.Rate = Vol / 0.15f;
	House.bOn = true;
	House.C->SetVolumeMultiplier(0.001f);
	House.C->Play(StartAt);
	Note(TEXT("house"), Bank, Vol);
}

void ATagAudio::HouseStop(float Fade)
{
	if (House.bOn) { House.Target = 0.f; House.Rate = FMath::Max(House.Vol, 0.05f) / FMath::Max(Fade, 0.05f); }
}

void ATagAudio::TickMusic(float Dt, float DuckNow)
{
	for (int32 i = 0; i < 2; ++i)
	{
		FDeck& D = Decks[i];
		if (!D.bOn) continue;
		D.Vol = D.Target > D.Vol ? FMath::Min(D.Target, D.Vol + D.Rate * Dt) : FMath::Max(D.Target, D.Vol - D.Rate * Dt);
		D.Left -= Dt;
		if ((D.Target <= 0.f && D.Vol <= 0.f) || D.Left <= 0.f)
		{
			D.C->Stop();
			D.bOn = false;
			if (i == Deck && D.Target > 0.f && MusicNow == D.Bank && D.Bank != NMusMenu && D.Bank != NMusHalf) MusicNow = NAME_None; // (it played out)
			continue;
		}
		D.C->SetVolumeMultiplier(FMath::Max(0.001f, D.Vol * kMusic * LevelMaster * LevelMusic * DuckNow));
		// a playlist: the next piece comes in under the end of this one
		if (i == Deck && D.Target > 0.f && D.Left < 2.5f && (D.Bank == NMusMenu || D.Bank == NMusHalf) && MusicNow == D.Bank)
			Music(D.Bank, D.Target, 2.f, 2.5f);
	}
	if (House.bOn)
	{
		House.Vol = House.Target > House.Vol ? FMath::Min(House.Target, House.Vol + House.Rate * Dt) : FMath::Max(House.Target, House.Vol - House.Rate * Dt);
		House.Left -= Dt;
		if ((House.Target <= 0.f && House.Vol <= 0.f) || House.Left <= 0.f) { House.C->Stop(); House.bOn = false; }
		else House.C->SetVolumeMultiplier(FMath::Max(0.001f, House.Vol * kHouse * LevelMaster * LevelStadium * (bLegacyMix ? 1.f : Duck))); // (the bowl's music: part of the crowd level)
	}
}

// ---------------------------------------------------------------------------------------------- the match

void ATagAudio::MatchStart(const tag::Game& G)
{
	Game = &G;
	bHushed = false;
	HomeMen.Reset();
	for (const auto& Pl : G.home->roster) HomeMen.Add(Pl.get());
	HomeAbbr = G.home->abbr;
	LogSeen = G.log.size();
	P = FPlay();
	K = FKickSeen();
	// the teams are out and the theme plays - but on the first frame the match is on screen (OpenStart), not here: the
	// match's load comes after this (it ran 26 s), and the open played to a cooled crowd with its music gone
	Excite = 0.f; ExciteHold = 0.f;
	bOpenPending = true;
	OpenT = -1.f;
	DNoise = Swell = Deflate = 0.f;
	Queue.Reset();
	Counts.Reset();
	TakeUses.Reset();
	T0 = FPlatformTime::Seconds();
	// the crowd's beds are heard on the first frame: here now if they are not already. Everything else the match can
	// play loads in the background (PreloadMatchBanks: the PA, the referee, the cadences and the music with it), and a
	// sound needed before its load lands is loaded on the spot, as it always was (Pick)
	for (const FName B : {NCalm, NBuzz, NRoar})
	{
		FBank* Bk = Banks.Find(B);
		if (Bk && !Bk->bLoaded) { Pick(B); Bk->Last = -1; }
	}
	PreloadMatchBanks();
	PreloadBank(FName(*FString::Printf(TEXT("PaTouchdown_%hs"), HomeAbbr.c_str())));
	PreloadBank(FName(*FString::Printf(TEXT("PaFirstDown_%hs"), HomeAbbr.c_str())));
	UE_LOG(LogTemp, Display, TEXT("TAG audio: match start, home %hs"), HomeAbbr.c_str());
}

void ATagAudio::MatchEnd()
{
	if (Game && Counts.Num())
	{
		FString S;
		Counts.KeySort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		for (const auto& It : Counts) S += FString::Printf(TEXT(" %s=%d"), *It.Key.ToString(), It.Value);
		UE_LOG(LogTemp, Display, TEXT("TAG audio summary:%s"), *S);
		// how often one take came round: bank=<takes heard>/<takes in the bank>:<most uses of one take> (05 A9 wants the
		// crowd's reactions at 3 or fewer a take a game)
		FString T;
		TakeUses.KeySort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		for (const auto& It : TakeUses)
		{
			int32 Most = 0;
			for (const auto& Tk : It.Value) Most = FMath::Max(Most, Tk.Value);
			const FBank* Bk = Banks.Find(It.Key);
			T += FString::Printf(TEXT(" %s=%d/%d:%d"), *It.Key.ToString(), It.Value.Num(), Bk ? Bk->Paths.Num() : 0, Most);
		}
		UE_LOG(LogTemp, Display, TEXT("TAG audio take uses:%s"), *T);
	}
	TakeUses.Reset();
	Game = nullptr;
	bHushed = false;
	P = FPlay();
	K = FKickSeen();
	Queue.Reset();
	StopLoops();
	HouseStop(0.3f);
	if (MusicNow != NMusMenu) MusicStop(1.f);
	GScreen = ScreenNow = NAME_None; // (the menu that comes next comes forward)
	bPauseMusic = false;
}

void ATagAudio::Hush()
{
	// (the game thread is about to be busy loading: these fades are the audio thread's own, they need no tick)
	for (FDeck& D : Decks)
	{
		if (D.C && D.bOn) D.C->FadeOut(0.5f, 0.f);
		else if (D.C) D.C->Stop();
		D.bOn = false;
		D.Vol = D.Target = 0.f;
	}
	for (FLoop& L : Loops)
	{
		if (L.C && L.bStarted) L.C->FadeOut(0.8f, 0.f);
		if (L.C2 && L.Xf >= 0.f) L.C2->FadeOut(0.8f, 0.f);
		L.bStarted = false;
		L.Xf = -1.f;
		L.Vol = L.Target = 0.f;
	}
	// kick off, from the menus: the hit, and the crowd coming up as the teams take the field
	if (!bHushed && !Game && !ScreenNow.IsNone()) PlayNow(FLater{0.f, NUiStart, FVector::ZeroVector, 1.f, 1.f, ECat::Ui});
	GScreen = ScreenNow = NAME_None;
	MusicNow = NAME_None;
	bHushed = true; // (the menus do not start their music again before the match does)
}

void ATagAudio::Roar(float Level, float Hold)
{
	if (Level >= Excite) { Excite = FMath::Min(1.f, Level); ExciteHold = FMath::Max(ExciteHold, Hold); }
}

float ATagAudio::Stakes(const tag::Game& G) const
{
	const int32 Margin = FMath::Abs(G.score.home - G.score.away);
	float S = 0.15f;
	if (G.q >= 4 && Margin <= 8) S = G.clock < 300 ? 0.8f : 0.6f;
	else if (G.q >= 3 && Margin <= 8) S = 0.35f;
	else if (Margin <= 8) S = 0.22f;
	if (G.phase == tag::GamePhase::Scrimmage)
	{
		if (G.ballOn >= 80) S += 0.22f;
		if (G.down >= 3) S += 0.15f;
	}
	return FMath::Clamp(S, 0.f, 1.f);
}

FVector ATagAudio::At(const tag::Ent* E, int32 Dir) const
{
	return E ? TagCoords::ToWorld(E->pos.x, E->pos.y, Dir, 1.0) : FVector::ZeroVector;
}

void ATagAudio::NewPlay(const FTagAudioView& V)
{
	const tag::PlaySim& S = *V.Sim;
	const bool bChantWas = P.bChant;
	P = FPlay();
	P.Sim = &S;
	Tape.Reset();
	TapeT = 0.f;
	bTaping = false;
	Feet.Reset();
	P.bReturn = S.isReturn();
	P.Los = S.los;
	P.Down = V.G->down;
	P.ToGo = V.G->toGo;
	const tag::Ent* O = S.qb ? S.qb : (S.off.empty() ? nullptr : S.off[0]);
	P.bOffHome = O ? IsHome(O->p) : V.G->poss == tag::TeamKey::Home;
	P.bChant = bChantWas;
	if (S.t > 0 && !S.isLive())
	{
		// seen first already dead (the last play shown again): nothing of it is played again
		P.EvSeen = S.events.size();
		P.bSnapped = P.bWhistled = P.bLineCrash = true;
		return;
	}
	// the offense comes to the line: the stadium's between-plays moment (its music, a chant, the third-down call)
	if (!P.bReturn && S.preSnap()) BetweenPlays(V);
	if (P.bReturn && S.kickerEnt && !S.kicked && IsHome(S.kickerEnt->p) && Has(NKickRise))
	{
		K.bRise = true;
		HouseStart(NKickRise, 1.6f, 3.2f);
	}
}

void ATagAudio::TickPresnap(float Dt, const FTagAudioView& V)
{
	const tag::PlaySim& S = *V.Sim;
	if (!S.qb || P.bReturn) return;
	if (S.walking()) return; // (not set yet)
	const float Was = P.PreT;
	P.PreT += Dt;
	const int32 Voices = 4;
	const int32 Vc = 1 + (S.qb->p ? S.qb->p->id : 0) % Voices;
	const FVector Q = At(S.qb, V.Dir);
	auto Crossed = [&](float T) { return Was < T && P.PreT >= T; };
	if (Crossed(0.5f) && Rand.FRand() < 0.45f && !S.def.empty())
	{
		// a linebacker makes his call
		const tag::Ent* Lb = S.def[Rand.RandHelper(static_cast<int32>(S.def.size()))];
		Field(NDefCall, At(Lb, V.Dir), 0.6f);
	}
	if (Crossed(1.4f)) Field(FName(*FString::Printf(TEXT("Cad%d_Color"), Vc)), Q, 0.85f);
	if (Crossed(3.6f)) Field(FName(*FString::Printf(TEXT("Cad%d_Set"), Vc)), Q, 0.9f);
	if (Crossed(7.5f) && Rand.FRand() < 0.5f) Field(FName(*FString::Printf(TEXT("Cad%d_Color"), Vc)), Q, 0.85f);
}

void ATagAudio::BetweenPlays(const FTagAudioView& V)
{
	const tag::Game& G = *V.G;
	if (G.phase != tag::GamePhase::Scrimmage) return;
	const bool bHomeBall = G.poss == tag::TeamKey::Home;
	P.bChant = false;
	if (House.bOn) return;
	const float R = Rand.FRand();
	if (!bHomeBall && G.down >= 3)
	{
		// the scoreboard asks for noise on third down
		if (R < 0.75f) StadiumSay(Rand.FRand() < 0.5f ? NPaThird : NPaNoise, 1.f, 0.6f);
	}
	else if (!bHomeBall && R < 0.3f && Has(NChant))
	{
		HouseStart(NChant, 1.5f);
		P.bChant = true;
	}
	else if (R < 0.38f && Excite < 0.5f)
		HouseStart(NHouseMusic, 1.f);
}

void ATagAudio::OnEvent(const FTagAudioView& V, const tag::PlayEvent& Ev)
{
	const std::string& T = Ev.type;
	const tag::PlaySim& S = *V.Sim;
	const FVector A = Ev.a ? At(Ev.a, V.Dir) : V.Ball;
	const FVector B = Ev.b ? At(Ev.b, V.Dir) : A;
	const bool bAHome = Ev.a && IsHome(Ev.a->p);
	if (T == "snap") Field(NSnap, A, 0.7f);
	else if (T == "handoff") Field(NSnap, B, 0.4f, 0.85f);
	else if (T == "slide") Field(NBodyFall, A, 0.5f, 1.f, 0.45f);  // (the quarterback's slide: the turf a beat in)
	else if (T == "throw")
	{
		Field(NThrow, A, 0.55f);
		if (Rand.FRand() < 0.4f) Field(NGrunt, A, 0.3f);
	}
	else if (T == "catch" || T == "divecatch" || T == "int")
	{
		Field(NCatch, A, 0.9f);
		if (T == "divecatch") Field(NBodyFall, A, 0.7f, 1.f, 0.22f);
		if (T == "int")
		{
			P.bReacted = true;
			if (bAHome) { Crowd(NCheerBig, 1.f, 0.1f); Roar(0.95f, 5.f); }
			else { Crowd(NGroanBig, 1.f, 0.1f); Excite *= 0.4f; Deflate = 5.f; Crowd(NAwayCheer, 0.7f, 0.3f); }
		}
		else if (!P.bReturn && Ev.a && Ev.a->pos.x - P.Los > 15.0)
		{
			// a ball caught well down the field
			if (P.bOffHome) { Crowd(NCheerMed, 0.85f, 0.05f); Roar(0.7f, 1.5f); }
			else Crowd(NGroan, 0.6f, 0.1f);
			P.bDeepCalled = true;
		}
		if (K.bRise) { K.bRise = false; HouseStop(0.4f); }
	}
	else if (T == "pbu" || T == "batted")
	{
		Field(NSlap, A, 0.75f);
		P.bReacted = true;
		if (!P.bOffHome) { Crowd(NCheerMed, 0.7f, 0.15f); Roar(0.55f, 1.5f); }
		else Crowd(NGroan, 0.55f, 0.15f);
	}
	else if (T == "drop" || T == "contested_inc") Field(NSlap, A, 0.5f, 0.9f);
	else if (T == "incomplete") Field(NBounce, V.Ball, 0.6f);
	else if (T == "shed") Field(NHitLight, A, 0.5f);
	else if (T == "rushfail")
	{
		// the trenches: hands and pads working all play long, heard now and then rather than as a drum roll
		if (Ev.t - P.TrenchT > 0.45 && Rand.FRand() < 0.6f) { P.TrenchT = Ev.t; Field(NHitLight, A, 0.3f, 0.9f); }
	}
	else if (T == "pancake")
	{
		Field(NHitMed, B, 0.85f);
		Field(NBodyFall, B, 0.8f, 1.f, 0.3f);
	}
	else if (T == "move")
	{
		if (Ev.note == "truck") { Field(NHitMed, A, 0.9f); Field(NGrunt, A, 0.5f, 1.f, 0.03f); }
		else if (Ev.note == "stiffarm") Field(NHitLight, A, 0.7f);
		else if (Ev.note != "hitstick") Field(NCut, A, 0.55f);
	}
	else if (T == "trucked")
	{
		Field(NHitHeavy, A, 0.9f);
		Field(NGrunt, B, 0.5f, 1.f, 0.05f);
		if (bAHome) Crowd(NGasp, 0.5f, 0.1f);
	}
	else if (T == "broken")
	{
		Field(NHitLight, A, 0.7f);
		if (bAHome && Rand.FRand() < 0.6f) Crowd(NGasp, 0.4f, 0.1f);
	}
	else if (T == "wrap") Field(NHitMed, A, 0.75f);
	else if (T == "bighit")
	{
		P.BigHitT = Ev.t;
		Field(NHitHuge, B, 1.f);
		Field(NGrunt, B, 0.7f, 1.f, 0.04f);
		Crowd(NGasp, 0.95f, 0.12f);
		Roar(0.5f, 0.8f);
	}
	else if (T == "tackle")
	{
		// how hard: how fast the two men close on each other
		const double Vx = Ev.a && Ev.b ? Ev.a->vel.x - Ev.b->vel.x : 0, Vy = Ev.a && Ev.b ? Ev.a->vel.y - Ev.b->vel.y : 0;
		const double Close = FMath::Sqrt(Vx * Vx + Vy * Vy);
		const bool bSoft = Ev.note == "dive" || Ev.note == "arm";
		if (Ev.t - P.BigHitT > 0.4)
		{
			const FName Hit = Close > 9.0 && !bSoft ? NHitHeavy : Close > 5.0 ? NHitMed : NHitLight;
			Field(Hit, B, FMath::Clamp(0.6f + static_cast<float>(Close) * 0.045f, 0.6f, 1.f));
			if (Rand.FRand() < 0.5f) Field(NGrunt, B, 0.4f, 1.f, 0.05f);
		}
		Field(NBodyFall, B, 0.75f, 1.f, 0.32f);
	}
	else if (T == "assist") Field(NHitMed, A, 0.6f, 1.f, 0.08f);
	else if (T == "fumble")
	{
		Field(NHitMed, A, 0.8f);
		Field(NBallOut, B, 0.9f, 1.f, 0.25f);
		Crowd(NGasp, 0.8f, 0.15f);
		Roar(0.5f, 1.f);
	}
	else if (T == "recover") Field(NBodyFall, A, 0.7f);
	else if (T == "kick" || T == "punt")
	{
		Field(T == "punt" ? NKickPunt : NKickPlace, A, 1.f);
		if (K.bRise)
		{
			K.bRise = false;
			HouseStop(0.12f);
			Crowd(NKickShout, 1.f);
			Roar(0.5f, 1.5f);
		}
		if (MusicNow == NMusIntro) MusicStop(2.f);
	}
	(void)S;
}

void ATagAudio::OnWhistle(const FTagAudioView& V, const tag::PlayResult& R)
{
	const tag::PlaySim& S = *V.Sim;
	const std::string& Why = S.endReason;
	if (Why != "td") Field(R.incomplete || R.outOfBounds ? NWhistle : NWhistle, V.Ball, Why == "kneel" ? 0.55f : 0.8f, 1.f, Why == "incomplete" ? 0.1f : 0.22f);
	const tag::Ent* H = S.ball.holder;
	const bool bHolderHome = H ? IsHome(H->p) : P.bOffHome;
	const bool bHomeOff = P.bOffHome;
	const bool bKey = P.Down >= 3;
	if (R.td)
	{
		const bool bHomeTd = P.bReturn ? bHolderHome : ((*R.td == tag::Side::Off) == bHomeOff);
		if (bHomeTd)
		{
			Crowd(NCheerBig, 1.15f);
			Crowd(NCheerBig, 0.8f, 2.4f);
			Roar(1.f, 9.f);
			StadiumSay(NHorn, 0.9f, 0.5f);
			StadiumSay(NFireworks, 0.8f, 1.3f);
			StadiumSay(FName(*FString::Printf(TEXT("PaTouchdown_%hs"), HomeAbbr.c_str())), 1.f, 4.2f);
			Queue.Add(FLater{9.5f, NHouseMusic, FVector::ZeroVector, 1.f, 1.f, ECat::Stadium});
		}
		else
		{
			Crowd(NGroanBig, 1.f);
			Crowd(NAwayCheer, 0.9f, 0.2f);
			Excite *= 0.3f; ExciteHold = 0.f;
			Deflate = 9.f;
		}
		return;
	}
	if (P.bReacted && !R.turnover) { if (R.incomplete) return; }
	if (R.safety)
	{
		if (!bHomeOff) { Crowd(NCheerBig, 1.f); Roar(0.9f, 5.f); } else { Crowd(NGroanBig, 0.9f); Deflate = 5.f; }
		return;
	}
	if (R.turnover)
	{
		if (R.interception) return; // (called as it was caught)
		if (!bHomeOff) { Crowd(NCheerBig, 1.f); Roar(0.95f, 5.f); }
		else { Crowd(NGroanBig, 0.95f); Crowd(NAwayCheer, 0.6f, 0.3f); Deflate = 5.f; }
		return;
	}
	if (P.bReturn)
	{
		if (R.yards >= 35) { if (bHolderHome) { Crowd(NCheerMed, 0.9f); Roar(0.7f, 2.f); } else Crowd(NGroan, 0.6f); }
		else if (!bHolderHome && R.endX < 20) Crowd(NCheerSmall, 0.7f);
		return;
	}
	if (R.kind == tag::PlayKind::Kneel || R.kind == tag::PlayKind::Spike) return;
	const bool bFirst = R.endX - P.Los >= P.ToGo - 1e-6;
	if (R.kind == tag::PlayKind::Sack)
	{
		if (!bHomeOff) { Crowd(bKey ? NCheerBig : NCheerMed, 1.f); Roar(bKey ? 0.9f : 0.75f, 3.f); }
		else Crowd(bKey ? NGroanBig : NGroan, 0.8f);
		return;
	}
	if (bHomeOff)
	{
		if (R.yards >= 20) { Crowd(P.bDeepCalled ? NCheerMed : NCheerBig, P.bDeepCalled ? 0.7f : 0.85f); Roar(0.85f, 3.5f); }
		else if (bFirst) { Crowd(NCheerMed, bKey ? 1.f : 0.75f); Roar(bKey ? 0.7f : 0.5f, 2.f); }
		else if (bKey) Crowd(P.Down >= 4 ? NGroanBig : NGroan, 0.85f);
		else if (R.incomplete) Crowd(NGroan, 0.45f);
		else if (R.yards <= 0) Crowd(NGroan, 0.5f);
		else if (R.yards >= 5) Crowd(NCheerSmall, 0.55f);
		if (bFirst && Rand.FRand() < 0.75f) StadiumSay(FName(*FString::Printf(TEXT("PaFirstDown_%hs"), HomeAbbr.c_str())), 1.f, 2.4f);
	}
	else
	{
		if (bKey && !bFirst) { Crowd(P.Down >= 4 ? NCheerBig : NCheerMed, 1.f); Roar(P.Down >= 4 ? 0.95f : 0.75f, 3.5f); }
		else if (R.yards >= 20) { Crowd(NGroanBig, 0.8f); Excite *= 0.5f; }
		else if (bFirst) Crowd(NGroan, bKey ? 0.8f : 0.4f);
		else if (R.incomplete) { Crowd(NCheerSmall, 0.7f); Roar(0.4f, 1.f); }
		else if (R.yards <= 0) { Crowd(NCheerMed, 0.7f); Roar(0.55f, 1.5f); }
	}
}

void ATagAudio::OnLog(const tag::Game& G, const tag::PlayLog& L)
{
	const std::string& D = L.desc;
	const size_t Pen = D.find("PENALTY on ");
	if (Pen != std::string::npos)
	{
		const size_t Colon = D.find(':', Pen);
		const std::string Abbr = D.substr(Pen + 11, Colon == std::string::npos ? 3 : Colon - Pen - 11);
		const bool bOnHome = Abbr == HomeAbbr;
		// the referee on the stadium PA, then the stands' answer
		std::string Name = Colon == std::string::npos ? std::string() : D.substr(Colon + 2);
		Name = Name.substr(0, Name.find(','));
		Name = Name.substr(0, Name.find(" (#")); // ("False Start (#72 J. Smith)")
		FString Slug;
		for (const char C : Name) if (isalpha(static_cast<unsigned char>(C))) Slug.AppendChar(static_cast<TCHAR>(C));
		const FName Ref(*FString::Printf(TEXT("Ref_%s"), *Slug));
		// (with -TagBoothRef the booth's referee says it in full, number and enforcement: the stock sample would double him)
		if (!bBoothRef) StadiumSay(Has(Ref) ? Ref : FName("Ref_Generic"), 1.f, 0.9f);
		if (bOnHome) Crowd(NBoo, 0.9f, 3.2f); else Crowd(NCheerSmall, 0.8f, 3.2f);
		return;
	}
	if (In(D, "Two-minute warning")) { Field(NWhistleLong, FVector(5000, 0, 100), 0.8f); if (!MusicPlaying()) Music(NMusOut, 0.85f, 0.05f); }
	else if (In(D, "Timeout ")) Field(NWhistleLong, FVector(5000, 0, 100), 0.8f);
	else if (In(D, "Halftime."))
	{
		Field(NWhistleLong, FVector(5000, 0, 100), 0.9f);
		const int32 M = G.score.home - G.score.away;
		Crowd(M >= 0 ? NCheerMed : NBoo, M >= 0 ? 0.8f : 0.5f, 0.6f);
		Music(NMusHalf, 0.8f, 0.6f);
	}
	else if (In(D, "End of "))
	{
		Field(NWhistleLong, FVector(5000, 0, 100), 0.8f);
		Crowd(NCheerSmall, 0.6f, 0.5f);
		if (!In(D, "2nd") && !In(D, "second")) Music(NMusOut, 0.85f, 0.05f);
	}
	else if (In(D, "FINAL:"))
	{
		Field(NWhistleLong, FVector(5000, 0, 100), 0.9f);
		const int32 M = G.score.home - G.score.away;
		if (M > 0) { Crowd(NCheerBig, 1.1f, 0.3f); Roar(1.f, 12.f); StadiumSay(NHorn, 0.9f, 0.8f); StadiumSay(NFireworks, 0.8f, 1.8f); }
		else if (M < 0) { Crowd(NGroan, 0.7f, 0.3f); Deflate = 30.f; }
		Music(NMusFinal, 0.9f, 1.5f, 2.f);
	}
	else if (In(D, "Overtime")) Crowd(NCheerMed, 0.9f);
}

void ATagAudio::TickKick(const FTagAudioView& V)
{
	const tag::Game& G = *V.G;
	if (!K.bOn || V.KickT + 0.05f < K.LastT)
	{
		K = FKickSeen();
		K.bOn = true;
		K.bHomeKicks = V.KickDir == G.worldDir(tag::TeamKey::Home);
		if (V.KickKind == 0 && K.bHomeKicks && V.KickLead > 0.6f && Has(NKickRise))
		{
			K.bRise = true;
			HouseStart(NKickRise, 1.6f, V.KickLead - V.KickT);
		}
	}
	K.LastT = V.KickT;
	const bool bPat = In(V.KickDesc, "extra point");
	if (!K.bSnap && V.KickSnapAt >= 0.f && V.KickT >= V.KickSnapAt)
	{
		K.bSnap = true;
		Field(NSnap, V.Ball, 0.6f);
		Field(NLineCrash, V.Ball, 0.8f, 1.f, 0.3f);
	}
	if (!K.bHeld && V.KickCatchAt >= 0.f && V.KickT >= V.KickCatchAt) { K.bHeld = true; Field(NCatch, V.Ball, 0.6f); }
	if (!K.bStruck && V.KickT >= V.KickLead)
	{
		K.bStruck = true;
		Field(V.KickKind == 1 ? NKickPunt : NKickPlace, V.Ball, 1.f);
		if (K.bRise) { K.bRise = false; HouseStop(0.12f); Crowd(NKickShout, 1.f); Roar(0.5f, 1.5f); }
		if (MusicNow == NMusIntro) MusicStop(2.f);
		if (V.bKickBlocked)
		{
			Field(NSlap, V.Ball, 1.f, 0.9f, 0.18f);
			Field(NHitMed, V.Ball, 0.8f, 1.f, 0.2f);
			Crowd(NGasp, 0.9f, 0.3f);
			if (!K.bHomeKicks) { Crowd(NCheerBig, 0.9f, 0.7f); Roar(0.9f, 4.f); } else Crowd(NGroanBig, 0.9f, 0.7f);
			K.bDone = true;
		}
	}
	if (V.bKickBounce && !K.bBounce) { K.bBounce = true; Field(NBounce, V.Ball, 0.8f); }
	if (V.KickHops > K.Hops) { K.Hops = V.KickHops; Field(NBounce, V.Ball, FMath::Max(0.3f, 0.7f - 0.12f * K.Hops)); }
	if (K.bStruck && !K.bDone && V.KickT >= V.KickLead + V.KickHang)
	{
		K.bDone = true;
		if (V.KickKind == 2)
		{
			const bool bGood = V.KickGood.value_or(In(V.KickDesc, "is GOOD"));
			if (In(V.KickDesc, "upright")) { Field(NPost, V.Ball, 1.f); Crowd(NGasp, 0.8f, 0.1f); }
			if (K.bHomeKicks)
			{
				if (bGood) { Crowd(bPat ? NCheerSmall : NCheerMed, bPat ? 0.8f : 1.f, 0.1f); Roar(bPat ? 0.35f : 0.75f, bPat ? 1.f : 3.f); }
				else Crowd(NGroanBig, bPat ? 0.8f : 1.f, 0.15f);
			}
			else
			{
				if (bGood) { if (!bPat) Crowd(NGroan, 0.5f, 0.2f); }
				else { Crowd(NCheerMed, 0.9f, 0.15f); Roar(0.7f, 2.5f); }
			}
			Field(NWhistle, V.Ball, 0.6f, 1.f, 0.5f);
		}
		else if (!V.bKickBounce) Field(NCatch, V.Ball, 0.8f);
	}
}

void ATagAudio::Cue(FName What, const FVector& AtPos)
{
	static const FName NHard("hardcount"), NAud("audible"), NFlag("flag"), NReplay("replay"), NBanner("banner"), NDef("defcall");
	// (the quarterback's voice is read only for his own calls: the last fed view's sim is gone by a dead-ball flag such
	// as delay of game, and reading it there crashed)
	const int32 Vc = (What == NHard || What == NAud) ? 1 + (P.Sim && P.Sim->qb && P.Sim->qb->p ? P.Sim->qb->p->id : 0) % 4 : 1;
	if (What == NHard) { Field(FName(*FString::Printf(TEXT("Cad%d_Hut"), Vc)), AtPos, 1.f); Field(FName(*FString::Printf(TEXT("Cad%d_Hut"), Vc)), AtPos, 1.f, 1.03f, 0.42f); }
	else if (What == NAud) Field(FName(*FString::Printf(TEXT("Cad%d_Audible"), Vc)), AtPos, 0.95f);
	else if (What == NDef) Field(NDefCall, AtPos, 0.8f);
	else if (What == NFlag) { Field(NWhistle, AtPos, 0.9f); Field(NWhistle, AtPos, 0.9f, 1.f, 0.35f); }
	else if (What == NReplay) PlayNow(FLater{0.f, NUiWhoosh, FVector::ZeroVector, 0.7f, 1.f, ECat::Ui});
	else if (What == NBanner) PlayNow(FLater{0.f, NUiWhoosh, FVector::ZeroVector, 0.45f, 1.15f, ECat::Ui});
}

void ATagAudio::ContactHit(const FVector& AtPos, float Strength)
{
	// (a tackle's hit plays at 0.6-1.0: these at 0.12-0.42, 6 dB and more under it - under the crowd and the voices too, T19;
	// the harder the meeting the lower the pitch)
	const float St = FMath::Clamp(Strength, 0.f, 1.f);
	Field(St < 0.4f ? NHitLight : NHitMed, AtPos, 0.12f + 0.3f * St, 1.06f - 0.1f * St);
}

void ATagAudio::Feed(float Dt, const FTagAudioView& V)
{
	Dt = FMath::Min(Dt, 0.1f);
	if (Fed < 100) ++Fed;
	if (!bRec && RecLeft > 0.f)
	{
		bRec = true;
		UAudioMixerBlueprintLibrary::StartRecordingOutput(this, RecLeft + 5.f);
		UE_LOG(LogTemp, Display, TEXT("TAG audio: recording the output for %.0f s"), RecLeft);
	}
	if (bRec)
	{
		RecLeft -= Dt;
		if (RecLeft <= 0.f)
		{
			bRec = false;
			UAudioMixerBlueprintLibrary::StopRecordingOutput(this, EAudioRecordingExportType::WavFile, TEXT("tag_audio"), FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AudioRec")));
			UE_LOG(LogTemp, Display, TEXT("TAG audio: recording written"));
		}
	}
	// the pause menu: the world's sounds are held by the engine; the menu has its own music, low
	if (V.bPaused != bWasPaused)
	{
		bWasPaused = V.bPaused;
		PlayNow(FLater{0.f, NUiPause, FVector::ZeroVector, 0.8f, V.bPaused ? 1.f : 1.12f, ECat::Ui});
		if (V.bPaused && !MusicPlaying() && !V.bQuiet) { Music(NMusMenu, 0.4f, 1.f); bPauseMusic = true; }
		else if (!V.bPaused && bPauseMusic) { MusicStop(0.6f); bPauseMusic = false; }
	}
	if (V.bPaused) { TickMusic(Dt, 1.f); return; }

	for (int32 i = Queue.Num() - 1; i >= 0; --i)
	{
		Queue[i].T -= Dt;
		if (Queue[i].T > 0.f) continue;
		const FLater L = Queue[i];
		Queue.RemoveAtSwap(i);
		if (L.Bank == NHouseMusic) { if (!House.bOn) HouseStart(NHouseMusic, 1.f); }
		else PlayNow(L);
	}
	const bool bMatch = Game && V.G == Game && V.Stage != EStage::Menu;
	if (bMixTest && bMatch) TickMixTest(Dt);
	TickMix(Dt, V);

	if (!bMatch)
	{
		// the menus: the playlist, and under it the building itself - near empty behind the main menu, filling up on
		// the way to the kickoff (the lobby, then the matchup)
		if (Fed > 3) ScreenChanged();
		MenuHold = FMath::Max(0.f, MenuHold - Dt);
		const bool bFront = !V.bQuiet && !Game && !bHushed && Fed > 3;
		if (bFront && MusicNow != NMusMenu && MenuHold <= 0.f) Music(NMusMenu, 0.85f, 1.2f);
		for (FLoop& L : Loops) L.Target = 0.f;
		if (bFront && !ScreenNow.IsNone())
		{
			Loops[LCalm].Target = ScreenNow == NScrTeams ? 0.30f : ScreenNow == NScrLobby ? 0.22f : ScreenNow == NScrMain ? 0.12f : 0.06f;
			Loops[LBuzz].Target = ScreenNow == NScrTeams ? 0.10f : 0.f;
		}
		TickLoops(Dt, LevelMaster * LevelStadium);
		TickMusic(Dt, 1.f);
		LastStage = V.Stage;
		return;
	}
	const tag::Game& G = *V.G;
	// the open, on its first frame on screen (the match's load is over): the theme, and the building on its feet as the
	// teams come out, filling as the open runs (the crane, the matchup, the kickoff units) until the ball is kicked
	if (bOpenPending)
	{
		bOpenPending = false;
		OpenT = 0.f;
		Music(NMusIntro, 0.9f, 0.8f, 1.2f);
		Roar(0.35f, 6.f);
		Crowd(NCheerMed, 0.65f, 0.6f);
	}
	if (OpenT >= 0.f)
	{
		OpenT += Dt;
		Roar(FMath::Min(0.55f, 0.3f + 0.012f * OpenT), 0.5f);
		if (V.Stage == EStage::Live || K.bStruck || OpenT > 45.f) OpenT = -1.f;
	}
	if (V.Stage != LastStage)
	{
		const EStage Was = LastStage;
		LastStage = V.Stage;
		StageT = 0.f;
			if (V.Stage == EStage::Replay || Was == EStage::Replay) Cue(FName("replay"));
		if (MusicNow == NMusHalf && (V.Stage == EStage::Kick || V.Stage == EStage::Live || V.Stage == EStage::Presnap || V.Stage == EStage::Call)) { MusicStop(2.5f); Music(NMusIn, 0.8f, 0.05f, 2.5f); }
	}
	else StageT += Dt;

	while (LogSeen < G.log.size()) OnLog(G, G.log[LogSeen++]);
	if (LogSeen > G.log.size()) LogSeen = G.log.size();

	if (V.Sim)
	{
		const tag::PlaySim& S = *V.Sim;
		if (&S != P.Sim || S.t + 1e-9 < P.SimT || S.events.size() < P.EvSeen) NewPlay(V);
		P.SimT = S.t;
		if (!P.bSnapped && S.t > 0)
		{
			P.bSnapped = true;
			if (!P.bReturn && S.qb)
			{
				const int32 Vc = 1 + (S.qb->p ? S.qb->p->id : 0) % 4;
				Field(FName(*FString::Printf(TEXT("Cad%d_Hut"), Vc)), At(S.qb, V.Dir), 1.f);
			}
			if (House.bOn) HouseStop(P.bChant ? 1.2f : 0.5f);
			P.bChant = false;
			TapeT = 0.f;
			bTaping = true;
		}
		if (P.bSnapped)
		{
			while (P.EvSeen < S.events.size()) OnEvent(V, S.events[P.EvSeen++]);
			if (!P.bLineCrash && S.t >= 0.36 && !P.bReturn)
			{
				// the two lines meet
				P.bLineCrash = true;
				double X = 0, Y = 0; int32 N = 0;
				for (const tag::Ent* E : S.off) if (E->isOL) { X += E->pos.x; Y += E->pos.y; ++N; }
				if (N && S.endReason != "kneel" && S.endReason != "spike")
					Field(NLineCrash, TagCoords::ToWorld(X / N + 0.5, Y / N, V.Dir, 1.0), 0.95f);
			}
			if (S.isLive())
			{
				// the ball carrier's feet (and the user's man's)
				auto Steps = [&](const tag::Ent* E, float& T)
				{
					if (!E) return;
					const float Sp = static_cast<float>(E->speed());
					if (Sp < 2.5f || E->state == tag::EntState::Down) { T = 0.f; return; }
					T -= Dt;
					if (T > 0.f) return;
					T = 1.f / (2.3f + 0.2f * Sp);
					Field(NStep, At(E, V.Dir) - FVector(0, 0, 80), 0.16f + 0.022f * Sp, Jit(0.94f, 1.06f));
				};
				Steps(S.ball.mode == tag::BallMode::Held ? S.ball.holder : nullptr, P.StepT);
				if (S.user && S.user != S.ball.holder) Steps(S.user, P.StepT2);
				// and the chase: the men running within a few yards of the ball, quieter, each on his own stride
				int32 Heard = 0;
				for (const tag::Ent* E : S.ents)
				{
					if (E == S.ball.holder || E == S.user || E->engaged || E->state == tag::EntState::Down) continue;
					const double Dx = E->pos.x - S.ball.pos.x, Dy = E->pos.y - S.ball.pos.y;
					const float Sp = static_cast<float>(E->speed());
					if (Dx * Dx + Dy * Dy > 64.0 || Sp < 3.5f) continue;
					if (++Heard > 6) break;
					float& T = Feet.FindOrAdd(E, Rand.FRandRange(0.f, 0.3f));
					T -= Dt;
					if (T > 0.f) continue;
					T = 1.f / (2.3f + 0.2f * Sp);
					Field(NStep, At(E, V.Dir) - FVector(0, 0, 80), 0.07f + 0.012f * Sp, Jit(0.92f, 1.08f));
				}
			}
			if (!S.isLive() && !P.bWhistled && S.result)
			{
				P.bWhistled = true;
				OnWhistle(V, *S.result);
			}
		}
		else if (V.Stage == EStage::Presnap && S.preSnap()) TickPresnap(Dt, V);
	}
	if (bTaping)
	{
		if (V.Stage == EStage::Live || V.Stage == EStage::Post) TapeT += Dt;
		if (V.Stage != EStage::Live && (V.Stage != EStage::Post || StageT > 1.3f)) bTaping = false;
	}
	if (V.Stage == EStage::Replay && V.ReplayT >= 0.f)
	{
		// the replay, heard: the play's own hits, catches and kicks at the moments they are shown, slowed like the picture.
		// A package of angles (Game/MatchBroadcast.cpp): each angle starts back in the play (its hits are heard again,
		// once, with a soft whoosh under the wipe); a cut that skips ahead plays nothing it skipped (no burst of hits)
		if (TapeSeen >= 0.f && V.ReplayT < TapeSeen - 0.3f) PlayNow(FLater{0.f, NUiWhoosh, FVector::ZeroVector, 0.35f, 0.9f, ECat::Ui});
		if (TapeSeen < 0.f || V.ReplayT < TapeSeen || V.ReplayT - TapeSeen > 0.3f) TapeSeen = V.ReplayT;
		// the pitch follows the replay's speed (its ramp 1x -> 0.35x -> 0.7x): a hit at a third of real speed sounds deeper
		const float Rate = Dt > 1e-4f ? FMath::Clamp((V.ReplayT - TapeSeen) / Dt, 0.f, 1.5f) : TapeRate;
		TapeRate += (Rate - TapeRate) * FMath::Min(1.f, Dt * 6.f);
		const float Slow = FMath::Clamp(0.62f + 0.38f * TapeRate, 0.72f, 1.f);
		for (const FLater& L : Tape)
			if (L.T > TapeSeen && L.T <= V.ReplayT) PlayNow(FLater{0.f, L.Bank, L.Pos, L.Vol * 0.85f, L.Pitch * Slow, ECat::Field});
		TapeSeen = V.ReplayT;
	}
	else { TapeSeen = -1.f; TapeRate = 0.5f; }
	if (V.Stage == EStage::Kick && V.KickKind >= 0) TickKick(V);
	else { if (K.bRise && !(V.Sim && P.bReturn)) { K.bRise = false; HouseStop(0.3f); } K.bOn = false; }

	// ---- the crowd's mood -> the layers
	const float St = Stakes(G);
	const bool bHomeBall = G.poss == tag::TeamKey::Home;
	const bool bPre = (V.Stage == EStage::Presnap || V.Stage == EStage::Call) && G.phase == tag::GamePhase::Scrimmage;
	float NoiseWant = 0.f;
	if (bPre && !bHomeBall)
	{
		static const float ByDown[4] = {0.2f, 0.3f, 0.85f, 1.f};
		NoiseWant = ByDown[FMath::Clamp(G.down, 1, 4) - 1] * (0.55f + 0.45f * St);
		if (G.ballOn >= 90) NoiseWant = FMath::Max(NoiseWant, 0.8f);
		if (V.Stage == EStage::Call) NoiseWant *= 0.55f;
	}
	DNoise = NoiseWant > DNoise ? FMath::Min(NoiseWant, DNoise + 0.4f * Dt) : FMath::Max(NoiseWant, DNoise - 1.3f * Dt);
	float SwellWant = 0.f;
	if (V.Stage == EStage::Live && V.Sim && P.bSnapped && V.Sim->isLive())
	{
		const tag::Ball& B = V.Sim->ball;
		if (B.mode == tag::BallMode::Held && B.holder && B.holder->side == tag::Side::Off && !P.bReturn)
		{
			const float Gn = FMath::Clamp((static_cast<float>(B.holder->pos.x - P.Los) - 4.f) / 16.f, 0.f, 1.f);
			SwellWant = P.bOffHome ? Gn : 0.3f * Gn;
			if (P.bOffHome && Gn > 0.45f) Roar(0.3f + 0.55f * Gn, 0.4f);
		}
		else if (B.mode == tag::BallMode::Held && B.holder && P.bReturn)
		{
			SwellWant = IsHome(B.holder->p) ? 0.6f : 0.25f;
			if (IsHome(B.holder->p) && B.holder->speed() > 6.0 && V.Sim->t > 6.0) Roar(0.55f, 0.4f);
		}
		else if (B.mode == tag::BallMode::Air && !B.away && B.targetSpot && B.targetSpot->x - B.throwX > 17.0) SwellWant = 0.75f;
	}
	Swell += (SwellWant - Swell) * FMath::Min(1.f, Dt * (SwellWant > Swell ? 3.f : 1.2f));
	if (ExciteHold > 0.f) ExciteHold -= Dt;
	else Excite = FMath::Max(0.f, Excite - Dt * (0.05f + 0.2f * Excite));
	Deflate = FMath::Max(0.f, Deflate - Dt);

	float Calm = 0.75f + 0.25f * St, Buzz = 0.1f + 0.45f * St + 0.55f * Swell + 0.3f * DNoise + 0.25f * Excite;
	if (bPre && bHomeBall) { Calm *= 0.82f; Buzz *= 0.6f; } // the home crowd lets its offense hear
	if (Deflate > 0.f) { const float Dn = FMath::Min(1.f, Deflate / 2.f); Calm *= 1.f - 0.4f * Dn; Buzz *= 1.f - 0.6f * Dn; }
	const int32 Margin = G.score.home - G.score.away;
	if (G.q >= 3 && Margin <= -17) { Calm *= 0.7f; Buzz *= 0.6f; }      // the building has given up on this one
	if (V.Stage == EStage::Halftime || V.Stage == EStage::Final) { Calm *= 0.7f; Buzz = 0.08f; }
	Loops[LCalm].Target = Calm;
	Loops[LBuzz].Target = FMath::Clamp(Buzz, 0.f, 1.f);
	Loops[LRoar].Target = Excite * Excite * (3.f - 2.f * Excite) * (Margin <= -17 && G.q >= 3 ? 0.75f : 1.f);
	Loops[LNoise].Target = DNoise;
	const float Wind = static_cast<float>(FMath::Sqrt(G.weather.windX * G.weather.windX + G.weather.windY * G.weather.windY));
	Loops[LRain].Target = G.weather.precip == tag::Precip::Rain ? 1.f : 0.f;
	Loops[LWind].Target = FMath::Clamp((Wind - 8.f) / 14.f, 0.f, 1.f) + (G.weather.precip == tag::Precip::Snow ? 0.35f : 0.f);
	TickLoops(Dt, LevelMaster * LevelStadium * Duck);
	TickMusic(Dt, MusicDuck);
}

void ATagAudio::TickMix(float Dt, const FTagAudioView& V)
{
	// the line on the air (BoothLine; the director's Talking() too, for a voice that never said what it was)
	const bool bOnAir = V.bBoothTalking || bTestOnAir;
	float CrowdWant = 1.f, MusicWant = 1.f;
	if (bLegacyMix)
	{
		CrowdWant = bOnAir ? kDuckOld : 1.f;
		MusicWant = bOnAir ? kDuckOldMusic : 1.f;
		Duck += (CrowdWant - Duck) * FMath::Min(1.f, Dt * 5.f);
		MusicDuck = MusicWant;
		return;
	}
	if (bOnAir)
	{
		const int32 E = VoiceEnergy < 0 ? 1 : VoiceEnergy;
		if (E >= 3 || Excite > 0.8f)
		{
			// the big call: the building is not pulled down under him; a roaring one comes up around the call (T18: on
			// the big play the announcers sit 6-10 dB under the crowd, the call's own lift makes up the rest)
			CrowdWant = Excite > 0.5f ? kBloom : 1.f;
		}
		else if (E == 2 && !bVoiceTalk) { CrowdWant = kDuckCall; MusicWant = kDuckCallMusic; }
		else { CrowdWant = kDuckTalk; MusicWant = kDuckTalkMusic; }
	}
	// in fast, out slow: the crowd is pulled down as the man starts (~0.15 s) and let back up behind him (~0.45 s)
	auto Ride = [Dt](float& Fader, float Want) { Fader += (Want - Fader) * FMath::Min(1.f, Dt * (Want < Fader ? 7.f : 2.2f)); };
	Ride(Duck, CrowdWant);
	Ride(MusicDuck, MusicWant);
	// the crowd's and the PA's one-shots still sounding ride the same fader as the beds
	for (int32 i = Shots.Num() - 1; i >= 0; --i)
	{
		FShot& S = Shots[i];
		UAudioComponent* C = S.C.Get();
		if (!C || !C->IsPlaying()) { Shots.RemoveAtSwap(i); continue; }
		if (FMath::Abs(S.Set - Duck) > 0.01f)
		{
			S.Set = Duck;
			C->SetVolumeMultiplier(S.Base * Duck);
		}
	}
}

float ATagAudio::LastShotDuck() const
{
	if (LastShotBase <= 0.f) return 0.f;
	if (const UAudioComponent* C = LastShot.Get(); C && C->IsPlaying()) return C->VolumeMultiplier / LastShotBase;
	return LastShotGain / LastShotBase;
}

void ATagAudio::MixCheck(const TCHAR* What, float Got, float Want, float Tol)
{
	// gains compared in dB (a fader at 0 is -inf: compared as -60)
	auto Db = [](float G) { return G > 0.001f ? 20.f * FMath::LogX(10.f, G) : -60.f; };
	const bool bOk = FMath::Abs(Db(Got) - Db(Want)) <= Tol;
	(bOk ? MixPass : MixFail)++;
	UE_LOG(LogTemp, Display, TEXT("TAG mix test %s: %+.2f dB (want %+.2f +-%.1f) %s"), What, Db(Got), Db(Want), Tol, bOk ? TEXT("ok") : TEXT("FAIL"));
}

void ATagAudio::TickMixTest(float Dt)
{
	// -TagMixTest: over a CPU game (no booth: the test is the booth), a calm line, an excited call and a touchdown call,
	// each held until the faders settle; the gains the mix applied are checked against the design (or, with
	// -TagMixLegacy, against the old fixed duck), then the result for the gate (Tools/QA/gate.ps1)
	if (MixTestStep < 0) return;
	if (MixTestT < 0.f) MixTestT = 0.f;
	MixTestT += Dt;
	const bool bOld = bLegacyMix;
	const float Lvl = LevelMaster * LevelStadium;
	// the building held quiet enough that the calm line and the call are not "the big one" (the CPU game plays on)
	if (MixTestStep < 3) { Excite = FMath::Min(Excite, 0.3f); ExciteHold = 0.f; }
	if (MixTestStep == 0 && MixTestT >= 4.f)
	{
		MixTestStep = 1;
		bTestOnAir = true;
		BoothLine(0, true);                       // a calm read
		Crowd(NCheerSmall, 0.8f, 0.4f);           // a one-shot that starts under it
	}
	else if (MixTestStep == 1 && MixTestT >= 6.5f)
	{
		MixTestStep = 2;
		MixCheck(TEXT("calm talk: crowd beds"), Duck, bOld ? kDuckOld : kDuckTalk, 0.5f);
		MixCheck(TEXT("calm talk: music"), MusicDuck, bOld ? kDuckOldMusic : kDuckTalkMusic, 0.5f);
		MixCheck(TEXT("calm talk: crowd one-shot"), LastShotDuck(), bOld ? 1.f : kDuckTalk, 0.6f);
		MixCheck(TEXT("calm talk: bed component"), Loops[LCalm].bStarted && Loops[LCalm].C ? Loops[LCalm].C->VolumeMultiplier / FMath::Max(0.001f, Loops[LCalm].Vol * kCalm * Lvl) : 0.f,
			bOld ? kDuckOld : kDuckTalk, 0.5f);
		MixCheck(TEXT("calm talk: voice e0"), VoiceEnergyGain(0, 0.4f, 1.f), bOld ? 1.f : kVoiceByEnergy[0], 0.1f);
		BoothLine(2, false);                      // an excited call of the action
	}
	else if (MixTestStep == 2 && MixTestT >= 9.f)
	{
		MixTestStep = 3;
		MixCheck(TEXT("live call e2: crowd beds"), Duck, bOld ? kDuckOld : kDuckCall, 0.5f);
		MixCheck(TEXT("live call e2: music"), MusicDuck, bOld ? kDuckOldMusic : kDuckCallMusic, 0.5f);
		MixCheck(TEXT("live call e2: voice"), VoiceEnergyGain(2, 0.4f, 1.f), bOld ? 1.f : kVoiceByEnergy[2], 0.1f);
		// the touchdown: the building erupts with the call
		Roar(1.f, 9.f);
		BoothLine(3, false);
		Crowd(NCheerBig, 1.15f);
	}
	else if (MixTestStep == 3 && MixTestT >= 11.5f)
	{
		MixTestStep = 4;
		MixCheck(TEXT("touchdown e3: crowd beds"), Duck, bOld ? kDuckOld : kBloom, 0.5f);
		MixCheck(TEXT("touchdown e3: crowd one-shot"), LastShotDuck(), bOld ? 1.f : kBloom, 0.6f);
		MixCheck(TEXT("touchdown e3: voice (quiet clip)"), VoiceEnergyGain(3, 0.25f, 1.f), bOld ? 1.f : kVoiceByEnergy[3], 0.1f);
		// a hot clip: the lift stops where its peak reaches the ceiling at the output (never under the level it was built at)
		const float PeakOut = 0.85f * MasterTrim();
		MixCheck(TEXT("touchdown e3: voice (hot clip) peak at the output"), VoiceEnergyGain(3, 0.85f, 1.f) * PeakOut,
			bOld ? PeakOut : FMath::Max(PeakOut, FMath::Min(kVoiceCeiling, PeakOut * kVoiceByEnergy[3])), 0.1f);
		bTestOnAir = false;
		BoothLine(-1, false);
	}
	else if (MixTestStep == 4 && MixTestT >= 14.f)
	{
		MixTestStep = 5;
		MixCheck(TEXT("after the line: crowd beds"), Duck, 1.f, 0.3f);
		MixCheck(TEXT("after the line: music"), MusicDuck, 1.f, 0.3f);
		const bool bNoLim = FParse::Param(FCommandLine::Get(), TEXT("TagNoLimiter"));
		(bNoLim || Limiter ? MixPass : MixFail)++;
		UE_LOG(LogTemp, Display, TEXT("TAG mix test master limiter: %s, trim %+.1f dB %s"), Limiter ? TEXT("on") : TEXT("off"), 20.f * FMath::LogX(10.f, MasterTrim()),
			bNoLim || Limiter ? TEXT("ok") : TEXT("FAIL"));
		MixTestStep = -1;
		TagResult::Finish(TEXT("TagMixTest"), MixPass, MixFail);
	}
}
