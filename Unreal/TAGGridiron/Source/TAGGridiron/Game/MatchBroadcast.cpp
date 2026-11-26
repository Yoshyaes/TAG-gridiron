// The broadcast around the game (AMatchDirector, MatchDirector.h "the broadcast"): the view's hard cuts told to the
// renderer and recorded for the telemetry, which plays earn a replay, the field's graphics as TV draws them, and the
// stands moving with the home crowd's mood. Small hooks in MatchDirector.cpp call in here (Research/NextLevel/04 A4,
// A7; 05 A2; 07 A5).
//
// And the package around the plays (04 A2, A3, A5, A8): the replay's angles, ramp and wipe; the shots between the plays
// with their memory (Game/TagRecent.h); the pace setting and the one skip rule; the pregame open. What is on screen is
// published as plain data (Game/TagBroadcast.h: ReplayNow, ShotNow, PregameNow, WipeNow) for the on-screen graphics.
// Nothing here reads or steps the sim's dice: every pick comes from the game's seed, the play's result and the store.
#include "Game/MatchDirector.h"

#include "Audio/TagAudio.h"
#include "Booth/TagBooth.h"
#include "Camera/PlayerCameraManager.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Game/TagRecent.h"
#include "Game/TagResult.h"
#include "Game/TagWipe.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Render/Athlete.h"
#include "Render/Coords.h"
#include "Render/Stadium.h"
#include "Sim/Abilities.h"
#include "UI/BroadcastGfx.h"
#include "UI/GridironOverlay.h"
#include "UnrealClient.h"
#include <utility>

using namespace TagCoords;

namespace TagRecentTest { void Run(); } // (Game/TagRecent.cpp)

// ------------------------------------------------------------------ camera cuts (07 A5)
void AMatchDirector::MarkCameraCut(const TCHAR* Why)
{
	bCamCutPending = true;
	if (!CamCutWhy) CamCutWhy = Why;
}

void AMatchDirector::CommitCameraFrame(float Dt)
{
	if (!Camera || !GetWorld()) return;
	const FVector P = Camera->GetActorLocation();
	const FVector F = Camera->GetActorForwardVector();
	const UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
	const float Focal = Lens ? Lens->CurrentFocalLength : 0.f;
	bool bCut = bCamCutPending;
	const bool bMarked = bCamCutPending;
	float Ang = 0.f;
	FString Jump;
	if (CamFrame.bValid)
	{
		// a cut nobody marked (a look-dev shot, a test's framing, a path still to be marked): one frame's jump past what
		// any operator's move does - the dolly at ~12 m/s, a whip pan well under 400 deg/s, a zoom far slower than e^3/s
		const float SafeDt = FMath::Max(Dt, 1e-3f);
		const float Moved = static_cast<float>(FVector::Dist(P, CamFrame.Pos));
		Ang = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(F, CamFrame.Fwd)), -1.f, 1.f)));
		const float Zoom = Focal > 0.f && CamFrame.Focal > 0.f ? FMath::Abs(FMath::Loge(Focal / CamFrame.Focal)) : 0.f;
		const bool bJump = (Moved > 300.f && Moved / SafeDt > 3000.f) || (Ang > 12.f && Ang / SafeDt > 400.f) || (Zoom > 0.3f && Zoom / SafeDt > 3.f);
		if (bJump && !bCut)
		{
			bCut = true;
			Jump = FString::Printf(TEXT("jump %.0f cm, %.0f deg, lens %.0f -> %.0f mm"), Moved, Ang, CamFrame.Focal, Focal);
		}
	}
	if (bCut)
	{
		// the frame after a cut starts its temporal history (TSR, motion blur's vectors) afresh, as the renderer is told here
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (APlayerController* PC = It->Get(); PC && PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame();
		++CamFrame.Cuts;
		UE_LOG(LogTemp, Display, TEXT("TAG camera cut: %s (state %d, cam %d%s)"), CamCutWhy ? CamCutWhy : *Jump, static_cast<int32>(State), static_cast<int32>(CamMode),
			bHighlights ? *FString::Printf(TEXT(", clip %d frame %d"), HlClip, HlFrame - 1) : TEXT(""));
		NoteCutForBroadcast(bMarked); // (a replay's cuts against the wipe's cover)
	}
	CamFrame.AngSpeed = CamFrame.bValid && Dt > 1e-4f ? Ang / Dt : 0.f;
	CamFrame.bCut = bCut;
	CamFrame.Pos = P;
	CamFrame.Fwd = F;
	CamFrame.Focal = Focal;
	CamFrame.bValid = true;
	bCamCutPending = false;
	CamCutWhy = nullptr;
}

FString AMatchDirector::CameraTelemetryCols()
{
	// camx..camz the lens (cm), camfx..camfz the way it looks, camfoc the focal length (mm), camang the view's swing
	// since the frame before (deg/s; a cut frame's is the cut's), camcut 1 on a cut frame, camnear the nearest head or
	// pelvis of any man on the field to the lens (cm; under 60 the lens is in a helmet or a body), camhold the ball
	// carrier on screen (1 / 0; -1 nobody holds it). The frame the last screenshot showed: CommitCameraFrame's numbers
	// and the bodies as they were drawn.
	const FVector L = CamFrame.Pos;
	float Near = NAN;
	for (const auto& KV : Athletes)
	{
		const AAthlete* O = KV.Value;
		if (!O || O->IsHidden()) continue;
		for (const TCHAR* Bn : {TEXT("head"), TEXT("pelvis")})
		{
			const float D = static_cast<float>(FVector::Dist(O->BoneWorld(Bn), L));
			if (FMath::IsNaN(Near) || D < Near) Near = D;
		}
	}
	int32 Hold = -1;
	if (Sim && Game && Sim->ball.mode == tag::BallMode::Held && Sim->ball.holder)
	{
		Hold = 0;
		const tag::Ent* Hd = Sim->ball.holder;
		const AAthlete* HA = AthleteFor(Hd->p, Hd->side == tag::Side::Off ? Game->poss : tag::other(Game->poss));
		APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		FVector2D VP(0.0, 0.0);
		if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(VP);
		FVector2D S2;
		if (HA && !HA->IsHidden() && PC && VP.X > 0.0 && VP.Y > 0.0 && PC->ProjectWorldLocationToScreen(HA->GetActorLocation() + FVector(0, 0, 90), S2, false))
			Hold = S2.X >= 0.0 && S2.Y >= 0.0 && S2.X <= VP.X && S2.Y <= VP.Y ? 1 : 0;
	}
	return FString::Printf(TEXT(",%.1f,%.1f,%.1f,%.4f,%.4f,%.4f,%.1f,%.1f,%d,%.1f,%d"), L.X, L.Y, L.Z, CamFrame.Fwd.X, CamFrame.Fwd.Y, CamFrame.Fwd.Z, CamFrame.Focal,
		CamFrame.AngSpeed, CamFrame.bCut ? 1 : 0, Near, Hold);
}

// ------------------------------------------------------------------ the pace setting (04 A8)
namespace
{
	ETagPace GTagPace = ETagPace::Auto;
	bool bTagPaceRead = false;
	const TCHAR* const kTagPaceKey = TEXT("Presentation");
}

ETagPace TagPace::Setting()
{
	if (!bTagPaceRead && GConfig)
	{
		bTagPaceRead = true;
		int32 V = 0;
		GConfig->GetInt(TEXT("TAG.Settings"), kTagPaceKey, V, GGameUserSettingsIni);
		GTagPace = static_cast<ETagPace>(FMath::Clamp(V, 0, 3));
	}
	return GTagPace;
}

