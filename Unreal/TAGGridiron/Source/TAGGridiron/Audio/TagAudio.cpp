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
#include "AudioDevice.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Engine/World.h"

namespace
{
	TWeakObjectPtr<ATagAudio> GTagAudio;
	using EStage = FTagAudioView::EStage;

	// the mix: how loud each crowd layer sits when it is fully up (the assets are all built to one loudness)
	constexpr float kCalm = 1.0f, kBuzz = 0.95f, kRoar = 1.6f, kNoise = 1.35f, kRain = 0.7f, kWind = 0.6f;
	constexpr float kField = 1.0f, kCrowdShot = 0.95f, kHouse = 0.42f, kPa = 0.62f, kMusic = 0.72f, kUi = 0.8f;

	const FName NCalm("CrowdCalm"), NBuzz("CrowdBuzz"), NRoar("CrowdRoar"), NNoise("CrowdNoise"), NRain("WxRain"), NWind("WxWind");
	const FName NCheerBig("CheerBig"), NCheerMed("CheerMed"), NCheerSmall("CheerSmall"), NGroan("Groan"), NGroanBig("GroanBig"), NBoo("Boo"),
		NGasp("Gasp"), NAwayCheer("AwayCheer"), NKickRise("KickRise"), NKickShout("KickShout"), NChant("ChantDefense");
	const FName NHitLight("HitLight"), NHitMed("HitMed"), NHitHeavy("HitHeavy"), NHitHuge("HitHuge"), NLineCrash("LineCrash"), NBodyFall("BodyFall"),
		NCatch("Catch"), NSlap("Slap"), NSnap("Snap"), NThrow("Throw"), NBounce("Bounce"), NKickPunt("KickPunt"), NKickPlace("KickPlace"), NPost("Post"),
		NStep("Step"), NCut("Cut"), NGrunt("Grunt"), NBallOut("BallOut"), NWhistle("Whistle"), NWhistleLong("WhistleLong"), NDefCall("DefCall");
	const FName NHorn("Horn"), NFireworks("Fireworks"), NHouseMusic("HouseMusic"), NPaThird("PaThirdDown"), NPaNoise("PaMakeNoise");
	const FName NMusMenu("MusMenu"), NMusIntro("MusIntro"), NMusOut("MusBumperOut"), NMusIn("MusBumperIn"), NMusHalf("MusHalftime"), NMusFinal("MusFinal"),
		NMusScore("MusScore");
	const FName NUiWhoosh("UiWhoosh"), NUiPause("UiPause");

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
	if (GConfig)
	{
		GConfig->SetInt(TEXT("TAG.Settings"), kLevelKeys[Which], GLevels[Which], GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	if (ATagAudio* A = GTagAudio.Get()) A->SetLevels(GLevels[0] / 4.f, GLevels[1] / 4.f, GLevels[2] / 4.f);
}

void ATagAudio::Ui(const TCHAR* Bank, float Vol)
{
	if (ATagAudio* A = GTagAudio.Get()) A->PlayNow(FLater{0.f, FName(Bank), FVector::ZeroVector, Vol, 1.f, ECat::Ui});
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
	Rand.Initialize(static_cast<int32>(FPlatformTime::Cycles64() & 0x7fffffff));
	T0 = FPlatformTime::Seconds();
	FindBanks();
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
		RecLeft = Rec;
		bRec = true;
		UAudioMixerBlueprintLibrary::StartRecordingOutput(this, Rec + 5.f);
		UE_LOG(LogTemp, Display, TEXT("TAG audio: recording the output for %.0f s"), Rec);
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

void ATagAudio::Note(const TCHAR* Kind, FName Bank, float Vol)
{
	Counts.FindOrAdd(Bank)++;
	if (bLog) UE_LOG(LogTemp, Display, TEXT("TAG audio %.2f %s %s %.2f"), FPlatformTime::Seconds() - T0, Kind, *Bank.ToString(), Vol);
}

void ATagAudio::PlayNow(const FLater& L)
{
	USoundWave* W = Pick(L.Bank);
	if (!W || L.Vol <= 0.f) { if (bLog && !W) UE_LOG(LogTemp, Display, TEXT("TAG audio %.2f (no bank) %s"), FPlatformTime::Seconds() - T0, *L.Bank.ToString()); return; }
	switch (L.Cat)
	{
	case ECat::Field:
		UGameplayStatics::SpawnSoundAtLocation(this, W, L.Pos, FRotator::ZeroRotator, L.Vol * kField * LevelMaster * LevelStadium, L.Pitch, 0.f, FieldAtt, nullptr, true);
		Note(TEXT("field"), L.Bank, L.Vol);
		break;
	case ECat::Crowd:
	case ECat::Stadium:
		if (UAudioComponent* C = UGameplayStatics::CreateSound2D(this, W, L.Vol * (L.Cat == ECat::Crowd ? kCrowdShot : kPa) * LevelMaster * LevelStadium, L.Pitch, 0.f, nullptr, false, true))
		{
			C->bIsUISound = false; // held with the world by the pause menu
			C->Play();
		}
		Note(L.Cat == ECat::Crowd ? TEXT("crowd") : TEXT("stadium"), L.Bank, L.Vol);
		break;
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
	LevelMaster = Master; LevelMusic = MusicLevel; LevelStadium = Stadium;
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
		const float G = i >= LRain ? LevelMaster * LevelStadium : CrowdGain;
		L.C->SetVolumeMultiplier(FMath::Max(0.001f, L.Vol * Gain[i] * G));
	}
}

void ATagAudio::StopLoops()
{
	for (FLoop& L : Loops)
	{
		if (L.C && L.bStarted) L.C->Stop();
		L.bStarted = false;
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
		else House.C->SetVolumeMultiplier(FMath::Max(0.001f, House.Vol * kHouse * LevelMaster * LevelStadium));
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
	Excite = 0.25f; ExciteHold = 4.f; // the teams are out: the building is up for the kickoff
	DNoise = Swell = Deflate = 0.f;
	Queue.Reset();
	Counts.Reset();
	T0 = FPlatformTime::Seconds();
	for (const FName B : {NCalm, NBuzz, NRoar, NNoise, NCheerBig, NCheerMed, NCheerSmall, NGroan, NGasp, NBoo, NHitLight, NHitMed, NHitHeavy, NHitHuge, NLineCrash,
		NBodyFall, NCatch, NSnap, NThrow, NWhistle, NKickPlace, NKickPunt, NStep, NGrunt})
	{
		FBank* Bk = Banks.Find(B);
		if (Bk && !Bk->bLoaded) { Pick(B); Bk->Last = -1; }
	}
	Music(NMusIntro, 0.9f, 0.05f, 1.2f);
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
	}
	Game = nullptr;
	bHushed = false;
	P = FPlay();
	K = FKickSeen();
	Queue.Reset();
	StopLoops();
	HouseStop(0.3f);
	if (MusicNow != NMusMenu) MusicStop(1.f);
	bPauseMusic = false;
}

void ATagAudio::Hush()
{
	for (FDeck& D : Decks)
	{
		if (D.C) D.C->Stop();
		D.bOn = false;
		D.Vol = D.Target = 0.f;
	}
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
		StadiumSay(Has(Ref) ? Ref : FName("Ref_Generic"), 1.f, 0.9f);
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
	const int32 Vc = 1 + (P.Sim && P.Sim->qb && P.Sim->qb->p ? P.Sim->qb->p->id : 0) % 4;
	if (What == NHard) { Field(FName(*FString::Printf(TEXT("Cad%d_Hut"), Vc)), AtPos, 1.f); Field(FName(*FString::Printf(TEXT("Cad%d_Hut"), Vc)), AtPos, 1.f, 1.03f, 0.42f); }
	else if (What == NAud) Field(FName(*FString::Printf(TEXT("Cad%d_Audible"), Vc)), AtPos, 0.95f);
	else if (What == NDef) Field(NDefCall, AtPos, 0.8f);
	else if (What == NFlag) { Field(NWhistle, AtPos, 0.9f); Field(NWhistle, AtPos, 0.9f, 1.f, 0.35f); }
	else if (What == NReplay) PlayNow(FLater{0.f, NUiWhoosh, FVector::ZeroVector, 0.7f, 1.f, ECat::Ui});
	else if (What == NBanner) PlayNow(FLater{0.f, NUiWhoosh, FVector::ZeroVector, 0.45f, 1.15f, ECat::Ui});
}

void ATagAudio::Feed(float Dt, const FTagAudioView& V)
{
	Dt = FMath::Min(Dt, 0.1f);
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
	const float DuckWant = V.bBoothTalking ? 0.74f : 1.f;
	Duck += (DuckWant - Duck) * FMath::Min(1.f, Dt * 5.f);

	const bool bMatch = Game && V.G == Game && V.Stage != EStage::Menu;
	if (!bMatch)
	{
		// the menus
		if (!V.bQuiet && MusicNow != NMusMenu && !Game && !bHushed) Music(NMusMenu, 0.85f, 1.2f);
		for (FLoop& L : Loops) L.Target = 0.f;
		TickLoops(Dt, LevelMaster * LevelStadium);
		TickMusic(Dt, 1.f);
		LastStage = V.Stage;
		return;
	}
	const tag::Game& G = *V.G;
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
		// the replay, heard: the play's own hits, catches and kicks at the moments they are shown, slowed like the picture
		if (TapeSeen < 0.f || V.ReplayT < TapeSeen) TapeSeen = V.ReplayT;
		for (const FLater& L : Tape)
			if (L.T > TapeSeen && L.T <= V.ReplayT) PlayNow(FLater{0.f, L.Bank, L.Pos, L.Vol * 0.85f, L.Pitch * 0.8f, ECat::Field});
		TapeSeen = V.ReplayT;
	}
	else TapeSeen = -1.f;
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
	TickMusic(Dt, V.bBoothTalking ? 0.55f : 1.f);
}