void TagPace::Step()
{
	GTagPace = static_cast<ETagPace>((static_cast<int32>(Setting()) + 1) % 4); // Auto -> Full -> Standard -> Quick -> Auto
	if (GConfig && !FParse::Param(FCommandLine::Get(), TEXT("TagMenuAudio"))) // (the scripted walk through the menus keeps nothing)
	{
		GConfig->SetInt(TEXT("TAG.Settings"), kTagPaceKey, static_cast<int32>(GTagPace), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	UE_LOG(LogTemp, Display, TEXT("TAG presentation pace set: %s"), Name(GTagPace));
}

ETagPace TagPace::Current(bool bWatch)
{
	// -TagPace=full|standard|quick: over the setting (captures, tests)
	static const ETagPace Forced = []
	{
		FString V;
		if (!FParse::Value(FCommandLine::Get(), TEXT("TagPace="), V)) return ETagPace::Auto;
		return V.Equals(TEXT("full"), ESearchCase::IgnoreCase) ? ETagPace::Full : V.Equals(TEXT("quick"), ESearchCase::IgnoreCase) ? ETagPace::Quick
			: V.Equals(TEXT("standard"), ESearchCase::IgnoreCase) ? ETagPace::Standard : ETagPace::Auto;
	}();
	if (Forced != ETagPace::Auto) return Forced;
	const ETagPace Set = Setting();
	return Set != ETagPace::Auto ? Set : bWatch ? ETagPace::Full : ETagPace::Standard;
}

const TCHAR* TagPace::Name(ETagPace P)
{
	switch (P)
	{
	case ETagPace::Full: return TEXT("Full");
	case ETagPace::Standard: return TEXT("Standard");
	case ETagPace::Quick: return TEXT("Quick");
	default: return TEXT("Auto");
	}
}

const TCHAR* TagPace::Blurb(ETagPace P)
{
	switch (P)
	{
	case ETagPace::Full: return TEXT("Every replay angle, the reactions after the big plays, the pregame open in full.");
	case ETagPace::Standard: return TEXT("A replay and a reaction after the big plays; routine plays go straight on.");
	case ETagPace::Quick: return TEXT("Replays of touchdowns and takeaways only, no shots between plays: straight back to the call.");
	default: return TEXT("Full when you watch a game, Standard when you play one.");
	}
}

// ------------------------------------------------------------------ the package's state
struct FTagBcBeat
{
	ETagShot Type = ETagShot::None;
	TWeakObjectPtr<AAthlete> Subject;
	FVector Fixed = FVector::ZeroVector; // (the crowd shot's end; the wide reset's spot)
	int32 Team = -1;                     // 1 home, 0 away
	float Secs = 0.f;
	FString Why;
	bool bLead = false;                  // the shot at the whistle (the Post), not a capper after the replay
};

struct FTagBcShot
{
	FTagBcBeat Beat;
	FString Variant;
	float T = 0.f, BlockedT = 0.f, NearMin = 1e9f;
	bool bPlaced = false, bJustPlaced = false, bEndNow = false;
	const TCHAR* EndHow = TEXT("done");
	FVector CamPos = FVector::ZeroVector, CamVel = FVector::ZeroVector, Look = FVector::ZeroVector, LookVel = FVector::ZeroVector;
	FVector Off = FVector::ZeroVector;   // lens - subject at the placement (the rig pans after him; it does not hop round him)
	float Focal = 50.f, Aperture = 1.2f;
	int32 Recuts = 0, Frames = 0, BadFrames = 0;
};

/** One play's context for the shots (made at the whistle, before the play is in the book). */
struct FTagBcPlay
{
	bool bValid = false, bTd = false, bTake = false, bSack = false, bScore = false, bBig = false, bBigHit = false;
	bool bThird = false, bFourth = false, bConv = false, bStop = false, bLate = false, bReplay = false, bFlag = false;
	bool bHasHero = false, bHasBeaten = false, bCrowdOk = false;
	int32 BenTeam = -1;                  // the side the play went for: 1 home, 0 away, -1 neither
	TWeakObjectPtr<AAthlete> Hero, Beaten;
	FVector Spot = FVector::ZeroVector;
	size_t LogN = 0;                     // (the log's length at the whistle: a flag adds to it)
	FString Seq;                         // the shots it got, "hero>opponent>wide" (tests)
	FString SeqVar;                      // ... with their variants
};

struct FTagReplayRun
{
	ETagReplayAngle Type = ETagReplayAngle::Reverse;
	float T0 = 0.f, T1 = 0.f, KeyT = 0.f;
	float Pre = 1.f, Slow = 0.35f, Post = 0.7f;   // the ramp's speeds: before, through and after the key moment
	float Side = 1.f;                              // which sideline / pylon (+1: world +Y)
	int32 SubjectId = -1;
	float Shown = 0.f, SlowestRate = 9.f, SlowestAt = 0.f;
	int32 Frames = 0, LensBad = 0;
};

struct FTagBcOpen
{
	bool bOn = false, bLong = false;
	int32 Stage = 0, CraneVar = 0;
	float T = 0.f, StageT = 0.f, Crane = 3.f, Card = 3.5f, Teams = 3.f;
	FString Version, CraneName;
	TArray<tag::Player*> Men;
	TArray<FVector> At;
	TArray<float> Yaw;
	TArray<bool> bKickSide;
	double Started = 0.0;
	// (measured: the open test's checks, and the log)
	float EmptyRun = 0.f, LongestEmpty = 0.f;
	int32 Frames = 0, BadFrames = 0, MostKickSeen = 0, MostRecvSeen = 0;
	bool bCardShown = false, bCardSeenUp = false; // (the matchup card asked for on its beat; the graphics had it up)
	TArray<float> StageSecs;
};

struct FTagBcTest
{
	int32 Mode = 0;                      // 1 replay, 2 shot, 3 pace, 4 open
	int32 Pass = 0, Fail = 0;
	float Clock = 0.f;
	bool bDone = false;
	// the pace test's presses
	int32 BeatsSeen = 0, InjectSerial = -1;
	uint64 InjectFrame = 0;
	double InjectT = 0.0;
	bool bReleasePending = false, bAwaitSkip = false, bSecondPress = false, bAwaitRefusal = false;
	int32 SkipsAt = 0, RefusedAt = 0, SerialAt = 0;
	double SkipAtT = 0.0;
	int32 SkipTests = 0;
	// the shot test's record of the first big plays
	TArray<FTagBcPlay> BigPlays;
	// the open test's rounds
	int32 OpenRound = 0;
	// the halftime / postgame tests
	bool bShowSeen = false, bShowChecked = false, bShowSkipped = false;
	float InjectAtShowT = -1.f;
	float InjectAtOpenT = -1.f;
};

/** A man walking off: a substitute going to his bench, a player leaving the field at the half. */
struct FTagBcLeaver
{
	tag::Player* P = nullptr;
	FVector At = FVector::ZeroVector, To = FVector::ZeroVector;
	float Speed = 550.f;                 // cm/s (a jog off the field)
	bool bShow = false;                  // the halftime show's (the last play's man: his sim is held, not left)
};

/** One beat of a halftime or postgame show: what it shows for how long. */
struct FTagBcShowStage
{
	const TCHAR* Name = TEXT("");
	float Secs = 0.f;
	int32 Act = 0;   // 1 leaving, 2 the half's stats, 3 the leading bench, 4 the crowd, 5 the kickoff units, 6 a highlight;
	int32 Hl = -1;   // (6: which stored highlight)
	                 // 11 the celebration, 12 the final score, 13 the player of the game, 14 the final stats
};

struct FTagBcShow
{
	int32 Kind = 0;                      // 1 halftime, 2 postgame
	bool bOn = false, bLong = false;
	int32 Stage = -1, CraneVar = 0;
	float T = 0.f, StageT = 0.f;
	TArray<FTagBcShowStage> Stages;
	FString Version;
	int32 Winner = -1;                   // (postgame) 1 home, 0 away, -1 a tie
	const tag::Player* Pog = nullptr;
	FString PogLine;
	// (measured)
	TArray<float> StageSecs;
	int32 Frames = 0, BadFrames = 0, GfxAsked = 0, GfxShown = 0, MostKickSeen = 0, MostRecvSeen = 0, FieldMenAtUnits = -1;
	int32 HlWant = 0, HlShown = 0;      // highlights planned, shown
	bool bHeldClock = true;
};

/** A stored highlight: the film of a play around its key moment (AMatchDirector's replay frames, copied out). */
struct FTagBcHighlight
{
	TArray<AMatchDirector::FReplayFrame> Frames;
	double KeySim = 0.0;                 // (the key moment on the sim's clock, until the film is copied)
	float KeyT = 0.f, T0 = 0.f, T1 = 0.f, Score = 0.f;
	int32 Half = 1, Q = 1, PlayN = 0, SubjectId = -1, Dir = 1;
	FString Class, Desc;
	int64 Bytes = 0;
};

struct FTagBcState
{
	// the replay package
	TArray<FTagReplayRun> Runs;
	int32 Run = 0, Phase = 0;            // phase: 0 the wipe in, 1 playing, 2 the wipe between angles, 3 the wipe out
	bool bSkipReplay = false, bRPlaced = false;
	FString RClass;
	const TCHAR* RKeyKind = TEXT("");
	FVector RCam = FVector::ZeroVector, RCamVel = FVector::ZeroVector, RLook = FVector::ZeroVector, RLookVel = FVector::ZeroVector;
	float RFocal = 50.f, RFocalVel = 0.f, RShown = 0.f;
	TArray<ETagReplayAngle> FirstAngles;
	int32 Packages = 0, TwoAngle = 0, CutsCovered = 0, CutsBare = 0, CutsPartial = 0, ReplayFrames = 0, ReplayLensBad = 0;
	int32 TagPicks = 0, TagsShown = 0, TagPicksThisPkg = 0, PkgsTwoPicks = 0; // (the stat tags: picked once a package, on its first angle)
	TMap<FString, int32> FirstByAngle;
	double WhistleSimT = 0.0;
	// the shots
	FTagBcShot Shot;
	TArray<FTagBcBeat> Queue;
	float GapLeft = 0.f;
	bool bProgram = false;
	FTagBcPlay Ctx;
	TArray<ETagShot> Seq;                // every shot of the game, in order
	TArray<FString> SeqWindow;           // the last 20 plays' three-shot sequences
	TMap<int32, int32> TypeLastPlay;
	TMap<FString, int32> SubjLastPlay;
	int32 PlayN = 0, Shots = 0, Fallbacks = 0, ShotsBad = 0, MaxRun = 0, RepeatSeqs = 0;
	TMap<FString, int32> TypeCount, VariantCount;
	TWeakObjectPtr<AStadium> StandFor;
	TArray<FVector> StandPeople;         // the stadium's own people (photographers, crews) and the fans: chest height
	// the pace and the skip
	ETagPace PaceOverride = ETagPace::Auto;
	ETagPace PaceAtWhistle = ETagPace::Standard;
	double SkipLockUntil = 0.0, WhistleT = -1.0, LastShotT = 0.0; // (LastShotT: when the last shot started, platform s)
	uint64 SkipFrame = 0;
	int32 Skips = 0, SkipsRefused = 0;
	bool bLeadThisPlay = false, bReplayThisPlay = false;
	float BeatSecsThisPlay = 0.f, ReplaySecsThisPlay = 0.f;
	TArray<float> ToCall[4], ToCallBig[4]; // whistle -> the next call, by pace (routine plays / the rest)
	// the open
	FTagBcOpen Open;
	// the halftime and postgame shows, the men walking off
	FTagBcShow Show;
	bool bHalftimeDone = false, bPostgameDone = false;
	// the stored highlights
	TArray<FTagBcHighlight> Highlights;
	FTagBcHighlight HlPending;
	bool bHlPending = false, bHlOn = false, bHlBallHidden = true, bRefsHidden = false;
	int64 HlBytes = 0, HlBytesPeak = 0, HlCapBytes = 0;
	int32 HlKept = 0, HlDropped = 0;
	TSet<int32> HlVisible;               // (the men shown before a highlight took the field)
	TArray<FVector> SubStarts;           // (this play's substitutes' starting spots: none on another's)
	unsigned long long SubStartsSerial = 0;
	TArray<FTagBcLeaver> Leavers;
	int32 SubsIn = 0, SubsOut = 0, SubsPulled = 0, Snaps = 0, SnapBad = 0, SnapWalking = 0;
	// the self-checks (kept across StartMatch)
	FTagBcTest Test;
};

FTagBcState& AMatchDirector::Bc()
{
	if (!BcState) BcState = std::make_shared<FTagBcState>();
	return *BcState;
}

FTagWipeView AMatchDirector::WipeNow() const
{
	FTagWipeView V;
	if (Wipe.IsValid())
	{
		V.bOn = Wipe->IsOn();
		V.Progress = Wipe->Progress();
		V.bCovered = Wipe->Covered();
	}
	return V;
}

ETagPace AMatchDirector::PaceNow() const
{
	if (BcState && BcState->PaceOverride != ETagPace::Auto) return BcState->PaceOverride;
	return TagPace::Current(bWatch);
}

namespace
{
	FString BcS(const std::string& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }
	std::string BcU(const FString& s) { return std::string(TCHAR_TO_UTF8(*s)); }
	float BcSmooth(float X) { X = FMath::Clamp(X, 0.f, 1.f); return X * X * (3.f - 2.f * X); }

	void BcSpring(FVector& X, FVector& V, const FVector& Goal, float Halflife, float Dt)
	{
		const float W = 1.3863f / FMath::Max(Halflife, 0.01f);
		const FVector J0 = X - Goal, J1 = V + J0 * W;
		const float E = FMath::Exp(-W * Dt);
		X = Goal + (J0 + J1 * Dt) * E;
		V = (V - J1 * W * Dt) * E;
	}

	int32 BcPaceIdx(ETagPace P) { return FMath::Clamp(static_cast<int32>(P), 0, 3); }

	const TCHAR* BcAngleName(ETagReplayAngle A)
	{
		switch (A)
		{
		case ETagReplayAngle::HighEndZone: return TEXT("high end zone");
		case ETagReplayAngle::TightIso: return TEXT("tight iso");
		case ETagReplayAngle::Pylon: return TEXT("pylon");
		default: return TEXT("reverse");
		}
	}

	const TCHAR* BcShotName(ETagShot T)
	{
		switch (T)
		{
		case ETagShot::Hero: return TEXT("hero");
		case ETagShot::Opponent: return TEXT("opponent");
		case ETagShot::Bench: return TEXT("bench");
		case ETagShot::Coach: return TEXT("coach");
		case ETagShot::Crowd: return TEXT("crowd");
		case ETagShot::Official: return TEXT("official");
		case ETagShot::Wide: return TEXT("wide");
		case ETagShot::OpenCrane: return TEXT("open crane");
		case ETagShot::OpenCard: return TEXT("open card");
		case ETagShot::OpenTeams: return TEXT("open teams");
		case ETagShot::ShowCrane: return TEXT("show crane");
		default: return TEXT("none");
		}
	}

	bool BcIsPerson(ETagShot T) { return T == ETagShot::Hero || T == ETagShot::Opponent || T == ETagShot::Coach || T == ETagShot::Official; }
	bool BcIsOpen(ETagShot T) { return T == ETagShot::OpenCrane || T == ETagShot::OpenCard || T == ETagShot::OpenTeams || T == ETagShot::ShowCrane; }

	/** The cranes over the bowl (E: 0..1 through the move): 0 the far corner craning across (the break's move), 1 up from
	 *  behind an end zone, rising over the field, 2 high over the home stand sweeping along the field from one 20 to the
	 *  other. */
	void BcCrane(int32 Var, float E, FVector& Lens, FVector& LookAt, float& Focal)
	{
		const double Y = YD;
		const double Ed = static_cast<double>(E);
		if (Var == 1)
		{
			Lens = FVector(FMath::Lerp(-17.0, 6.0, Ed) * Y, FMath::Lerp(-10.0, -4.0, Ed) * Y, FMath::Lerp(3.0, 28.0, Ed) * Y);
			LookAt = FVector(50.0 * Y, 0.0, 0.0);
			Focal = FMath::Lerp(20.f, 26.f, E);
		}
		else if (Var == 2)
		{
			Lens = FVector(FMath::Lerp(22.0, 78.0, Ed) * Y, 46.0 * Y, 26.0 * Y);
			LookAt = FVector(FMath::Lerp(30.0, 70.0, Ed) * Y, 0.0, 0.0);
			Focal = 24.f;
		}
		else
		{
			const double A = FMath::DegreesToRadians(30.0 + 70.0 * Ed);
			Lens = FVector((50.0 + 62.0 * FMath::Cos(A)) * Y, 48.0 * FMath::Sin(A) * Y, (24.0 + 8.0 * Ed) * Y);
			LookAt = FVector(50.0 * Y, 0.0, 2.0 * Y);
			Focal = 22.f;
		}
	}

	/** The speed of the film at time T of a run: real speed (Pre) until 0.6 s before the key moment, eased down to Slow
	 *  through it, back up to Post after. */
	float BcRampRate(const FTagReplayRun& R, float T)
	{
		const float D = T - R.KeyT;
		if (D < -0.6f) return R.Pre;
		if (D < -0.1f) return FMath::Lerp(R.Pre, R.Slow, BcSmooth((D + 0.6f) / 0.5f));
		if (D < 0.35f) return R.Slow;
		if (D < 0.8f) return FMath::Lerp(R.Slow, R.Post, BcSmooth((D - 0.35f) / 0.45f));
		return R.Post;
	}

	/** A body fit to be filmed: there, shown, awake, a mesh with its bones where a body has them (not a reference pose,
	 *  not a collapsed or stretched one). */
	bool BcPosed(const AAthlete* A, FString& Why)
	{
		if (!A) { Why = TEXT("no subject"); return false; }
		if (A->IsHidden()) { Why = TEXT("hidden"); return false; }
		if (A->IsDormant()) { Why = TEXT("asleep (a held pose)"); return false; }
		const FVector Loc = A->GetActorLocation();
		const FVector Head = A->BoneWorld(TEXT("head")), Pelvis = A->BoneWorld(TEXT("pelvis"));
		if (Head.ContainsNaN() || Pelvis.ContainsNaN()) { Why = TEXT("bones not a number"); return false; }
		if (Head.Equals(Loc + FVector(0, 0, 120), 0.5)) { Why = TEXT("no body shown"); return false; } // (BoneWorld's stand-in)
		const double HP = FVector::Dist(Head, Pelvis);
		if (Head.Z < 5.0 || Head.Z > 240.0 || HP < 20.0 || HP > 120.0 || FVector::Dist2D(Pelvis, Loc) > 250.0)
		{
			Why = FString::Printf(TEXT("pose out of shape (head %.0f cm up, %.0f cm from the pelvis)"), Head.Z, HP);
			return false;
		}
		FVector L, R;
		if (A->Hands(L, R) && FMath::Abs(L.Z - R.Z) < 15.0 && FVector::Dist(L, R) > 150.0 && L.Z > Head.Z - 45.0)
		{
			Why = TEXT("arms straight out (a reference pose)");
			return false;
		}
		return true;
	}

	FVector BcChest(const AAthlete* A) { return A->BoneWorld(TEXT("head")) * 0.55f + A->BoneWorld(TEXT("pelvis")) * 0.45f; }

	FString BcWho(const AAthlete* A)
	{
		if (!A || !A->Player) return TEXT("-");
		if (A->Player->id >= 900000) return FString::Printf(TEXT("staff %d"), A->Player->id - 900000);
		return FString::Printf(TEXT("#%d %s"), A->Player->number, ANSI_TO_TCHAR(tag::posName(A->Player->pos)));
	}

	// the person rigs: around the man's facing, 150 cm up (shoulder height), or low
	struct FBcRig { const TCHAR* Name; float Deg; float Z; };
	const FBcRig kBcRigs[] = {{TEXT("front"), 0.f, 150.f}, {TEXT("front-left"), 35.f, 150.f}, {TEXT("front-right"), -35.f, 150.f},
		{TEXT("wide-left"), 70.f, 150.f}, {TEXT("wide-right"), -70.f, 150.f}, {TEXT("low"), 0.f, 95.f}};

	/** The first angle's candidates by the kind of play, the best look first. */
	TArray<ETagReplayAngle> BcFirstCands(const FString& Class)
	{
		using A = ETagReplayAngle;
		if (Class == TEXT("td-goal")) return {A::Pylon, A::HighEndZone, A::Reverse};
		if (Class == TEXT("td") || Class == TEXT("run") || Class == TEXT("return")) return {A::HighEndZone, A::Reverse};
		if (Class == TEXT("take") || Class == TEXT("pass")) return {A::Reverse, A::HighEndZone};
		if (Class == TEXT("sack")) return {A::HighEndZone, A::TightIso, A::Reverse};
		if (Class == TEXT("kick")) return {A::TightIso, A::HighEndZone};
		return {A::HighEndZone, A::Reverse};
	}

	/** The replay's angles: the best look first (of the two best for this kind of play, the one it opened with least
	 *  lately, this game and the ones before: the store), never the first angle of the last two replays a third time, an
	 *  angle whose lens the play blocks left out; on a touchdown or a takeaway the tight iso on the key man after it.
	 *  bDry: the store is read, not written (the tests). */
	void BcAnglePlan(const FString& Class, bool bTwo, const TArray<ETagReplayAngle>& Prev, TFunctionRef<bool(ETagReplayAngle)> Clear, uint32 Salt, bool bDry,
		TArray<ETagReplayAngle>& Out)
	{
		using A = ETagReplayAngle;
		Out.Reset();
		const std::string Cat = BcU(FString(TEXT("replay.first.")) + Class);
		TArray<A> Order = BcFirstCands(Class);
		if (Order.Num() >= 2)
		{
			const std::vector<std::string> Names = {BcU(BcAngleName(Order[0])), BcU(BcAngleName(Order[1]))};
			const std::vector<int> O = tag::Recent::get().order(Cat, Names, Salt);
			if (O.size() == 2 && O[0] == 1) Order.Swap(0, 1);
		}
		const bool bTwice = Prev.Num() >= 2 && Prev.Last() == Prev.Last(1);
		A First = A::HighEndZone;
		bool bFound = false;
		for (const A Cand : Order)
		{
			if ((bTwice && Cand == Prev.Last()) || !Clear(Cand)) continue;
			First = Cand;
			bFound = true;
			break;
		}
		if (!bFound) First = bTwice && Prev.Last() == A::HighEndZone ? A::Reverse : A::HighEndZone; // (over every body)
		Out.Add(First);
		if (!bDry) tag::Recent::get().use(Cat, BcU(BcAngleName(First)));
		if (bTwo)
			Out.Add(First != A::TightIso && Clear(A::TightIso) ? A::TightIso : First != A::Reverse ? A::Reverse : A::HighEndZone);
	}

	/** Median and 90th percentile of a list (0 when empty). */
	void BcStats(TArray<float> V, float& Med, float& P90)
	{
		Med = P90 = 0.f;
		if (V.Num() == 0) return;
		V.Sort();
		Med = V[V.Num() / 2];
		P90 = V[FMath::Min(V.Num() - 1, FMath::FloorToInt(V.Num() * 0.9f))];
	}
}

// ------------------------------------------------------------------ replay-worthiness (04 A4)
void AMatchDirector::ResetReplayBudget()
{
	ReplayCredit = 1.f;
	ReplayScoreNow = 0.f;
	bReplayForced = false;
	ReplayWhyNow.Reset();
	LastReplayWhy.Reset();
	ReplayPlays = ReplaysShown = ReplayGap = ReplayGapMax = 0;
	ReplayWhyCount.Reset();
	// (a new game: the package's own counts and histories too - the self-check's state and the store stay)
	if (BcState)
	{
		FTagBcTest Keep = MoveTemp(BcState->Test);
		const ETagPace Pace = BcState->PaceOverride;
		*BcState = FTagBcState();
		BcState->Test = MoveTemp(Keep);
		BcState->PaceOverride = Pace;
	}
	ShotView = FTagShotView();
	ReplayView = FTagReplayView();
	PregameView = FTagPregameView();
	HalftimeView = FTagHalftimeView();
	PostgameView = FTagPostgameView();
	if (Wipe.IsValid()) Wipe->Stop();
}

void AMatchDirector::DecideReplay(const tag::PlayResult& R)
{
	static const bool bReplayAll = FParse::Param(FCommandLine::Get(), TEXT("TagReplayAll")); // review: replay every play
	static const bool bOldRule = FParse::Param(FCommandLine::Get(), TEXT("TagReplayOld"));  // A/B: the yes/no test this replaced
	// the budget: Rate of a replay earned per scrimmage play (one per five), two at most banked; a play needs a score of
	// Need with a replay in hand, more as the budget runs down (-TagReplayRate=, -TagReplayNeed=: for tuning)
	static float Rate = FParse::Value(FCommandLine::Get(), TEXT("TagReplayRate="), Rate) ? Rate : 0.2f;
	static float NeedBase = FParse::Value(FCommandLine::Get(), TEXT("TagReplayNeed="), NeedBase) ? NeedBase : 1.f;
	bReplayWorthy = false;
	bReplayForced = false;
	ReplayScoreNow = 0.f;
	ReplayWhyNow.Reset();
	if (!Sim || !Game) return;
	const tag::Game& G = *Game; // (the play is not in the book yet: the down, the distance, the spot and the score before it)
	const bool bPlace = Sim->isPlaceKick(), bRet = Sim->isReturn();
	if (bReplayAll || bOldRule)
	{
		bReplayWorthy = bReplayAll || R.td || R.interception || R.fumbleLost || (bPlace ? Sim->placeBlocked
			: bRet ? R.yards >= 40 : (R.kind == tag::PlayKind::Sack || R.yards >= 20));
		bReplayForced = true;
		ReplayWhyNow = bReplayAll ? TEXT("all") : TEXT("old rule");
		return;
	}
	// what TV always shows again
	const TCHAR* Forced = R.td ? TEXT("touchdown") : (R.interception || R.fumbleLost) ? TEXT("takeaway") : R.safety ? TEXT("safety")
		: (bPlace && Sim->placeBlocked) ? TEXT("blocked kick") : nullptr;
	const bool bScrim = !bPlace && !bRet && !bTwoPoint && R.kind != tag::PlayKind::Kneel && R.kind != tag::PlayKind::Spike;
	if (bScrim)
	{
		++ReplayPlays;
		++ReplayGap;
		ReplayCredit = FMath::Min(2.f, ReplayCredit + Rate);
	}
	// the parts (about 1 is a replay on its own)
	float Gain = 0.f, Sack = 0.f, Down = 0.f, Zone = 0.f, Hit = 0.f, Catch = 0.f, Late = 1.f;
	const int32 Dn = FMath::Clamp(G.down, 1, 4);
	const double ToGo = G.toGo;
	if (bRet)
		Gain = R.yards >= 40 ? 1.f + (R.yards - 40) / 30.f : FMath::Max(0.f, (R.yards - 20) / 25.f) * 0.6f;
	else if (bScrim)
	{
		// yards past what the down usually gives (first down ~4, second about 60% of the distance, third and fourth the
		// distance), the explosive play on top
		const double Exp = Dn == 1 ? 4.0 : Dn == 2 ? FMath::Clamp(ToGo * 0.6, 2.0, 6.0) : ToGo;
		if (!R.turnover && R.yards > 0)
			Gain = static_cast<float>(FMath::Max(0.0, R.yards - Exp) / 16.0) + (R.yards >= 20 ? 0.35f : 0.f) + (R.yards >= 40 ? 0.3f : 0.f);
		// a sack (the one that ends a drive, on third or fourth down, more)
		if (R.kind == tag::PlayKind::Sack) Sack = Dn >= 3 ? 1.f : 0.7f;
		// the money downs: a third down converted (a long one more), a fourth down either way
		const bool bMade = !R.turnover && R.yards >= ToGo - 1e-6;
		if (Dn == 4) Down = 0.95f;
		else if (Dn == 3) Down = bMade ? 0.55f + (ToGo >= 7.0 ? 0.25f : 0.f) : R.kind == tag::PlayKind::Sack ? 0.f : 0.15f;
		if (G.ballOn >= 80.0 && Gain + Sack + Down > 0.f) Zone = 0.15f;
	}
	// the sim's big hit (the hit stick: a man laid out), broken tackles
	for (const tag::PlayEvent& Ev : Sim->events)
		if (Ev.type == "bighit") { Hit = 0.6f; break; }
	Hit += FMath::Min(0.5f, 0.2f * R.brokenTackles);
	// the catch: a layout, long odds (the odds the sim rolled it with), a contested one
	if (R.complete)
	{
		const tag::Ball& B = Sim->ball;
		if (B.fateDive) Catch += 0.45f;
		if (B.fateP >= 0.0 && B.fateP < 0.4) Catch += 0.3f + static_cast<float>(0.4 - B.fateP) * 1.5f;
		if (R.catchSep >= 0.0 && R.catchSep < 0.8) Catch += 0.15f;
	}
	// late and close: everything counts for more
	if (G.q >= 4 && G.clock <= 300.0 && FMath::Abs(G.score.home - G.score.away) <= 8) Late = 1.3f;
	const float Score = (Gain + Sack + Down + Zone + Hit + Catch) * Late;
	// the budget: with a replay in hand a score of Need is enough; spent, a play has to be that much better
	float Need = NeedBase + FMath::Max(0.f, 1.f - ReplayCredit) * 0.8f;
	if (R.kind == tag::PlayKind::Sack && LastReplayWhy == TEXT("sack")) Need += 0.8f; // (no two routine sacks back to back)
	const TCHAR* Top = TEXT("gain");
	float TopV = Gain;
	auto Consider = [&Top, &TopV](float V, const TCHAR* Name) { if (V > TopV) { TopV = V; Top = Name; } };
	Consider(Sack, TEXT("sack"));
	Consider(Down, Dn == 4 ? TEXT("fourth down") : TEXT("third down"));
	Consider(Zone, TEXT("red zone"));
	Consider(Hit, TEXT("hit"));
	Consider(Catch, TEXT("catch"));
	if (bRet) Top = TEXT("return");
	bReplayForced = Forced != nullptr;
	bReplayWorthy = bReplayForced || Score >= Need;
	ReplayScoreNow = Score;
	ReplayWhyNow = Forced ? Forced : Top;
	UE_LOG(LogTemp, Display, TEXT("TAG replay score %.2f need %.2f credit %.2f (gain %.2f sack %.2f down %.2f zone %.2f hit %.2f catch %.2f late x%.1f; jolt %.0f cm/s) -> %s, %s; %d replays in %d plays"),
		Score, Need, ReplayCredit, Gain, Sack, Down, Zone, Hit, Catch, Late, MaxJoltThisPlay, bReplayWorthy ? TEXT("replay") : TEXT("skip"), *ReplayWhyNow, ReplaysShown, ReplayPlays);
}

void AMatchDirector::SpendReplay()
{
	++ReplaysShown;
	ReplayGapMax = FMath::Max(ReplayGapMax, ReplayGap);
	ReplayGap = 0;
	ReplayCredit = FMath::Max(-1.f, ReplayCredit - 1.f);
	ReplayWhyCount.FindOrAdd(ReplayWhyNow)++;
	LastReplayWhy = ReplayWhyNow;
	UE_LOG(LogTemp, Display, TEXT("TAG replay shown: %s (score %.2f%s), %d replays in %d scrimmage plays, credit %.2f"), *ReplayWhyNow, ReplayScoreNow,
		bReplayForced ? TEXT(", always") : TEXT(""), ReplaysShown, ReplayPlays, ReplayCredit);
}

void AMatchDirector::LogReplaySummary() const
{
	FString By;
	for (const TPair<FString, int32>& KV : ReplayWhyCount)
		By += FString::Printf(TEXT(" %s %d (%.0f%%)"), *KV.Key, KV.Value, ReplaysShown ? 100.0 * KV.Value / ReplaysShown : 0.0);
	UE_LOG(LogTemp, Display, TEXT("TAG replay summary: %d replays in %d scrimmage plays (one per %.1f), longest run without one %d plays; by reason:%s"),
		ReplaysShown, ReplayPlays, ReplaysShown ? static_cast<double>(ReplayPlays) / ReplaysShown : 0.0, FMath::Max(ReplayGapMax, ReplayGap), *By);
}

// ------------------------------------------------------------------ the replay package (04 A2)
bool AMatchDirector::StartReplay()
{
	if (!bReplayWorthy || ReplayBuf.Num() < 20) return false;
	bReplayWorthy = false;
	FTagBcState& St = Bc();
	if (ShotView.bOn) EndShot(TEXT("replay")); // (the lead shot ends under the wipe)
	PlanReplay();
	if (St.Runs.Num() == 0) return false;
	ReplayEnd = ReplayBuf.Last().T;
	ReplayT = St.Runs[0].T0;
	ReplayIdx = 0;
	ReplayFocus = FVector::ZeroVector;
	St.Run = 0;
	St.Phase = 0;
	St.bSkipReplay = false;
	St.RShown = 0.f;
	St.bReplayThisPlay = true;
	State = EMatchState::Replay;
	St.TagPicksThisPkg = 0;
	if (Overlay) Overlay->Gfx().SetAutoReplayTags(false); // (the stat tags come at the key moment: TickReplay)
	// the wipe in: the cut to the first angle comes once the slats cover the picture (TickReplay)
	if (!Wipe.IsValid() && GEngine && GEngine->GameViewport)
	{
		// (under the overlay, at 10: the menus - a pause in the middle of a wipe - and the broadcast's graphics sit over it)
		SAssignNew(Wipe, STagWipe);
		GEngine->GameViewport->AddViewportWidgetContent(Wipe.ToSharedRef(), 9);
	}
	if (Wipe.IsValid()) Wipe->Start(0.15f, TEXT("REPLAY"));
	Overlay->SetHint(bWatch ? TEXT("REPLAY") : TEXT("REPLAY      SPACE skip"));
	FString Angles;
	for (const FTagReplayRun& R : St.Runs) Angles += FString::Printf(TEXT("%s%s"), Angles.IsEmpty() ? TEXT("") : TEXT(" + "), BcAngleName(R.Type));
	UE_LOG(LogTemp, Display, TEXT("TAG replay: %d frames, %s (%s, key %s at %.1f s of %.1f)"), ReplayBuf.Num(), *Angles, *St.RClass, St.RKeyKind, St.Runs[0].KeyT, ReplayEnd);
	++St.Packages;
	if (St.Runs.Num() > 1) ++St.TwoAngle;
	St.FirstAngles.Add(St.Runs[0].Type);
	St.FirstByAngle.FindOrAdd(BcAngleName(St.Runs[0].Type))++;
	++ReplayView.Serial;
	ReplayView.bOn = true;
	ReplayView.NumAngles = St.Runs.Num();
	ReplayView.Why = ReplayWhyNow;
	SpendReplay(); // (out of the game's replay budget)
	return true;
}

void AMatchDirector::PlanReplay()
{
	FTagBcState& St = Bc();
	St.Runs.Reset();
	const float Start = ReplayBuf[0].T, End = ReplayBuf.Last().T;
	static const bool bOne = FParse::Param(FCommandLine::Get(), TEXT("TagReplayOne")); // A/B: the one reverse angle at 0.55x
	auto Clamp = [Start, End](FTagReplayRun& R)
	{
		R.T0 = FMath::Clamp(R.T0, Start, End);
		R.T1 = FMath::Clamp(R.T1, Start, End);
		if (R.T1 - R.T0 < 1.f) R.T0 = FMath::Max(Start, R.T1 - 1.f);
		R.KeyT = FMath::Clamp(R.KeyT, R.T0, R.T1);
	};
	if (bOne || !Sim || !Sim->result || !Game)
	{
		FTagReplayRun R;
		R.Type = ETagReplayAngle::Reverse;
		R.T0 = End - 5.f;
		R.T1 = End;
		R.KeyT = End - 2.5f;
		R.Pre = R.Slow = R.Post = 0.55f;
		Clamp(R);
		St.Runs.Add(R);
		St.RClass = TEXT("one");
		St.RKeyKind = TEXT("");
		return;
	}
	const tag::PlayResult& Res = *Sim->result;
	const tag::PlaySim& S = *Sim;
	const bool bPlace = S.isPlaceKick(), bRet = S.isReturn();
	const bool bTake = Res.interception || Res.fumbleLost;
	// the key moment and the key man, from the sim's events (the catch, the hit, the release, the break)
	auto LastEv = [&S](std::initializer_list<const char*> Types) -> const tag::PlayEvent*
	{
		const tag::PlayEvent* Found = nullptr;
		for (const tag::PlayEvent& E : S.events)
			for (const char* Ty : Types)
				if (E.type == Ty) { Found = &E; break; }
		return Found;
	};
	auto FirstEv = [&S](std::initializer_list<const char*> Types, double After) -> const tag::PlayEvent*
	{
		for (const tag::PlayEvent& E : S.events)
			for (const char* Ty : Types)
				if (E.type == Ty && E.t >= After) return &E;
		return nullptr;
	};
	const double SimEnd = St.WhistleSimT > 0.0 ? St.WhistleSimT : S.t;
	double KeySim = -1.0;
	const tag::Ent* Who = S.ball.holder;
	const TCHAR* Kind = TEXT("run");
	FString Class = TEXT("other");
	const tag::PlayEvent* Hit = LastEv({"bighit"});
	if (bPlace)
	{
		Class = TEXT("kick");
		Kind = TEXT("kick");
		if (const tag::PlayEvent* E = LastEv({"kickblock", "kicktip"})) { KeySim = E->t; Who = E->a; }
	}
	else if (bRet)
	{
		Class = TEXT("return");
		Who = S.returner ? S.returner : S.ball.holder;
		const double CatchT = S.kickCatchT >= 0.0 ? S.kickCatchT : 0.0;
		if (const tag::PlayEvent* E = FirstEv({"broken", "move", "trucked"}, CatchT)) KeySim = E->t;
		else KeySim = FMath::Min(SimEnd - 0.4, CatchT + 1.8);
	}
	else if (Res.td && !bTake)
	{
		// a score: the catch if it was made near the line, else the last stride over it (a goal-line score: the pylon)
		Class = Game->ballOn >= 88.0 ? TEXT("td-goal") : TEXT("td");
		Kind = TEXT("score");
		KeySim = SimEnd - 0.25;
		if (const tag::PlayEvent* E = LastEv({"catch", "divecatch"}); E && SimEnd - E->t < 2.5) { KeySim = E->t; Kind = TEXT("catch"); Who = E->a; }
	}
	else if (Res.interception)
	{
		Class = TEXT("take");
		Kind = TEXT("takeaway");
		if (const tag::PlayEvent* E = LastEv({"int"})) { KeySim = E->t; Who = E->a; }
	}
	else if (Res.fumbleLost)
	{
		Class = TEXT("take");
		Kind = TEXT("takeaway");
		if (const tag::PlayEvent* E = LastEv({"fumble"})) { KeySim = E->t; Who = E->b ? E->b : E->a; }
	}
	else if (Res.kind == tag::PlayKind::Sack)
	{
		Class = TEXT("sack");
		Kind = TEXT("hit");
		if (const tag::PlayEvent* E = Hit ? Hit : LastEv({"tackle"})) { KeySim = E->t; Who = E->a; }
		if (Res.sackedBy)
			for (const tag::Ent* X : S.ents)
				if (X->p == Res.sackedBy) Who = X;
	}
	else if (Res.complete)
	{
		Class = TEXT("pass");
		Kind = TEXT("catch");
		if (const tag::PlayEvent* E = LastEv({"catch", "divecatch"})) { KeySim = E->t; Who = E->a; }
	}
	else if (Res.incomplete)
	{
		Class = TEXT("pass");
		if (const tag::PlayEvent* E = LastEv({"pbu", "batted"})) { KeySim = E->t; Who = E->a; Kind = TEXT("hit"); }
		else if (const tag::PlayEvent* Th = LastEv({"throw", "throwaway"})) { KeySim = Th->t; Who = S.qb; Kind = TEXT("release"); }
	}
	else
	{
		Class = TEXT("run");
		if (const tag::PlayEvent* E = FirstEv({"broken", "move", "trucked"}, 0.0)) { KeySim = E->t; Kind = TEXT("run"); }
		else if (const tag::PlayEvent* Tk = LastEv({"tackle"})) { KeySim = Tk->t; Kind = TEXT("hit"); }
	}
	// a big hit the replay was chosen for: the hit, and the man who threw it
	if (Hit && ReplayWhyNow == TEXT("hit")) { KeySim = Hit->t; Who = Hit->a; Kind = TEXT("hit"); }
	if (KeySim < 0.0) KeySim = SimEnd - 0.4;
	// (the sim's clock to the film's)
	auto FilmT = [this, End](double SimT) -> float
	{
		for (const FReplayFrame& F : ReplayBuf)
			if (F.SimT >= SimT - 1e-4) return F.T;
		return End;
	};
	const float KeyT = FilmT(KeySim);
	const int32 SubjId = Who && Who->p ? Who->p->id : -1;
	St.RClass = Class;
	St.RKeyKind = Kind;
	auto SubjAtT = [this, SubjId](float T) -> FVector
	{
		for (const FReplayFrame& F : ReplayBuf)
			if (F.T >= T)
			{
				for (const FReplayAth& RA : F.Ath)
					if (RA.Id == SubjId) return RA.World.GetLocation();
				return F.Ball.GetLocation();
			}
		return ReplayBuf.Last().Ball.GetLocation();
	};
	// the lens of an angle that moves with the man (the cart on the sideline) or sits at the pylon, at one frame
	auto LensAt = [this](const FTagReplayRun& R, const FVector& Subj) -> FVector
	{
		const double Y = YD;
		if (R.Type == ETagReplayAngle::TightIso) return FVector(Subj.X - Dir * 3.0 * Y, R.Side * (tag::FIELD_W / 2 - 2.0) * Y, 180.0);
		return FVector((Dir > 0 ? 100.0 : 0.0) * Y + Dir * 0.6 * Y, R.Side * (tag::FIELD_W / 2 + 0.4) * Y, 45.0);
	};
	// how much of a run's film the bodies block from that lens (on its line to the subject; his own pile within 1.5 m
	// of him belongs to the picture): 1 (never this angle) when on any frame of the stretch the lens is in the stands,
	// at a man of the film, or at one of the people who are not on it - the crew, the benches, the staff, where they
	// stand now (a pylon camera at the goal line sat in the line judge on some plays: 238 of 720 frames)
	auto StaffNear = [this](const FVector& C)
	{
		float Best = 1e9f;
		auto Probe = [&Best, &C](const AAthlete* O)
		{
			if (!O || O->IsHidden()) return;
			for (const TCHAR* Bn : {TEXT("head"), TEXT("pelvis")}) Best = FMath::Min(Best, static_cast<float>(FVector::Dist(O->BoneWorld(Bn), C)));
		};
		for (const AAthlete* O : Sideline) Probe(O);
		for (const AAthlete* O : Officials) Probe(O);
		for (const AAthlete* O : Crew) Probe(O);
		Probe(Umpire);
		return Best;
	};
	auto Blocked = [this, SubjId, &LensAt, &StaffNear](const FTagReplayRun& R) -> float
	{
		int32 N = 0, Bad = 0;
		for (const FReplayFrame& F : ReplayBuf)
		{
			if (F.T < R.T0 || F.T > R.T1) continue;
			FVector Subj = F.Ball.GetLocation();
			for (const FReplayAth& RA : F.Ath)
				if (RA.Id == SubjId) Subj = RA.World.GetLocation() + FVector(0, 0, 110);
			const FVector C = LensAt(R, Subj);
			++N;
			if (!ShotSpotOk(C) || StaffNear(C) < 150.f) return 1.f; // (the stands, the sideline's own people, the crew)
			for (const FReplayAth& RA : F.Ath)
			{
				const FVector Q = RA.World.GetLocation() + FVector(0, 0, 100);
				if (FVector::Dist2D(Q, C) < 200.0) return 1.f; // (a man of the film at the lens, the key man too)
				if (RA.Id == SubjId || FVector::Dist2D(Q, Subj) < 150.0) continue;
				if (FMath::PointDistToSegment(Q, C, Subj) < 55.0) { ++Bad; break; }
			}
		}
		return N ? static_cast<float>(Bad) / N : 1.f;
	};
	const bool bFullPace = PaceNow() == ETagPace::Full;
	auto Make = [&](ETagReplayAngle A) -> FTagReplayRun
	{
		FTagReplayRun R;
		R.Type = A;
		R.KeyT = KeyT;
		R.SubjectId = SubjId;
		switch (A)
		{
		case ETagReplayAngle::HighEndZone:
			R.T0 = KeyT - (Class.StartsWith(TEXT("td")) || Class == TEXT("run") || Class == TEXT("return") ? 5.f : 4.f);
			R.T1 = KeyT + 1.2f;
			break;
		case ETagReplayAngle::TightIso:
			R.T0 = KeyT - (bFullPace ? 1.8f : 1.4f);
			R.T1 = KeyT + (bFullPace ? 1.3f : 1.0f);
			R.Pre = 0.7f;
			R.Slow = 0.3f;
			R.Post = 0.55f;
			break;
		case ETagReplayAngle::Pylon:
			R.T0 = KeyT - 2.4f;
			R.T1 = KeyT + 1.f;
			R.Pre = 0.9f;
			break;
		default:
			R.T0 = KeyT - 3.2f;
			R.T1 = KeyT + 1.4f;
			break;
		}
		Clamp(R);
		if (A == ETagReplayAngle::TightIso)
		{
			// the near (TV) sideline unless the play blocks it more than the far one does
			R.Side = -1.f;
			const float Near = Blocked(R);
			R.Side = 1.f;
			const float Far = Blocked(R);
			R.Side = Near <= Far + 0.05f ? -1.f : 1.f;
		}
		else if (A == ETagReplayAngle::Pylon) R.Side = SubjAtT(KeyT).Y >= 0.0 ? 1.f : -1.f; // (the corner he scores at)
		return R;
	};
	auto ClearOf = [&](ETagReplayAngle A) -> bool
	{
		if (A == ETagReplayAngle::HighEndZone || A == ETagReplayAngle::Reverse) return true; // (over the bodies)
		return SubjId >= 0 && Blocked(Make(A)) <= 0.3f;
	};
	const bool bTwo = (Res.td || bTake) && PaceNow() != ETagPace::Quick;
	TArray<ETagReplayAngle> Plan;
	BcAnglePlan(Class, bTwo, St.FirstAngles, ClearOf, BroadcastSeed ^ (static_cast<uint32>(St.PlayN) * 2654435761u), false, Plan);
	for (const ETagReplayAngle A : Plan) St.Runs.Add(Make(A));
}

void AMatchDirector::SetupReplayRun()
{
	FTagBcState& St = Bc();
	FTagReplayRun& R = St.Runs[St.Run];
	ReplayT = R.T0;
	ReplayIdx = 0;
	ReplayFocus = FVector::ZeroVector;
	St.bRPlaced = false;
	R.Shown = 0.f;
	R.SlowestRate = 9.f;
	R.SlowestAt = R.T0;
	R.Frames = R.LensBad = 0;
	ReplayView.Angle = St.Run;
	ReplayView.Type = R.Type;
	ReplayView.AngleName = BcAngleName(R.Type);
	ReplayView.KeyKind = St.RKeyKind;
	ReplayView.SubjectId = R.SubjectId;
	ReplayView.Subject = nullptr;
	ReplayView.bSubjectHome = false;
	if (const AAthlete* SA = R.SubjectId >= 0 ? Athletes.FindRef(R.SubjectId).Get() : nullptr)
	{
		ReplayView.Subject = SA->Player;
		ReplayView.bSubjectHome = Game && SA->Player && Game->home->hasPlayer(SA->Player);
	}
}

void AMatchDirector::ApplyReplayAt(float T, FVector& OutBall, FVector& OutSubject, bool& bOutSubject)
{
	bOutSubject = false;
	OutBall = OutSubject = FVector::ZeroVector;
	if (ReplayBuf.Num() == 0) return;
	const FTagBcState& St = Bc();
	const int32 SubjId = St.Runs.IsValidIndex(St.Run) ? St.Runs[St.Run].SubjectId : -1;
	if (!ReplayBuf.IsValidIndex(ReplayIdx) || ReplayBuf[ReplayIdx].T > T) ReplayIdx = 0; // (a second angle starts earlier in the film)
	while (ReplayIdx + 1 < ReplayBuf.Num() && ReplayBuf[ReplayIdx + 1].T <= T) ++ReplayIdx;
	const FReplayFrame& A = ReplayBuf[ReplayIdx];
	const FReplayFrame& B = ReplayBuf[FMath::Min(ReplayIdx + 1, ReplayBuf.Num() - 1)];
	const float Al = B.T > A.T ? FMath::Clamp((T - A.T) / (B.T - A.T), 0.f, 1.f) : 0.f;
	for (const FReplayAth& RA : A.Ath)
	{
		AAthlete* Ath = Athletes.FindRef(RA.Id);
		if (!Ath) continue;
		const FReplayAth* RB = B.Ath.FindByPredicate([&RA](const FReplayAth& X) { return X.Id == RA.Id; });
		if (!RB || RB->Rot.Num() != RA.Rot.Num()) RB = &RA;
		ReplayScratch.SetNum(RA.Rot.Num());
		for (int32 i = 0; i < RA.Rot.Num(); ++i)
			ReplayScratch[i] = FTransform(FQuat::Slerp(FQuat(RA.Rot[i]), FQuat(RB->Rot[i]), Al).GetNormalized(),
				FMath::Lerp(FVector(RA.Pos[i]), FVector(RB->Pos[i]), Al));
		FTransform W;
		W.Blend(RA.World, RB->World, Al);
		if (!Ath->IsHidden()) Ath->SetDormant(false); // (a man on the film is awake to show it; no-op when he already is)
		Ath->ShowReplayPose(W, ReplayScratch);
		if (RA.Id == SubjId)
		{
			OutSubject = W.GetLocation();
			bOutSubject = true;
		}
	}
	FTransform BallT;
	BallT.Blend(A.Ball, B.Ball, Al);
	if (Ball)
	{
		Ball->SetActorTransform(BallT);
		Ball->SetActorHiddenInGame(!A.bBall);
	}
	OutBall = BallT.GetLocation();
}

void AMatchDirector::ReplayCameraFrame(float Dt, const FVector& BallAt, const FVector& SubjAt, bool bSubj, bool bCutNow)
{
	FTagBcState& St = Bc();
	FTagReplayRun& R = St.Runs[St.Run];
	const double Y = YD;
	const FVector Subj = bSubj ? SubjAt + FVector(0, 0, 110) : BallAt;
	FVector Focus = FMath::Lerp(BallAt, Subj, bSubj ? 0.4f : 0.f);
	Focus.Z = 100.0;
	const bool bPlace = bCutNow || !St.bRPlaced;
	const int32 RDir = ReplayDirOverride != 0 ? ReplayDirOverride : Dir; // (a stored highlight: its own play's direction)
	FVector P, Look;
	float Focal = 50.f, Aperture = 5.6f;
	switch (R.Type)
	{
	case ETagReplayAngle::HighEndZone:
	{
		// high behind the offense's end zone, wide, the play coming at the lens (a fixed head: it pans and zooms)
		if (bPlace)
		{
			const double BehindX = (RDir > 0 ? 0.0 : 100.0) * Y - RDir * 13.0 * Y;
			St.RCam = FVector(BehindX, FMath::Clamp(Focus.Y * 0.35, -12.0 * Y, 12.0 * Y), 15.0 * Y);
			St.RLook = Focus;
			St.RLookVel = FVector::ZeroVector;
		}
		BcSpring(St.RLook, St.RLookVel, Focus, 0.3f, Dt);
		P = St.RCam;
		Look = St.RLook;
		Focal = FMath::Clamp(36.f * static_cast<float>((Look - P).Size()) / static_cast<float>(26.0 * Y), 24.f, 200.f);
		break;
	}
	case ETagReplayAngle::TightIso:
	{
		// the sideline cart on the key man, a long lens, chest high: 2 yd inside the sideline (on it, the cart ran
		// through the photographers and the crews standing there - 641 of 825 frames with a body at the lens)
		const FVector Want(Subj.X - RDir * 3.0 * Y, R.Side * (tag::FIELD_W / 2 - 2.0) * Y, 180.0);
		if (bPlace)
		{
			St.RCam = Want;
			St.RCamVel = FVector::ZeroVector;
			St.RLook = Subj;
			St.RLookVel = FVector::ZeroVector;
		}
		BcSpring(St.RCam, St.RCamVel, Want, 0.45f, Dt);
		BcSpring(St.RLook, St.RLookVel, Subj, 0.12f, Dt);
		P = St.RCam;
		Look = St.RLook;
		Focal = FMath::Clamp(36.f * static_cast<float>((Look - P).Size()) / 420.f, 70.f, 160.f);
		Aperture = 2.8f;
		break;
	}
	case ETagReplayAngle::Pylon:
	{
		// low at the front corner of the end zone he scores in, looking back up the field
		if (bPlace)
		{
			St.RCam = FVector((RDir > 0 ? 100.0 : 0.0) * Y + RDir * 0.6 * Y, R.Side * (tag::FIELD_W / 2 + 0.4) * Y, 45.0);
			St.RLook = FVector(Focus.X, Focus.Y, 90.0);
			St.RLookVel = FVector::ZeroVector;
		}
		BcSpring(St.RLook, St.RLookVel, FVector(Focus.X, Focus.Y, 90.0), 0.2f, Dt);
		P = St.RCam;
		Look = St.RLook;
		Focal = FMath::Clamp(36.f * static_cast<float>((Look - P).Size()) / 900.f, 22.f, 60.f);
		Aperture = 4.f;
		break;
	}
	default:
	{
		// reverse angle from field level on the far side, a longer lens, easing after the ball. (It stays on the playing
		// field: 16 m to the far side of a play near that sideline put it behind the bench - then the near side, chosen
		// once a run so the angle does not flip. 5.6 m up: from head height the men between were the picture.)
		const float SideLim = static_cast<float>(tag::FIELD_W) * 0.5f * static_cast<float>(Y) - 150.f;
		if (bPlace || ReplayFocus.IsZero())
		{
			R.Side = BallAt.Y + 1650.f > SideLim ? -1.f : 1.f;
			ReplayFocus = BallAt;
		}
		ReplayFocus = FMath::Lerp(ReplayFocus, BallAt, 1.f - FMath::Exp(-Dt / 0.55f * 4.f));
		P = ReplayFocus + FVector(-350.f, 1650.f * R.Side, 560.f);
		P.Y = FMath::Clamp(P.Y, -static_cast<double>(SideLim), static_cast<double>(SideLim));
		Look = FVector(ReplayFocus.X, ReplayFocus.Y, 95.f);
		Focal = 50.f;
		break;
	}
	}
	if (bPlace)
	{
		St.RFocal = Focal;
		St.RFocalVel = 0.f;
	}
	else
	{
		const float W = 1.3863f / 0.4f, J0 = St.RFocal - Focal, J1 = St.RFocalVel + J0 * W, E = FMath::Exp(-W * Dt);
		St.RFocal = Focal + (J0 + J1 * Dt) * E;
		St.RFocalVel = (St.RFocalVel - J1 * W * Dt) * E;
	}
	St.bRPlaced = true;
	Camera->SetActorLocation(P);
	Camera->SetActorRotation((Look - P).Rotation());
	if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
	{
		C->FocusSettings.ManualFocusDistance = (Look - P).Size();
		C->CurrentFocalLength = St.RFocal;
		C->CurrentAperture = Aperture;
	}
	// (measured: a body at the lens, a lens in the stands)
	++R.Frames;
	++St.ReplayFrames;
	if ((P.Z < 300.0 && NearestPerson(P, nullptr) < 100.f) || !ShotSpotOk(P))
	{
		++R.LensBad;
		++St.ReplayLensBad;
	}
}

void AMatchDirector::TickReplay(float Dt, bool bSkip)
{
	FTagBcState& St = Bc();
	if (St.Runs.Num() == 0) { FinishReplay(); return; }
	if (bSkip && !St.bSkipReplay)
	{
		// one press: the replay is over (out through the wipe); the press is the skip's, nothing else for 0.3 s
		St.bSkipReplay = true;
		++St.Skips;
		St.SkipFrame = GFrameCounter;
		St.SkipLockUntil = FPlatformTime::Seconds() + 0.3;
		UE_LOG(LogTemp, Display, TEXT("TAG skip: replay (angle %d of %d, %.1f s in)"), St.Run + 1, St.Runs.Num(), St.RShown);
		if (St.Phase == 1)
		{
			if (Wipe.IsValid()) Wipe->Start(0.12f);
			St.Phase = 3;
		}
	}
	const bool bCover = !Wipe.IsValid() || Wipe->Covered();
	St.RShown += Dt;
	FVector BallAt, SubjAt;
	bool bSubj = false;
	if (St.Phase == 0)
	{
		// the slats come down over the live picture; under them, the first angle
		if (!bCover) return;
		if (St.bSkipReplay) { FinishReplay(); return; }
		SetupReplayRun();
		ApplyReplayAt(ReplayT, BallAt, SubjAt, bSubj);
		ReplayCameraFrame(Dt, BallAt, SubjAt, bSubj, true);
		MarkCameraCut(TEXT("replay in"));
		if (Wipe.IsValid()) Wipe->Release();
		St.Phase = 1;
		// the sim-truth stat tags (UI/BroadcastGfx), once a package, on its first angle: in as the film slows into the key
		// moment (the screen seconds to it, through the ramp)
		if (Overlay && Sim && Sim->result && St.TagPicksThisPkg++ == 0)
		{
			const FTagReplayRun& R0 = St.Runs[0];
			float Delay = 0.f;
			const float To = FMath::Max(R0.T0, R0.KeyT - 0.1f);
			for (int32 k = 0; k < 40; ++k)
			{
				const float Step = (To - R0.T0) / 40.f;
				Delay += Step / FMath::Max(0.05f, BcRampRate(R0, R0.T0 + (k + 0.5f) * Step));
			}
			const tag::PlayResult& Res = *Sim->result;
			const tag::Player* Car = Res.complete && Res.receiver ? Res.receiver : Res.rusher;
			FGfxStatTags StatTags = FBroadcastGfx::PickStatTags(Res, Sim->ball, Overlay->Gfx().CarrierTopMph(Car));
			StatTags.Delay = Delay;
			++St.TagPicks;
			if (StatTags.Num > 0)
			{
				++St.TagsShown;
				Overlay->Gfx().ShowStatTags(StatTags);
			}
			UE_LOG(LogTemp, Display, TEXT("TAG replay stat tags: %d (%s%s%s), in after %.2f s at the %s"), StatTags.Num, StatTags.Num ? *StatTags.Tag[0].Label : TEXT("none notable"),
				StatTags.Num > 1 ? TEXT(", ") : TEXT(""), StatTags.Num > 1 ? *StatTags.Tag[1].Label : TEXT(""), Delay, St.RKeyKind);
		}
		else if (St.TagPicksThisPkg > 1) ++St.PkgsTwoPicks;
		return;
	}
	FTagReplayRun& R = St.Runs[St.Run];
	const float Rate = BcRampRate(R, ReplayT);
	if (St.Phase == 1 && Rate < R.SlowestRate - 1e-4f)
	{
		R.SlowestRate = Rate;
		R.SlowestAt = ReplayT;
	}
	// (the film runs on under a wipe that is coming down, to the end of what was recorded)
	ReplayT = FMath::Min(ReplayT + Dt * Rate, ReplayEnd);
	R.Shown += Dt;
	ApplyReplayAt(ReplayT, BallAt, SubjAt, bSubj);
	ReplayCameraFrame(Dt, BallAt, SubjAt, bSubj, false);
	ReplayView.Rate = Rate;
	ReplayView.KeyIn = R.KeyT - ReplayT;
	ReplayView.bAtKey = FMath::Abs(ReplayView.KeyIn) < 0.45f;
	{
		static int32 GReplayShots = 0;
		static float NextShot = 0.f;
		if (bAuto && GReplayShots < 12 && ReplayCount == 0 && St.Phase == 1 && (ReplayT >= NextShot || NextShot > ReplayEnd))
		{
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_replay_%02d.png"), GReplayShots++), false, false);
			NextShot = ReplayT + 0.45f;
		}
	}
	if (St.Phase == 1 && ReplayT >= R.T1 - 1e-3f)
	{
		UE_LOG(LogTemp, Display, TEXT("TAG replay angle=%s ramp=%.2f,%.2f secs=%.2f (angle %d of %d, film %.2f-%.2f s, key %s at %.2f, slowest %.2fx at %.2f, subject %d, lens bad %d of %d frames)"),
			BcAngleName(R.Type), R.KeyT - 0.6f, R.KeyT + 0.8f, R.Shown, St.Run + 1, St.Runs.Num(), R.T0, R.T1, St.RKeyKind, R.KeyT, R.SlowestRate, R.SlowestAt,
			R.SubjectId, R.LensBad, R.Frames);
		if (St.Test.Mode == 1)
		{
			// (the replay test: the slowest part of the ramp at the key moment, a clear lens)
			const bool bKeyIn = R.KeyT > R.T0 + 0.2f && R.KeyT < R.T1 - 0.2f;
			const bool bRampOk = !bKeyIn || FMath::Abs(R.SlowestAt - R.KeyT) <= 0.35f;
			(bRampOk ? St.Test.Pass : St.Test.Fail)++;
			UE_LOG(LogTemp, Display, TEXT("TAG replaytest %s: %s ramp slowest %.2fx at %.2f s, key %.2f s"), bRampOk ? TEXT("PASS") : TEXT("FAIL"), BcAngleName(R.Type), R.SlowestRate, R.SlowestAt, R.KeyT);
			const bool bLensOk = R.LensBad <= FMath::Max(1, R.Frames / 20);
			(bLensOk ? St.Test.Pass : St.Test.Fail)++;
			UE_LOG(LogTemp, Display, TEXT("TAG replaytest %s: %s lens clear (%d of %d frames bad)"), bLensOk ? TEXT("PASS") : TEXT("FAIL"), BcAngleName(R.Type), R.LensBad, R.Frames);
		}
		const bool bMore = St.Run + 1 < St.Runs.Num();
		if (Wipe.IsValid()) Wipe->Start(0.15f);
		St.Phase = bMore ? 2 : 3;
	}
	else if (St.Phase == 2 && bCover)
	{
		if (St.bSkipReplay) { FinishReplay(); return; }
		++St.Run;
		SetupReplayRun();
		ApplyReplayAt(ReplayT, BallAt, SubjAt, bSubj);
		ReplayCameraFrame(Dt, BallAt, SubjAt, bSubj, true);
		MarkCameraCut(TEXT("replay angle"));
		if (Wipe.IsValid()) Wipe->Release();
		St.Phase = 1;
	}
	else if (St.Phase == 3 && bCover) FinishReplay();
}

void AMatchDirector::FinishReplay()
{
	FTagBcState& St = Bc();
	for (auto& KV : Athletes)
		if (KV.Value) KV.Value->EndReplay();
	++ReplayCount;
	bCutNext = true; // (out of the replay: a cut back to the game camera at its framing, under the wipe)
	if (Wipe.IsValid()) Wipe->Release();
	St.ReplaySecsThisPlay += St.RShown;
	UE_LOG(LogTemp, Display, TEXT("TAG replay done: %d angle(s), %.2f s on screen%s (cuts under the wipe %d, outside it %d this game)"), St.Runs.Num(), St.RShown,
		St.bSkipReplay ? TEXT(", skipped") : TEXT(""), St.CutsCovered, St.CutsBare);
	const int32 Ser = ReplayView.Serial;
	ReplayView = FTagReplayView();
	ReplayView.Serial = Ser;
	Overlay->SetHint(TEXT(""));
	State = EMatchState::Post;
	Overlay->SetClockOffset(0);
	ApplyPlay(); // (the play into the book; the shots after it start under the wipe: BroadcastAfterPlay)
}

void AMatchDirector::NoteCutForBroadcast(bool bMarked)
{
	if (!BcState) return;
	FTagBcState& St = *BcState;
	const bool bWipeOn = Wipe.IsValid() && Wipe->IsOn();
	if (State != EMatchState::Replay && !bWipeOn) return;
	if (Wipe.IsValid() && Wipe->Covered()) ++St.CutsCovered;
	else if (State == EMatchState::Replay)
	{
		++St.CutsBare;
		UE_LOG(LogTemp, Warning, TEXT("TAG replay cut outside the wipe (%s, wipe %.2f)"), bMarked ? TEXT("marked") : TEXT("a jump"), Wipe.IsValid() ? Wipe->Progress() : -1.f);
	}
	else ++St.CutsPartial; // (a cut while the slats lift off: the shot after the replay)
}

// ------------------------------------------------------------------ the shots between the plays (04 A3)
namespace
{
	struct FBcCand { ETagShot Type = ETagShot::None; float W = 0.f; int32 Team = -1; FString SubjKey; };

	/** What the play gives each kind of capper: the rule table (weights; zero: not this play). */
	void BcCandidates(const FTagBcPlay& C, bool bFull, uint32 Salt, TArray<FBcCand>& Out)
	{
		auto Add = [&Out](ETagShot T, float W, int32 Team, const FString& Key) { if (W > 0.f) Out.Add(FBcCand{T, W, Team, Key}); };
		// the beaten man (the sacked quarterback, the passer picked off, the defender beaten for the score: the capper)
		Add(ETagShot::Opponent, C.bHasBeaten ? (C.bSack || C.bTake ? 3.f : C.bTd ? 2.f : C.bStop ? 1.6f : 0.f) : 0.f, -1, TEXT("opponent"));
		// the bench of the side it went for (a score; a takeaway, a fourth down)
		if (C.BenTeam >= 0)
			Add(ETagShot::Bench, C.bScore ? 3.f : C.bTake ? 2.4f : C.bFourth ? 1.8f : bFull && C.bBig ? 1.f : 0.f, C.BenTeam,
				FString::Printf(TEXT("bench:%d"), C.BenTeam));
		// a head coach (the money downs, a takeaway, a score; late and close above all): either side's
		const int32 CoachTeam = C.BenTeam < 0 ? 1 : (tag::Recent::hash("coach", Salt) & 1) ? C.BenTeam : 1 - C.BenTeam;
		Add(ETagShot::Coach, (C.bThird || C.bFourth || C.bTake || C.bScore) ? 1.6f + (C.bLate ? 1.f : 0.f) : 0.f, CoachTeam, FString::Printf(TEXT("coach:%d"), CoachTeam));
		// the home crowd, when it is the home side's moment (the stands are in its colours)
		Add(ETagShot::Crowd, !C.bCrowdOk ? 0.f : C.BenTeam == 1 && (C.bScore || C.bTake || C.bBig) ? 2.2f : C.bScore ? 1.f : 0.f, 1, TEXT("crowd"));
		// the referee on a flag (TV always shows the official who has it)
		Add(ETagShot::Official, C.bFlag ? 5.f : 0.f, -1, TEXT("official"));
	}

	int32 BcCooldown(ETagShot T)
	{
		switch (T)
		{
		case ETagShot::Opponent: return 2;
		case ETagShot::Bench: return 4;
		case ETagShot::Coach: return 6;
		case ETagShot::Crowd: return 6;
		default: return 0;
		}
	}

	int32 BcSubjCooldown(const FString& Key) { return Key.StartsWith(TEXT("bench")) ? 8 : Key.StartsWith(TEXT("coach")) ? 10 : 0; }

	/** The cappers: the best by weight x how long ago the store last saw the kind (this game and the games before it) x
	 *  a salted jitter; none in its cooldown, none whose subject is in its own, none a third of a kind in a row; Want at
	 *  most. (The same for a real pick and the test's dry one: only the store and the history decide.) */
	void BcPickCappers(const FTagBcPlay& C, bool bFull, int32 Want, int32 PlayN, uint32 Salt, const TArray<ETagShot>& Seq, const TMap<int32, int32>& TypeLast,
		const TMap<FString, int32>& SubjLast, TArray<FBcCand>& Out)
	{
		TArray<FBcCand> Cands;
		BcCandidates(C, bFull, Salt, Cands);
		TArray<ETagShot> Stream = Seq;
		for (int32 k = 0; k < Want; ++k)
		{
			float Best = -1.f;
			int32 BestI = -1;
			for (int32 i = 0; i < Cands.Num(); ++i)
			{
				const FBcCand& Cd = Cands[i];
				if (Out.ContainsByPredicate([&Cd](const FBcCand& X) { return X.Type == Cd.Type; })) continue;
				const int32 Cool = BcCooldown(Cd.Type), SCool = BcSubjCooldown(Cd.SubjKey);
				if (const int32* L = TypeLast.Find(static_cast<int32>(Cd.Type)); L && Cool > 0 && PlayN - *L <= Cool) continue;
				if (const int32* L = SubjLast.Find(Cd.SubjKey); L && SCool > 0 && PlayN - *L <= SCool) continue;
				if (Stream.Num() >= 2 && Stream.Last() == Cd.Type && Stream.Last(1) == Cd.Type) continue;
				const int32 Age = tag::Recent::get().age("shot.capper", BcU(BcShotName(Cd.Type)));
				const float Novel = 1.f + 0.12f * static_cast<float>(FMath::Min(Age < 0 ? 8 : Age, 8));
				const float Jit = static_cast<float>(tag::Recent::hash(BcU(BcShotName(Cd.Type)), Salt) % 1000u) / 1000.f * 0.3f;
				const float Sc = Cd.W * Novel + Jit;
				if (Sc > Best) { Best = Sc; BestI = i; }
			}
			if (BestI < 0) break;
			Out.Add(Cands[BestI]);
			Stream.Add(Cands[BestI].Type);
		}
	}

	int32 BcFansNear(const TArray<FVector>& People, const FVector& At, double R)
	{
		int32 N = 0;
		const double R2 = R * R;
		for (const FVector& P : People)
			if (FVector::DistSquared(P, At) < R2) ++N;
		return N;
	}

	/** The crowd shot's sections: the end stand behind the end zone nearer Spot, four sections across it (clear of the
	 *  goalposts in the middle). */
	void BcCrowdSection(const FVector& Spot, int32 i, FVector& OutLook, FVector& OutLens)
	{
		const double Y = YD;
		const double Sgn = Spot.X >= 50.0 * Y ? 1.0 : -1.0;
		static const double Ys[4] = {-14.0, -7.0, 7.0, 14.0};
		OutLook = FVector(50.0 * Y + Sgn * 74.0 * Y + Sgn * 400.0, Ys[i] * Y, 330.0);
		OutLens = FVector(50.0 * Y + Sgn * 56.0 * Y, Ys[i] * Y, 160.0);
	}

	float BcProgramLeft(const FTagBcState& St, const FTagShotView& V)
	{
		float L = V.bOn ? FMath::Max(0.f, St.Shot.Beat.Secs - St.Shot.T) : 0.f;
		L += FMath::Max(0.f, St.GapLeft);
		for (const FTagBcBeat& B : St.Queue) L += B.Secs;
		return L;
	}
}

bool AMatchDirector::BeatsAllowed() const
{
	static const bool bForce = FParse::Param(FCommandLine::Get(), TEXT("TagCutIns")); // review the shots under automation
	static const bool bUiTest = FParse::Param(FCommandLine::Get(), TEXT("TagInputTest")) || FParse::Param(FCommandLine::Get(), TEXT("TagControlsTest")) ||
		FParse::Param(FCommandLine::Get(), TEXT("TagUiShots")) || FParse::Param(FCommandLine::Get(), TEXT("TagMenuAudio"));
	if ((bAuto || bLineup) && !bForce) return false; // (automation drives its own shots)
	return !(bTackleCam || bArtTest || bCallShot || bMoveShot || bPadTest || bPadPlayTest || bMultiTest || bFeelTest || bPauseTest || bHitStopTest || bUiTest);
}

AAthlete* AMatchDirector::AthleteOfEnt(const tag::Ent* E)
{
	if (!E || !E->p || !Game) return nullptr;
	return AthleteFor(E->p, E->side == tag::Side::Off ? Game->poss : tag::other(Game->poss));
}

float AMatchDirector::NearestPerson(const FVector& At, const AActor* Except) const
{
	float Best = 1e9f;
	auto Probe = [&Best, &At, Except](const AAthlete* O)
	{
		if (!O || O == Except || O->IsHidden()) return;
		for (const TCHAR* Bn : {TEXT("head"), TEXT("pelvis")}) Best = FMath::Min(Best, static_cast<float>(FVector::Dist(O->BoneWorld(Bn), At)));
	};
	for (const auto& KV : Athletes) Probe(KV.Value);
	for (const AAthlete* O : Sideline) Probe(O);
	for (const AAthlete* O : Officials) Probe(O);
	for (const AAthlete* O : Crew) Probe(O);
	Probe(Umpire);
	if (BcState)
		for (const FVector& Pp : BcState->StandPeople) Best = FMath::Min(Best, static_cast<float>(FVector::Dist(Pp, At)));
	return Best;
}

bool AMatchDirector::SightClear(const FVector& C, const FVector& At, const AActor* Except, float SubjectR) const
{
	bool bClear = true;
	auto Probe = [&bClear, &C, &At, Except, SubjectR](const AAthlete* O)
	{
		if (!bClear || !O || O == Except || O->IsHidden()) return;
		const FVector Q = O->GetActorLocation() + FVector(0, 0, 100);
		if (FVector::Dist2D(Q, At) <= SubjectR) return; // (his own pile, his bench mates: the picture)
		// (nobody at the lens or on the sight line: a long lens turns a man a stride away into a white blur)
		if (FVector::Dist2D(Q, C) < 230.0 || FMath::PointDistToSegment(Q, C, At) < 60.0) bClear = false;
	};
	for (const auto& KV : Athletes) Probe(KV.Value);
	for (const AAthlete* O : Sideline) Probe(O);
	for (const AAthlete* O : Officials) Probe(O);
	for (const AAthlete* O : Crew) Probe(O);
	Probe(Umpire);
	return bClear;
}

bool AMatchDirector::ShotSpotOk(const FVector& C) const
{
	const double Y = YD;
	if (C.ContainsNaN() || C.Z < 30.0) return false; // (in the turf)
	// the stands: the bowl's front edge 9 yd past the sidelines and 74 yd either side of midfield, the seats on a 33 deg
	// rake from 1.35 m - a lens out there must be over the seats with a man's height to spare (Render/Stadium.cpp)
	const double OutY = FMath::Abs(C.Y) - (tag::FIELD_W / 2 + 9.0) * Y;
	const double OutX = FMath::Abs(C.X - 50.0 * Y) - 74.0 * Y;
	const double Out = FMath::Max(OutY, OutX);
	if (Out > -60.0 && C.Z < 135.0 + FMath::Max(0.0, Out) * 0.65 + 220.0) return false;
	// the goalposts on the end lines (the post, its padding, the uprights)
	for (const double Gx : {-10.0 * Y, 110.0 * Y})
		if (FMath::Abs(C.X - Gx) < 120.0 && FMath::Abs(C.Y) < 400.0 && C.Z < 1100.0) return false;
	// the stadium's own people (photographers, the crews on the sidelines, the fans)
	if (BcState)
		for (const FVector& Pp : BcState->StandPeople)
			if (FVector::DistSquared(Pp, C) < 100.0 * 100.0) return false;
	return true;
}

void AMatchDirector::BroadcastPlayDead(const tag::PlayResult& R, const tag::Ent* Star)
{
	FTagBcState& St = Bc();
	static const bool bOldShots = FParse::Param(FCommandLine::Get(), TEXT("TagShotsOld")); // A/B: the one cut-in, as before
	static const int32 BigGain = FParse::Param(FCommandLine::Get(), TEXT("TagCutIns")) ? 5 : 20; // review: more samples
	St.WhistleSimT = Sim ? Sim->t : 0.0;
	++St.PlayN;
	St.WhistleT = FPlatformTime::Seconds();
	St.bLeadThisPlay = St.bReplayThisPlay = false;
	St.BeatSecsThisPlay = St.ReplaySecsThisPlay = 0.f;
	if (St.Test.Mode == 3) // (the pace test: each play at the next pace in turn)
		St.PaceOverride = St.PlayN % 3 == 1 ? ETagPace::Quick : St.PlayN % 3 == 2 ? ETagPace::Standard : ETagPace::Full;
	const ETagPace Pace = PaceNow();
	St.PaceAtWhistle = Pace;
	if (St.Test.Mode == 1 && Sim && !Sim->isPlaceKick()) bReplayWorthy = true; // (the replay test: every play)
	if (bOldShots)
	{
		if (R.td || R.interception || R.fumbleLost || R.kind == tag::PlayKind::Sack || R.yards >= BigGain) StartCutIn(Star, R.td ? 3.5f : 2.8f);
		return;
	}
	FTagBcPlay& C = St.Ctx;
	C = FTagBcPlay();
	if (!Sim || !Game) return;
	const tag::Game& G = *Game; // (the play is not in the book yet: the down, the distance and the score before it)
	C.bValid = true;
	C.LogN = G.log.size();
	const bool bScrim = !Sim->isReturn() && !Sim->isPlaceKick() && !bTwoPoint;
	C.bTd = R.td.has_value();
	C.bTake = R.interception || R.fumbleLost;
	C.bSack = R.kind == tag::PlayKind::Sack;
	C.bScore = C.bTd || R.safety;
	const bool bMade = !R.turnover && R.yards >= G.toGo - 1e-6;
	C.bThird = bScrim && G.down == 3;
	C.bFourth = bScrim && G.down == 4;
	C.bConv = (C.bThird || C.bFourth) && bMade;
	C.bStop = (C.bThird || C.bFourth) && !bMade && !C.bTake && !C.bTd;
	const tag::PlayEvent* HitEv = nullptr;
	for (const tag::PlayEvent& Ev : Sim->events)
		if (Ev.type == "bighit") HitEv = &Ev;
	C.bBigHit = HitEv != nullptr;
	C.bLate = G.q >= 4 && G.clock <= 300.0 && FMath::Abs(G.score.home - G.score.away) <= 8;
	const bool bOffWon = (R.td && *R.td == tag::Side::Off) || C.bConv || (R.yards >= 20 && !R.turnover);
	const bool bDefWon = C.bTake || C.bSack || C.bStop || R.safety || (R.td && *R.td == tag::Side::Def);
	const tag::TeamKey OffK = G.poss, DefK = tag::other(G.poss);
	C.BenTeam = bDefWon ? (DefK == tag::TeamKey::Home ? 1 : 0) : bOffWon ? (OffK == tag::TeamKey::Home ? 1 : 0) : -1;
	C.bBig = bReplayWorthy || C.bTd || C.bTake || C.bSack || R.yards >= BigGain;
	C.Spot = Ball ? Ball->GetActorLocation() : FVector::ZeroVector;
	auto EntOf = [this](const tag::Player* P) -> const tag::Ent*
	{
		if (P && Sim)
			for (const tag::Ent* E : Sim->ents)
				if (E->p == P) return E;
		return nullptr;
	};
	// the man beaten on it
	const tag::Ent* Beat = nullptr;
	if (C.bSack) Beat = Sim->qb;
	else if (R.interception) Beat = EntOf(R.passer);
	else if (R.fumbleLost) Beat = EntOf(R.fumbleBy);
	else if (R.td && *R.td == tag::Side::Off && Sim->ball.holder)
	{
		double BestD = 1e9;
		for (const tag::Ent* E : Sim->def)
			if (const double D = tag::dist(E->pos, Sim->ball.holder->pos); D < BestD) { BestD = D; Beat = E; }
	}
	else if (C.bStop) Beat = R.incomplete ? Sim->qb : Sim->ball.holder;
	C.Beaten = AthleteOfEnt(Beat);
	C.bHasBeaten = C.Beaten.IsValid();
	// the man who made it (the lead shot): the carrier, the sacker, the hitter on a big hit, the tackler on a stop
	const tag::Ent* Maker = Star;
	if (C.bBigHit && HitEv->a && !C.bTd && !C.bTake) Maker = HitEv->a;
	else if (C.bStop && !R.tacklers.empty()) Maker = EntOf(R.tacklers.front());
	C.Hero = AthleteOfEnt(Maker);
	C.bHasHero = C.Hero.IsValid();
	// the pace: Quick replays only the touchdowns and the takeaways
	if (Pace == ETagPace::Quick && bReplayWorthy && !(C.bTd || C.bTake))
	{
		bReplayWorthy = false;
		UE_LOG(LogTemp, Display, TEXT("TAG replay passed over: the pace is Quick (%s)"), *ReplayWhyNow);
	}
	C.bReplay = bReplayWorthy;
	{
		// a highlight in the making: a play worth seeing again at the half or the end (scores, takeaways, explosives,
		// sacks, a fourth down stopped), its key moment from the sim's events; its film is copied when the play is in the
		// book (CaptureHighlight). (The halftime and postgame tests keep every play: a short half has few big ones.)
		static const bool bNoHl = FParse::Param(FCommandLine::Get(), TEXT("TagNoHighlights"));
		const float HlScore = ReplayScoreNow + (C.bTd ? 3.f : 0.f) + (C.bTake ? 2.5f : 0.f) + (C.bSack ? 0.8f : 0.f) + (R.yards >= 20 ? 1.f : 0.f) +
			(C.bFourth && C.bStop ? 1.5f : 0.f);
		St.bHlPending = !bNoHl && HlScore >= (St.Test.Mode >= 5 ? -100.f : 1.f);
		if (St.bHlPending)
		{
			FTagBcHighlight& Hp = St.HlPending;
			Hp = FTagBcHighlight();
			Hp.Score = HlScore;
			Hp.Half = G.q <= 2 ? 1 : 2;
			Hp.Q = G.q;
			Hp.PlayN = St.PlayN;
			Hp.Dir = Dir;
			Hp.Class = C.bTd ? TEXT("td") : C.bTake ? TEXT("take") : C.bSack ? TEXT("sack") : R.complete ? TEXT("pass") : TEXT("run");
			Hp.KeySim = St.WhistleSimT - (C.bTd ? 0.3 : 0.6);
			const tag::Ent* HlWho = Sim->ball.holder;
			for (const tag::PlayEvent& Ev : Sim->events)
				if (Ev.type == "catch" || Ev.type == "divecatch" || Ev.type == "int" || Ev.type == "fumble" || Ev.type == "bighit" || (C.bSack && Ev.type == "tackle"))
				{
					if (C.bTd && !C.bTake && St.WhistleSimT - Ev.t > 2.5) continue; // (a long score: its last strides, not the catch upfield)
					Hp.KeySim = Ev.t;
					HlWho = Ev.a ? Ev.a : HlWho;
				}
			Hp.SubjectId = HlWho && HlWho->p ? HlWho->p->id : -1;
			Hp.Desc = FString::Printf(TEXT("Q%d %s, %d yd"), G.q, *Hp.Class, R.yards);
		}
	}
	if (!BeatsAllowed() || Pace == ETagPace::Quick) return;
	const bool bBigOne = C.bTd || C.bTake || C.bSack || R.yards >= BigGain;
	const bool bLead = bBigOne || (Pace == ETagPace::Full && (C.bConv || C.bStop || C.bBigHit));
	if (!bLead || !C.Hero.IsValid()) return;
	FTagBcBeat B;
	B.Type = ETagShot::Hero;
	B.Subject = C.Hero;
	B.bLead = true;
	B.Team = C.Hero->Player && G.home->hasPlayer(C.Hero->Player) ? 1 : 0;
	B.Secs = C.bTd ? 3.5f : bBigOne ? 2.8f : 2.4f;
	B.Why = C.bTd ? TEXT("touchdown") : C.bTake ? TEXT("takeaway") : C.bSack ? TEXT("sack") : R.yards >= BigGain ? TEXT("big gain")
		: C.bBigHit ? TEXT("big hit") : C.bConv ? TEXT("conversion") : TEXT("stop");
	B.Fixed = C.Spot;
	// (never one kind three shots running: after two heroes the man he beat leads, or nothing does. Two short-yardage
	// conversions and a touchdown, each led by its hero with no capper between: shots, gate 2026-11-25 16:17.)
	if (const int32 N = St.Seq.Num(); N >= 2 && St.Seq[N - 1] == ETagShot::Hero && St.Seq[N - 2] == ETagShot::Hero)
	{
		if (!C.Beaten.IsValid()) return;
		B.Type = ETagShot::Opponent;
		B.Subject = C.Beaten;
		B.Team = 1 - B.Team;
	}
	if (StartShot(B))
	{
		St.bLeadThisPlay = true;
		Timer = FMath::Max(Timer, B.Secs); // (the post-play beat holds for the shot)
	}
}

float AMatchDirector::BroadcastAfterPlay()
{
	FTagBcState& St = Bc();
	St.Queue.Reset();
	St.bProgram = false;
	St.GapLeft = 0.f;
	FTagBcPlay& C = St.Ctx;
	CaptureHighlight(); // (the play's film, if it is one to keep: before anything below can return)
	static const bool bOldShots = FParse::Param(FCommandLine::Get(), TEXT("TagShotsOld"));
	if (bOldShots || !C.bValid || !BeatsAllowed() || bHurryNext || bSpikeNext || !Game) return 0.f;
	C.bValid = false; // (once a play)
	const ETagPace Pace = PaceNow();
	if (Pace == ETagPace::Quick) return 0.f;
	const bool bFull = Pace == ETagPace::Full;
	C.bFlag = Game->log.size() > C.LogN && Game->log.back().big == tag::LogTag::Penalty;
	// (the crowd shot needs the fans in: the pack's spectators in the end stand behind the spot)
	if (Stadium && St.StandFor.Get() != Stadium.Get())
	{
		St.StandFor = Stadium.Get();
		St.StandPeople.Reset();
		TInlineComponentArray<USkeletalMeshComponent*> Comps(Stadium.Get());
		for (const USkeletalMeshComponent* SC : Comps)
			if (SC && SC->IsVisible()) St.StandPeople.Add(SC->GetComponentLocation() + FVector(0, 0, 100));
	}
	C.bCrowdOk = false;
	for (int32 i = 0; i < 4 && !C.bCrowdOk; ++i)
	{
		FVector L, Lens;
		BcCrowdSection(C.Spot, i, L, Lens);
		C.bCrowdOk = BcFansNear(St.StandPeople, L, 450.0) >= 6;
	}
	const int32 Want = C.bBig ? (bFull ? 2 : 1) : (C.bFlag || (bFull && (C.bThird || C.bFourth || C.bBigHit))) ? 1 : 0;
	if (Want == 0) return 0.f;
	const uint32 Salt = BroadcastSeed ^ (static_cast<uint32>(St.PlayN) * 2654435761u);
	TArray<FBcCand> Picks;
	BcPickCappers(C, bFull, Want, St.PlayN, Salt, St.Seq, St.TypeLastPlay, St.SubjLastPlay, Picks);
	if (Picks.Num() == 0) return 0.f;
	bool bWide = bFull || C.bBig;
	// no three-shot sequence twice in twenty plays: the wide reset left off, else the last capper
	auto Key = [&]()
	{
		TArray<FString> Parts;
		if (St.bLeadThisPlay) Parts.Add(BcShotName(ETagShot::Hero));
		for (const FBcCand& Pk : Picks) Parts.Add(BcShotName(Pk.Type));
		if (bWide) Parts.Add(BcShotName(ETagShot::Wide));
		return Parts.Num() == 3 ? FString::Join(Parts, TEXT(">")) : FString();
	};
	for (int32 Tries = 0; Tries < 2; ++Tries)
	{
		const FString K = Key();
		if (K.IsEmpty() || !St.SeqWindow.Contains(K)) break;
		if (bWide) bWide = false;
		else if (Picks.Num() > 1) Picks.Pop();
		UE_LOG(LogTemp, Display, TEXT("TAG shot sequence %s was shown in the last 20 plays: trimmed"), *K);
	}
	auto SecsOf = [bFull](ETagShot T)
	{
		const float K = bFull ? 1.15f : 1.f;
		switch (T)
		{
		case ETagShot::Coach: case ETagShot::Crowd: return 2.f * K;
		case ETagShot::Wide: return bFull ? 1.4f : 1.2f;
		default: return 2.4f * K;
		}
	};
	for (const FBcCand& Pk : Picks)
	{
		FTagBcBeat B;
		B.Type = Pk.Type;
		B.Team = Pk.Team;
		B.Secs = SecsOf(Pk.Type);
		B.Fixed = C.Spot;
		B.Why = C.bScore ? TEXT("score") : C.bTake ? TEXT("takeaway") : C.bSack ? TEXT("sack") : C.bFlag ? TEXT("flag") : C.bFourth ? TEXT("fourth down")
			: C.bThird ? TEXT("third down") : C.bBigHit ? TEXT("big hit") : TEXT("big play");
		if (Pk.Type == ETagShot::Opponent) B.Subject = C.Beaten;
		else if (Pk.Type == ETagShot::Coach)
		{
			for (AAthlete* O : Officials)
				if (O && O->Player && O->Player->id == (Pk.Team == 1 ? 900005 : 900006)) B.Subject = O; // (SpawnSidelines' head coaches)
		}
		else if (Pk.Type == ETagShot::Official)
		{
			// the official nearest the spot (the referee's mic is the booth's: he is shown with the flag's call)
			double BestD = 1e18;
			auto Try = [&B, &BestD, &C](AAthlete* O)
			{
				if (!O || O->IsHidden()) return;
				const double D = FVector::DistSquared2D(O->GetActorLocation(), C.Spot);
				if (D < BestD) { BestD = D; B.Subject = O; }
			};
			for (AAthlete* O : Crew) Try(O);
			Try(Umpire);
		}
		else if (Pk.Type == ETagShot::Bench && !C.bScore)
		{
			// a takeaway, a fourth down: that bench is on its feet (a score sets it off by itself: TickSidelines)
			const int32 K = Pk.Team == 1 ? 0 : 1;
			SidelineCheerLeft[K] = FMath::Max(SidelineCheerLeft[K], 4.5f);
			for (int32 i = 0; i < Sideline.Num(); ++i)
				if (Sideline[i] && SidelineIsHome.IsValidIndex(i) && SidelineIsHome[i] == (Pk.Team == 1)) Sideline[i]->SidelineCheer(true);
		}
		St.Queue.Add(B);
	}
	if (bWide)
	{
		FTagBcBeat W;
		W.Type = ETagShot::Wide;
		W.Fixed = C.Spot;
		W.Secs = SecsOf(ETagShot::Wide);
		W.Why = TEXT("reset");
		St.Queue.Add(W);
	}
	St.bProgram = true;
	// (a replay skipped a moment ago: the first capper waits out the 0.3 s too - TickBeats starts it)
	const double Lock = St.SkipLockUntil - FPlatformTime::Seconds();
	if (Lock > 0.0) St.GapLeft = static_cast<float>(Lock);
	else
	{
		const FTagBcBeat First = St.Queue[0];
		St.Queue.RemoveAt(0);
		StartShot(First);
	}
	return BcProgramLeft(St, ShotView);
}

bool AMatchDirector::StartShot(const FTagBcBeat& B)
{
	FTagBcState& St = Bc();
	if (ShotView.bOn) EndShot(TEXT("cut away"));
	// (the stadium's people for the lens checks, once a stadium)
	if (Stadium && St.StandFor.Get() != Stadium.Get())
	{
		St.StandFor = Stadium.Get();
		St.StandPeople.Reset();
		TInlineComponentArray<USkeletalMeshComponent*> Comps(Stadium.Get());
		for (const USkeletalMeshComponent* SC : Comps)
			if (SC && SC->IsVisible()) St.StandPeople.Add(SC->GetComponentLocation() + FVector(0, 0, 100));
	}
	St.Shot = FTagBcShot();
	St.Shot.Beat = B;
	St.LastShotT = FPlatformTime::Seconds();
	if (!BcIsOpen(B.Type) && !PlaceShot(false))
	{
		// never a broken frame: no posed subject, no clear lens - the wide reset instead (but not a third wide running:
		// a lead shot with no lens after two wides is not shown at all)
		if (const int32 N = St.Seq.Num(); B.bLead && N >= 2 && St.Seq[N - 1] == ETagShot::Wide && St.Seq[N - 2] == ETagShot::Wide)
		{
			UE_LOG(LogTemp, Display, TEXT("TAG shot dropped: %s %s (no lens, and the last two shots were wides)"), BcShotName(B.Type), *BcWho(B.Subject.Get()));
			St.Shot = FTagBcShot();
			return false;
		}
		UE_LOG(LogTemp, Display, TEXT("TAG shot fallback wide: %s %s (%s)"), BcShotName(B.Type), *BcWho(B.Subject.Get()), *St.Shot.Variant);
		++St.Fallbacks;
		St.Shot = FTagBcShot();
		St.Shot.Beat = B;
		St.Shot.Beat.Type = ETagShot::Wide;
		St.Shot.Beat.Subject = nullptr;
		St.Shot.Beat.Why = FString::Printf(TEXT("fallback for the %s shot"), BcShotName(B.Type));
		PlaceShot(false);
	}
	const FTagBcBeat& Bb = St.Shot.Beat;
	const int32 Ser = ShotView.Serial + 1;
	ShotView = FTagShotView();
	ShotView.bOn = true;
	ShotView.Serial = Ser;
	ShotView.Type = Bb.Type;
	ShotView.TypeName = BcShotName(Bb.Type);
	ShotView.Variant = St.Shot.Variant;
	ShotView.Team = Bb.Team;
	ShotView.Left = Bb.Secs;
	if (const AAthlete* SA = Bb.Subject.Get())
	{
		ShotView.Subject = SA->Player;
		ShotView.SubjectId = SA->Player ? SA->Player->id : -1;
	}
	if (!BcIsOpen(Bb.Type))
	{
		// the game's record: the stream of shots (the run of one kind), this play's sequence, the cooldowns, the store
		St.Seq.Add(Bb.Type);
		int32 Run = 0;
		for (int32 i = St.Seq.Num() - 1; i >= 0 && St.Seq[i] == Bb.Type; --i) ++Run;
		St.MaxRun = FMath::Max(St.MaxRun, Run);
		++St.Shots;
		St.TypeCount.FindOrAdd(BcShotName(Bb.Type))++;
		St.VariantCount.FindOrAdd(FString::Printf(TEXT("%s:%s"), BcShotName(Bb.Type), *St.Shot.Variant))++;
		St.TypeLastPlay.Add(static_cast<int32>(Bb.Type), St.PlayN);
		if (Bb.Type == ETagShot::Bench || Bb.Type == ETagShot::Coach) St.SubjLastPlay.Add(FString::Printf(TEXT("%s:%d"), Bb.Type == ETagShot::Bench ? TEXT("bench") : TEXT("coach"), Bb.Team), St.PlayN);
		St.Ctx.Seq += FString::Printf(TEXT("%s%s"), St.Ctx.Seq.IsEmpty() ? TEXT("") : TEXT(">"), BcShotName(Bb.Type));
		St.Ctx.SeqVar += FString::Printf(TEXT("%s%s:%s"), St.Ctx.SeqVar.IsEmpty() ? TEXT("") : TEXT(">"), BcShotName(Bb.Type), *St.Shot.Variant);
		if (!Bb.bLead && Bb.Type != ETagShot::Wide) tag::Recent::get().use("shot.capper", BcU(BcShotName(Bb.Type)));
		UE_LOG(LogTemp, Display, TEXT("TAG shot start %s %s variant %s (%s) %.1f s, play %d, run %d"), BcShotName(Bb.Type), *BcWho(Bb.Subject.Get()), *St.Shot.Variant,
			*Bb.Why, Bb.Secs, St.PlayN, Run);
	}
	return true;
}

bool AMatchDirector::PlaceShot(bool bRecut)
{
	FTagBcState& St = Bc();
	FTagBcShot& Sh = St.Shot;
	const FTagBcBeat& B = Sh.Beat;
	const double Y = YD;
	const uint32 Salt = BroadcastSeed ^ (static_cast<uint32>(St.PlayN) * 2654435761u) ^ (bRecut ? 0x5bd1e995u : 0u);
	tag::Recent& Rec = tag::Recent::get();
	const std::string Cat = BcU(FString(TEXT("shot.")) + BcShotName(B.Type));
	auto Commit = [&](const FVector& Lens, const FVector& LookAt, float Focal, float Ap, const FString& Var)
	{
		Sh.CamPos = Lens;
		Sh.Look = LookAt;
		Sh.Off = Lens - FVector(LookAt.X, LookAt.Y, 0.0);
		Sh.CamVel = Sh.LookVel = FVector::ZeroVector;
		Sh.Focal = Focal;
		Sh.Aperture = Ap;
		Sh.Variant = Var;
		Sh.bPlaced = Sh.bJustPlaced = true;
		Sh.BlockedT = 0.f;
		Rec.use(Cat, BcU(Var));
	};
	// the least recently used of the variants that pass (the store: this game and the ones before it)
	auto PickVar = [&](const TArray<FString>& Names) -> int32
	{
		if (Names.Num() == 0) return -1;
		std::vector<std::string> V;
		for (const FString& N : Names) V.push_back(BcU(N));
		return Rec.leastRecent(Cat, V, Salt);
	};
	// (a lens for a man is on the playing field or its end zones, a metre in from the sidelines)
	auto FieldOk = [Y](const FVector& C)
	{
		return FMath::Abs(C.Y) < (tag::FIELD_W / 2) * Y - 100.0 && C.X > -9.0 * Y && C.X < 109.0 * Y;
	};
	switch (B.Type)
	{
	case ETagShot::Hero: case ETagShot::Opponent: case ETagShot::Coach: case ETagShot::Official:
	{
		// a long lens on a tripod, placed once in front of him: on the game camera's side of the field so the cut does
		// not flip the picture, at shoulder height or low (the cut-in's rig: StartCutIn)
		AAthlete* A = B.Subject.Get();
		FString Why;
		if (!BcPosed(A, Why)) { Sh.Variant = Why; return false; }
		const FVector Tgt = BcChest(A);
		FVector Face = A->GetActorForwardVector();
		Face.Z = 0.0;
		Face = Face.GetSafeNormal();
		if (Face.IsNearlyZero()) Face = FVector::ForwardVector;
		FVector ToCam = CamPos - Tgt;
		ToCam.Z = 0.0;
		ToCam = ToCam.GetSafeNormal();
		const float Dist = B.Type == ETagShot::Hero ? 940.f : B.Type == ETagShot::Opponent ? 820.f : B.Type == ETagShot::Coach ? 860.f : 900.f;
		const float Focal = B.Type == ETagShot::Hero || B.Type == ETagShot::Official ? 85.f : 100.f;
		struct FOk { FVector Lens; float Score; FString Name; };
		TArray<FOk> Oks;
		float BestS = -1e9f;
		for (const FBcRig& Rg : kBcRigs)
		{
			const FVector D = Face.RotateAngleAxis(Rg.Deg, FVector::UpVector);
			const FVector Lens = FVector(Tgt.X, Tgt.Y, 0.0) + D * Dist + FVector(0, 0, Rg.Z);
			if (!FieldOk(Lens) || !ShotSpotOk(Lens) || NearestPerson(Lens, A) < 150.f || !SightClear(Lens, Tgt, A, 150.f)) continue;
			const float S = static_cast<float>(FVector::DotProduct(D, Face) + 0.7 * FVector::DotProduct(D, ToCam));
			Oks.Add(FOk{Lens, S, Rg.Name});
			BestS = FMath::Max(BestS, S);
		}
		// (the framings close to the best are equals: the store picks among them)
		TArray<FString> Names;
		TArray<FVector> Lenses;
		for (const FOk& O : Oks)
			if (O.Score >= BestS - 0.5f) { Names.Add(O.Name); Lenses.Add(O.Lens); }
		const int32 I = PickVar(Names);
		if (I < 0) { Sh.Variant = TEXT("no clear lens"); return false; }
		Commit(Lenses[I], Tgt, Focal, B.Type == ETagShot::Official ? 1.8f : 1.2f, Names[I]);
		return true;
	}
	case ETagShot::Bench:
	{
		// a third of the team's bench (left, middle, right of it) from the field, 5 yd in, eye height
		static const TCHAR* Parts[3] = {TEXT("left"), TEXT("middle"), TEXT("right")};
		const double SideSign = B.Team == 1 ? 1.0 : -1.0;
		TArray<FString> Names;
		TArray<FVector> Lenses, Looks;
		for (int32 k = 0; k < 3; ++k)
		{
			FVector Sum = FVector::ZeroVector;
			int32 N = 0;
			for (int32 i = 0; i < Sideline.Num(); ++i)
			{
				const AAthlete* O = Sideline[i];
				if (!O || !SidelineIsHome.IsValidIndex(i) || SidelineIsHome[i] != (B.Team == 1)) continue;
				const double X = O->GetActorLocation().X / Y;
				if ((k == 0 && X >= 44.0) || (k == 1 && (X < 44.0 || X > 56.0)) || (k == 2 && X <= 56.0)) continue;
				FString Why;
				if (!BcPosed(O, Why)) continue;
				Sum += O->GetActorLocation();
				++N;
			}
			if (N < 2) continue;
			const FVector Mid = Sum / N;
			const FVector LookAt = Mid + FVector(0, 0, 130);
			const FVector Lens(Mid.X + ((k + St.PlayN) % 2 ? 2.0 : -2.0) * Y, SideSign * (tag::FIELD_W / 2 - 5.0) * Y, 170.0);
			if (!ShotSpotOk(Lens) || NearestPerson(Lens, nullptr) < 150.f || !SightClear(Lens, LookAt, nullptr, 400.f)) continue;
			Names.Add(Parts[k]);
			Lenses.Add(Lens);
			Looks.Add(LookAt);
		}
		const int32 I = PickVar(Names);
		if (I < 0) { Sh.Variant = TEXT("no bench in shot"); return false; }
		Commit(Lenses[I], Looks[I], FMath::Clamp(36.f * static_cast<float>((Looks[I] - Lenses[I]).Size()) / 700.f, 35.f, 110.f), 2.f, Names[I]);
		return true;
	}
	case ETagShot::Crowd:
	{
		// a section of the end stand behind the end zone nearer the play, from the end zone (the pack's fans in the rows)
		TArray<FString> Names;
		TArray<FVector> Lenses, Looks;
		for (int32 i = 0; i < 4; ++i)
		{
			FVector LookAt, Lens;
			BcCrowdSection(B.Fixed, i, LookAt, Lens);
			if (BcFansNear(St.StandPeople, LookAt, 450.0) < 6 || !ShotSpotOk(Lens) || NearestPerson(Lens, nullptr) < 150.f || !SightClear(Lens, LookAt, nullptr, 0.f)) continue;
			Names.Add(FString::Printf(TEXT("section %d"), i + 1));
			Lenses.Add(Lens);
			Looks.Add(LookAt);
		}
		const int32 I = PickVar(Names);
		if (I < 0) { Sh.Variant = TEXT("no fans in shot"); return false; }
		Commit(Lenses[I], Looks[I], FMath::Clamp(36.f * static_cast<float>((Looks[I] - Lenses[I]).Size()) / 650.f, 50.f, 135.f), 2.f, Names[I]);
		return true;
	}
	default:
	{
		// the wide reset: the TV camera's platform nearest the spot, a wide lens on the field (always clear: it is the
		// game camera's own position high in the stands)
		const float SpotX = static_cast<float>(B.Fixed.X / Y);
		float Station = 50.f;
		for (const float Sx : {22.f, 50.f, 78.f})
			if (FMath::Abs(SpotX - Sx) < FMath::Abs(SpotX - Station)) Station = Sx;
		const FVector Lens(Station * Y, -(tag::FIELD_W / 2 + 22.0) * Y, 13.0 * Y);
		const FVector LookAt(FMath::Clamp(B.Fixed.X, 0.0, 100.0 * Y), B.Fixed.Y * 0.6, 0.0);
		Commit(Lens, LookAt, 24.f, 5.6f, FString::Printf(TEXT("tv %d"), FMath::RoundToInt(Station)));
		return true;
	}
	}
}

bool AMatchDirector::ShotCamera(float Dt, FVector& OutP, const FVector& GameT, const FVector& GameLook)
{
	if (!ShotView.bOn || !BcState) return false;
	FTagBcState& St = *BcState;
	FTagBcShot& Sh = St.Shot;
	const ETagShot Type = Sh.Beat.Type;
	const double Y = YD;
	bool bAbort = false; // (away this very frame, to the game camera's framing: never one frame of a broken shot)
	if (BcIsOpen(Type))
	{
		// the pregame open's camera (TickOpen keeps the stage): a crane over the bowl, a push on the home bench, both
		// kickoff units, the TV wide
		const FTagBcOpen& O = St.Open;
		const bool bShowCrane = Type == ETagShot::ShowCrane; // (a halftime / postgame beat over the bowl: on the show's clock)
		const float Dur = bShowCrane ? Sh.Beat.Secs : O.Stage == 1 ? O.Crane : O.Stage == 2 ? O.Card : O.Teams;
		const float U = FMath::Clamp((bShowCrane ? St.Show.StageT : O.StageT) / FMath::Max(Dur, 0.1f), 0.f, 1.f);
		const float E = BcSmooth(U);
		FVector Lens, LookAt;
		float Focal = 22.f;
		if (bShowCrane || O.Stage == 1) BcCrane(bShowCrane ? St.Show.CraneVar : O.CraneVar, E, Lens, LookAt, Focal);
		else if (O.Stage == 2)
		{
			// the matchup card over a slow push across the field toward the home bench and the stand behind it
			// (from high behind the near sideline, the field and the far stand beyond: low on the field the lens framed the
			// stand behind the home bench close up, its painted rows flat - tag_open_10)
			Lens = FMath::Lerp(FVector(40.0 * Y, -(tag::FIELD_W / 2 + 18.0) * Y, 16.0 * Y), FVector(60.0 * Y, -(tag::FIELD_W / 2 + 18.0) * Y, 15.0 * Y), E);
			LookAt = FVector(FMath::Lerp(44.0, 56.0, static_cast<double>(E)) * Y, 8.0 * Y, 0.0);
			Focal = 26.f;
		}
		else if (O.Stage == 3 && O.bLong && O.Men.Num() == O.At.Num() && O.Men.Num() > 0)
		{
			// the long open: the receiving unit, then the kicking one, each whole across the field (11 men 41 yd wide):
			// from 26 yd in front of its line and 5 yd up, a 22 mm lens (43 yd at that distance), easing along the line
			// (from 5 yd in front on 35 mm, a dolly past two men at a time showed at most 2 of 11)
			const bool bKickHalf = U >= 0.5f;
			const float H = BcSmooth(bKickHalf ? (U - 0.5f) * 2.f : U * 2.f);
			FVector Mid = FVector::ZeroVector;
			float FaceYaw = 0.f;
			int32 N = 0;
			for (int32 i = 0; i < O.Men.Num(); ++i)
				if (O.bKickSide[i] == bKickHalf)
				{
					Mid += O.At[i];
					FaceYaw = O.Yaw[i];
					++N;
				}
			Mid /= static_cast<double>(FMath::Max(1, N));
			const FVector Fwd = FRotator(0.f, FaceYaw, 0.f).Vector();
			const FVector Side(-Fwd.Y, Fwd.X, 0.0);
			Lens = Mid + Fwd * (26.0 * Y) + Side * (FMath::Lerp(-4.0, 4.0, static_cast<double>(H)) * Y) + FVector(0, 0, 5.0 * Y);
			LookAt = Mid + FVector(0, 0, 100.0);
			Focal = 22.f;
		}
		else
		{
			// the TV wide on midfield: both units at their 25s
			Lens = FVector(50.0 * Y, -(tag::FIELD_W / 2 + 22.0) * Y, 13.0 * Y);
			LookAt = FVector(50.0 * Y, 0.0, 0.0);
			Focal = 20.f;
		}
		Sh.CamPos = Lens;
		Sh.Look = LookAt;
		Sh.Focal = Focal;
		Sh.Aperture = 5.6f;
	}
	else if (BcIsPerson(Type))
	{
		AAthlete* A = Sh.Beat.Subject.Get();
		FString Why;
		if (!BcPosed(A, Why))
		{
			// (a man who stops being fit to show - hidden, put to sleep, a pose gone wrong - is cut away from at once)
			Sh.EndHow = TEXT("subject broke");
			bAbort = true;
		}
		else
		{
			// per frame it only pans; it trails him if he walks off
			const FVector Tgt = BcChest(A);
			BcSpring(Sh.Look, Sh.LookVel, Tgt, 0.3f, Dt);
			BcSpring(Sh.CamPos, Sh.CamVel, FVector(Sh.Look.X + Sh.Off.X, Sh.Look.Y + Sh.Off.Y, Sh.CamPos.Z), 1.4f, Dt);
		}
	}
	// the lens kept clear: a body at the lens (a second camera at once, else away at once), or across the line for half
	// a second (a second camera once, else away early)
	if (!bAbort && !BcIsOpen(Type) && Type != ETagShot::Wide)
	{
		const AAthlete* Subj = Sh.Beat.Subject.Get();
		float Near = NearestPerson(Sh.CamPos, Subj);
		const bool bClear = Near >= 60.f && SightClear(Sh.CamPos, Sh.Look, Subj, Type == ETagShot::Bench ? 400.f : 150.f);
		Sh.BlockedT = bClear ? 0.f : Sh.BlockedT + Dt;
		if (Sh.BlockedT > 0.5f || Near < 60.f)
		{
			const bool bCanRecut = Sh.Recuts < 1 && Sh.Beat.Secs - Sh.T > 1.2f;
			if (bCanRecut) ++Sh.Recuts;
			if (bCanRecut && PlaceShot(true)) Near = NearestPerson(Sh.CamPos, Subj); // (the second camera, checked as placed)
			else if (Near < 60.f) { Sh.EndHow = TEXT("body at the lens"); bAbort = true; }
			else { Sh.bEndNow = true; Sh.EndHow = TEXT("blocked"); }
		}
		Sh.NearMin = FMath::Min(Sh.NearMin, Near);
		if (!bAbort && Near < 60.f) ++Sh.BadFrames; // (drawn with a body in the lens: must never be)
	}
	else if (!bAbort && (!ShotSpotOk(Sh.CamPos) || NearestPerson(Sh.CamPos, nullptr) < 60.f)) ++Sh.BadFrames;
	if (bAbort)
	{
		const bool bLead = Sh.Beat.bLead;
		EndShot(Sh.EndHow);
		bCutNext = false; // (the cut is this frame's)
		if (bLead && State == EMatchState::Post) Timer = FMath::Min(Timer, 0.8f);
		OutP = CamPos = GameT;
		CamLook = GameLook;
		CamVel = LookVel = FVector::ZeroVector;
		MarkCameraCut(TEXT("shot cut away"));
		return true;
	}
	if (Sh.bJustPlaced)
	{
		Sh.bJustPlaced = false;
		MarkCameraCut(BcIsOpen(Type) ? TEXT("open shot") : TEXT("broadcast shot"));
	}
	++Sh.Frames;
	const float Drift = BcIsPerson(Type) ? 4.f : 1.5f; // (a long lens on a head; the wide and the open on a crane)
	OutP = Sh.CamPos + FVector(FMath::PerlinNoise1D(CamTime * 0.4f), FMath::PerlinNoise1D(CamTime * 0.35f + 9.f), FMath::PerlinNoise1D(CamTime * 0.3f + 23.f) * 0.5f) * Drift;
	CamLook = Sh.Look;
	ShotFocal = Sh.Focal;
	ShotAperture = Sh.Aperture;
	return true;
}

void AMatchDirector::EndShot(const TCHAR* How)
{
	if (!ShotView.bOn || !BcState) return;
	FTagBcState& St = *BcState;
	const FTagBcShot& Sh = St.Shot;
	if (!BcIsOpen(Sh.Beat.Type))
	{
		St.BeatSecsThisPlay += Sh.T;
		if (Sh.BadFrames > 0) ++St.ShotsBad;
		const AAthlete* SA = Sh.Beat.Subject.Get();
		const FString Who = SA ? BcWho(SA) : Sh.Beat.Type == ETagShot::Bench ? FString::Printf(TEXT("%s bench"), Sh.Beat.Team == 1 ? TEXT("home") : TEXT("away"))
			: FString(BcShotName(Sh.Beat.Type));
		UE_LOG(LogTemp, Display, TEXT("TAG shot %s %s %.2f s (%s; variant %s, planned %.1f, %s, lens nearest %.0f cm, lens bad %d of %d frames, recuts %d)"),
			BcShotName(Sh.Beat.Type), *Who, Sh.T, How, *Sh.Variant, Sh.Beat.Secs, *Sh.Beat.Why, Sh.NearMin, Sh.BadFrames, Sh.Frames, Sh.Recuts);
		if (St.Test.Mode == 2 && Sh.Frames > 0)
		{
			const bool bOk = Sh.BadFrames == 0;
			(bOk ? St.Test.Pass : St.Test.Fail)++;
			UE_LOG(LogTemp, Display, TEXT("TAG shottest %s: %s %s lens never in a body (%d bad of %d frames, nearest %.0f cm)"), bOk ? TEXT("PASS") : TEXT("FAIL"),
				BcShotName(Sh.Beat.Type), *Who, Sh.BadFrames, Sh.Frames, Sh.NearMin);
		}
	}
	const int32 Ser = ShotView.Serial;
	ShotView = FTagShotView();
	ShotView.Serial = Ser;
	bCutNext = true; // (back to the game camera at its framing, unless the next shot is placed first)
}

void AMatchDirector::BroadcastEndShotForPlay()
{
	// (as TickBroadcast's "shots belong between the whistle and the call" check, a frame earlier)
	if (!ShotView.bOn || !BcState || PregameView.bOn || BcState->Show.bOn || State == EMatchState::Post || State == EMatchState::Wait) return;
	EndShot(TEXT("moved on"));
	BcState->Queue.Reset();
	BcState->bProgram = false;
}

bool AMatchDirector::BroadcastSkipPress(APlayerController* PC)
{
	if (!PC) return false;
	const bool bDown = PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom) || PC->WasInputKeyJustPressed(EKeys::SpaceBar) ||
		PC->WasInputKeyJustPressed(EKeys::Enter);
	if (!bDown || (Overlay && Overlay->HasModal())) return false; // (a menu's press is the menu's)
	FTagBcState& St = Bc();
	if (FPlatformTime::Seconds() < St.SkipLockUntil)
	{
		// (0.3 s after a skip a press skips nothing: a double press does not take the next beat with it)
		if (State == EMatchState::Post || State == EMatchState::Wait || State == EMatchState::Replay)
		{
			++St.SkipsRefused;
			UE_LOG(LogTemp, Display, TEXT("TAG skip refused: %.2f s after the last"), 0.3 - (St.SkipLockUntil - FPlatformTime::Seconds()));
		}
		return false;
	}
	return true;
}

bool AMatchDirector::BroadcastSkip(bool bPress)
{
	if (!bPress) return false;
	FTagBcState& St = Bc();
	if (PregameView.bOn)
	{
		// the open: straight to the kickoff (and past the booth's hold on it, as the confirm press always did)
		++St.Skips;
		St.SkipFrame = GFrameCounter;
		St.SkipLockUntil = FPlatformTime::Seconds() + 0.3;
		BoothSkipUntil = FPlatformTime::Seconds() + 45.0;
		UE_LOG(LogTemp, Display, TEXT("TAG skip: the open (stage %d, %.1f s in)"), St.Open.Stage, St.Open.T);
		EndOpen(TEXT("skipped"));
		Timer = 0.f;
		return true;
	}
	if (St.Show.bOn)
	{
		// a halftime or postgame show: the rest of it (to the second half's kickoff, or to the final box score)
		++St.Skips;
		St.SkipFrame = GFrameCounter;
		St.SkipLockUntil = FPlatformTime::Seconds() + 0.3;
		BoothSkipUntil = FPlatformTime::Seconds() + 45.0;
		UE_LOG(LogTemp, Display, TEXT("TAG skip: the %s show (stage %d, %.1f s in)"), St.Show.Kind == 1 ? TEXT("halftime") : TEXT("postgame"), St.Show.Stage + 1, St.Show.T);
		if (St.Test.Mode == 5 || St.Test.Mode == 6) St.Test.bShowSkipped = true;
		EndShow(TEXT("skipped"));
		Timer = 0.f;
		return true;
	}
	if (ShotView.bOn)
	{
		++St.Skips;
		St.SkipFrame = GFrameCounter;
		St.SkipLockUntil = FPlatformTime::Seconds() + 0.3;
		UE_LOG(LogTemp, Display, TEXT("TAG skip: the %s shot (%.1f of %.1f s)"), BcShotName(St.Shot.Beat.Type), St.Shot.T, St.Shot.Beat.Secs);
		const bool bLead = St.Shot.Beat.bLead;
		EndShot(TEXT("skipped"));
		St.GapLeft = 0.3f; // (the next beat waits 0.3 s: the game camera between)
		if (bLead && State == EMatchState::Post) Timer = FMath::Min(Timer, 0.3f);
		return true;
	}
	if (State == EMatchState::Post && Timer < 1.2f)
	{
		// the last of the beat after the whistle, on the game camera (as SPACE / CROSS always did): on to the replay or the call
		++St.Skips;
		St.SkipFrame = GFrameCounter;
		St.SkipLockUntil = FPlatformTime::Seconds() + 0.3;
		UE_LOG(LogTemp, Display, TEXT("TAG skip: the rest of the play's beat (%.1f s)"), Timer);
		Timer = -1.f;
		return true;
	}
	return false;
}

bool AMatchDirector::BroadcastHoldsClock() const
{
	return ShotView.bOn || PregameView.bOn || HalftimeView.bOn || PostgameView.bOn;
}

bool AMatchDirector::BroadcastFilming() const
{
	static const bool bHlOpen = FParse::Param(FCommandLine::Get(), TEXT("TagHlOpen"));
	return (ShotView.bOn && !PregameView.bOn) || (PregameView.bOn && bHlOpen);
}

void AMatchDirector::TickBeats(float SDt)
{
	if (!BcState) return;
	FTagBcState& St = *BcState;
	// whistle to the next call (or snap, or kick): the pace's measure
	if (St.WhistleT >= 0.0 && (State == EMatchState::Call || State == EMatchState::Presnap || State == EMatchState::Kick || State == EMatchState::Live ||
		State == EMatchState::Final))
	{
		const float Secs = static_cast<float>(FPlatformTime::Seconds() - St.WhistleT);
		const bool bRoutine = !St.bLeadThisPlay && !St.bReplayThisPlay && St.BeatSecsThisPlay <= 0.f;
		const bool bBreak = BreakLeft > 0.f || bSetAfterBreak;
		const int32 Pi = BcPaceIdx(St.PaceAtWhistle);
		// (a CPU call is made the frame its screen would open: the pre-snap is its call)
		const bool bCall = State == EMatchState::Call || State == EMatchState::Presnap;
		if (bCall && !bBreak) (bRoutine ? St.ToCall[Pi] : St.ToCallBig[Pi]).Add(Secs);
		UE_LOG(LogTemp, Display, TEXT("TAG beats play %d pace %s: whistle to %s %.2f s (shots %.2f s, replay %.2f s, %s) [%s]%s"), St.PlayN, TagPace::Name(St.PaceAtWhistle),
			bCall ? TEXT("the call") : TEXT("the next play"), Secs, St.BeatSecsThisPlay, St.ReplaySecsThisPlay, bRoutine ? TEXT("routine") : TEXT("big"),
			*St.Ctx.Seq, bBreak ? TEXT(" (a break)") : TEXT(""));
		// the play's sequence into the last twenty (a three-shot one seen there already is a repeat)
		if (!St.Ctx.Seq.IsEmpty())
		{
			TArray<FString> Parts;
			St.Ctx.Seq.ParseIntoArray(Parts, TEXT(">"));
			if (Parts.Num() == 3 && St.SeqWindow.Contains(St.Ctx.Seq)) ++St.RepeatSeqs;
			St.SeqWindow.Add(St.Ctx.Seq);
			while (St.SeqWindow.Num() > 20) St.SeqWindow.RemoveAt(0);
		}
		else St.SeqWindow.Add(FString());
		if (St.Test.Mode == 2 && St.Ctx.bBig && St.Test.BigPlays.Num() < 5) St.Test.BigPlays.Add(St.Ctx);
		St.WhistleT = -1.0;
	}
	// (the open and the shows time their own shots)
	const bool bShowing = PregameView.bOn || St.Show.bOn;
	// a hurry-up or a spike: no more shots, to the line
	if ((bHurryNext || bSpikeNext) && (ShotView.bOn || St.bProgram) && !bShowing)
	{
		if (ShotView.bOn) EndShot(TEXT("hurry-up"));
		St.Queue.Reset();
		St.bProgram = false;
	}
	// shots belong between the whistle and the call: anything else on screen now ends them
	if (ShotView.bOn && !bShowing && State != EMatchState::Post && State != EMatchState::Wait)
	{
		EndShot(TEXT("moved on"));
		St.Queue.Reset();
		St.bProgram = false;
	}
	if (ShotView.bOn && !bShowing)
	{
		St.Shot.T += SDt;
		ShotView.Elapsed = St.Shot.T;
		ShotView.Left = FMath::Max(0.f, St.Shot.Beat.Secs - St.Shot.T);
		// (the lead shot with a replay to come stays up to the end of the beat: StartReplay ends it under the wipe - cut back
		// to the game camera here, the frame the wipe starts, it was a hard cut with the slats only just coming down)
		const bool bHoldForReplay = St.Shot.Beat.bLead && State == EMatchState::Post && bReplayWorthy && !bHurryNext && !bSpikeNext && !bTackleCam;
		if ((St.Shot.T >= St.Shot.Beat.Secs && !bHoldForReplay) || St.Shot.bEndNow)
		{
			const bool bEarly = St.Shot.bEndNow;
			const bool bLead = St.Shot.Beat.bLead;
			EndShot(bEarly ? St.Shot.EndHow : TEXT("done"));
			if (bEarly && bLead && State == EMatchState::Post) Timer = FMath::Min(Timer, 0.8f); // (the rest of the beat on the game camera)
		}
	}
	// the program after the replay (the Wait holds for it)
	if (St.bProgram)
	{
		if (State != EMatchState::Wait)
		{
			St.Queue.Reset();
			St.bProgram = false;
		}
		// (the gap after a skip is 0.3 s of real time whatever the frame rate or the watched game's speed: the lock's)
		else if (!ShotView.bOn && (St.GapLeft -= SDt) <= 0.f && FPlatformTime::Seconds() >= St.SkipLockUntil)
		{
			St.GapLeft = 0.f;
			if (St.Queue.Num())
			{
				const FTagBcBeat B = St.Queue[0];
				St.Queue.RemoveAt(0);
				StartShot(B);
			}
			else St.bProgram = false;
		}
		if (State == EMatchState::Wait) Timer = St.bProgram ? BcProgramLeft(St, ShotView) : FMath::Min(Timer, 0.f);
	}
}

// ------------------------------------------------------------------ the pregame open (04 A5)
bool AMatchDirector::BeginPregameOpen()
{
	static const bool bNoOpen = FParse::Param(FCommandLine::Get(), TEXT("TagNoOpen")); // A/B: the wait for the booth, as before
	static const bool bHlOpen = FParse::Param(FCommandLine::Get(), TEXT("TagHlOpen"));
	FTagBcState& St = Bc();
	if (bNoOpen || !Game || !BeatsAllowed() || (bHighlights && !bHlOpen) || (St.Test.Mode != 0 && St.Test.Mode != 4) || bMulti) return false;
	if (Game->phase != tag::GamePhase::Kickoff) return false;
	FTagBcOpen& O = St.Open;
	O = FTagBcOpen();
	const ETagPace Pace = PaceNow();
	O.bLong = Pace == ETagPace::Full;
	O.Version = Pace == ETagPace::Full ? TEXT("long") : Pace == ETagPace::Quick ? TEXT("quick") : TEXT("short");
	O.Crane = Pace == ETagPace::Full ? 6.f : Pace == ETagPace::Quick ? 2.f : 3.f;
	O.Card = Pace == ETagPace::Full ? 5.f : Pace == ETagPace::Quick ? 2.5f : 3.5f;
	O.Teams = Pace == ETagPace::Full ? 6.f : Pace == ETagPace::Quick ? 1.5f : 3.f;
	// the crane's move: the one the opens used least lately (game ten does not open as game nine did)
	{
		static const char* Cranes[3] = {"corner", "end rise", "sideline sweep"};
		const std::vector<std::string> Names = {Cranes[0], Cranes[1], Cranes[2]};
		O.CraneVar = FMath::Max(0, tag::Recent::get().leastRecent("open.crane", Names, BroadcastSeed));
		tag::Recent::get().use("open.crane", Names[O.CraneVar]);
		O.CraneName = UTF8_TO_TCHAR(Cranes[O.CraneVar]);
	}
	StageKickoffUnits(); // (both teams on the field: the kickoff units at their 25s)
	State = EMatchState::Wait;
	Timer = 1e6f; // (held by the open: TickOpen lets it go)
	O.bOn = true;
	O.Stage = 1;
	O.Started = FPlatformTime::Seconds();
	// the matchup (the card is the graphics' to draw: PregameNow)
	PregameView = FTagPregameView();
	PregameView.bOn = true;
	PregameView.Stage = 1;
	PregameView.bLong = O.bLong;
	PregameView.Home = Game->home;
	PregameView.Away = Game->away;
	PregameView.Weather = BcS(Game->weatherText());
	PregameView.bNight = bBoothNight;
	auto Star = [](const tag::Team& T)
	{
		FTagMatchStar S;
		const tag::Player* Best = nullptr;
		auto Rank = [](const tag::Player* P) { return static_cast<int32>(P->grade) * 1000 + (P->xFactor != tag::AbilityId::None ? 500 : 0) + P->ovr; };
		for (const auto& P : T.roster)
			if (P->injuredWeeks <= 0 && (!Best || Rank(P.get()) > Rank(Best))) Best = P.get();
		if (!Best) return S;
		S.Player = Best;
		S.Name = FString::Printf(TEXT("%s. %s"), *FString(1, UTF8_TO_TCHAR(Best->first.empty() ? "?" : Best->first.substr(0, 1).c_str())), UTF8_TO_TCHAR(Best->last.c_str()));
		S.Pos = ANSI_TO_TCHAR(tag::posName(Best->pos));
		S.Grade = ANSI_TO_TCHAR(tag::starGradeName(Best->grade));
		if (Best->xFactor != tag::AbilityId::None) S.Ability = ANSI_TO_TCHAR(tag::abilityDef(Best->xFactor).name);
		else if (!Best->abilities.empty()) S.Ability = ANSI_TO_TCHAR(tag::abilityDef(Best->abilities.front()).name);
		return S;
	};
	PregameView.HomeStar = Star(*Game->home);
	PregameView.AwayStar = Star(*Game->away);
	FTagBcBeat B;
	B.Type = ETagShot::OpenCrane;
	B.Secs = O.Crane;
	B.Why = TEXT("open");
	StartShot(B);
	St.Shot.Variant = O.CraneName;
	ShotView.Variant = O.CraneName;
	St.Shot.bJustPlaced = true;
	UE_LOG(LogTemp, Display, TEXT("TAG open begin: %s version (crane %s %.1f s, card %.1f s, teams %.1f s), %s at %s, stars %s / %s"), *O.Version, *O.CraneName, O.Crane,
		O.Card, O.Teams, *BcS(Game->away->abbr), *BcS(Game->home->abbr), *PregameView.AwayStar.Name, *PregameView.HomeStar.Name);
	return true;
}

void AMatchDirector::EndOpen(const TCHAR* How)
{
	if (!BcState || !PregameView.bOn) return;
	FTagBcOpen& O = BcState->Open;
	O.StageSecs.Add(O.StageT);
	FString Stages;
	for (const float S : O.StageSecs) Stages += FString::Printf(TEXT(" %.2f"), S);
	UE_LOG(LogTemp, Display, TEXT("TAG open done: %s after %.2f s (%s version, stages%s; longest without a person on screen %.2f s; most of each unit on screen %d / %d; lens bad %d of %d frames; booth holding %s)"),
		How, O.T, *O.Version, *Stages, O.LongestEmpty, O.MostKickSeen, O.MostRecvSeen, O.BadFrames, O.Frames, BoothHold() ? TEXT("yes") : TEXT("no"));
	O.bOn = false;
	if (Overlay && Overlay->Gfx().MatchupCardUp()) Overlay->Gfx().HideMatchupCard(); // (skipped on its beat)
	PregameView = FTagPregameView();
	const int32 Ser = ShotView.Serial;
	ShotView = FTagShotView();
	ShotView.Serial = Ser;
	bCutNext = true; // (a cut to the kickoff)
}

void AMatchDirector::TickOpen(float InDt)
{
	if (!BcState || !PregameView.bOn) return;
	const float Dt = FMath::Min(InDt, 0.1f); // (the first frame after the loading screen's hitch does not jump the crane's stage)
	FTagBcState& St = *BcState;
	FTagBcOpen& O = St.Open;
	if (State != EMatchState::Wait) { EndOpen(TEXT("kickoff")); return; }
	DriveStagedUnits(Dt); // (the men stand where the open put them: the kick's staging moves them on)
	O.T += Dt;
	O.StageT += Dt;
	const float Dur = O.Stage == 1 ? O.Crane : O.Stage == 2 ? O.Card : O.Stage == 3 ? O.Teams : 1e9f;
	if (O.Stage == 3 && O.bLong && O.StageT >= O.Teams * 0.5f && O.StageT - Dt < O.Teams * 0.5f) St.Shot.bJustPlaced = true; // (the receiving unit, then a cut to the kicking one)
	if (O.StageT >= Dur && O.Stage <= 3)
	{
		O.StageSecs.Add(O.StageT);
		UE_LOG(LogTemp, Display, TEXT("TAG open stage %d done: %.2f s"), O.Stage, O.StageT);
		++O.Stage;
		O.StageT = 0.f;
		if (O.Stage <= 3 || O.bLong) St.Shot.bJustPlaced = true; // (a cut to the next stage's camera; the long open's last is the TV wide)
		ShotView.Type = St.Shot.Beat.Type = O.Stage == 2 ? ETagShot::OpenCard : ETagShot::OpenTeams;
		ShotView.TypeName = BcShotName(ShotView.Type);
		if (O.Stage == 4)
		{
			// the open is done: the kickoff once the booth lets it go (the TV wide holds over its last words)
			UE_LOG(LogTemp, Display, TEXT("TAG open stages done after %.2f s%s"), O.T, BoothHold() ? TEXT(", the booth still talking: the wide holds") : TEXT(""));
			if (St.Test.Mode == 4 && St.Test.OpenRound == 0)
			{
				// (the open test, its second round: the short open, skipped by a press a second in)
				St.Test.OpenRound = 1;
				const FTagBcOpen Was = O;
				auto Check = [&St](bool bOk, const FString& What) { (bOk ? St.Test.Pass : St.Test.Fail)++; UE_LOG(LogTemp, Display, TEXT("TAG opentest %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What); };
				Check(Was.StageSecs.Num() == 3 && FMath::Abs(Was.StageSecs[0] - Was.Crane) < 0.3f && FMath::Abs(Was.StageSecs[1] - Was.Card) < 0.3f && FMath::Abs(Was.StageSecs[2] - Was.Teams) < 0.3f,
					FString::Printf(TEXT("the long open ran its three stages in order (%d stages, %.1f s)"), Was.StageSecs.Num(), Was.T));
				Check(Was.LongestEmpty <= 2.f, FString::Printf(TEXT("no stretch over 2 s without a person on screen (longest %.2f s)"), Was.LongestEmpty));
				Check(Was.MostKickSeen >= 9 && Was.MostRecvSeen >= 9, FString::Printf(TEXT("both units on screen (at most %d and %d of 11)"), Was.MostKickSeen, Was.MostRecvSeen));
				Check(Was.BadFrames == 0, FString::Printf(TEXT("the lens never in the stands or a body (%d of %d frames bad)"), Was.BadFrames, Was.Frames));
				Check(Was.bCardShown && Was.bCardSeenUp, FString::Printf(TEXT("the matchup card shown on its beat (asked %d, up %d)"), Was.bCardShown ? 1 : 0, Was.bCardSeenUp ? 1 : 0));
				Check(PregameView.Home && PregameView.Away && PregameView.HomeStar.Player && PregameView.AwayStar.Player,
					FString::Printf(TEXT("the matchup for the card (%s / %s)"), *PregameView.AwayStar.Name, *PregameView.HomeStar.Name));
				EndOpen(TEXT("test round 1"));
				St.PaceOverride = ETagPace::Standard;
				BeginPregameOpen();
				St.Test.InjectAtOpenT = 1.f;
				return;
			}
			Timer = 0.f;
		}
	}
	PregameView.Stage = FMath::Min(O.Stage, 3);
	PregameView.StageT = O.StageT;
	PregameView.StageLeft = O.Stage <= 3 ? FMath::Max(0.f, Dur - O.StageT) : 0.f;
	PregameView.bCardUp = O.Stage == 2;
	if (O.Stage == 2 && !O.bCardShown && Overlay && Game)
	{
		// the matchup card (UI/BroadcastGfx) for the beat's length at this pace, the conditions as the loading screen words them
		O.bCardShown = true;
		const FString Cond = FString::Printf(TEXT("%s  ·  %s"), *BcS(Game->weatherText()), bBoothNight ? TEXT("NIGHT GAME") : TEXT("DAY GAME"));
		Overlay->Gfx().ShowMatchupCard(FBroadcastGfx::MatchupFor(*Game->away, *Game->home, &League, Cond), O.Card);
		UE_LOG(LogTemp, Display, TEXT("TAG open matchup card: %.1f s (%s)"), O.Card, *Cond);
	}
	if (O.Stage == 2 && Overlay && Overlay->Gfx().MatchupCardUp()) O.bCardSeenUp = true;
	ShotView.Elapsed = O.T;
	// (measured: the lens clear, somebody on screen, both units seen)
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	FVector2D VP(0.0, 0.0);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(VP);
	int32 Seen = 0, KickSeen = 0, RecvSeen = 0;
	auto OnScreen = [PC, &VP](const AActor* A)
	{
		FVector2D S2;
		return A && !A->IsHidden() && PC && VP.X > 0.0 && PC->ProjectWorldLocationToScreen(A->GetActorLocation() + FVector(0, 0, 100), S2, false) &&
			S2.X >= 0.0 && S2.Y >= 0.0 && S2.X <= VP.X && S2.Y <= VP.Y;
	};
	for (int32 i = 0; i < O.Men.Num(); ++i)
		if (const AAthlete* A = O.Men[i] ? Athletes.FindRef(O.Men[i]->id).Get() : nullptr; OnScreen(A))
		{
			++Seen;
			(O.bKickSide[i] ? KickSeen : RecvSeen)++;
		}
	for (const AAthlete* A : Sideline) Seen += OnScreen(A) ? 1 : 0;
	for (const AAthlete* A : Officials) Seen += OnScreen(A) ? 1 : 0;
	if (O.Stage == 3) { O.MostKickSeen = FMath::Max(O.MostKickSeen, KickSeen); O.MostRecvSeen = FMath::Max(O.MostRecvSeen, RecvSeen); }
	O.EmptyRun = Seen == 0 && O.Stage != 1 ? O.EmptyRun + Dt : 0.f; // (the crane is the stadium's shot: its people are the crowd)
	O.LongestEmpty = FMath::Max(O.LongestEmpty, O.EmptyRun);
	++O.Frames;
	if (Camera && (!ShotSpotOk(Camera->GetActorLocation()) || NearestPerson(Camera->GetActorLocation(), nullptr) < 60.f)) ++O.BadFrames;
}

// ------------------------------------------------------------------ the kickoff units (the open, the second half)
void AMatchDirector::StageKickoffUnits()
{
	// both teams on the field: the kickoff units at their 25s, facing each other (they kick off from where they stand:
	// the kick's own staging puts them on their spots under the cut to it)
	FTagBcOpen& O = Bc().Open;
	O.Men.Reset();
	O.At.Reset();
	O.Yaw.Reset();
	O.bKickSide.Reset();
	if (!Game) return;
	const tag::TeamKey KT = Game->kicking;
	const int32 KDir = Game->worldDir(KT);
	std::vector<tag::Player*> All;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const bool bKick = Side == 0;
		const tag::TeamKey K = bKick ? KT : tag::other(KT);
		const std::vector<tag::Player*> Unit = Pick11(Game->teamOf(K), bKick ? "K" : "KR");
		const int32 D = bKick ? KDir : -KDir;
		for (int32 i = 0; i < static_cast<int32>(Unit.size()); ++i)
		{
			const double Yd = 6.0 + i * 41.0 / FMath::Max(1, static_cast<int32>(Unit.size()) - 1);
			O.Men.Add(Unit[i]);
			O.At.Add(ToWorld(25.0 + (i % 2) * 0.8, Yd, D));
			O.Yaw.Add(FaceToYaw(0.0, D));
			O.bKickSide.Add(bKick);
			All.push_back(Unit[i]);
		}
	}
	ShowOnly(All);
}

void AMatchDirector::DriveStagedUnits(float Dt)
{
	const FTagBcOpen& O = Bc().Open;
	if (!Game) return;
	for (int32 i = 0; i < O.Men.Num(); ++i)
		if (O.Men[i])
			if (AAthlete* A = AthleteFor(O.Men[i], Game->home->hasPlayer(O.Men[i]) ? tag::TeamKey::Home : tag::TeamKey::Away))
				A->Drive(O.At[i], O.Yaw[i], FVector::ZeroVector, 9.f, TEXT("idle"), 0.f, Dt, false);
}

// ------------------------------------------------------------------ the halftime and postgame shows (04 A10)
namespace
{
	// The graphics' halftime, player-of-the-game and final panels (UI/BroadcastGfx: ShowHalftimeStats, ShowPlayerOfGame,
	// ShowFinalStats, their data made by its static helpers as MatchupFor makes the card's). Each is called in the first
	// of the shapes below the graphics have; false: none of them (the show runs without the panel, and says so in the log).
	template <typename G> bool BcGfxHalftimeStats(G& Gfx, const tag::Game& Gm, const tag::League& L, float Secs)
	{
		if constexpr (requires { std::declval<G&>().ShowHalftimeStats(G::HalftimeStatsFor(std::declval<const tag::Game&>(), std::declval<const tag::League*>()), 1.f); })
		{
			Gfx.ShowHalftimeStats(G::HalftimeStatsFor(Gm, &L), Secs);
			return true;
		}
		else if constexpr (requires { std::declval<G&>().ShowHalftimeStats(G::HalftimeStatsFor(std::declval<const tag::Game&>()), 1.f); })
		{
			Gfx.ShowHalftimeStats(G::HalftimeStatsFor(Gm), Secs);
			return true;
		}
		else if constexpr (requires { std::declval<G&>().ShowHalftimeStats(G::HalftimeStatsFor(std::declval<const tag::Game&>())); })
		{
			Gfx.ShowHalftimeStats(G::HalftimeStatsFor(Gm));
			return true;
		}
		else return false;
	}

	template <typename G> bool BcGfxPlayerOfGame(G& Gfx, const tag::Game& Gm, const tag::Player* Pog, float Secs)
	{
		if constexpr (requires { std::declval<G&>().ShowPlayerOfGame(G::PlayerOfGameFor(std::declval<const tag::Game&>(), std::declval<const tag::Player*>()), 1.f); })
		{
			Gfx.ShowPlayerOfGame(G::PlayerOfGameFor(Gm, Pog), Secs);
			return true;
		}
		else if constexpr (requires { std::declval<G&>().ShowPlayerOfGame(G::PlayerOfGameFor(std::declval<const tag::Game&>()), 1.f); })
		{
			Gfx.ShowPlayerOfGame(G::PlayerOfGameFor(Gm), Secs);
			return true;
		}
		else if constexpr (requires { std::declval<G&>().ShowPlayerOfGame(G::PlayerOfGameFor(std::declval<const tag::Game&>())); })
		{
			Gfx.ShowPlayerOfGame(G::PlayerOfGameFor(Gm));
			return true;
		}
		else return false;
	}

	template <typename G> bool BcGfxFinalStats(G& Gfx, const tag::Game& Gm, const tag::League& L, float Secs)
	{
		if constexpr (requires { std::declval<G&>().ShowFinalStats(G::FinalStatsFor(std::declval<const tag::Game&>(), std::declval<const tag::League*>()), 1.f); })
		{
			Gfx.ShowFinalStats(G::FinalStatsFor(Gm, &L), Secs);
			return true;
		}
		else if constexpr (requires { std::declval<G&>().ShowFinalStats(G::FinalStatsFor(std::declval<const tag::Game&>()), 1.f); })
		{
			Gfx.ShowFinalStats(G::FinalStatsFor(Gm), Secs);
			return true;
		}
		else if constexpr (requires { std::declval<G&>().ShowFinalStats(G::FinalStatsFor(std::declval<const tag::Game&>())); })
		{
			Gfx.ShowFinalStats(G::FinalStatsFor(Gm));
			return true;
		}
		else return false;
	}

	/** The player of the game: the best line in the box score (yards, scores, takeaways, sacks, kicks), the winners' a
	 *  quarter ahead; his line as a graphic words it. */
	const tag::Player* BcPlayerOfGame(const tag::Game& Gm, int32 Winner, FString& OutLine, int32& OutTeam)
	{
		const tag::PlayerLine* Best = nullptr;
		double BestV = -1e9;
		for (const tag::PlayerLine& Ln : Gm.stats.all())
		{
			double V = Ln.passYds / 25.0 + Ln.passTD * 4.0 - Ln.passInt * 2.0 + Ln.rushYds / 10.0 + Ln.rushTD * 6.0 + Ln.recYds / 10.0 + Ln.recTD * 6.0 +
				Ln.tackles * 0.5 + Ln.sacks * 3.0 + Ln.ints * 5.0 + Ln.ff * 3.0 + Ln.fr * 2.0 + Ln.defTD * 6.0 + Ln.fgm * 2.0;
			const bool bHomeLine = Ln.team == Gm.home->abbr;
			if (Winner >= 0 && (Winner == 1) == bHomeLine) V *= 1.25;
			if (V > BestV)
			{
				BestV = V;
				Best = &Ln;
			}
		}
		if (!Best) return nullptr;
		OutTeam = Best->team == Gm.home->abbr ? 1 : 0;
		if (Best->passAtt >= 10)
			OutLine = FString::Printf(TEXT("%d/%d, %d YDS, %d TD%s"), Best->passComp, Best->passAtt, Best->passYds, Best->passTD,
				Best->passInt ? *FString::Printf(TEXT(", %d INT"), Best->passInt) : TEXT(""));
		else if (Best->rushAtt > 0 && Best->rushYds >= Best->recYds)
			OutLine = FString::Printf(TEXT("%d CAR, %d YDS, %d TD"), Best->rushAtt, Best->rushYds, Best->rushTD);
		else if (Best->rec > 0)
			OutLine = FString::Printf(TEXT("%d REC, %d YDS, %d TD"), Best->rec, Best->recYds, Best->recTD);
		else if (Best->fga > 0)
			OutLine = FString::Printf(TEXT("%d/%d FG, %d/%d XP"), Best->fgm, Best->fga, Best->xpm, Best->xpa);
		else
			OutLine = FString::Printf(TEXT("%d TKL, %d SACKS, %d INT"), Best->tackles, Best->sacks, Best->ints);
		for (const tag::Team* Tm : {Gm.home, Gm.away})
			for (const auto& P : Tm->roster)
				if (P->id == Best->pid) return P.get();
		return nullptr;
	}

	/** A man's bench: his team's sideline (home far, visitors near), between the 35s, just in front of its front row
	 *  (he is put away there). */
	FVector BcBenchSpot(bool bHome, double AtX)
	{
		const double Y = YD;
		return FVector(FMath::Clamp(AtX, 35.0 * Y, 65.0 * Y), (bHome ? 1.0 : -1.0) * (tag::FIELD_W / 2 + 1.6) * Y, 0.0);
	}

	/** P moved along the sideline (X) by StepCm at a time, either way in turn, until it is MinCm from every spot in Taken:
	 *  no two men start from or walk to the same spot (two substitutes stood inside each other). */
	FVector BcSpread(const FVector& P, const TArray<FVector>& Taken, double MinCm, double StepCm)
	{
		for (int32 k = 0; k < 32; ++k)
		{
			const double Off = (k % 2 ? 1.0 : -1.0) * ((k + 1) / 2) * StepCm;
			const FVector C(P.X + Off, P.Y, P.Z);
			bool bFree = true;
			for (const FVector& Tk : Taken)
				if (FVector::Dist2D(Tk, C) < MinCm) { bFree = false; break; }
			if (bFree) return C;
		}
		return P;
	}

	/** A stored highlight's one angle: the wide look (the high end zone; a pass or a takeaway the reverse), the ramp at a
	 *  quick pace. */
	FTagReplayRun BcHighlightRun(const FTagBcHighlight& H)
	{
		FTagReplayRun R;
		R.Type = H.Class == TEXT("pass") || H.Class == TEXT("take") ? ETagReplayAngle::Reverse : ETagReplayAngle::HighEndZone;
		R.T0 = H.T0;
		R.T1 = H.T1;
		R.KeyT = FMath::Clamp(H.KeyT, H.T0, H.T1);
		R.Pre = 1.f;
		R.Slow = 0.4f;
		R.Post = 0.8f;
		R.SubjectId = H.SubjectId;
		return R;
	}

	/** Seconds on screen of a run of the film (through its ramp). */
	float BcRunSecs(const FTagReplayRun& R)
	{
		float Secs = 0.f;
		const float Step = (R.T1 - R.T0) / 60.f;
		for (int32 k = 0; k < 60; ++k) Secs += Step / FMath::Max(0.05f, BcRampRate(R, R.T0 + (k + 0.5f) * Step));
		return Secs;
	}

	/** The show's highlight stages: the top N stored (Half 1: the first half's; 0: the game's), best first, from InsertAt. */
	int32 BcAddHighlightStages(FTagBcState& St, int32 N, int32 Half, int32 InsertAt)
	{
		TArray<int32> Idx;
		for (int32 i = 0; i < St.Highlights.Num(); ++i)
			if (Half == 0 || St.Highlights[i].Half == Half) Idx.Add(i);
		Idx.Sort([&St](int32 X, int32 Z) { return St.Highlights[X].Score > St.Highlights[Z].Score; });
		const int32 Num = FMath::Min(N, Idx.Num());
		for (int32 k = 0; k < Num; ++k)
		{
			FTagBcShowStage Sg;
			Sg.Name = TEXT("a highlight");
			Sg.Act = 6;
			Sg.Hl = Idx[k];
			Sg.Secs = BcRunSecs(BcHighlightRun(St.Highlights[Idx[k]])) + 0.2f;
			St.Show.Stages.Insert(Sg, FMath::Clamp(InsertAt + k, 0, St.Show.Stages.Num()));
		}
		return Num;
	}
}

bool AMatchDirector::BeginHalftimeShow()
{
	static const bool bNoShow = FParse::Param(FCommandLine::Get(), TEXT("TagNoHalftimeShow")); // A/B: the old banner / box score
	static const bool bHlOpen = FParse::Param(FCommandLine::Get(), TEXT("TagHlOpen"));
	FTagBcState& St = Bc();
	if (bNoShow || !Game || St.bHalftimeDone || St.Show.bOn || !BeatsAllowed() || (bHighlights && !bHlOpen) || bMulti || (St.Test.Mode != 0 && St.Test.Mode < 5)) return false;
	St.bHalftimeDone = true;
	if (St.Test.Mode == 5) St.PaceOverride = ETagPace::Full; // (the halftime test: the long show)
	const ETagPace Pace = PaceNow();
	FTagBcShow& Sh = St.Show;
	Sh = FTagBcShow();
	Sh.Kind = 1;
	Sh.bLong = Pace == ETagPace::Full;
	Sh.Version = Pace == ETagPace::Full ? TEXT("long") : Pace == ETagPace::Quick ? TEXT("quick") : TEXT("short");
	auto Add = [&Sh](const TCHAR* Name, float Secs, int32 Act) { FTagBcShowStage Sg; Sg.Name = Name; Sg.Secs = Secs; Sg.Act = Act; Sh.Stages.Add(Sg); };
	if (Pace == ETagPace::Full)
	{
		Add(TEXT("the teams leaving"), 4.f, 1);
		Add(TEXT("the first half's stats"), 6.f, 2);
		Add(TEXT("the leading bench"), 3.f, 3);
		Add(TEXT("the crowd"), 3.f, 4);
		Add(TEXT("the kickoff units"), 6.f, 5);
	}
	else if (Pace == ETagPace::Standard)
	{
		Add(TEXT("the teams leaving"), 3.f, 1);
		Add(TEXT("the first half's stats"), 5.f, 2);
		Add(TEXT("the crowd"), 2.5f, 4);
		Add(TEXT("the kickoff units"), 3.f, 5);
	}
	else
	{
		Add(TEXT("the teams leaving"), 2.f, 1);
		Add(TEXT("the first half's stats"), 3.f, 2);
		Add(TEXT("the kickoff units"), 2.f, 5);
	}
	{
		// the half's best plays again, before the units (in place of the bench, and of the crowd but in the long show)
		const int32 Want = Pace == ETagPace::Full ? 3 : Pace == ETagPace::Standard ? 2 : 1;
		int32 Have = 0;
		for (const FTagBcHighlight& H : St.Highlights) Have += H.Half == 1 ? 1 : 0;
		if (Have > 0)
		{
			const bool bFullShow = Pace == ETagPace::Full;
			Sh.Stages.RemoveAll([bFullShow](const FTagBcShowStage& Sg) { return Sg.Act == 3 || (!bFullShow && Sg.Act == 4); });
			Sh.HlWant = BcAddHighlightStages(St, Want, 1, Sh.Stages.Num() - 1);
		}
	}
	{
		const std::vector<std::string> Names = {"corner", "end rise", "sideline sweep"};
		Sh.CraneVar = FMath::Max(0, tag::Recent::get().leastRecent("show.crane", Names, BroadcastSeed ^ 0x51u));
		tag::Recent::get().use("show.crane", Names[Sh.CraneVar]);
	}
	// the last play's men walk off to their benches (the show drives them: BroadcastDrivesMen)
	St.Leavers.Reset();
	if (Sim)
		for (const tag::Ent* E : Sim->ents)
		{
			AAthlete* A = E && E->p ? Athletes.FindRef(E->p->id).Get() : nullptr;
			if (!A || A->IsHidden()) continue;
			FTagBcLeaver L;
			L.P = E->p;
			L.At = A->GetActorLocation();
			L.At.Z = 0.0;
			{
				TArray<FVector> Taken;
				for (const FTagBcLeaver& O : St.Leavers) Taken.Add(O.To);
				L.To = BcSpread(BcBenchSpot(Game->home->hasPlayer(E->p), L.At.X), Taken, 130.0, 120.0);
			}
			L.Speed = 480.f;
			L.bShow = true;
			St.Leavers.Add(L);
		}
	HelpStage = 0; // (a help-up on its way: the men are leaving)
	State = EMatchState::Wait;
	Timer = 1e6f; // (held by the show: TickShow lets it go)
	Sh.bOn = true;
	const int32 Ser = HalftimeView.Serial + 1;
	HalftimeView = FTagHalftimeView();
	HalftimeView.bOn = true;
	HalftimeView.bLong = Sh.bLong;
	HalftimeView.Serial = Ser;
	const tag::Game& G = *Game;
	if (Overlay)
		Overlay->Banner(TEXT("HALFTIME"), FString::Printf(TEXT("%s %d  -  %s %d"), *BcS(G.away->abbr), G.score.away, *BcS(G.home->abbr), G.score.home),
			FLinearColor::White);
	UE_LOG(LogTemp, Display, TEXT("TAG show begin: halftime, %s version, %d stages, %d men walking off, %s %d - %s %d"), *Sh.Version, Sh.Stages.Num(), St.Leavers.Num(),
		*BcS(G.away->abbr), G.score.away, *BcS(G.home->abbr), G.score.home);
	Sh.Stage = 0;
	StartShowStage();
	return true;
}

bool AMatchDirector::BeginPostgameShow()
{
	static const bool bNoShow = FParse::Param(FCommandLine::Get(), TEXT("TagNoPostgameShow")); // A/B: the box score at once
	static const bool bHlOpen = FParse::Param(FCommandLine::Get(), TEXT("TagHlOpen"));
	FTagBcState& St = Bc();
	if (bNoShow || !Game || St.bPostgameDone || St.Show.bOn || !BeatsAllowed() || (bHighlights && !bHlOpen) || bMulti || (St.Test.Mode != 0 && St.Test.Mode < 5)) return false;
	St.bPostgameDone = true;
	if (St.Test.Mode == 6) St.PaceOverride = ETagPace::Full; // (the postgame test: the long show)
	const ETagPace Pace = PaceNow();
	const tag::Game& G = *Game;
	FTagBcShow& Sh = St.Show;
	Sh = FTagBcShow();
	Sh.Kind = 2;
	Sh.bLong = Pace == ETagPace::Full;
	Sh.Version = Pace == ETagPace::Full ? TEXT("long") : Pace == ETagPace::Quick ? TEXT("quick") : TEXT("short");
	Sh.Winner = G.score.home > G.score.away ? 1 : G.score.away > G.score.home ? 0 : -1;
	int32 PogTeam = -1;
	Sh.Pog = BcPlayerOfGame(G, Sh.Winner, Sh.PogLine, PogTeam);
	auto Add = [&Sh](const TCHAR* Name, float Secs, int32 Act) { FTagBcShowStage Sg; Sg.Name = Name; Sg.Secs = Secs; Sg.Act = Act; Sh.Stages.Add(Sg); };
	if (Pace == ETagPace::Full)
	{
		Add(TEXT("the celebration"), 4.f, 11);
		Add(TEXT("the final score"), 3.5f, 12);
		Add(TEXT("the player of the game"), 4.5f, 13);
		Add(TEXT("the final stats"), 6.f, 14);
	}
	else if (Pace == ETagPace::Standard)
	{
		Add(TEXT("the celebration"), 3.f, 11);
		Add(TEXT("the final score"), 2.5f, 12);
		Add(TEXT("the player of the game"), 3.5f, 13);
		Add(TEXT("the final stats"), 4.f, 14);
	}
	else
	{
		Add(TEXT("the celebration"), 2.f, 11);
		Add(TEXT("the player of the game"), 2.5f, 13);
		Add(TEXT("the final stats"), 3.f, 14);
	}
	{
		// the game's best plays again, before the player of the game
		int32 At = 0;
		while (At < Sh.Stages.Num() && Sh.Stages[At].Act != 13) ++At;
		Sh.HlWant = BcAddHighlightStages(St, Pace == ETagPace::Full ? 2 : 1, 0, At);
	}
	{
		const std::vector<std::string> Names = {"corner", "end rise", "sideline sweep"};
		Sh.CraneVar = FMath::Max(0, tag::Recent::get().leastRecent("show.crane", Names, BroadcastSeed ^ 0x52u));
		tag::Recent::get().use("show.crane", Names[Sh.CraneVar]);
	}
	HelpStage = 0;
	State = EMatchState::Wait;
	Timer = 1e6f; // (held by the show: TickShow lets it go; then ShowFinal again, the box score)
	Sh.bOn = true;
	const int32 Ser = PostgameView.Serial + 1;
	PostgameView = FTagPostgameView();
	PostgameView.bOn = true;
	PostgameView.bLong = Sh.bLong;
	PostgameView.Serial = Ser;
	PostgameView.Winner = Sh.Winner;
	PostgameView.PlayerOfGame = Sh.Pog;
	PostgameView.PlayerOfGameTeam = PogTeam;
	PostgameView.PlayerOfGameLine = Sh.PogLine;
	UE_LOG(LogTemp, Display, TEXT("TAG show begin: postgame, %s version, %d stages, %s %d - %s %d, player of the game %s (%s)"), *Sh.Version, Sh.Stages.Num(),
		*BcS(G.away->abbr), G.score.away, *BcS(G.home->abbr), G.score.home, Sh.Pog ? *BcS(Sh.Pog->last) : TEXT("-"), *Sh.PogLine);
	Sh.Stage = 0;
	StartShowStage();
	return true;
}

void AMatchDirector::StartShowStage()
{
	FTagBcState& St = Bc();
	FTagBcShow& Sh = St.Show;
	if (St.bHlOn) EndHighlight(); // (the highlight before this stage, over)
	if (!Sh.Stages.IsValidIndex(Sh.Stage) || !Game) return;
	const FTagBcShowStage Sg = Sh.Stages[Sh.Stage];
	const tag::Game& G = *Game;
	const double Y = YD;
	FTagBcBeat B;
	B.Secs = Sg.Secs;
	B.Why = Sg.Name;
	B.Fixed = FVector(50.0 * Y, 0.0, 0.0);
	bool bGfxAsked = false, bGfxShown = false;
	switch (Sg.Act)
	{
	case 1:
	{
		// the men walking off, from the TV platform nearest them
		FVector Sum = FVector::ZeroVector;
		int32 N = 0;
		for (const FTagBcLeaver& L : St.Leavers) { Sum += L.At; ++N; }
		if (N) B.Fixed = Sum / static_cast<double>(N);
		B.Type = ETagShot::Wide;
		break;
	}
	case 2:
	case 14:
	{
		// the stats over the bowl (a crane the shows used least lately; the final one another)
		B.Type = ETagShot::ShowCrane;
		if (Sg.Act == 14) Sh.CraneVar = (Sh.CraneVar + 1) % 3;
		bGfxAsked = true;
		bGfxShown = Overlay && (Sg.Act == 2 ? BcGfxHalftimeStats(Overlay->Gfx(), G, League, Sg.Secs) : BcGfxFinalStats(Overlay->Gfx(), G, League, Sg.Secs));
		if (Sg.Act == 2) HalftimeView.bStatsUp = true;
		else PostgameView.bStatsUp = true;
		break;
	}
	case 3:
		B.Type = ETagShot::Bench; // (the leading team's bench; level: the home one)
		B.Team = G.score.away > G.score.home ? 0 : 1;
		break;
	case 4:
		B.Type = ETagShot::Crowd; // (the end stand behind the home team's end zone of the second half)
		B.Fixed = FVector((G.worldDir(tag::TeamKey::Home) > 0 ? 100.0 : 0.0) * Y, 0.0, 0.0);
		break;
	case 5:
	{
		// the second half's kickoff units at their 25s (everyone else put away), from in front of each (Full) or the TV wide
		StageKickoffUnits();
		St.Leavers.Reset();
		FTagBcOpen& O = St.Open;
		O.Stage = 3;
		O.Teams = Sg.Secs;
		O.StageT = 0.f;
		O.bLong = Sh.bLong;
		B.Type = ETagShot::OpenTeams;
		// (measured: nobody left on the field but the units)
		int32 Others = 0;
		for (const auto& KV : Athletes)
		{
			const AAthlete* A = KV.Value;
			if (A && !A->IsHidden() && A->Player && !O.Men.Contains(const_cast<tag::Player*>(A->Player)) && FMath::Abs(A->GetActorLocation().Y) < (tag::FIELD_W / 2) * Y) ++Others;
		}
		Sh.FieldMenAtUnits = Others;
		break;
	}
	case 11:
	{
		// the winners: their bench on its feet, their men on the field celebrating (a tie: the crowd)
		if (Sh.Winner >= 0)
		{
			const int32 K = Sh.Winner == 1 ? 0 : 1;
			SidelineCheerLeft[K] = FMath::Max(SidelineCheerLeft[K], 30.f);
			for (int32 i = 0; i < Sideline.Num(); ++i)
				if (Sideline[i] && SidelineIsHome.IsValidIndex(i) && SidelineIsHome[i] == (Sh.Winner == 1)) Sideline[i]->SidelineCheer(true);
			if (Sim)
				for (tag::Ent* E : Sim->ents)
					if (E && E->p && G.home->hasPlayer(E->p) == (Sh.Winner == 1) && E->state != tag::EntState::Down)
					{
						E->anim = tag::Anim::Celebrate; // (the game is over: only the drawn men's clip)
						E->animT = 0;
					}
			B.Type = ETagShot::Bench;
			B.Team = Sh.Winner;
		}
		else B.Type = ETagShot::Crowd;
		break;
	}
	case 12:
		B.Type = ETagShot::ShowCrane;
		if (Overlay)
			Overlay->Banner(TEXT("FINAL"), FString::Printf(TEXT("%s %d  -  %s %d"), *BcS(G.away->abbr), G.score.away, *BcS(G.home->abbr), G.score.home), FLinearColor::White);
		break;
	case 13:
	{
		// the player of the game: on him where he is on the field; else he is stood on his sideline's edge, facing the
		// field, and the shot comes a moment later (his body posed there first)
		B.Type = ETagShot::Hero;
		B.Team = PostgameView.PlayerOfGameTeam;
		AAthlete* PA = Sh.Pog ? Athletes.FindRef(Sh.Pog->id).Get() : nullptr;
		if (!PA && Sh.Pog) PA = AthleteFor(const_cast<tag::Player*>(Sh.Pog), PostgameView.PlayerOfGameTeam == 1 ? tag::TeamKey::Home : tag::TeamKey::Away);
		if (PA && PA->IsHidden())
		{
			const double Side = PostgameView.PlayerOfGameTeam == 1 ? 1.0 : -1.0;
			PA->SetActorHiddenInGame(false);
			PA->SetDormant(false);
			PA->Drive(FVector(50.0 * Y, Side * (tag::FIELD_W / 2 - 2.0) * Y, 0.0), Side > 0.0 ? -90.f : 90.f, FVector::ZeroVector, 9.f, TEXT("idle"), 0.f, 0.f, false);
			Sh.StageT = -0.35f; // (the stage runs 0.35 s longer: the shot once his pose has been drawn there)
		}
		B.Subject = PA;
		bGfxAsked = true;
		bGfxShown = Overlay && BcGfxPlayerOfGame(Overlay->Gfx(), G, Sh.Pog, Sg.Secs);
		PostgameView.bPlayerUp = true;
		break;
	}
	case 6:
		// a stored highlight, through the replay package (TickHighlight plays it; no shot of the show's own)
		if (St.Highlights.IsValidIndex(Sg.Hl)) BeginHighlight(Sg.Hl);
		break;
	default:
		B.Type = ETagShot::Wide;
		break;
	}
	if (Sg.Act == 6)
	{
		if (Sh.Kind == 1) { HalftimeView.Stage = 3; HalftimeView.StageName = Sg.Name; }
		else { PostgameView.Stage = 2; PostgameView.StageName = Sg.Name; }
		UE_LOG(LogTemp, Display, TEXT("TAG show %s stage %d: %s, %.1f s"), Sh.Kind == 1 ? TEXT("halftime") : TEXT("postgame"), Sh.Stage + 1, Sg.Name, Sg.Secs);
		return;
	}
	if (bGfxAsked)
	{
		++Sh.GfxAsked;
		Sh.GfxShown += bGfxShown ? 1 : 0;
		if (!bGfxShown) UE_LOG(LogTemp, Display, TEXT("TAG show graphic missing: UI/BroadcastGfx has no %s of the shape asked for (the show runs on without it)"),
			Sg.Act == 2 ? TEXT("ShowHalftimeStats") : Sg.Act == 13 ? TEXT("ShowPlayerOfGame") : TEXT("ShowFinalStats"));
	}
	// (a staged man's shot waits for his first drawn pose: TickShow starts it)
	if (!(Sg.Act == 13 && Sh.StageT < 0.f)) StartShot(B);
	else if (ShotView.bOn) EndShot(TEXT("cut away"));
	St.Shot.bJustPlaced = true;
	if (Sh.Kind == 1)
	{
		HalftimeView.Stage = Sg.Act <= 2 ? Sg.Act : Sg.Act == 5 ? 4 : 3; // (1 leaving, 2 the stats, 3 the moments and highlights, 4 the units)
		HalftimeView.StageName = Sg.Name;
	}
	else
	{
		PostgameView.Stage = Sg.Act == 6 ? 2 : Sg.Act - 10; // (a highlight: with the final score)
		PostgameView.StageName = Sg.Name;
	}
	UE_LOG(LogTemp, Display, TEXT("TAG show %s stage %d: %s, %.1f s (%s shot)"), Sh.Kind == 1 ? TEXT("halftime") : TEXT("postgame"), Sh.Stage + 1, Sg.Name, Sg.Secs,
		BcShotName(St.Shot.Beat.Type));
}

void AMatchDirector::TickShow(float InDt)
{
	if (!BcState || !BcState->Show.bOn) return;
	FTagBcState& St = *BcState;
	FTagBcShow& Sh = St.Show;
	if (State != EMatchState::Wait) { EndShow(Sh.Kind == 1 ? TEXT("kickoff") : TEXT("final")); return; }
	const float Dt = FMath::Min(InDt, 0.1f); // (a hitch does not jump a stage)
	Sh.T += Dt;
	const float Was = Sh.StageT;
	Sh.StageT += Dt;
	const bool bInStages = Sh.Stages.IsValidIndex(Sh.Stage);
	const int32 Act = bInStages ? Sh.Stages[Sh.Stage].Act : 0;
	// the second half's units stand where they were put; the long version's cut from one unit to the other
	if (Act == 6) TickHighlight(Dt); // (a stored highlight playing)
	if (Sh.Kind == 1 && (Act == 5 || !bInStages))
	{
		DriveStagedUnits(Dt);
		St.Open.StageT = Sh.StageT;
		if (Act == 5 && Sh.bLong && Sh.StageT >= St.Open.Teams * 0.5f && Was < St.Open.Teams * 0.5f) St.Shot.bJustPlaced = true;
	}
	// the player of the game stood on his sideline: his shot once his pose has been drawn there
	if (Act == 13 && Was < 0.f && Sh.StageT >= 0.f)
	{
		const AAthlete* PA = Sh.Pog ? Athletes.FindRef(Sh.Pog->id).Get() : nullptr;
		FTagBcBeat B;
		B.Type = ETagShot::Hero;
		B.Subject = const_cast<AAthlete*>(PA);
		B.Team = PostgameView.PlayerOfGameTeam;
		B.Secs = Sh.Stages[Sh.Stage].Secs;
		B.Why = Sh.Stages[Sh.Stage].Name;
		StartShot(B);
	}
	if (Act == 13 && Sh.Pog)
		if (AAthlete* PA = Athletes.FindRef(Sh.Pog->id).Get(); PA && !PA->IsHidden() && !(Sim && [&]() { for (const tag::Ent* E : Sim->ents) if (E->p == Sh.Pog) return true; return false; }()))
		{
			const double Side = PostgameView.PlayerOfGameTeam == 1 ? 1.0 : -1.0;
			PA->Drive(FVector(50.0 * YD, Side * (tag::FIELD_W / 2 - 2.0) * YD, 0.0), Side > 0.0 ? -90.f : 90.f, FVector::ZeroVector, 9.f, TEXT("idle"), 0.f, Dt, false);
		}
	if (bInStages && Sh.StageT >= Sh.Stages[Sh.Stage].Secs)
	{
		Sh.StageSecs.Add(Sh.StageT);
		UE_LOG(LogTemp, Display, TEXT("TAG show %s stage %d done: %.2f s"), Sh.Kind == 1 ? TEXT("halftime") : TEXT("postgame"), Sh.Stage + 1, Sh.StageT);
		++Sh.Stage;
		Sh.StageT = 0.f;
		if (Sh.Stages.IsValidIndex(Sh.Stage)) StartShowStage();
		else
		{
			// done: the second half's kickoff (the TV wide holds on the units while the booth finishes), or the final flow
			if (Sh.Kind == 1)
			{
				St.Open.Stage = 4;
				if (Sh.bLong) St.Shot.bJustPlaced = true;
			}
			UE_LOG(LogTemp, Display, TEXT("TAG show %s stages done after %.2f s%s"), Sh.Kind == 1 ? TEXT("halftime") : TEXT("postgame"), Sh.T,
				BoothHold() ? TEXT(", the booth still talking: the shot holds") : TEXT(""));
			Timer = 0.f;
		}
	}
	// the views (the graphics, the sound)
	// (the stage now: the one above may have just stepped past the last - it was read here as the old one, index 3 of 3)
	const float Left = Sh.Stages.IsValidIndex(Sh.Stage) ? FMath::Max(0.f, Sh.Stages[Sh.Stage].Secs - Sh.StageT) : 0.f;
	if (Sh.Kind == 1) { HalftimeView.StageT = FMath::Max(0.f, Sh.StageT); HalftimeView.StageLeft = Left; }
	else { PostgameView.StageT = FMath::Max(0.f, Sh.StageT); PostgameView.StageLeft = Left; }
	// (measured: the lens clear, the clocks held, both units seen)
	++Sh.Frames;
	if (Camera && (!ShotSpotOk(Camera->GetActorLocation()) || NearestPerson(Camera->GetActorLocation(), nullptr) < 60.f)) ++Sh.BadFrames;
	Sh.bHeldClock = Sh.bHeldClock && BroadcastHoldsClock();
	if (Sh.Kind == 1 && Act == 5)
	{
		APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		FVector2D VP(0.0, 0.0);
		if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(VP);
		int32 KickSeen = 0, RecvSeen = 0;
		const FTagBcOpen& O = St.Open;
		for (int32 i = 0; i < O.Men.Num(); ++i)
		{
			const AAthlete* A = O.Men[i] ? Athletes.FindRef(O.Men[i]->id).Get() : nullptr;
			FVector2D S2;
			if (A && !A->IsHidden() && PC && VP.X > 0.0 && PC->ProjectWorldLocationToScreen(A->GetActorLocation() + FVector(0, 0, 100), S2, false) && S2.X >= 0.0 &&
				S2.Y >= 0.0 && S2.X <= VP.X && S2.Y <= VP.Y)
				(O.bKickSide[i] ? KickSeen : RecvSeen)++;
		}
		Sh.MostKickSeen = FMath::Max(Sh.MostKickSeen, KickSeen);
		Sh.MostRecvSeen = FMath::Max(Sh.MostRecvSeen, RecvSeen);
	}
}

void AMatchDirector::EndShow(const TCHAR* How)
{
	if (!BcState || !BcState->Show.bOn) return;
	FTagBcState& St = *BcState;
	FTagBcShow& Sh = St.Show;
	Sh.bOn = false;
	if (Sh.Stages.IsValidIndex(Sh.Stage)) Sh.StageSecs.Add(FMath::Max(0.f, Sh.StageT));
	FString Stages;
	for (const float S : Sh.StageSecs) Stages += FString::Printf(TEXT(" %.2f"), S);
	UE_LOG(LogTemp, Display, TEXT("TAG show done: %s, %s after %.2f s (%s version, stages%s; graphics %d of %d shown; lens bad %d of %d frames; clocks held %s%s)"),
		Sh.Kind == 1 ? TEXT("halftime") : TEXT("postgame"), How, Sh.T, *Sh.Version, *Stages, Sh.GfxShown, Sh.GfxAsked, Sh.BadFrames, Sh.Frames, Sh.bHeldClock ? TEXT("yes") : TEXT("no"),
		Sh.Kind == 1 ? *FString::Printf(TEXT("; units on screen %d / %d; others on the field at the units %d"), Sh.MostKickSeen, Sh.MostRecvSeen, Sh.FieldMenAtUnits) : TEXT(""));
	if (St.bHlOn) EndHighlight();
	if (ShotView.bOn) EndShot(How);
	// the player of the game stood on his sideline for the show goes back where he was (off: the menus are up)
	if (Sh.Kind == 2 && Sh.Pog)
		if (AAthlete* PA = Athletes.FindRef(Sh.Pog->id).Get(); PA && !PA->IsHidden() && !(Sim && [&]() { for (const tag::Ent* E : Sim->ents) if (E->p == Sh.Pog) return true; return false; }()))
		{
			PA->ResetPose();
			PA->SetActorHiddenInGame(true);
			PA->SetDormant(true);
		}
	St.Leavers.RemoveAll([](const FTagBcLeaver& L) { return L.bShow; });
	const int32 HSer = HalftimeView.Serial, PSer = PostgameView.Serial;
	HalftimeView = FTagHalftimeView();
	HalftimeView.Serial = HSer;
	PostgameView = FTagPostgameView();
	PostgameView.Serial = PSer;
	bCutNext = true;
}

bool AMatchDirector::BroadcastDrivesMen() const
{
	return BcState && BcState->Show.bOn && (BcState->Show.Kind == 1 || BcState->bHlOn); // (the half's men walking off; a highlight's film)
}

// ------------------------------------------------------------------ stored highlights (04 A10)
void AMatchDirector::CaptureHighlight()
{
	FTagBcState& St = Bc();
	if (!St.bHlPending) return;
	St.bHlPending = false;
	if (ReplayBuf.Num() < 20) return;
	// the cap on what is kept (the films of the best plays of both halves)
	static int32 CapMB = FParse::Value(FCommandLine::Get(), TEXT("TagHighlightMB="), CapMB) ? FMath::Clamp(CapMB, 16, 2048) : 192;
	St.HlCapBytes = static_cast<int64>(CapMB) * 1024 * 1024;
	FTagBcHighlight H = St.HlPending;
	// the stretch around the key moment (the sim's clock to the film's), 4.5 s before it to 1.5 s after
	H.KeyT = ReplayBuf.Last().T - 1.5f;
	for (const FReplayFrame& F : ReplayBuf)
		if (F.SimT >= H.KeySim - 1e-4) { H.KeyT = F.T; break; }
	H.T0 = FMath::Max(ReplayBuf[0].T, H.KeyT - 4.5f);
	H.T1 = FMath::Min(ReplayBuf.Last().T, H.KeyT + 1.5f);
	int64 Bytes = 0;
	for (const FReplayFrame& F : ReplayBuf)
	{
		if (F.T < H.T0 - 0.05f || F.T > H.T1 + 0.05f) continue;
		H.Frames.Add(F);
		Bytes += sizeof(FReplayFrame);
		for (const FReplayAth& RA : F.Ath) Bytes += sizeof(FReplayAth) + RA.Rot.Num() * sizeof(FQuat4f) + RA.Pos.Num() * sizeof(FVector3f);
	}
	H.Bytes = Bytes;
	if (H.Frames.Num() < 10) return;
	if (Bytes > St.HlCapBytes / 3)
	{
		++St.HlDropped;
		UE_LOG(LogTemp, Warning, TEXT("TAG highlight not kept: %.1f MB for one play, over a third of the %d MB cap"), Bytes / 1048576.0, CapMB);
		return;
	}
	const int32 Half = H.Half;
	St.HlBytes += Bytes;
	St.Highlights.Add(MoveTemp(H));
	++St.HlKept;
	// six a half at most, and under the cap: the lowest score goes (of the half that has too many, else of all)
	for (;;)
	{
		int32 InHalf = 0;
		for (const FTagBcHighlight& Kh : St.Highlights) InHalf += Kh.Half == Half ? 1 : 0;
		if (InHalf <= 6 && St.HlBytes <= St.HlCapBytes) break;
		int32 Worst = -1;
		for (int32 i = 0; i < St.Highlights.Num(); ++i)
			if ((InHalf <= 6 || St.Highlights[i].Half == Half) && (Worst < 0 || St.Highlights[i].Score < St.Highlights[Worst].Score)) Worst = i;
		if (Worst < 0) break;
		St.HlBytes -= St.Highlights[Worst].Bytes;
		St.Highlights.RemoveAt(Worst);
		++St.HlDropped;
	}
	St.HlBytesPeak = FMath::Max(St.HlBytesPeak, St.HlBytes);
	UE_LOG(LogTemp, Display, TEXT("TAG highlight kept: play %d (%s, score %.2f), %d frames %.1f MB; %d kept, %.1f MB of the %d MB cap (%d dropped)"), St.HlPending.PlayN,
		*St.HlPending.Desc, St.HlPending.Score, St.Highlights.Num() ? St.Highlights.Last().Frames.Num() : 0, Bytes / 1048576.0, St.Highlights.Num(), St.HlBytes / 1048576.0, CapMB,
		St.HlDropped);
}

void AMatchDirector::BeginHighlight(int32 Idx)
{
	FTagBcState& St = Bc();
	if (!St.Highlights.IsValidIndex(Idx)) return;
	if (St.bHlOn) EndHighlight();
	if (ShotView.bOn) EndShot(TEXT("highlight"));
	const FTagBcHighlight& H = St.Highlights[Idx];
	// the men of the film on the field, everyone else off it (put back after: EndHighlight)
	St.HlVisible.Reset();
	for (const auto& KV : Athletes)
		if (KV.Value && !KV.Value->IsHidden())
		{
			St.HlVisible.Add(KV.Key);
			KV.Value->SetActorHiddenInGame(true);
		}
	TSet<int32> InFilm;
	for (const FReplayFrame& F : H.Frames)
		for (const FReplayAth& RA : F.Ath) InFilm.Add(RA.Id);
	for (const int32 Id : InFilm)
		if (AAthlete* A = Athletes.FindRef(Id).Get())
		{
			A->SetActorHiddenInGame(false);
			A->SetDormant(false);
		}
	St.bHlBallHidden = Ball ? Ball->IsHidden() : true;
	ReplayBuf = H.Frames;
	ReplayEnd = ReplayBuf.Last().T;
	ReplayDirOverride = H.Dir;
	St.Runs.Reset();
	St.Runs.Add(BcHighlightRun(H));
	St.Run = 0;
	St.RKeyKind = TEXT("highlight");
	SetupReplayRun();
	const int32 Ser = ReplayView.Serial + 1;
	ReplayView.bOn = true;
	ReplayView.Serial = Ser;
	ReplayView.NumAngles = 1;
	ReplayView.Why = FString::Printf(TEXT("highlight: %s"), *H.Desc);
	St.bHlOn = true;
	FVector BallAt, SubjAt;
	bool bSubj = false;
	ApplyReplayAt(ReplayT, BallAt, SubjAt, bSubj);
	ReplayCameraFrame(0.f, BallAt, SubjAt, bSubj, true);
	MarkCameraCut(TEXT("highlight"));
	++St.Show.HlShown;
	UE_LOG(LogTemp, Display, TEXT("TAG highlight shown: %s (score %.2f, %s angle, film %.1f-%.1f s, key %.1f, %d men)"), *H.Desc, H.Score, BcAngleName(St.Runs[0].Type), H.T0, H.T1,
		H.KeyT, InFilm.Num());
}

void AMatchDirector::TickHighlight(float Dt)
{
	if (!BcState || !BcState->bHlOn || BcState->Runs.Num() == 0) return;
	FTagBcState& St = *BcState;
	FTagReplayRun& R = St.Runs[0];
	const float Rate = BcRampRate(R, ReplayT);
	ReplayT = FMath::Min(ReplayT + Dt * Rate, FMath::Min(R.T1, ReplayEnd));
	FVector BallAt, SubjAt;
	bool bSubj = false;
	ApplyReplayAt(ReplayT, BallAt, SubjAt, bSubj);
	ReplayCameraFrame(Dt, BallAt, SubjAt, bSubj, false); // (after the game camera's: UpdateCamera ran in the Wait before)
	ReplayView.Rate = Rate;
	ReplayView.KeyIn = R.KeyT - ReplayT;
	ReplayView.bAtKey = FMath::Abs(ReplayView.KeyIn) < 0.45f;
}

void AMatchDirector::EndHighlight()
{
	if (!BcState || !BcState->bHlOn) return;
	FTagBcState& St = *BcState;
	St.bHlOn = false;
	for (auto& KV : Athletes)
		if (KV.Value) KV.Value->EndReplay();
	// everyone as he was before the film
	for (auto& KV : Athletes)
	{
		AAthlete* A = KV.Value.Get();
		if (!A) continue;
		const bool bWas = St.HlVisible.Contains(KV.Key);
		if (!bWas && !A->IsHidden())
		{
			A->ResetPose();
			A->SetActorHiddenInGame(true);
			A->SetDormant(true);
		}
		else if (bWas && A->IsHidden())
		{
			A->SetActorHiddenInGame(false);
			A->SetDormant(false);
		}
	}
	if (Ball) Ball->SetActorHiddenInGame(St.bHlBallHidden);
	ReplayDirOverride = 0;
	const int32 Ser = ReplayView.Serial;
	ReplayView = FTagReplayView();
	ReplayView.Serial = Ser;
	bCutNext = true;
}

// ------------------------------------------------------------------ substitutes on and off (04 A11)
bool AMatchDirector::SubEntrySpot(const tag::Ent* E, double MaxYd, tag::V2& Out)
{
	static const bool bNoSubJog = FParse::Param(FCommandLine::Get(), TEXT("TagNoSubJog")); // A/B: they pop on as before
	if (bNoSubJog || !Game || !E || !E->p || MaxYd <= 1.0) return false;
	const double Y = YD;
	const bool bHome = Game->home->hasPlayer(E->p);
	const FVector W = ToWorld(E->pos.x, E->pos.y, Dir);
	// from his own sideline, level with his spot; a spot on the far side of the field from his bench, the nearer one
	double SideY = (bHome ? 1.0 : -1.0) * (tag::FIELD_W / 2 + 1.0) * Y;
	if (FMath::Abs(W.Y - SideY) > 30.0 * Y) SideY = -SideY;
	FTagBcState& St = Bc();
	if (Sim && St.SubStartsSerial != Sim->serial)
	{
		St.SubStartsSerial = Sim->serial;
		St.SubStarts.Reset();
	}
	// (each from his own spot on the sideline: two men level with the same spot stood inside each other)
	const FVector Start = BcSpread(FVector(W.X - Dir * 1.5 * Y, SideY, 0.0), St.SubStarts, 150.0, 120.0);
	if (FVector::Dist2D(Start, W) > MaxYd * Y) return false;
	St.SubStarts.Add(Start);
	Out = TagCoords::FromWorld(Start, Dir);
	++St.SubsIn;
	return true;
}

float AMatchDirector::BeginSubsOff(const std::map<tag::Player*, FVector>& From)
{
	static const bool bNoSubJog = FParse::Param(FCommandLine::Get(), TEXT("TagNoSubJog"));
	FTagBcState& St = Bc();
	if (bNoSubJog || !Sim || !Game || From.size() < 11) return 0.f;
	// (a whole unit changing - the offense on after a kickoff, a punt team - is not a substitution: put away, as before)
	int32 Going = 0;
	for (const auto& KV : From)
	{
		bool bStays = false;
		for (const tag::Ent* E : Sim->ents) bStays = bStays || E->p == KV.first;
		Going += bStays ? 0 : 1;
	}
	if (Going > 6) return 0.f;
	const double Y = YD;
	float Most = 0.f;
	for (const auto& KV : From)
	{
		tag::Player* P = KV.first;
		bool bIn = false;
		for (const tag::Ent* E : Sim->ents) bIn = bIn || E->p == P;
		AAthlete* A = P && !bIn ? Athletes.FindRef(P->id).Get() : nullptr;
		if (!A) continue;
		FTagBcLeaver L;
		L.P = P;
		L.At = KV.second;
		L.At.Z = 0.0;
		{
			TArray<FVector> Taken;
			for (const FTagBcLeaver& O : St.Leavers) Taken.Add(O.To);
			L.To = BcSpread(BcBenchSpot(Game->home->hasPlayer(P), L.At.X), Taken, 130.0, 120.0); // (each to a spot of his own)
		}
		L.Speed = 600.f;
		const FVector D = L.To - L.At;
		const double Dist = D.Size2D();
		if (Dist < 60.0) continue;
		// (back on: ShowOnly put him away; he jogs off from where the last play left him)
		A->SetActorHiddenInGame(false);
		A->SetDormant(false);
		St.Leavers.Add(L);
		++St.SubsOut;
		// the seconds he is still on the field: his travel across the field's width, along his path
		const double DY = FMath::Abs(D.Y);
		const double InField = FMath::Max(0.0, DY - FMath::Max(0.0, FMath::Abs(L.To.Y) - (tag::FIELD_W / 2) * Y));
		const double Path = DY > 1.0 ? InField * Dist / DY : Dist;
		Most = FMath::Max(Most, static_cast<float>(Path / L.Speed));
	}
	if (Most > 0.f) UE_LOG(LogTemp, Display, TEXT("TAG subs off: %d men jogging off (the last off the field in %.1f s), %d on so far"), St.Leavers.Num(), Most, St.SubsIn);
	return Most > 0.f ? FMath::Min(Most + 0.3f, 6.f) : 0.f;
}

void AMatchDirector::TickLeavers(float Dt)
{
	if (!BcState || BcState->Leavers.Num() == 0) return;
	FTagBcState& St = *BcState;
	// (a kick stages its own men; the menus and the end have nobody walking)
	const bool bDrop = State == EMatchState::Kick || State == EMatchState::Menu || State == EMatchState::Final;
	for (int32 i = St.Leavers.Num() - 1; i >= 0; --i)
	{
		FTagBcLeaver& L = St.Leavers[i];
		AAthlete* A = L.P ? Athletes.FindRef(L.P->id).Get() : nullptr;
		bool bInPlay = false;
		if (!L.bShow && Sim)
			for (const tag::Ent* E : Sim->ents) bInPlay = bInPlay || E->p == L.P;
		if (!A || A->IsHidden() || bInPlay || bDrop || (L.bShow && !St.Show.bOn))
		{
			St.Leavers.RemoveAt(i);
			continue;
		}
		FVector D = L.To - L.At;
		D.Z = 0.0;
		const double Dist = D.Size();
		if (Dist < 60.0)
		{
			// at his bench: put away (the bench's own men stand there)
			A->ResetPose();
			A->SetActorHiddenInGame(true);
			A->SetDormant(true);
			St.Leavers.RemoveAt(i);
			continue;
		}
		const FVector Dn = D / Dist;
		L.At += Dn * FMath::Min(Dist, static_cast<double>(L.Speed) * Dt);
		A->Drive(L.At, static_cast<float>(Dn.Rotation().Yaw), Dn * L.Speed, 9.f, TEXT("run"), 0.f, Dt, false);
	}
}

void AMatchDirector::BroadcastAtSnap()
{
	if (!Game || !Sim) return;
	FTagBcState& St = Bc();
	const double Y = YD;
	auto OnField = [Y](const FVector& P) { return FMath::Abs(P.Y) < (tag::FIELD_W / 2) * Y + 30.0 && P.X > -10.0 * Y && P.X < 110.0 * Y; };
	// a man going off who is still on the field: put away now (never twelve at the snap)
	for (int32 i = St.Leavers.Num() - 1; i >= 0; --i)
	{
		const FTagBcLeaver& L = St.Leavers[i];
		if (L.bShow) continue;
		AAthlete* A = L.P ? Athletes.FindRef(L.P->id).Get() : nullptr;
		if (A && !A->IsHidden() && OnField(A->GetActorLocation()))
		{
			A->ResetPose();
			A->SetActorHiddenInGame(true);
			A->SetDormant(true);
			++St.SubsPulled;
			UE_LOG(LogTemp, Display, TEXT("TAG subs: %s still on the field at the snap, put away"), *BcWho(A));
			St.Leavers.RemoveAt(i);
		}
	}
	// the check: eleven a side on the field, nobody still walking to his spot
	int32 N[2] = {0, 0};
	for (const auto& KV : Athletes)
	{
		const AAthlete* A = KV.Value;
		if (!A || A->IsHidden() || !A->Player || !OnField(A->GetActorLocation())) continue;
		N[Game->home->hasPlayer(A->Player) ? 1 : 0]++;
	}
	const bool bWalking = Sim->walking();
	++St.Snaps;
	if (N[0] != 11 || N[1] != 11)
	{
		++St.SnapBad;
		UE_LOG(LogTemp, Warning, TEXT("TAG snap: %d visitors and %d home men on the field (11 a side)"), N[0], N[1]);
	}
	if (bWalking)
	{
		++St.SnapWalking;
		UE_LOG(LogTemp, Warning, TEXT("TAG snap: a man still walking to his spot"));
	}
}

// ------------------------------------------------------------------ the self-checks
bool AMatchDirector::BeginBroadcastTest()
{
	const TCHAR* Cmd = FCommandLine::Get();
	if (FParse::Param(Cmd, TEXT("TagRecentTest")))
	{
		TagRecentTest::Run();
		return true;
	}
	const int32 Mode = FParse::Param(Cmd, TEXT("TagReplayTest")) ? 1 : FParse::Param(Cmd, TEXT("TagShotTest")) ? 2 : FParse::Param(Cmd, TEXT("TagPaceTest")) ? 3
		: FParse::Param(Cmd, TEXT("TagOpenTest")) ? 4 : FParse::Param(Cmd, TEXT("TagHalftimeTest")) ? 5 : FParse::Param(Cmd, TEXT("TagPostgameTest")) ? 6 : 0;
	if (!Mode) return false;
	FTagBcState& St = Bc();
	St.Test = FTagBcTest();
	St.Test.Mode = Mode;
	// (the halftime and postgame tests play their game at Quick and turn Full for the show they check)
	St.PaceOverride = Mode == 1 ? ETagPace::Standard : Mode == 3 || Mode >= 5 ? ETagPace::Quick : ETagPace::Full;
	// a watched CPU game (the TV camera; the same teams and, with -TagWeatherSeed=, the same plays every run)
	bWatch = true;
	double QLen = Mode == 4 ? 900.0 : Mode == 5 ? 30.0 : Mode == 6 ? 20.0 : 300.0; // (short quarters: to the half, to the end)
	FParse::Value(Cmd, TEXT("TagQuarterLen="), QLen);
	UE_LOG(LogTemp, Display, TEXT("TAG broadcast test %d starts (pace %s)"), Mode, TagPace::Name(St.PaceOverride));
	StartMatch(3, 7, false, true, 1, true, QLen);
	return true;
}

void AMatchDirector::TickBroadcastTests(float Dt)
{
	if (!BcState || BcState->Test.Mode == 0 || BcState->Test.bDone) return;
	FTagBcState& St = *BcState;
	FTagBcTest& T = St.Test;
	T.Clock += Dt;
	auto Check = [&T](bool bOk, const FString& What, const TCHAR* Tag)
	{
		(bOk ? T.Pass : T.Fail)++;
		UE_LOG(LogTemp, Display, TEXT("TAG %s %s: %s"), Tag, bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	const bool bOver = (Game && Game->isFinal()) || T.Clock > 840.f;
	if (T.Mode == 1)
	{
		const TCHAR* Tg = TEXT("replaytest");
		// (after the eighth package is over: the test had ended on the frame the eighth one began, before its first angle
		// and its tags - 7 picks in 8)
		if ((St.Packages < 8 || State == EMatchState::Replay) && !bOver) return;
		T.bDone = true;
		// the plan's own rules on made-up plays: two angles on a score or a takeaway, never a first angle a third time
		{
			const TArray<ETagReplayAngle> NoPrev;
			auto AllClear = [](ETagReplayAngle) { return true; };
			TArray<ETagReplayAngle> Out;
			bool bOk = true;
			for (const TCHAR* Cl : {TEXT("td"), TEXT("td-goal"), TEXT("take"), TEXT("sack"), TEXT("pass"), TEXT("run")})
			{
				const bool bTwo = FString(Cl).StartsWith(TEXT("t"));
				BcAnglePlan(Cl, bTwo, NoPrev, AllClear, 7, true, Out);
				bOk = bOk && Out.Num() == (bTwo ? 2 : 1) && BcFirstCands(Cl).Contains(Out[0]) && (!bTwo || Out[1] != Out[0]);
				TArray<ETagReplayAngle> Twice;
				Twice.Add(Out[0]);
				Twice.Add(Out[0]);
				BcAnglePlan(Cl, bTwo, Twice, AllClear, 7, true, Out);
				bOk = bOk && Out[0] != Twice[0];
			}
			Check(bOk, TEXT("the plan: two angles on scores and takeaways, the best looks first, no first angle three times running"), Tg);
		}
		int32 MaxRun = 0, Run = 0;
		for (int32 i = 0; i < St.FirstAngles.Num(); ++i)
		{
			Run = i > 0 && St.FirstAngles[i] == St.FirstAngles[i - 1] ? Run + 1 : 1;
			MaxRun = FMath::Max(MaxRun, Run);
		}
		FString By;
		for (const TPair<FString, int32>& KV : St.FirstByAngle) By += FString::Printf(TEXT(" %s %d"), *KV.Key, KV.Value);
		Check(St.Packages >= 4, FString::Printf(TEXT("replays shown (%d; %d with two angles)"), St.Packages, St.TwoAngle), Tg);
		Check(MaxRun <= 2, FString::Printf(TEXT("no first angle on more than 2 replays in a row (longest %d; firsts:%s)"), MaxRun, *By), Tg);
		Check(St.TagPicks == St.Packages && St.PkgsTwoPicks == 0 && St.TagsShown >= 1, FString::Printf(TEXT("stat tags picked once a package (%d picks in %d replays, %d with tags, %d picked twice)"),
			St.TagPicks, St.Packages, St.TagsShown, St.PkgsTwoPicks), Tg);
		Check(St.CutsBare == 0 && St.CutsCovered >= St.Packages, FString::Printf(TEXT("every replay cut under the wipe (%d covered, %d outside)"), St.CutsCovered, St.CutsBare), Tg);
		Check(St.ReplayFrames > 0 && St.ReplayLensBad <= St.ReplayFrames / 33, FString::Printf(TEXT("replay lens clear (%d of %d frames bad)"), St.ReplayLensBad, St.ReplayFrames), Tg);
		LogBroadcastSummary();
		TagResult::Finish(TEXT("TagReplayTest"), T.Pass, T.Fail);
	}
	else if (T.Mode == 2)
	{
		const TCHAR* Tg = TEXT("shottest");
		if (St.PlayN < 30 && !bOver) return;
		T.bDone = true;
		TSet<FString> Kinds;
		for (const TPair<FString, int32>& KV : St.TypeCount) Kinds.Add(KV.Key);
		Check(St.Shots >= 8 && Kinds.Num() >= 3, FString::Printf(TEXT("shots shown (%d, %d kinds)"), St.Shots, Kinds.Num()), Tg);
		Check(St.MaxRun <= 2, FString::Printf(TEXT("no kind three times in a row (longest run %d)"), St.MaxRun), Tg);
		Check(St.RepeatSeqs == 0, FString::Printf(TEXT("no three-shot sequence twice within 20 plays (%d repeats)"), St.RepeatSeqs), Tg);
		Check(St.ShotsBad == 0, FString::Printf(TEXT("no shot with its lens in a body or the stands (%d)"), St.ShotsBad), Tg);
		Check(St.Snaps >= 10 && St.SnapBad == 0 && St.SnapWalking == 0, FString::Printf(TEXT("11 a side and nobody walking at every snap (%d snaps: %d not 11 a side, %d with a man walking; subs on %d, off %d, put away at the snap %d)"),
			St.Snaps, St.SnapBad, St.SnapWalking, St.SubsIn, St.SubsOut, St.SubsPulled), Tg);
		// the next game, the same seed: its first big plays' shots as the store now has it (a dry pick, nothing kept)
		{
			FString Was, NextGame;
			TArray<ETagShot> Seq;
			TMap<int32, int32> TypeLast;
			TMap<FString, int32> SubjLast;
			tag::Recent Scratch;
			for (const char* Cat : {"shot.hero", "shot.capper"})
				for (const std::string& V : tag::Recent::get().history(Cat)) Scratch.use(Cat, V);
			std::vector<std::string> HeroVars;
			for (const FBcRig& Rg : kBcRigs) HeroVars.push_back(BcU(Rg.Name));
			for (int32 k = 0; k < T.BigPlays.Num(); ++k)
			{
				const FTagBcPlay& C = T.BigPlays[k];
				Was += FString::Printf(TEXT("[%s] "), *C.SeqVar);
				FString Line;
				if (C.bHasHero)
				{
					const std::string V = Scratch.take("shot.hero", HeroVars, BroadcastSeed ^ (static_cast<uint32>(k + 1) * 2654435761u));
					Line += FString::Printf(TEXT("hero:%s"), *BcS(V));
				}
				TArray<FBcCand> Picks;
				BcPickCappers(C, true, 2, k + 1, BroadcastSeed ^ (static_cast<uint32>(k + 1) * 2654435761u), Seq, TypeLast, SubjLast, Picks);
				for (const FBcCand& Pk : Picks)
				{
					Line += FString::Printf(TEXT(">%s"), BcShotName(Pk.Type));
					Seq.Add(Pk.Type);
					TypeLast.Add(static_cast<int32>(Pk.Type), k + 1);
				}
				NextGame += FString::Printf(TEXT("[%s] "), *Line);
			}
			// (the dry pick's store is the real one: BcPickCappers reads tag::Recent::get(), which this game has filled)
			Check(T.BigPlays.Num() >= 2 && Was != NextGame, FString::Printf(TEXT("the next game opens its big plays differently: this one %s, the next %s"), *Was, *NextGame), Tg);
		}
		LogBroadcastSummary();
		TagResult::Finish(TEXT("TagShotTest"), T.Pass, T.Fail);
	}
	else if (T.Mode == 3)
	{
		const TCHAR* Tg = TEXT("pacetest");
		// the presses: a beat or a replay on screen, the confirm press injected through the controller 0.4 s in
		const bool bReplayOn = State == EMatchState::Replay && St.Phase == 1;
		const bool bBeatOn = ShotView.bOn && !PregameView.bOn;
		if (T.bReleasePending)
		{
			PadKey(EKeys::Gamepad_FaceButton_Bottom, false);
			T.bReleasePending = false;
		}
		else if (T.bAwaitSkip)
		{
			if (St.Skips > T.SkipsAt)
			{
				const uint64 Frames = St.SkipFrame - T.InjectFrame;
				Check(Frames <= 2, FString::Printf(TEXT("the press skipped at once (%llu frames after it was sent, the controller reads it the next)"), Frames), Tg);
				T.bAwaitSkip = false;
				T.SkipAtT = FPlatformTime::Seconds();
				T.SerialAt = ShotView.Serial;
				T.bSecondPress = true;
			}
			else if (FPlatformTime::Seconds() - T.InjectT > 1.0)
			{
				Check(false, TEXT("the press skipped nothing within a second"), Tg);
				T.bAwaitSkip = false;
			}
		}
		else if (T.bSecondPress && FPlatformTime::Seconds() - T.SkipAtT > 0.1)
		{
			// a second press 0.1 s after the skip: refused (the 0.3 s rule)
			T.bSecondPress = false;
			T.RefusedAt = St.SkipsRefused;
			T.SkipsAt = St.Skips;
			PadKey(EKeys::Gamepad_FaceButton_Bottom, true);
			T.bReleasePending = true;
			T.bAwaitRefusal = true;
			T.InjectT = FPlatformTime::Seconds();
		}
		else if (T.bAwaitRefusal && FPlatformTime::Seconds() - T.InjectT > 0.12)
		{
			T.bAwaitRefusal = false;
			Check(St.Skips == T.SkipsAt && St.SkipsRefused > T.RefusedAt, FString::Printf(TEXT("a second press 0.1 s after a skip skips nothing (skips %d -> %d, refused %d -> %d)"),
				T.SkipsAt, St.Skips, T.RefusedAt, St.SkipsRefused), Tg);
			// and no shot started in the 0.3 s after the skip
			// (from the skip itself to the next shot's start: the test noticed the skip a frame late)
			Check(ShotView.Serial == T.SerialAt || St.LastShotT - (St.SkipLockUntil - 0.3) >= 0.29, FString::Printf(TEXT("the next beat waits 0.3 s after a skip (serial %d -> %d)"),
				T.SerialAt, ShotView.Serial), Tg);
			++T.SkipTests;
		}
		else if ((bBeatOn && St.Shot.T > 0.4f) || (bReplayOn && St.RShown > 0.6f))
		{
			const int32 Ser = bReplayOn ? -ReplayView.Serial : ShotView.Serial;
			if (Ser != T.InjectSerial && T.SkipTests < 8 && (++T.BeatsSeen % 2) == 0)
			{
				T.InjectSerial = Ser;
				T.SkipsAt = St.Skips;
				T.InjectFrame = GFrameCounter;
				T.InjectT = FPlatformTime::Seconds();
				PadKey(EKeys::Gamepad_FaceButton_Bottom, true);
				T.bReleasePending = true;
				T.bAwaitSkip = true;
				UE_LOG(LogTemp, Display, TEXT("TAG pacetest press sent: %s"), bReplayOn ? TEXT("the replay") : BcShotName(St.Shot.Beat.Type));
			}
			else if (Ser != T.InjectSerial) T.InjectSerial = Ser;
		}
		if (St.PlayN < 36 && !bOver) return;
		if (T.bAwaitSkip || T.bAwaitRefusal || T.bReleasePending) return;
		T.bDone = true;
		float QMed, QP90, SMed, SP90, FMed, FP90;
		BcStats(St.ToCall[BcPaceIdx(ETagPace::Quick)], QMed, QP90);
		BcStats(St.ToCall[BcPaceIdx(ETagPace::Standard)], SMed, SP90);
		BcStats(St.ToCall[BcPaceIdx(ETagPace::Full)], FMed, FP90);
		const int32 Qn = St.ToCall[BcPaceIdx(ETagPace::Quick)].Num(), Sn = St.ToCall[BcPaceIdx(ETagPace::Standard)].Num();
		Check(Qn >= 3 && QMed <= 2.5f, FString::Printf(TEXT("Quick: whistle to the call on routine plays, median %.2f s (<= 2.5; %d plays)"), QMed, Qn), Tg);
		Check(Sn >= 3 && SMed <= 4.f && SP90 <= 12.f, FString::Printf(TEXT("Standard: median %.2f s (<= 4), p90 %.2f s (<= 12; %d plays)"), SMed, SP90, Sn), Tg);
		UE_LOG(LogTemp, Display, TEXT("TAG pacetest Full: routine median %.2f s, p90 %.2f s (%d plays; no limit)"), FMed, FP90, St.ToCall[BcPaceIdx(ETagPace::Full)].Num());
		Check(T.SkipTests >= 3, FString::Printf(TEXT("presses tested (%d)"), T.SkipTests), Tg);
		LogBroadcastSummary();
		TagResult::Finish(TEXT("TagPaceTest"), T.Pass, T.Fail);
	}
	else if (T.Mode == 5 || T.Mode == 6)
	{
		const TCHAR* Tg = T.Mode == 5 ? TEXT("halftimetest") : TEXT("postgametest");
		const TCHAR* ModeName = T.Mode == 5 ? TEXT("TagHalftimeTest") : TEXT("TagPostgameTest");
		const FTagBcShow& Sh = St.Show;
		const bool bMine = Sh.Kind == (T.Mode == 5 ? 1 : 2);
		if (Sh.bOn && bMine) T.bShowSeen = true;
		if (T.bReleasePending)
		{
			PadKey(EKeys::Gamepad_FaceButton_Bottom, false);
			T.bReleasePending = false;
		}
		// the show as it ran: its stages in order and to time, a clear lens, the clocks held, the graphics asked for
		auto CheckShow = [&Sh, &Check, Tg](int32 Done)
		{
			bool bTimes = Sh.StageSecs.Num() >= Done;
			for (int32 i = 0; i < FMath::Min(Sh.StageSecs.Num(), Sh.Stages.Num()); ++i) bTimes = bTimes && FMath::Abs(Sh.StageSecs[i] - Sh.Stages[i].Secs) < 0.4f;
			Check(bTimes, FString::Printf(TEXT("the %s version ran its stages in order and to time (%d of %d, %.1f s)"), *Sh.Version, Sh.StageSecs.Num(), Sh.Stages.Num(), Sh.T), Tg);
			Check(Sh.BadFrames == 0, FString::Printf(TEXT("the lens never in the stands or a body (%d of %d frames bad)"), Sh.BadFrames, Sh.Frames), Tg);
			Check(Sh.bHeldClock, TEXT("the clocks held through the show"), Tg);
			Check(Sh.GfxAsked >= (Sh.Kind == 1 ? 1 : 2), FString::Printf(TEXT("the graphics asked for their panels (%d asked, %d there)"), Sh.GfxAsked, Sh.GfxShown), Tg);
		};
		// the stored highlights: shown in the show, their films under the memory cap
		auto CheckHighlights = [&Sh, &Check, &St, Tg]()
		{
			const double PerPlay = St.HlKept > 0 ? static_cast<double>(St.HlBytesPeak) / FMath::Max(1, St.Highlights.Num()) / 1048576.0 : 0.0;
			Check(Sh.HlWant >= 1 && Sh.HlShown >= Sh.HlWant, FString::Printf(TEXT("highlights shown (%d of %d planned; %d kept so far, %d dropped)"), Sh.HlShown, Sh.HlWant,
				St.HlKept, St.HlDropped), Tg);
			Check(St.HlCapBytes > 0 && St.HlBytesPeak <= St.HlCapBytes, FString::Printf(TEXT("the films under the cap (peak %.1f MB of %.0f MB, about %.1f MB a play)"),
				St.HlBytesPeak / 1048576.0, St.HlCapBytes / 1048576.0, PerPlay), Tg);
		};
		if (T.Mode == 5 && Sh.bOn && bMine && !T.bShowChecked && Sh.Stages.IsValidIndex(Sh.Stage) && Sh.Stages[Sh.Stage].Act == 5 &&
			Sh.StageT >= Sh.Stages[Sh.Stage].Secs * 0.7f)
		{
			// in the last stage (the units): the checks so far; the show then runs out by itself and hands on to the kickoff
			T.bShowChecked = true;
			CheckShow(Sh.Stages.Num() - 1);
			CheckHighlights();
			Check(Sh.FieldMenAtUnits == 0, FString::Printf(TEXT("the last play's men off the field for the units (%d left on it)"), Sh.FieldMenAtUnits), Tg);
			Check(Sh.MostKickSeen >= 9 && Sh.MostRecvSeen >= 9, FString::Printf(TEXT("both kickoff units on screen (at most %d and %d of 11)"), Sh.MostKickSeen, Sh.MostRecvSeen), Tg);
			Check(HalftimeView.bOn && HalftimeView.Stage == 4 && HalftimeView.bStatsUp, FString::Printf(TEXT("HalftimeNow: on, stage %d, stats asked %d"), HalftimeView.Stage, HalftimeView.bStatsUp ? 1 : 0), Tg);
			T.bAwaitSkip = true; // (now waiting for the end of the show and the kickoff)
			T.InjectT = FPlatformTime::Seconds();
			return;
		}
		if (T.Mode == 5 && T.bAwaitSkip && State != EMatchState::Wait && T.Clock > 0.f)
		{
			// the end of the show: every stage run out (no skip), the show off, the second half kicked off
			Check(!Sh.bOn && !T.bShowSkipped && Sh.StageSecs.Num() == Sh.Stages.Num() && !HalftimeView.bOn &&
				(State == EMatchState::Kick || State == EMatchState::Live || State == EMatchState::Presnap),
				FString::Printf(TEXT("the show ran out and handed on to the second half's kickoff (%d of %d stages, state %d)"), Sh.StageSecs.Num(), Sh.Stages.Num(),
					static_cast<int32>(State)), Tg);
			Check(Game && Game->q >= 3, FString::Printf(TEXT("the second half under way (quarter %d)"), Game ? Game->q : 0), Tg);
			T.bDone = true;
			TagResult::Finish(ModeName, T.Pass, T.Fail);
			return;
		}
		if (T.Mode == 6 && T.bShowSeen && !Sh.bOn && !T.bShowChecked && State == EMatchState::Final)
		{
			// the show over: the final box score is up, as before it
			T.bShowChecked = true;
			CheckShow(Sh.Stages.Num());
			CheckHighlights();
			Check(Sh.Pog != nullptr, FString::Printf(TEXT("a player of the game (%s, %s)"), Sh.Pog ? *BcS(Sh.Pog->last) : TEXT("-"), *Sh.PogLine), Tg);
			Check(Overlay && Overlay->HasModal(), TEXT("handed on to the final box score"), Tg);
			T.bDone = true;
			TagResult::Finish(ModeName, T.Pass, T.Fail);
			return;
		}
		if (T.Clock > 900.f || (T.Mode == 5 && T.bAwaitSkip && FPlatformTime::Seconds() - T.InjectT > 60.0))
		{
			Check(false, T.bShowSeen ? TEXT("the show did not finish in time") : TEXT("the show never began"), Tg);
			T.bDone = true;
			TagResult::Finish(ModeName, T.Pass, T.Fail);
		}
	}
	else if (T.Mode == 4)
	{
		const TCHAR* Tg = TEXT("opentest");
		if (T.OpenRound == 1 && PregameView.bOn && T.InjectAtOpenT >= 0.f && St.Open.T >= T.InjectAtOpenT && !T.bAwaitSkip)
		{
			T.InjectAtOpenT = -1.f;
			T.InjectFrame = GFrameCounter;
			T.InjectT = FPlatformTime::Seconds();
			T.SkipsAt = St.Skips;
			PadKey(EKeys::Gamepad_FaceButton_Bottom, true);
			T.bReleasePending = true;
			T.bAwaitSkip = true;
			return;
		}
		if (T.bReleasePending)
		{
			PadKey(EKeys::Gamepad_FaceButton_Bottom, false);
			T.bReleasePending = false;
		}
		if (T.bAwaitSkip && State != EMatchState::Wait)
		{
			// the kickoff is set up: how soon after the press
			const uint64 Frames = GFrameCounter - T.InjectFrame;
			const double Secs = FPlatformTime::Seconds() - T.InjectT;
			Check(St.Skips > T.SkipsAt && Frames <= 3 && Secs <= 0.3, FString::Printf(TEXT("one press reaches the kickoff (%llu frames, %.3f s after it was sent)"), Frames, Secs), Tg);
			T.bAwaitSkip = false;
			T.bDone = true;
			TagResult::Finish(TEXT("TagOpenTest"), T.Pass, T.Fail);
			return;
		}
		if (T.bAwaitSkip && FPlatformTime::Seconds() - T.InjectT > 2.0)
		{
			Check(false, TEXT("the press did not reach the kickoff within 2 s"), Tg);
			T.bDone = true;
			TagResult::Finish(TEXT("TagOpenTest"), T.Pass, T.Fail);
			return;
		}
		if (T.Clock > 120.f)
		{
			Check(false, TEXT("the open test ran out of time"), Tg);
			T.bDone = true;
			TagResult::Finish(TEXT("TagOpenTest"), T.Pass, T.Fail);
		}
	}
}

// ------------------------------------------------------------------ the summary, the end
void AMatchDirector::LogBroadcastSummary()
{
	if (!BcState) return;
	FTagBcState& St = *BcState;
	FString Types, Vars, Firsts;
	for (const TPair<FString, int32>& KV : St.TypeCount) Types += FString::Printf(TEXT(" %s %d"), *KV.Key, KV.Value);
	for (const TPair<FString, int32>& KV : St.VariantCount) Vars += FString::Printf(TEXT(" %s %d"), *KV.Key, KV.Value);
	for (const TPair<FString, int32>& KV : St.FirstByAngle) Firsts += FString::Printf(TEXT(" %s %d"), *KV.Key, KV.Value);
	UE_LOG(LogTemp, Display, TEXT("TAG shot summary: %d shots in %d plays; by kind:%s; longest run of one kind %d; three-shot sequences repeated within 20 plays %d; fallbacks to the wide %d; shots with a lens in a body %d; skips %d (refused %d)"),
		St.Shots, St.PlayN, *Types, St.MaxRun, St.RepeatSeqs, St.Fallbacks, St.ShotsBad, St.Skips, St.SkipsRefused);
	UE_LOG(LogTemp, Display, TEXT("TAG shot variants:%s"), *Vars);
	UE_LOG(LogTemp, Display, TEXT("TAG replay package summary: %d replays (%d with two angles); first angles:%s; cuts under the wipe %d, outside it %d, as it lifted %d; lens bad %d of %d frames"),
		St.Packages, St.TwoAngle, *Firsts, St.CutsCovered, St.CutsBare, St.CutsPartial, St.ReplayLensBad, St.ReplayFrames);
	for (const ETagPace P : {ETagPace::Quick, ETagPace::Standard, ETagPace::Full})
	{
		float Med, P90, BMed, BP90;
		BcStats(St.ToCall[BcPaceIdx(P)], Med, P90);
		BcStats(St.ToCallBig[BcPaceIdx(P)], BMed, BP90);
		if (St.ToCall[BcPaceIdx(P)].Num() || St.ToCallBig[BcPaceIdx(P)].Num())
			UE_LOG(LogTemp, Display, TEXT("TAG pace %s: whistle to the call, routine plays median %.2f s p90 %.2f s (%d), the rest median %.2f s p90 %.2f s (%d)"), TagPace::Name(P),
				Med, P90, St.ToCall[BcPaceIdx(P)].Num(), BMed, BP90, St.ToCallBig[BcPaceIdx(P)].Num());
	}
	tag::Recent::get().flush(); // (the store's picks kept for the next game)
}

void AMatchDirector::BroadcastEndPlay()
{
	if (Wipe.IsValid() && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Wipe.ToSharedRef());
	Wipe.Reset();
	tag::Recent::get().flush();
}

// ------------------------------------------------------------------ the field's graphics (04 A7)
void AMatchDirector::TickFieldOverlays(float Dt)
{
	if (!Stadium || !Game || !Camera) return;
	UStaticMeshComponent* LosC = Stadium->OverlayLine(0);
	UStaticMeshComponent* FdC = Stadium->OverlayLine(1);
	if (!LosC || !FdC) return;
	static const bool bLit = FParse::Param(FCommandLine::Get(), TEXT("TagLitLines"));    // A/B: the old lit lines
	static const bool bNoMark = FParse::Param(FCommandLine::Get(), TEXT("TagNoDownMark"));
	// the glow by day and by night (the night's exposure is held brighter: the ribbon boards run 3 at night, 5 by day)
	static float GlowDay = FParse::Value(FCommandLine::Get(), TEXT("TagLineGlow="), GlowDay) ? GlowDay : 1.6f;
	static float GlowNight = FParse::Value(FCommandLine::Get(), TEXT("TagLineGlowNight="), GlowNight) ? GlowNight : 0.9f;
	const FLinearColor Blue(0.1f, 0.35f, 1.f), Yellow(1.f, 0.82f, 0.f), Red(1.f, 0.07f, 0.04f);
	const tag::Game& G = *Game;
	if (OverlayStadium.Get() != Stadium.Get())
	{
		// a new stadium (every match builds its own): its two lines on the emissive panel material (M_TagLED as a solid
		// colour, its lit part turned down): a broadcast graphic, not paint lit by the sun or the floodlights
		OverlayStadium = Stadium.Get();
		LosLineMID = FdLineMID = nullptr;
		OverlayDown = -1;
		bOverlayHidden = false;
		UMaterialInterface* Base = FAthleteAssets::Get().LEDMat;
		if (!bLit && Base)
		{
			auto Make = [&](const FLinearColor& C)
			{
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, Stadium.Get());
				M->SetVectorParameterValue(TEXT("Color"), C);
				M->SetVectorParameterValue(TEXT("ColorB"), C);
				M->SetScalarParameterValue(TEXT("Count"), 0.f);
				M->SetScalarParameterValue(TEXT("Speed"), 0.f);
				M->SetScalarParameterValue(TEXT("Diffuse"), 0.2f);
				M->SetScalarParameterValue(TEXT("Roughness"), 0.9f);
				M->SetScalarParameterValue(TEXT("Intensity"), bBoothNight ? GlowNight : GlowDay);
				return M;
			};
			LosLineMID = Make(Blue);
			FdLineMID = Make(Yellow);
			LosC->SetMaterial(0, LosLineMID);
			FdC->SetMaterial(0, FdLineMID);
		}
	}
	// the line to gain is red on fourth down (styled as the lines are put down: the play call, the pre-snap)
	if (State == EMatchState::Call || State == EMatchState::Presnap)
	{
		const int32 Want = G.phase == tag::GamePhase::Scrimmage && !bTwoPoint ? G.down : 0;
		if ((Want == 4) != (OverlayDown == 4) || OverlayDown < 0)
		{
			const FLinearColor C = Want == 4 ? Red : Yellow;
			if (FdLineMID)
			{
				FdLineMID->SetVectorParameterValue(TEXT("Color"), C);
				FdLineMID->SetVectorParameterValue(TEXT("ColorB"), C);
			}
			else FdC->SetMaterial(0, TagColorMat(Stadium.Get(), C, 0.3f));
		}
		OverlayDown = Want;
	}
	// nothing of it in a replay (the lines mark the next play, not the one on the film), a cut-in, a close shot, a
	// between-play shot, the open or a break's wide shot. (Hidden in game: SetLines' own showing and hiding is left as it is.)
	const bool bHide = State == EMatchState::Replay || ReplayView.bOn || (CutInTarget && CutInLeft > 0.f) || CloseupTarget || BreakLeft > 0.f || bSetAfterBreak || ShotView.bOn ||
		PregameView.bOn;
	if (bHide != bOverlayHidden)
	{
		bOverlayHidden = bHide;
		LosC->SetHiddenInGame(bHide);
		FdC->SetHiddenInGame(bHide);
	}
	// the down and distance on the turf beside the line before the snap ("3RD & 7"), gone at the snap
	const bool bMark = !bNoMark && !bHide && State == EMatchState::Presnap && Sim && !Sim->cfg.kick && !Sim->cfg.place && !bTwoPoint &&
		G.phase == tag::GamePhase::Scrimmage;
	if (bMark && !DownMark)
	{
		DownMark = NewObject<UTextRenderComponent>(this);
		DownMark->SetupAttachment(RootComponent);
		DownMark->SetHorizontalAlignment(EHTA_Center);
		DownMark->SetVerticalAlignment(EVRTA_TextCenter);
		DownMark->SetWorldSize(130.f);
		DownMark->SetTextRenderColor(FColor(245, 245, 238));
		DownMark->SetCastShadow(false);
		DownMark->SetVisibility(false);
		DownMark->RegisterComponent();
	}
	if (!DownMark) return;
	if (bMark)
	{
		auto Ord = [](int32 D) -> const TCHAR* { return D <= 1 ? TEXT("1ST") : D == 2 ? TEXT("2ND") : D == 3 ? TEXT("3RD") : TEXT("4TH"); };
		const bool bGoal = G.ballOn + G.toGo >= 100.0 - 1e-6;
		const FString Txt = FString::Printf(TEXT("%s & %s"), Ord(G.down), bGoal ? TEXT("GOAL") : G.toGo < 0.95 ? TEXT("INCHES") : *FString::FromInt(FMath::RoundToInt(G.toGo)));
		if (Txt != DownMarkText)
		{
			DownMarkText = Txt;
			DownMark->SetText(FText::FromString(Txt));
		}
		// on the offense's side of the line, toward the wide side of the field (clear of the ball and the interior line),
		// lying on the turf and turned to read upright from the camera
		const double Y = YD;
		const FVector LosW = ToWorld(Sim->los, Sim->by, Dir);
		const double Wide = LosW.Y > 0.0 ? -1.0 : 1.0;
		const double Lim = (tag::FIELD_W / 2 - 4) * Y;
		const FVector At(LosW.X - Dir * 2.5 * Y, FMath::Clamp(LosW.Y + Wide * 13.0 * Y, -Lim, Lim), 1.5);
		FVector Away = At - Camera->GetActorLocation();
		Away.Z = 0.0;
		Away = Away.GetSafeNormal();
		if (Away.IsNearlyZero()) Away = FVector(static_cast<double>(Dir), 0.0, 0.0);
		// (a text render faces its +X and stands up its +Z: faced to the sky, its top away from the lens)
		DownMark->SetWorldLocationAndRotation(At, FRotationMatrix::MakeFromXZ(FVector::UpVector, Away).Rotator());
	}
	if (DownMark->IsVisible() != bMark) DownMark->SetVisibility(bMark);
}

// ------------------------------------------------------------------ the stands (05 A2)
void AMatchDirector::HomeCheer(float Level, tag::TeamKey For)
{
	// (no sound, so no crowd mood to follow: the home side's own big moments only)
	if (!Audio && Stadium && For == tag::TeamKey::Home) Stadium->Hype = FMath::Max(Stadium->Hype, Level);
}

void AMatchDirector::TickStands()
{
	if (!Stadium || !Audio || !Game || State == EMatchState::Menu || bLineup) return;
	// the fans on their feet when the building roars, seated when it groans (the stands' thresholds: up past 0.5, down
	// under 0.15 - Stadium's UpdateHeroCrowd / UpdateMassFans)
	Stadium->Hype = Audio->CrowdEnergy();
	static const bool bLog = FParse::Param(FCommandLine::Get(), TEXT("TagAudioLog"));
	const bool bUp = bStandsUp ? Stadium->Hype > 0.15f : Stadium->Hype > 0.5f;
	if (bUp != bStandsUp)
	{
		bStandsUp = bUp;
		if (bLog) UE_LOG(LogTemp, Display, TEXT("TAG stands %s: hype %.2f (q%d %.0f, %d-%d)"), bUp ? TEXT("up") : TEXT("down"), Stadium->Hype, Game->q, Game->clock,
			Game->score.away, Game->score.home);
	}
}

void AMatchDirector::TickBroadcast(float Dt)
{
	TickFieldOverlays(Dt);
	TickStands();
	if (Wipe.IsValid()) Wipe->Advance(Dt); // (real seconds: a watched game's 2x / 4x does not hurry the wipe)
	TickOpen(Dt);
	TickShow(Dt);    // (the halftime and postgame shows)
	TickLeavers(Dt); // (the men walking off: substitutes, the half)
	if (BcState)
	{
		// the officials out of the open's, the halftime show's and a highlight's pictures (the umpire stood between the
		// kickoff units, in the second unit's shot)
		const bool bHideRefs = PregameView.bOn || HalftimeView.bOn || BcState->bHlOn;
		if (bHideRefs != BcState->bRefsHidden)
		{
			BcState->bRefsHidden = bHideRefs;
			if (Umpire) Umpire->SetActorHiddenInGame(bHideRefs);
			for (AAthlete* O : Crew)
				if (O) O->SetActorHiddenInGame(bHideRefs);
		}
	}
	TickBeats(Dt * (bWatch ? Speed : 1.f));
	TickBroadcastTests(Dt);
	{
		// -TagOpenShots: a frame (with the HUD) every second from the match's start to the opening kickoff - the open, or
		// with -TagNoOpen the old wait for the booth - as tag_open_<n>.png, and a log line of what is on screen
		static const bool bOpenShots = FParse::Param(FCommandLine::Get(), TEXT("TagOpenShots"));
		static int32 OpenShotN = 0;
		static double NextOpenShot = 0.0;
		static bool bOpenOver = false;
		if (bOpenShots && Game && State != EMatchState::Wait) bOpenOver = true; // (the kickoff: done)
		if (bOpenShots && !bOpenOver && Game && State == EMatchState::Wait && OpenShotN < 90 && FPlatformTime::Seconds() >= NextOpenShot)
		{
			NextOpenShot = FPlatformTime::Seconds() + 1.0;
			int32 Shown = 0;
			for (const auto& KV : Athletes) Shown += KV.Value && !KV.Value->IsHidden() ? 1 : 0;
			UE_LOG(LogTemp, Display, TEXT("TAG open shot %d: open %s stage %d, %d field players shown, booth holding %s, camera %s lens %.0f mm"), OpenShotN,
				PregameView.bOn ? TEXT("on") : TEXT("off"), PregameView.Stage, Shown, BoothHold() ? TEXT("yes") : TEXT("no"),
				Camera ? *Camera->GetActorLocation().ToCompactString() : TEXT("-"), Camera && Camera->GetCineCameraComponent() ? Camera->GetCineCameraComponent()->CurrentFocalLength : 0.f);
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_open_%02d.png"), OpenShotN++), true, false);
		}
	}
}
