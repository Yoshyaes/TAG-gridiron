#include "Game/MatchDirector.h"
#include "Game/TagBindings.h"
#include "Game/TackleMomentum.h"
#include "Game/TagResult.h"
#include "Game/TagLatency.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"

#include "Render/Athlete.h"
#include "Render/Stadium.h"
#include "Render/Coords.h"
#include "Render/PlayArt.h"
#include "Render/TagMotionMatchInstance.h"
#include "UObject/UObjectIterator.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Game/PlayArtStrokes.h"
#include "TimerManager.h"
#include "UI/GridironOverlay.h"
#include "UI/LoadingScreen.h"
#include "Booth/TagBooth.h"
#include "Audio/TagAudio.h"
#include "MoviePlayer.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "Engine/StaticMeshActor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Sim/Playbook.h"
#include "Sim/Roster.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Kismet/GameplayStatics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UnrealClient.h"
#include "AssetCompilingManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

using namespace TagCoords;

namespace
{
	constexpr float STEP = 1.f / 60.f;
	FString S(const std::string& s) { return FString(s.c_str()); }
	const FLinearColor Good(0.24f, 0.86f, 0.52f);
	const FLinearColor Bad(1.f, 0.35f, 0.35f);
	const FLinearColor Flag(1.f, 0.69f, 0.f);
}

// -TagTempoTest (ReadTempoInput)
static bool TempoTest() { static const bool b = FParse::Param(FCommandLine::Get(), TEXT("TagTempoTest")); return b; }

// -TagAnimStats: the measured-only counters (officials among the players before the snap, limbs inside bodies in a
// pile) and their log lines; off unless asked for (they read bones and walk every body each frame)
static bool AnimStats() { static const bool b = FParse::Param(FCommandLine::Get(), TEXT("TagAnimStats")); return b; }

// the pre-snap flag's captures (CallPresnapFlag; loaded under the loading screen, WarmMatchAssets): a lineman's false
// start, a back's or a receiver's, a 4-point D-lineman's offside, a 3-point one's
static const TCHAR* const PreFlagClips[4] = {
	TEXT("A_TAG_Pack_madnjun04_0169_1_OL_Flinch_from_3_pt_stance__P01"),
	TEXT("A_TAG_Pack_MADNMAY04_1589_1_HB_Stance_2Point_Subtle_False_Start_variation1__P01"),
	TEXT("A_TAG_Pack_madnaug04_0116_1_1_Pre_Play_Dead_Ball_Trans_DL_4pt_TO_Obv_Offsides_TO_Stand_and_argue__P01"),
	TEXT("A_TAG_Pack_MADNAUG04_0114_2_2_Pre_Play_Dead_Ball_Trans_DL_3pt_TO_Obv_Offsides_TO_Stand_argue__P01")};

AMatchDirector::AMatchDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true; // the pause menu's own input (Esc / OPTIONS to resume)
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

// The pads plugged in or paired right now, from RawInput's device list: bit 0 a Sony pad, bit 1 a Microsoft (Xbox) one.
// No engine state: TrackInputDevice runs it on a worker thread (the enumeration stalled the game thread every 2 s).
static int32 TagScanPads()
{
	bool bFound = false, bXbox = false;
#if PLATFORM_WINDOWS
	{
		UINT N = 0;
		GetRawInputDeviceList(nullptr, &N, sizeof(RAWINPUTDEVICELIST));
		TArray<RAWINPUTDEVICELIST> L;
		L.SetNumZeroed(N);
		if (N && GetRawInputDeviceList(L.GetData(), &N, sizeof(RAWINPUTDEVICELIST)) != (UINT)-1)
			for (const RAWINPUTDEVICELIST& D : L)
			{
				if (D.dwType != RIM_TYPEHID) continue;
				UINT Len = 0;
				GetRawInputDeviceInfoW(D.hDevice, RIDI_DEVICENAME, nullptr, &Len);
				TArray<WCHAR> Name;
				Name.SetNumZeroed(Len + 1);
				GetRawInputDeviceInfoW(D.hDevice, RIDI_DEVICENAME, Name.GetData(), &Len);
				// Sony's vendor id, as USB ("VID_054C") and Bluetooth ("VID&0002054C") name it
				const FString Dev = FString(Name.GetData()).ToUpper();
				if (Dev.Contains(TEXT("VID_054C")) || Dev.Contains(TEXT("VID&0002054C")) || Dev.Contains(TEXT("VID&054C"))) bFound = true;
				// and Microsoft's (an Xbox pad: USB "VID_045E", Bluetooth "VID&02045E")
				if (Dev.Contains(TEXT("VID_045E")) || Dev.Contains(TEXT("VID&0002045E")) || Dev.Contains(TEXT("VID&02045E")) || Dev.Contains(TEXT("VID&045E"))) bXbox = true;
			}
	}
#endif
	return (bFound ? 1 : 0) | (bXbox ? 2 : 0);
}

bool AMatchDirector::FindPlayStationPad()
{
	// -TagFakePad=ps|xbox: act as if that pad were plugged in (look-dev of the button glyphs)
	{
		FString Fake;
		if (FParse::Value(FCommandLine::Get(), TEXT("TagFakePad="), Fake))
		{
			bXboxPad = Fake == TEXT("xbox");
			return Fake == TEXT("ps");
		}
	}
	const int32 Bits = TagScanPads();
	bXboxPad = (Bits & 2) != 0;
	return (Bits & 1) != 0;
}

void AMatchDirector::BeginPlay()
{
	Super::BeginPlay();
	PostTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &AMatchDirector::OnPostActorTick);
	TagResult::StartWatchdog(); // -TagTimeout=<s>: a test run that hangs ends with status 2 (Game/TagResult.h)
	FTagLatency::Get().Start(); // the frame-pacing A/B flags, the latency log (Game/TagLatency.h)
	ATagBooth::WarmService(); // the booth's voices load while the menus are up
	if (!FParse::Param(FCommandLine::Get(), TEXT("TagNoAudio"))) Audio = GetWorld()->SpawnActor<ATagAudio>();
	// controllers: GameInput (GameInputWindows plugin) reads PS5 DualSense pads as standard gamepads, XInput pads too;
	// all of them drive the one player (Config/Windows/WindowsInput.ini). Note whether a Sony pad is plugged in so the
	// hints can name its buttons.
	bPlayStationPad = FindPlayStationPad();
	UE_LOG(LogTemp, Display, TEXT("TAG controllers: PlayStation pad %s, Xbox pad %s, device mapping policy %d"), bPlayStationPad ? TEXT("found") : TEXT("not found"), bXboxPad ? TEXT("found") : TEXT("not found"),
		static_cast<int32>(IPlatformInputDeviceMapper::Get().GetCurrentDeviceMappingPolicy()));
	bPadActive = bPlayStationPad || bXboxPad; // until the player touches something
	{
		// -TagTune=name=value[,name=value...]: sim constants for a filmed check (e.g. scrambleK=20 to see quarterbacks run)
		FString Tunes;
		if (FParse::Value(FCommandLine::Get(), TEXT("TagTune="), Tunes, false))
		{
			TArray<FString> Parts;
			Tunes.ParseIntoArray(Parts, TEXT(","));
			for (const FString& P : Parts)
			{
				FString K, V;
				if (P.Split(TEXT("="), &K, &V)) UE_LOG(LogTemp, Display, TEXT("TAG tune %s = %s (%s)"), *K, *V, tag::setTune(TCHAR_TO_UTF8(*K), FCString::Atod(*V)) || tag::setCallTune(TCHAR_TO_UTF8(*K), FCString::Atod(*V)) ? TEXT("set") : TEXT("unknown"));
			}
		}
	}
	if (GConfig) GConfig->GetBool(TEXT("TAG.Settings"), TEXT("Captions"), bOptCaptions, GGameUserSettingsIni);
	if (GConfig) GConfig->GetBool(TEXT("TAG.Settings"), TEXT("InvertRightY"), bOptInvertRY, GGameUserSettingsIni);
	{
		TArray<FKey> All;
		EKeys::GetAllKeys(All);
		for (const FKey& K : All)
		{
			if (K.IsAxis1D() || K.IsAxis2D() || K.IsAxis3D() || K.IsTouch() || K.IsGesture()) continue;
			if (K.IsGamepadKey()) PadKeys.Add(K);
			else if (!K.IsMouseButton()) KeyboardKeys.Add(K);
		}
	}
	League = tag::createLeague(2027);
	// the user's rosters: the created players on their teams, the men cut, the depth charts (UI/CreatePlayer.cpp)
	SGridironOverlay::LoadRosterEdits(this);
	// the user's Superstar / X-Factor loadouts over the coach's picks (-TagCoachPicks: the coach's picks only)
	if (!FParse::Param(FCommandLine::Get(), TEXT("TagCoachPicks"))) SGridironOverlay::LoadAbilityLoadouts(this);
	FAthleteAssets::Get().Load();

	Camera = GetWorld()->SpawnActor<ACineCameraActor>(ACineCameraActor::StaticClass(), FTransform(FRotator(-20, -90, 0), CamPos));
	if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
	{
		C->Filmback.SensorWidth = 36.f;
		C->Filmback.SensorHeight = 20.25f;
		C->CurrentFocalLength = 32.f;
		C->CurrentAperture = 5.6f;
		C->FocusSettings.FocusMethod = ECameraFocusMethod::Manual;
	}
	Ball = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform::Identity);
	Ball->SetMobility(EComponentMobility::Movable);
	const FAthleteAssets& GA = FAthleteAssets::Get();
	UStaticMeshComponent* BC = Ball->GetStaticMeshComponent();
	BC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (GA.FootballMesh)
	{
		// TAG football (Tools/Art/gear_build.py): pointed tips and white laces, authored at real size
		BC->SetStaticMesh(GA.FootballMesh);
		BC->SetMaterial(0, TagColorMat(this, FLinearColor(0.16f, 0.055f, 0.018f), 0.5f));
		BC->SetMaterial(1, TagColorMat(this, FLinearColor(0.9f, 0.9f, 0.88f), 0.6f));
		Ball->SetActorScale3D(FVector::OneVector);
	}
	else
	{
		BC->SetStaticMesh(GA.Sphere);
		BC->SetMaterial(0, TagColorMat(this, FLinearColor(0.2f, 0.07f, 0.02f), 0.55f));
		Ball->SetActorScale3D(FVector(0.28f, 0.17f, 0.17f));
	}

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->SetViewTarget(Camera);
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = true;
	}
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->AddViewportWidgetContent(SAssignNew(Overlay, SGridironOverlay).Director(this), 10);
	}
	tag::Weather W;
	W.tempF = 60;
	BuildWorld(*League[0], *League[1], W, true);
	FAthleteAssets::Get().WaitMotionMatching(); // (the databases loaded beside the build; what is left lands here, not over the menu's frames)
	ShowMainMenu();
	if (Overlay) Overlay->SetPadKind(TagBind::Sony(bPlayStationPad));
	if (Overlay) Overlay->Toast(bPlayStationPad ? TEXT("PS5 controller connected: PlayStation buttons") : bXboxPad ? TEXT("Xbox controller connected: Xbox buttons")
		: TEXT("No controller found: keyboard (plug in a PS5 or Xbox controller any time)"));
	FTagInputHub::Get(); // (every pad and the keyboard, each on its own: local multiplayer)
	// -TagMultiTest: a scripted four-player game on made-up devices, from the lobby (TickMultiTest)
	bMultiTest = FParse::Param(FCommandLine::Get(), TEXT("TagMultiTest"));
	if (FParse::Param(FCommandLine::Get(), TEXT("TagAutoWatch")))
	{
		bAuto = true;
		int32 Wx = 0;
		FParse::Value(FCommandLine::Get(), TEXT("TagWeather="), Wx);
		const bool bDay = FParse::Param(FCommandLine::Get(), TEXT("TagDay"));
		StartMatch(3, 7, false, true, Wx, !bDay, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagCallShot")))
	{
		bCallShot = true;
		StartMatch(3, 7, true, true, 0, true, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagMoveShot")))
	{
		bMoveShot = true;
		StartMatch(3, 7, true, true, 0, false, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagPlayNow")) || FParse::Param(FCommandLine::Get(), TEXT("TagPauseTest")))
	{
		// straight into a user game, once the startup movie has played (the loading screen then shows, as from the menu)
		bPauseTest = FParse::Param(FCommandLine::Get(), TEXT("TagPauseTest"));
		bPlayNowPending = true;
	}
	else if (FParse::Value(FCommandLine::Get(), TEXT("TagSoak="), SoakSecs) && SoakSecs > 0.f)
	{
		double QLen = 900.0;
		FParse::Value(FCommandLine::Get(), TEXT("TagQuarterLen="), QLen);
		SoakStart = FPlatformTime::Seconds();
		bWatch = true;
		int32 SoakWx = 1; // dry unless -TagWeather= asks for wind (2), rain (3) or snow (4)
		FParse::Value(FCommandLine::Get(), TEXT("TagWeather="), SoakWx);
		int32 SoakHome = 3, SoakAway = 7;  // -TagHome= / -TagAway=: league indexes (0..31)
		FParse::Value(FCommandLine::Get(), TEXT("TagHome="), SoakHome);
		FParse::Value(FCommandLine::Get(), TEXT("TagAway="), SoakAway);
		const int32 NT = static_cast<int32>(League.size());
		SoakHome = FMath::Clamp(SoakHome, 0, NT - 1);
		SoakAway = FMath::Clamp(SoakAway, 0, NT - 1);
		if (SoakAway == SoakHome) SoakAway = (SoakHome + 1) % NT;
		StartMatch(SoakHome, SoakAway, false, true, SoakWx, true, QLen);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagTackleCam")))
	{
		bTackleCam = true;
		bWatch = true;
		FParse::Value(FCommandLine::Get(), TEXT("TagTcClips="), TcMaxClips);
		if (Overlay) Overlay->SetBroadcastOnly(true);
		double QLen = 900.0;
		FParse::Value(FCommandLine::Get(), TEXT("TagQuarterLen="), QLen);
		StartMatch(3, 7, false, true, 1, true, QLen);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagHighlights")))
	{
		bHighlights = true;
		HlStart = FPlatformTime::Seconds();
		if (Overlay) Overlay->SetBroadcastOnly(true);
		bHlFull = FParse::Param(FCommandLine::Get(), TEXT("TagHlFull"));
		double HlQLen = 900.0;
		FParse::Value(FCommandLine::Get(), TEXT("TagQuarterLen="), HlQLen);
		int32 HlWx = 1;
		FParse::Value(FCommandLine::Get(), TEXT("TagWeather="), HlWx);
		// -TagHome=<n> -TagAway=<n>: film any two teams (league order: 0 AUS, 1 POR, 3 OKC, 7 MEM, ...)
		int32 HlHome = 3, HlAway = 7;
		FParse::Value(FCommandLine::Get(), TEXT("TagHome="), HlHome);
		FParse::Value(FCommandLine::Get(), TEXT("TagAway="), HlAway);
		StartMatch(HlHome, HlAway, false, true, HlWx, !FParse::Param(FCommandLine::Get(), TEXT("TagDay")), HlQLen); // (-TagDay films a day game)
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagFeelTest")))
	{
		// the feel checks (Game/MatchFeelTest.cpp): a user game, his calls made for him, the CPU kicking for him
		bFeelTest = true;
		bPadTest = true;
		StartMatch(3, 7, true, true, 1, true, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagHitStopTest")))
	{
		// a watched CPU game with a hit-stop on every big hit, its play log's digest against the -TagNoHitStop run's
		// (Game/MatchFeelTest.cpp; give both runs the same -TagWeatherSeed=)
		bHitStopTest = true;
		bWatch = true;
		double HsQLen = 120.0;
		FParse::Value(FCommandLine::Get(), TEXT("TagQuarterLen="), HsQLen);
		StartMatch(3, 7, false, true, 1, true, HsQLen);
	}
	else if (BeginBroadcastTest()) {} // (-TagRecentTest, -TagReplayTest, -TagShotTest, -TagPaceTest, -TagOpenTest: Game/MatchBroadcast.cpp)
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagPadPlayTest")))
	{
		bPadPlayTest = true;
		bPadTest = true; // (no kick meter: the CPU kicks for the user)
		StartMatch(3, 7, true, true, 0, true, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagArtTest")))
	{
		bArtTest = true;
		bPadTest = FParse::Param(FCommandLine::Get(), TEXT("TagPadTest"));
		StartMatch(3, 7, true, true, 0, true, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagMoveTest")))
	{
		bLineup = true;
		bMoveTest = true;
		StartMatch(3, 7, false, true, 0, false, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagTackleTest")))
	{
		bLineup = true;
		bTackleTest = true;
		StartMatch(3, 7, false, true, 0, false, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagLineTest")))
	{
		bLineup = true;
		bLineTest = true;
		StartMatch(3, 7, false, true, 0, false, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagGapTest")))
	{
		bLineup = true;
		bGapTest = true;
		StartMatch(3, 7, false, true, 0, false, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagLineup")))
	{
		// look-dev lineup: six athletes (home and away, every body type) in a row, shot front then back, then exit.
		// -TagWeather=<n> and -TagDay as -TagAutoWatch reads them (a night game unless -TagDay), so the look matrix's
		// close-ups are of each condition
		bLineup = true;
		int32 LineupWx = 0;
		FParse::Value(FCommandLine::Get(), TEXT("TagWeather="), LineupWx);
		StartMatch(3, 7, false, true, LineupWx, !FParse::Param(FCommandLine::Get(), TEXT("TagDay")), 900);
	}
}

/**
 * -TagLineup -TagHeroGallery[=<Kind>]: every model pack character in the catalog, page by page (10 x 4, one kind per
 * page), each in its bind-sized cell playing its skeleton family's idle, shot to tag_gallery_<kind>_<page>.png. After
 * the pose settles each character's rendered bounds are checked against its bind-pose bounds: a body stretched by a
 * skinning / retarget fault (the 100x pelvis kind) is logged "TAG gallery SUSPICIOUS". Exits when done.
 */
static bool TickHeroGallery(UWorld* World, ACineCameraActor* Camera, float Dt)
{
	static bool bOn = FParse::Param(FCommandLine::Get(), TEXT("TagHeroGallery")) || FCommandLine::Get() && FCString::Strifind(FCommandLine::Get(), TEXT("TagHeroGallery="));
	if (!bOn || !World || !Camera) return false;
	FAthleteAssets& A = FAthleteAssets::Get();
	struct FPage { FString Kind; TArray<int32> Items; };
	static TArray<FPage> Pages;
	static int32 PageI = -1, Suspicious = 0, Shown = 0;
	static float T = -3.f;
	static TWeakObjectPtr<AActor> Holder;
	static TArray<TWeakObjectPtr<USkeletalMeshComponent>> Comps;
	static TArray<FVector> RefExt;
	static bool bChecked = false;
	if (PageI == -1)
	{
		FString Only;
		FParse::Value(FCommandLine::Get(), TEXT("TagHeroGallery="), Only);
		static const TCHAR* Order[] = {TEXT("Kit"), TEXT("Helmet"), TEXT("Cap"), TEXT("Head"), TEXT("CoachHead"), TEXT("CoachFull"), TEXT("Coach"), TEXT("Ref"),
			TEXT("RefHead"), TEXT("Staff"), TEXT("SidePlayer"), TEXT("Crew"), TEXT("Fan"), TEXT("FanProp"), TEXT("Story")};
		for (const TCHAR* K : Order)
		{
			if (!Only.IsEmpty() && Only != K) continue;
			const TArray<int32> L = A.HeroOf(K);
			for (int32 i = 0; i < L.Num(); i += 40)
			{
				FPage P;
				P.Kind = K;
				for (int32 j = i; j < FMath::Min(i + 40, L.Num()); ++j) P.Items.Add(L[j]);
				Pages.Add(P);
			}
		}
		PageI = 0;
		UE_LOG(LogTemp, Display, TEXT("TAG gallery %d pages over %d catalog items"), Pages.Num(), A.HeroItems.Num());
	}
	T += Dt;
	if (T < 0.f) return true;
	if (PageI >= Pages.Num())
	{
		if (T > 1.f)
		{
			UE_LOG(LogTemp, Display, TEXT("TAG gallery done: %d characters shown, %d suspicious"), Shown, Suspicious);
			FPlatformMisc::RequestExit(false);
		}
		return true;
	}
	if (Comps.Num() == 0)
	{
		// lay the page out: 10 columns x 4 rows in a wall facing the camera (-X), cells sized to the largest bind bounds
		if (!Holder.IsValid())
		{
			AActor* H = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
			USceneComponent* R = NewObject<USceneComponent>(H, TEXT("Root"));
			H->SetRootComponent(R);
			R->RegisterComponent();
			Holder = H;
		}
		const FPage& P = Pages[PageI];
		float CW = 30.f, CH = 30.f;
		for (int32 It : P.Items)
			if (USkeletalMesh* M = A.HeroMesh(It))
			{
				const FBoxSphereBounds B = M->GetBounds();
				CW = FMath::Max(CW, static_cast<float>(FMath::Max(B.BoxExtent.X, B.BoxExtent.Y)) * 2.f + 12.f);
				CH = FMath::Max(CH, static_cast<float>(B.BoxExtent.Z) * 2.f + 12.f);
			}
		const FVector Wall(50 * YD + 600.f, 0, 0);
		RefExt.Reset();
		for (int32 k = 0; k < P.Items.Num(); ++k)
		{
			USkeletalMesh* M = A.HeroMesh(P.Items[k]);
			if (!M) continue;
			USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(Holder.Get());
			C->SetSkeletalMesh(M);
			C->SetupAttachment(Holder->GetRootComponent());
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			C->RegisterComponent();
			const FBoxSphereBounds B = M->GetBounds();
			const int32 Col = k % 10, Row = k / 10;
			const FVector Cell = Wall + FVector(0, (Col - 4.5f) * CW, 40.f + (3 - Row) * CH + CH * 0.5f);
			const FRotator Rot(0, 90.f, 0); // the pack characters face +Y; this turns them to the camera
			C->SetWorldRotation(Rot);
			C->SetWorldLocation(Cell - Rot.RotateVector(B.Origin));
			// a sample team (blue / gold) on every team-painted slot, as the game paints them
			for (int32 Mi = 0; Mi < C->GetNumMaterials(); ++Mi)
			{
				float Mode = 0.f;
				UMaterialInterface* Cur = C->GetMaterial(Mi);
				if (!Cur || !Cur->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Mode")), Mode) || Mode < 0.5f) continue;
				if (UMaterialInstanceDynamic* D = C->CreateDynamicMaterialInstance(Mi))
				{
					const bool bBottom = C->GetMaterialSlotNames().IsValidIndex(Mi) && C->GetMaterialSlotNames()[Mi].ToString().StartsWith(TEXT("Bottom"));
					D->SetVectorParameterValue(TEXT("Tint"), bBottom ? FLinearColor(0.55f, 0.47f, 0.33f) : FLinearColor(0.05f, 0.18f, 0.6f));
					D->SetVectorParameterValue(TEXT("Trim"), FLinearColor(0.9f, 0.65f, 0.05f));
					D->SetVectorParameterValue(TEXT("Trim2"), FLinearColor(0.05f, 0.18f, 0.6f));
				}
			}
			const FString Rig = A.HeroItems[P.Items[k]].Rig;
			const bool bWhole = !(P.Kind == TEXT("Head") || P.Kind == TEXT("CoachHead") || P.Kind == TEXT("RefHead") || P.Kind == TEXT("Helmet") ||
				P.Kind == TEXT("Cap") || P.Kind == TEXT("FanProp"));
			if (bWhole)
				if (UAnimationAsset* Idle = A.HeroClip(Rig, TEXT("Idle")))
				{
					C->PlayAnimation(Idle, true);
					if (UAnimSingleNodeInstance* I = C->GetSingleNodeInstance()) I->SetPosition(FMath::Frac(k * 0.37f) * Idle->GetPlayLength(), false);
				}
			Comps.Add(C);
			RefExt.Add(B.BoxExtent);
		}
		const float WallW = 10 * CW, WallH = 4 * CH;
		const float Dist = FMath::Max(WallW * 0.62f, WallH * 1.05f) + 150.f;
		Camera->SetActorLocation(Wall + FVector(-Dist, 0, 40.f + WallH * 0.5f));
		Camera->SetActorRotation(FRotator::ZeroRotator);
		if (UCineCameraComponent* Cc = Camera->GetCineCameraComponent())
		{
			Cc->CurrentFocalLength = 24.f;
			Cc->FocusSettings.ManualFocusDistance = Dist;
		}
		T = 0.f;
		bChecked = false;
		UE_LOG(LogTemp, Display, TEXT("TAG gallery page %d %s: %d items (cell %.0f x %.0f cm)"), PageI, *P.Kind, Comps.Num(), CW, CH);
		return true;
	}
	if (!bChecked && T > 1.8f)
	{
		bChecked = true;
		const FPage& P = Pages[PageI];
		for (int32 k = 0; k < Comps.Num(); ++k)
		{
			USkeletalMeshComponent* C = Comps[k].Get();
			if (!C) continue;
			++Shown;
			const FVector E = C->Bounds.BoxExtent;
			const FVector R = RefExt[k];
			const float Grow = static_cast<float>(FMath::Max3(E.X / FMath::Max(R.X, 5.0), E.Y / FMath::Max(R.Y, 5.0), E.Z / FMath::Max(R.Z, 5.0)));
			if (E.GetMax() > R.GetMax() * 1.6 + 40.0 || !FMath::IsFinite(E.X))
			{
				++Suspicious;
				UE_LOG(LogTemp, Warning, TEXT("TAG gallery SUSPICIOUS %s (%s): bounds %s vs bind %s (x%.1f)"), *A.HeroItems[P.Items[k]].Name, *P.Kind,
					*E.ToCompactString(), *R.ToCompactString(), Grow);
			}
		}
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_gallery_%s_%02d.png"), *P.Kind, PageI), false, false);
	}
	if (T > 2.6f)
	{
		for (TWeakObjectPtr<USkeletalMeshComponent>& C : Comps)
			if (C.IsValid()) C->DestroyComponent();
		Comps.Reset();
		++PageI;
		T = 0.f;
	}
	return true;
}

void AMatchDirector::TickLineup(float Dt)
{
	LineupT += Dt;
	if (Lineup.Num() > 0 && TickHeroGallery(GetWorld(), Camera, Dt))
	{
		for (AAthlete* L : Lineup)
			if (L) L->SetActorHiddenInGame(true);
		return;
	}
	if (Lineup.Num() == 0)
	{
		const tag::Pos Pick[] = {tag::Pos::OT, tag::Pos::DE, tag::Pos::LB, tag::Pos::RB, tag::Pos::WR, tag::Pos::QB};
		const tag::Pos MovePick[] = {tag::Pos::WR, tag::Pos::RB, tag::Pos::CB, tag::Pos::OT};
		const tag::Pos GapPick[] = {tag::Pos::QB, tag::Pos::RB, tag::Pos::WR, tag::Pos::LB, tag::Pos::CB};
		const tag::TeamKey GapSide[] = {tag::TeamKey::Home, tag::TeamKey::Home, tag::TeamKey::Home, tag::TeamKey::Away, tag::TeamKey::Away};
		const tag::TeamKey Side[] = {tag::TeamKey::Home, tag::TeamKey::Away, tag::TeamKey::Home, tag::TeamKey::Away, tag::TeamKey::Home, tag::TeamKey::Away};
		for (int32 i = 0; i < ((bGapTest || bTackleTest) ? 5 : bMoveTest ? 4 : 6); ++i)
		{
			const tag::TeamKey Sd = (bGapTest || bTackleTest) ? GapSide[i] : Side[i];
			const std::vector<tag::Player*> D = tag::depth(Game->teamOf(Sd), (bGapTest || bTackleTest) ? GapPick[i] : bMoveTest ? MovePick[i] : Pick[i]);
			if (!D.empty()) Lineup.Add(AthleteFor(D[0], Sd));
		}
		for (auto& KV : Athletes)
		{
			KV.Value->SetActorHiddenInGame(!Lineup.Contains(KV.Value));
			KV.Value->SetDormant(!Lineup.Contains(KV.Value)); // (the opening kickoff's men, not in the lineup: asleep)
		}
		if (Ball) Ball->SetActorHiddenInGame(true);
	}
	if (bMoveTest)
	{
		TickMoveTest(Dt);
		return;
	}
	if (bGapTest)
	{
		TickGapTest(Dt);
		return;
	}
	if (bLineTest)
	{
		TickLineTest(Dt);
		return;
	}
	if (bTackleTest)
	{
		TickTackleTest(Dt);
		return;
	}
	// 0-9 s pre-snap stances facing the camera, 9-13 s from behind, 13-15 s celebrating, 15-17 s contact / evasion:
	// linemen tackle, skill players juke (alternating sides, with real lateral velocity), the QB kneels
	const bool bBack = LineupT > 9.f && LineupT <= 13.f;
	const bool bCelebrate = LineupT > 13.f && LineupT <= 15.f;
	const bool bAction = LineupT > 15.f;
	const FVector Center = TagCoords::ToWorld(50, tag::FIELD_W * 0.5, 1);
	for (int32 i = 0; i < Lineup.Num(); ++i)
	{
		const FVector L = Center + FVector(0, (i - (Lineup.Num() - 1) * 0.5) * 115.0, 0);
		FAthleteFootball F;
		F.bPreSnap = !bCelebrate && !bAction;
		if (F.bPreSnap)
		{
			// the match's pre-snap gaze (see the play feed's default look): eyes downfield at head height, so the stance
			// shots show what a game shows (without it a set lineman kept the capture's head as it was, bowed)
			F.bLook = true;
			F.LookWorld = Lineup[i]->GetActorLocation() + Lineup[i]->GetActorForwardVector() * 1500.f + FVector(0, 0, 160);
		}
		Lineup[i]->SetFootball(F, Dt);
		const TCHAR* LineState = bCelebrate ? TEXT("celebrate") : TEXT("idle");
		FVector Vel = FVector::ZeroVector;
		if (bAction)
		{
			LineState = i < 2 ? TEXT("tackle") : i == Lineup.Num() - 1 ? TEXT("kneel") : TEXT("juke");
			// facing -X (yaw 180): the actor's right is +Y; alternate cut sides across the skill players
			if (i >= 2 && i < Lineup.Num() - 1) Vel = FVector(-300.f, (i % 2) ? 250.f : -250.f, 0.f);
		}
		Lineup[i]->Drive(L, bBack ? 0.f : 180.f, Vel, 9.f, LineState, LineupT, Dt, false);
	}
	if (LineupT > 17.5f && !bLineupFloatCheck)
	{
		// a check for floating people: every skinned body whose pelvis stands far above (or below) its own feet origin
		bLineupFloatCheck = true;
		int32 Checked = 0, Odd = 0;
		for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
		{
			USkeletalMeshComponent* C = *It;
			if (!C || C->GetWorld() != GetWorld() || !C->IsVisible() || !C->GetSkeletalMeshAsset() || C->GetBoneIndex(TEXT("pelvis")) == INDEX_NONE) continue;
			if (C->LeaderPoseComponent.IsValid() || !C->WasRecentlyRendered(0.25f)) continue; // on screen (off-screen fans hold no pose)
			++Checked;
			const float Up = (C->GetSocketLocation(TEXT("pelvis")).Z - C->GetComponentLocation().Z) / FMath::Max(0.01f, static_cast<float>(C->GetComponentScale().Z));
			if (Up > 135.f || Up < 20.f)
			{
				++Odd;
				const UAnimSingleNodeInstance* SI = C->GetSingleNodeInstance();
				UE_LOG(LogTemp, Display, TEXT("TAG float check: %s %s pelvis %.0f cm above its origin (anim %s, owner %s, at %s)"), *C->GetName(), *GetNameSafe(C->GetSkeletalMeshAsset()), Up,
					SI && SI->GetAnimationAsset() ? *SI->GetAnimationAsset()->GetName() : C->GetAnimInstance() ? *C->GetAnimInstance()->GetClass()->GetName() : TEXT("-"), *GetNameSafe(C->GetOwner()), *C->GetComponentLocation().ToCompactString());
			}
		}
		UE_LOG(LogTemp, Display, TEXT("TAG float check: %d bodies, %d odd"), Checked, Odd);
	}
	if (LineupT > 16.8f)
	{
		// last beat: look at the near sideline's front rows (3D hero crowd) from the field, crowd hyped (cheering)
		if (Stadium) Stadium->Hype = 1.f;
		const FVector Stand(50 * TagCoords::YD, (tag::FIELD_W / 2 + 9) * TagCoords::YD, 0);
		Camera->SetActorLocation(Stand + FVector(0, -900, 300));
		Camera->SetActorRotation(((Stand + FVector(0, 250, 250)) - Camera->GetActorLocation()).Rotation());
	}
	else if (Lineup.Num() > 3 && LineupT > 12.4f && LineupT < 13.3f)
	{
		// helmet side closeups (the team mark on the shell): an away man, then a home man, from his right
		AAthlete* Who = Lineup[LineupT < 12.85f ? 3 : 2];
		const FVector Head = Who->BoneWorld(TEXT("head")) + FVector(0, 0, 8);
		Camera->SetActorLocation(Head + Who->GetActorRightVector() * 80.f + FVector(0, 0, 6));
		Camera->SetActorRotation((Head - Camera->GetActorLocation()).Rotation());
	}
	else if (Lineup.Num() > 3 && ((LineupT > 6.4f && LineupT < 7.6f) || (LineupT > 14.3f && LineupT < 14.9f)))
	{
		// face closeups (expressions): pre-snap focus, then the celebration
		const FVector Head = Lineup[3]->BoneWorld(TEXT("head")) + FVector(0, 0, 6);
		const FVector Fw = Lineup[3]->GetActorForwardVector();
		Camera->SetActorLocation(Head + Fw * 95.f + FVector(0, 0, 4));
		Camera->SetActorRotation((Head - Camera->GetActorLocation()).Rotation());
	}
	else
	{
		Camera->SetActorLocation(Center + FVector(-1050, 0, 150));
		Camera->SetActorRotation(((Center + FVector(0, 0, 100)) - Camera->GetActorLocation()).Rotation());
	}
	if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
	{
		C->CurrentFocalLength = 30.f;
		C->FocusSettings.ManualFocusDistance = 1050.f;
	}
	auto Shot = [&](float At, const TCHAR* Name, int32 Idx)
	{
		if (LineupShot == Idx && LineupT >= At)
		{
			FScreenshotRequest::RequestScreenshot(Name, false, false);
			++LineupShot;
		}
	};
	Shot(7.f, TEXT("tag_lineup_face_focus.png"), 0);
	Shot(7.7f, TEXT("tag_lineup_front.png"), 1);
	Shot(12.f, TEXT("tag_lineup_back.png"), 2);
	{
		static float NextLog = 0.f;
		if (LineupT >= NextLog && Lineup.Num() > 3)
		{
			NextLog = LineupT + 0.5f;
			for (int32 i : {0, 1, 3})
				UE_LOG(LogTemp, Display, TEXT("TAG lineup t=%.2f #%d head=%s %s"), LineupT, i, *Lineup[i]->BoneWorld(TEXT("head")).ToString(), *Lineup[i]->DebugAnimState());
		}
	}
	Shot(12.7f, TEXT("tag_lineup_helmet_away.png"), 3);
	Shot(13.15f, TEXT("tag_lineup_helmet_home.png"), 4);
	Shot(14.1f, TEXT("tag_lineup_celebrate_a.png"), 5);
	Shot(14.5f, TEXT("tag_lineup_face_celebrate.png"), 6);
	Shot(15.25f, TEXT("tag_lineup_action_a.png"), 7);
	Shot(15.45f, TEXT("tag_lineup_action_b.png"), 8);
	Shot(16.4f, TEXT("tag_lineup_action_c.png"), 9);
	Shot(17.6f, TEXT("tag_lineup_crowd.png"), 10);
	Shot(17.8f, TEXT("tag_lineup_crowd_b.png"), 11);
	PerfT += Dt; ++PerfFrames;
	if (LineupT > 18.f)
	{
		UE_LOG(LogTemp, Display, TEXT("TAG perf avg %.1f fps over %d frames"), PerfFrames / FMath::Max(PerfT, 0.001f), PerfFrames);
		{
			double GazeMean, GazeMin;
			int32 GazeN;
			AAthlete::GazeStats(GazeMean, GazeMin, GazeN, true);
			UE_LOG(LogTemp, Display, TEXT("TAG presnap gaze: set linemen look %.1f deg (lowest %.1f) over %d samples (0 = level, - = down)"), GazeMean, GazeMin, GazeN);
		}
		FPlatformMisc::RequestExit(false);
	}
}

void AMatchDirector::TickMoveTest(float Dt)
{
	// hold the clock until motion matching has its search indexes (uncooked runs build them in the background)
	FAthleteAssets& AA = FAthleteAssets::Get();
	if (AA.bMotionMatch && !AA.MotionMatchReady())
	{
		LineupT = 0.f;
		return;
	}
	const float Seg = 9.f;
	const int32 R = FMath::Clamp(static_cast<int32>(LineupT / Seg), 0, Lineup.Num() - 1);
	const float T = LineupT - R * Seg;
	const FVector Start = TagCoords::ToWorld(30, tag::FIELD_W * 0.5, 1);
	if (R != MoveRunner)
	{
		double Raw, Shown, Sec, Metres;
		int32 Steps;
		AAthlete::SlideStats(Raw, Shown, true);
		AAthlete::GaitStats(Sec, Metres, Steps, true);
		if (MoveRunner >= 0) UE_LOG(LogTemp, Display, TEXT("TAG slide r%d: animation %.0f cm, shown %.0f cm (planted-foot travel over the route)"), MoveRunner, Raw, Shown);
		double HipsM, HeadM;
		AAthlete::HeightStats(HipsM, HeadM, true);
		if (MoveRunner >= 0 && Sec > 0.5) UE_LOG(LogTemp, Display, TEXT("TAG gait r%d: %.2f steps/s, %.2f m per step at %.1f m/s over %.1f s, hips %.2f m head %.2f m (sprint reference ~4.7 steps/s, 2.1 m at 10 m/s; hips 0.90-0.99, head 1.32-1.54)"), MoveRunner, Steps / Sec, Steps > 0 ? Metres / Steps : 0.0, Metres / Sec, Sec, HipsM / Sec, HeadM / Sec);
		MoveRunner = R;
		MovePos = Start;
		MoveVel = FVector::ZeroVector;
		for (int32 i = 0; i < Lineup.Num(); ++i) Lineup[i]->SetActorHiddenInGame(i != R);
		Lineup[R]->ResetPose();
	}
	// target velocity and facing for this runner's route (yaw 0 = +X, downfield)
	FVector Target = FVector::ZeroVector;
	float Yaw = 0.f;
	if (R == 0)
	{
		// receiver: standing start, sprint 10 m/s (past the 9.1 the sped-up capture covers: stride warping), plant and
		// cut 90 deg right at 4.5 s, stop at 7 s
		if (T > 1.f && T <= 4.5f) Target = FVector(1000, 0, 0);
		else if (T > 4.5f && T <= 7.f) Target = FVector(0, -850, 0);
	}
	else if (R == 1)
	{
		// back: 7 m/s, then a 120 deg sweeping curve, stop at 7 s
		const float CurveYaw = T <= 3.f ? 0.f : FMath::Min(T - 3.f, 4.f) * 30.f;
		if (T > 1.f && T <= 7.f) Target = 750.f * FRotator(0, CurveYaw, 0).Vector();
	}
	else if (R == 3)
	{
		// tackle: pass set kick-slide (back and out at 1.9 m/s, facing downfield), anchor, drive block forward, settle
		if (T > 1.f && T <= 3.f) Target = FVector(-110, 150, 0);
		else if (T > 4.5f && T <= 6.5f) Target = FVector(220, 0, 0);
	}
	else
	{
		// defensive back: backpedal at 3.8 m/s facing downfield, plant and break forward at 4 s, stop at 7.5 s
		if (T > 1.f && T <= 4.f) Target = FVector(-380, 0, 0);
		else if (T > 4.f && T <= 7.5f) Target = FVector(850, 0, 0);
	}
	const float MaxDv = (Target.Size() > MoveVel.Size() ? 900.f : 1500.f) * Dt;
	MoveVel += (Target - MoveVel).GetClampedToMaxSize(MaxDv);
	MovePos += MoveVel * Dt;
	if (R < 2 && MoveVel.Size() > 50.f) Yaw = MoveVel.Rotation().Yaw;
	else if (R < 2) Yaw = Lineup[R]->GetActorRotation().Yaw;
	FAthleteFootball F;
	Lineup[R]->SetFootball(F, Dt);
	Lineup[R]->Drive(MovePos, Yaw, MoveVel, 10.f, MoveVel.Size() > 30.f ? TEXT("run") : TEXT("idle"), T, Dt, R == 3);

	// tracking side camera (from the -Y sideline side), 8 m off, at chest height
	Camera->SetActorLocation(MovePos + FVector(-250, -800, 170));
	Camera->SetActorRotation(((MovePos + FVector(0, 0, 95)) - Camera->GetActorLocation()).Rotation());
	if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
	{
		C->CurrentFocalLength = 28.f;
		C->FocusSettings.ManualFocusDistance = 840.f;
	}
	// -TagMoveFrames=<route>,<from>,<to>: every frame of that window (foot skating shows between consecutive frames)
	{
		static FString Win = FParse::Value(FCommandLine::Get(), TEXT("TagMoveFrames="), Win, false) ? Win : FString();
		static int32 FrameNum = 0;
		TArray<FString> P;
		// a 4th field "shots" also writes a screenshot per frame (run with -UseFixedTimeStep -FPS=30: writing one stalls)
		const int32 N = Win.ParseIntoArray(P, TEXT(","));
		if (N >= 3 && R == FCString::Atoi(*P[0]) && T >= FCString::Atof(*P[1]) && T <= FCString::Atof(*P[2]))
		{
			if (N > 3) FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_frames_r%d_%03d.png"), R, FrameNum), false, false);
			UE_LOG(LogTemp, Display, TEXT("TAG frame r%d %03d t=%.3f v=%s footL=%s footR=%s %s"), R, FrameNum++, T, *MoveVel.ToCompactString(),
				*Lineup[R]->BoneWorld(TEXT("ball_l")).ToCompactString(), *Lineup[R]->BoneWorld(TEXT("ball_r")).ToCompactString(), *Lineup[R]->DebugAnimState());
		}
	}
	const float ShotT = 0.8f + (MoveShot % 27) * 0.3f;
	if (MoveShot / 27 == R && T >= ShotT)
	{
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_move_r%d_%02d.png"), R, MoveShot % 27), false, false);
		double SlRaw, SlShown, GSec, GM;
		int32 GSteps;
		AAthlete::SlideStats(SlRaw, SlShown, false);
		AAthlete::GaitStats(GSec, GM, GSteps, false);
		UE_LOG(LogTemp, Display, TEXT("TAG move r%d #%d t=%.2f v=%.0f slide=%.0f/%.0f steps=%d %s"), R, Lineup[R]->Player ? Lineup[R]->Player->number : -1, T, MoveVel.Size(), SlRaw, SlShown, GSteps, *Lineup[R]->DebugAnimState());
		++MoveShot;
	}
	PerfT += Dt; ++PerfFrames;
	if (LineupT > Seg * Lineup.Num())
	{
		double Raw, Shown, Sec, Metres;
		int32 Steps;
		AAthlete::SlideStats(Raw, Shown, true);
		AAthlete::GaitStats(Sec, Metres, Steps, true);
		UE_LOG(LogTemp, Display, TEXT("TAG slide r%d: animation %.0f cm, shown %.0f cm (planted-foot travel over the route)"), MoveRunner, Raw, Shown);
		double HipsM, HeadM;
		AAthlete::HeightStats(HipsM, HeadM, true);
		if (Sec > 0.5) UE_LOG(LogTemp, Display, TEXT("TAG gait r%d: %.2f steps/s, %.2f m per step at %.1f m/s over %.1f s, hips %.2f m head %.2f m (sprint reference ~4.7 steps/s, 2.1 m at 10 m/s; hips 0.90-0.99, head 1.32-1.54)"), MoveRunner, Steps / Sec, Steps > 0 ? Metres / Steps : 0.0, Metres / Sec, Sec, HipsM / Sec, HeadM / Sec);
		UE_LOG(LogTemp, Display, TEXT("TAG perf avg %.1f fps over %d frames"), PerfFrames / FMath::Max(PerfT, 0.001f), PerfFrames);
		{
			double GazeMean, GazeMin;
			int32 GazeN;
			AAthlete::GazeStats(GazeMean, GazeMin, GazeN, true);
			UE_LOG(LogTemp, Display, TEXT("TAG presnap gaze: set linemen look %.1f deg (lowest %.1f) over %d samples (0 = level, - = down)"), GazeMean, GazeMin, GazeN);
		}
		FPlatformMisc::RequestExit(false);
	}
}

// the pairs as captured (Tools/Anim/mocap_pairs.py from the PT_* clips' jsons; both men of a pair come from one mocap
// group in a shared space, cut at the hit): the carrier's actor in the tackler's clip frame (m; x left, y back) and his
// yaw relative to the tackler (deg, + turns left); where the carrier's pelvis ends (his clip frame)
namespace
{
	struct FTacklePairDef { float Ox, Oy, Yaw, EndX, EndY, TacklerLie, CarrierLie; };
	const FTacklePairDef GPairs[FAthleteAssets::PairCount] = {
		{-0.07f, -0.62f, 151.3f, -0.99f, 1.29f, 0.45f, 1.05f},  // Drive: Tackle_D_Win_12_D1 - driven back onto his back, tackler on top
		{0.01f, -0.65f, -77.7f, -1.93f, -2.10f, 1.05f, 0.45f},  // Over: Tackle_O_Win_12_D1 - the tackler goes low, the carrier pitches over him
		{-0.05f, -0.34f, -9.8f, 1.93f, -2.39f, 0.45f, 1.05f},   // Behind: Tackle_Neu_12_D5
		{-0.00f, -0.38f, -58.5f, 1.76f, 0.73f, 0.45f, 1.05f},   // Side: Tackle_Neu_12_D3 (from his right; mirrored from his left)
		{-0.04f, -1.23f, 0.f, 0.28f, -5.08f, 0.70f, 0.90f},     // Dive: Tackle_Dive_Low_from_D5 - shoestring from behind at full speed, he slides
	};
	// clip frame (x left, y back, m) -> actor local (forward, right, cm)
	FVector ClipToActor(float X, float Y) { return FVector(-Y * 100.f, -X * 100.f, 0.f); }

	// -TagTackleCheck: every paired / gang tackle against the sim's carrier ("TAG tcheck" lines; Fred 2026-11-22: the
	// carrier turned into the tackler and the tackler into the carrier - captures whose __P01 was the tackler). At the
	// start: the sim's ball holder, the man the capture's carrier role went to, both captures' carry scores
	// (FAthleteAssets::CarryScore: the ball socket held still at the chest, cm). Each frame after, for 2.5 s or until
	// he is up: each man's tuck (his nearer ball socket to spine_05, cm), the drawn ball's distance to each man's nearer
	// ball socket, the sim's holder. "done": frames in the first second the tackler shows the carry and the carrier
	// does not (a role swap on screen), frames the ball is nearer the tackler while the sim's carrier still has it.
	struct FTackleCheck
	{
		TWeakObjectPtr<AAthlete> C, T;
		FString Kind, Clip;
		int32 Id = 0, Frames = 0, TuckSwap = 0, BallOff = 0;
		double T0 = 0.0;
		bool bMismatch = false, bTurnover = false;
	};
	TArray<FTackleCheck> GTackleChecks;
	int32 GTackleCheckSeq = 0;
	bool TackleCheckOn()
	{
		static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("TagTackleCheck"));
		return bOn;
	}
	float TuckCm(const AAthlete* Who)
	{
		const FVector Ch = Who->BoneWorld(TEXT("spine_05"));
		return static_cast<float>(FMath::Min(FVector::Dist(Who->BoneWorld(TEXT("RightFootball")), Ch), FVector::Dist(Who->BoneWorld(TEXT("LeftFootball")), Ch)));
	}
	float BallCm(const AAthlete* Who, const FVector& BallW)
	{
		return static_cast<float>(FMath::Min(FVector::Dist(Who->BoneWorld(TEXT("RightFootball")), BallW), FVector::Dist(Who->BoneWorld(TEXT("LeftFootball")), BallW)));
	}
	int32 PidOf(const AAthlete* Who) { return Who && Who->Player ? Who->Player->id : -1; }
	int32 NumOf(const AAthlete* Who) { return Who && Who->Player ? Who->Player->number : -1; }
	void TackleCheckBegin(const TCHAR* Kind, AAthlete* Tk, AAthlete* Cr, AAthlete* As, UAnimationAsset* TClip, UAnimationAsset* CClip, UAnimationAsset* AClip,
		float Start, const tag::PlaySim* S, bool bStrip, double Now)
	{
		if (!TackleCheckOn() || !Tk || !Cr) return;
		const tag::Ent* H = S ? S->ball.holder : nullptr;
		// (a fumble in the same sim step has the ball loose already: no carrier to mismatch)
		const bool bSimCarrier = !H || !S || S->ball.mode != tag::BallMode::Held || (H->p && H->p == Cr->Player);
		const float W0 = FMath::Max(Start, 0.4f);
		const float Sc = FAthleteAssets::CarryScore(Cast<UAnimSequence>(CClip), 0.f, W0);
		const float St = FAthleteAssets::CarryScore(Cast<UAnimSequence>(TClip), 0.f, W0);
		const float Sa = AClip ? FAthleteAssets::CarryScore(Cast<UAnimSequence>(AClip), 0.f, W0) : -1.f;
		// (the capture's own verdict, as the loader's guard: another man clearly the one holding it still)
		const bool bCapSwap = Sc >= 0.f && ((St >= 0.f && St < Sc - 12.f && St < 50.f) || (Sa >= 0.f && Sa < Sc - 12.f && Sa < 50.f));
		FTackleCheck K;
		K.C = Cr;
		K.T = Tk;
		K.Kind = Kind;
		K.Clip = GetNameSafe(CClip);
		K.Id = ++GTackleCheckSeq;
		K.T0 = Now;
		K.bMismatch = !bSimCarrier && !bStrip;
		GTackleChecks.RemoveAll([Cr](const FTackleCheck& X) { return X.C.Get() == Cr; });
		GTackleChecks.Add(K);
		UE_LOG(LogTemp, Display, TEXT("TAG tcheck start %d %s: sim holder #%d (pid %d), carrier role #%d (pid %d), tackler #%d (pid %d)%s; C %s carry %.0f cm, T %s carry %.0f cm%s%s -> %s"),
			K.Id, Kind, H && H->p ? H->p->number : -1, H && H->p ? H->p->id : -1, NumOf(Cr), PidOf(Cr), NumOf(Tk), PidOf(Tk),
			As ? *FString::Printf(TEXT(", second #%d (pid %d)"), NumOf(As), PidOf(As)) : TEXT(""), *GetNameSafe(CClip), Sc, *GetNameSafe(TClip), St,
			AClip ? *FString::Printf(TEXT(", A carry %.0f cm"), Sa) : TEXT(""), bStrip ? TEXT(" (strip)") : TEXT(""),
			K.bMismatch ? TEXT("SIM MISMATCH") : bCapSwap ? TEXT("CAPTURE ROLE SWAP") : TEXT("ok"));
	}
	void TackleCheckTick(const tag::PlaySim* S, const FVector& BallW, double Now)
	{
		if (!TackleCheckOn()) return;
		for (int32 i = GTackleChecks.Num() - 1; i >= 0; --i)
		{
			FTackleCheck& K = GTackleChecks[i];
			const AAthlete* Cw = K.C.Get();
			const AAthlete* Tw = K.T.Get();
			const tag::Ent* H = S ? S->ball.holder : nullptr;
			const double Since = Now - K.T0;
			if (!Cw || !Tw || !S || Since > 2.5 || (Since > 0.3 && !Cw->IsFalling()))
			{
				UE_LOG(LogTemp, Display, TEXT("TAG tcheck done %d %s %s: carrier #%d, %d frames, tackler shows the carry %d, ball nearer the tackler %d, sim turnover %d, sim mismatch %d"),
					K.Id, *K.Kind, *K.Clip, NumOf(Cw), K.Frames, K.TuckSwap, K.BallOff, K.bTurnover ? 1 : 0, K.bMismatch ? 1 : 0);
				GTackleChecks.RemoveAt(i);
				continue;
			}
			const bool bSimC = H && H->p && H->p == Cw->Player && S->ball.mode == tag::BallMode::Held;
			if (!bSimC) K.bTurnover = true;
			const float TuC = TuckCm(Cw), TuT = TuckCm(Tw), BaC = BallCm(Cw, BallW), BaT = BallCm(Tw, BallW);
			++K.Frames;
			if (Since < 1.0 && TuT < 36.f && TuC > 45.f) ++K.TuckSwap;
			if (bSimC && BaT + 10.f < BaC && BaC > 25.f) ++K.BallOff;
			UE_LOG(LogTemp, Display, TEXT("TAG tcheck f %d t=%.2f sim=%s tuckC=%.0f tuckT=%.0f ballC=%.0f ballT=%.0f"),
				K.Id, Since, bSimC ? TEXT("C") : TEXT("other"), TuC, TuT, BaC, BaT);
		}
	}
}

void AMatchDirector::StartPairedTackle(AAthlete* Tk, AAthlete* Cr, const FVector& SimSpot, int32 Pair, bool bDive, float PileSecs, bool bBigHit, int32 Strip)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	LastStripOut = -1.f;
	if (!Tk || !Cr || Tk->IsFalling() || Cr->IsFalling()) return;
	if (Strip && !A.PackStrips.Num()) Strip = 0;
	const FVector H = Cr->GetActorLocation(); // held where the hit landed
	FVector Fc = Cr->GetLastVel().GetSafeNormal2D();
	if (Fc.IsNearlyZero()) Fc = Cr->GetActorForwardVector();
	const FVector Rc(-Fc.Y, Fc.X, 0.f);       // his right
	const FVector Rel = (Tk->GetActorLocation() - H).GetSafeNormal2D();
	const float Front = FVector::DotProduct(Rel, Fc), Right = FVector::DotProduct(Rel, Rc);
	const float Yac = FVector::DotProduct(SimSpot - H, Fc);
	const float Yc = Fc.Rotation().Yaw;
	// the kinds the hit's angle allows: head on (driven back, or pitched forward over the tackler), from behind, from
	// the side (his right, or the mirror / the library's SideL from his left), the dive
	TArray<int32, TInlineAllocator<2>> Kinds;
	if (Pair >= 0) Kinds.Add(Pair);
	else if (bDive) Kinds.Add(FAthleteAssets::PairDive);
	else if (Front > 0.45f) { Kinds.Add(FAthleteAssets::PairOver); Kinds.Add(FAthleteAssets::PairDrive); }
	else if (Front < -0.45f) Kinds.Add(FAthleteAssets::PairBehind);
	else Kinds.Add(FAthleteAssets::PairSide);
	// every capture of those kinds (the placed pair and the pack library's two-man tackles), ranked by how far the pile
	// would still have to slide after the capture's own travel to reach the sim's spot. The best few take turns, so the
	// fall that plays is one whose bodies get there by themselves (the old pick took the next capture in turn and slid
	// the pair up to 1.5 m over the turf in 0.45 s to make up the rest).
	// The tackler's own place counts double: the pair starts at the captured contact, and a tackler far from where the
	// capture has him glides there (through the carrier when he is on the other side).
	struct FCand { int32 Pair; int32 Lib; bool bMirror; float Resid; };
	TArray<FCand, TInlineAllocator<64>> Cands;
	auto TravelOf = [&](float EndX, float EndY) { return FRotator(0, Yc, 0).RotateVector(ClipToActor(EndX, EndY)); };
	const FVector TkAt = Tk->GetActorLocation();
	auto Cost = [&](float Ox, float Oy, float YawRel, const FVector& Trav)
	{
		const FVector Off = FRotator(0, Yc + YawRel, 0).RotateVector(ClipToActor(Ox, Oy));
		// (what the drive cannot take off, below, is left on the field as a man down past the spot the ball is then put on:
		// a capture that runs on 3.2 m was played where the sim had 0.9 m left and he fell 2-3 yd beyond the mark, p60)
		const double Res = FVector::Dist2D(SimSpot, H + Trav);
		return static_cast<float>(0.5 * Res + 1.5 * FMath::Max(0.0, Res - (PileSecs > 0.f ? 240.0 : 150.0)) + FVector::Dist2D(TkAt, H - Off));
	};
	// momentum (Game/TackleMomentum.h): both men's speed into the hit against each capture's own just after the contact.
	// Only the root-driven captures can be measured: their terms count from the best of them (MinMom), so a placed pair
	// is neither favoured nor passed over by it.
	const float VTk = static_cast<float>(Tk->GetLastVel().Size2D()), VCr = static_cast<float>(Cr->GetLastVel().Size2D());
	float MinMom = 1e9f;
	auto Mom = [&](const UAnimationAsset* MT, const UAnimationAsset* MC, float MStart)
	{
		const float Mo = TackleMomentum::Cost(MT, MC, MStart, VTk, VCr);
		MinMom = FMath::Min(MinMom, Mo);
		return Mo;
	};
	// a fumble at the tackle: the pack's strip captures, the ball punched out of the carrying arm (a sack's strip from the
	// sack captures where there are), ranked the same way (Lib 9000 + index)
	if (Strip)
	{
		bool bAnySack = false;
		for (const FAthleteAssets::FPackStrip& S : A.PackStrips) bAnySack |= S.bSack;
		for (int32 i = 0; i < A.PackStrips.Num(); ++i)
		{
			const FAthleteAssets::FPackStrip& S = A.PackStrips[i];
			if (!S.T || !S.C || (bAnySack && S.bSack != (Strip == 2))) continue;
			Cands.Add({FAthleteAssets::PairDrive, 9000 + i, false, Cost(S.Ox, S.Oy, S.Yaw, Cr->FallRootTravel(S.C, S.Start, Yc)) + Mom(S.T, S.C, S.Start)});
		}
	}
	for (const int32 K : Kinds)
	{
		if (Strip) break;
		const bool bMirror = K == FAthleteAssets::PairSide && Right < 0.f; // the tackler on his left: the mirrored pair
		if (A.Side(A.PairT[K], bMirror) && A.Side(A.PairC[K], bMirror))
		{
			const FTacklePairDef& P = GPairs[K];
			Cands.Add({K, -1, bMirror, Cost(bMirror ? -P.Ox : P.Ox, P.Oy, bMirror ? -P.Yaw : P.Yaw, TravelOf(bMirror ? -P.EndX : P.EndX, P.EndY))});
		}
		const int32 LK = K == FAthleteAssets::PairDrive ? 0 : K == FAthleteAssets::PairOver ? 1 : K == FAthleteAssets::PairBehind ? 2 : K == FAthleteAssets::PairDive ? 5 : bMirror ? 4 : 3;
		const TArray<FAthleteAssets::FPackPair>& Lib = A.PackPairs[LK];
		for (int32 i = 0; i < Lib.Num(); ++i)
		{
			const FAthleteAssets::FPackPair& L = Lib[i];
			if (!L.T || !L.C) continue;
			// captures where the two part at once (pelvises over 1.3 m apart through the next 1.2 s, the capture's own
			// numbers): a hit stick blowing the carrier back, a shoestring trip. Only for the plays that are that (the sim's
			// big hit, a dive); on an ordinary tackle they read as two men falling down near each other.
			const bool bStick = L.Name.Contains(TEXT("Hitstick"));
			if (bStick && !bBigHit) continue;
			if (L.Sep > 1.3f && !bDive && !bBigHit) continue;
			// (a library capture's root carries the actor: its travel is the root's, as TickFall plays it. The record's
			// pelvis end was 35-50 deg off it on the momentum tackles and the carrier came to rest 2.2-2.7 m off the
			// planned spot - the worst shown-vs-scored plays of p60 / p61)
			Cands.Add({K, LK * 1000 + i, false, Cost(L.Ox, L.Oy, L.Yaw, Cr->FallRootTravel(L.C, L.Start, Yc)) - (bBigHit && bStick ? 80.f : 0.f) + Mom(L.T, L.C, L.Start)});
		}
	}
	if (MinMom < 1e8f)
		for (FCand& Cd : Cands)
			if (Cd.Lib >= 0) Cd.Resid -= MinMom;
	if (Cands.IsEmpty())
	{
		if (Strip) StartPairedTackle(Tk, Cr, SimSpot, Pair, bDive, PileSecs, bBigHit, 0); // (no strip capture: an ordinary pair, the ball out at the hit)
		return;
	}
	Cands.Sort([](const FCand& X, const FCand& Y) { return X.Resid < Y.Resid; });
	int32 NGood = 1;
	while (NGood < Cands.Num() && NGood < 4 && Cands[NGood].Resid < Cands[0].Resid + 35.f) ++NGood;
	static int32 GPairSeq = 0;
	const FCand& C = Cands[(GPairSeq++) % NGood];
	Pair = C.Pair;
	bool bMirror = C.bMirror;
	UAnimationAsset* TClip;
	UAnimationAsset* CClip;
	FTacklePairDef P;
	float Start = 0.f;
	bool bRoot = false;
	if (C.Lib >= 9000)
	{
		// the strip: as a library pair, root driven from the contact; the punch's moment and arm for the ball
		const FAthleteAssets::FPackStrip& S = A.PackStrips[C.Lib - 9000];
		TClip = S.T; CClip = S.C;
		P = {S.Ox, S.Oy, S.Yaw, 0.f, 0.f, 0.45f, 1.05f};
		Start = S.Start;
		bRoot = true;
		LastStripOut = FMath::Max(0.f, S.Out - S.Start);
		bLastStripRight = S.bRightHand;
		UE_LOG(LogTemp, Display, TEXT("TAG strip tackle %s: #%d strips #%d (ball out %.2f s after the contact, %s arm)"), *S.Name, Tk->Player ? Tk->Player->number : -1,
			Cr->Player ? Cr->Player->number : -1, LastStripOut, S.bRightHand ? TEXT("right") : TEXT("left"));
	}
	else if (C.Lib < 0)
	{
		TClip = A.Side(A.PairT[Pair], bMirror);
		CClip = A.Side(A.PairC[Pair], bMirror);
		P = GPairs[Pair];
	}
	else
	{
		// the capture's root carries the fall (and its turn) from the contact
		const FAthleteAssets::FPackPair& L = A.PackPairs[C.Lib / 1000][C.Lib % 1000];
		TClip = L.T; CClip = L.C;
		P = {L.Ox, L.Oy, L.Yaw, L.EndX, L.EndY, L.TLie, L.CLie};
		Start = L.Start;
		bRoot = true;
	}
	const float Ox = bMirror ? -P.Ox : P.Ox, YawRel = bMirror ? -P.Yaw : P.Yaw, EndX = bMirror ? -P.EndX : P.EndX;
	const float Yt = Yc + YawRel;
	// the carrier lies where the clip takes him; the pile drives what is left onto the sim's spot (within reason), spread
	// over the part of the fall the two are still on their feet (a moving pile: over the drive the sim gives it)
	const FVector Travel = bRoot ? Cr->FallRootTravel(CClip, Start, Yc) : TravelOf(EndX, P.EndY);
	const bool bPile = PileSecs > 0.f;
	// (the sim's spot only places the umpire's ball; the shown ball stays with the shown carrier. A short drive, done
	// before the bodies land: a man lying on the turf and still gliding toward a spot looked pulled by a string)
	// (to 2.4 m for a pile, 1.5 m otherwise: at 1.4 / 0.7 the shown spot ended a median 0.9 yd off the scored one, at 2.0 / 1.1 0.6 yd)
	const FVector Drive = (FVector(SimSpot.X, SimSpot.Y, H.Z) - (H + Travel)).GetClampedToMaxSize2D(bPile ? 240.f : 150.f);
	// the pair's relation: the tackler where the pair puts him relative to the carrier. The two close the gap between
	// where they are and that relation together (each moves half of it), so neither glides far.
	const FVector Off = FRotator(0, Yt, 0).RotateVector(ClipToActor(Ox, P.Oy));
	FVector Gap = (Tk->GetActorLocation() - (H - Off));
	Gap.Z = 0.f;
	Gap = Gap.GetClampedToMaxSize2D(160.f) * 0.5f;
	const FVector CFrom = H + Gap, CTo = CFrom + FVector(Drive.X, Drive.Y, 0.f);
	const FVector TFrom = CFrom - Off, TTo = CTo - Off;
	const float DriveT = bPile ? FMath::Clamp(PileSecs, 0.45f, 0.7f) : 0.4f;
	const float Jit = FMath::Frac(FPlatformTime::Seconds() * 7.13) * 0.35f; // not every pile gets up on the same beat
	Cr->BeginPairedFall(CClip, CFrom, CTo, Yc, DriveT, P.CarrierLie + Jit, true, Start, bRoot);
	Cr->PileBodyEnd = CTo + Travel; // where his body comes to rest (a second man lands on that)
	Tk->BeginPairedFall(TClip, TFrom, TTo, Yt, DriveT, P.TacklerLie, true, Start, bRoot);
	// a longer glide for a longer gap (at most ~2.5 m/s on top of the run; 60 cm in the fixed 0.2 s skated)
	const float Close = FVector::Dist2D(Tk->GetActorLocation(), TFrom);
	Tk->SetFallEaseTime(FMath::Clamp(Close / 250.f, 0.2f, 0.4f));
	Cr->SetFallEaseTime(FMath::Clamp(Gap.Size2D() / 250.f, 0.2f, 0.4f));
	// the pile carries the two men's momentum (mass weighted), the closing speed between them is spent in the hit
	{
		const float Mt = Tk->MassKg(), Mc = Cr->MassKg();
		const FVector Vp = (Tk->GetLastVel() * Mt + Cr->GetLastVel() * Mc) / FMath::Max(1.f, Mt + Mc);
		Tk->SetFallCarry(Vp);
		Cr->SetFallCarry(Vp);
	}
	PileCarrier = Cr;
	PileTackler = Tk;
	PileSpot = SimSpot;
	if (const UAnimSequence* CSq = Cast<UAnimSequence>(CClip); CSq && bRoot)
	{
		const FVector RMv = CSq->ExtractRootMotionFromRange(Start, CSq->GetPlayLength(), FAnimExtractContext(0.0, true)).GetTranslation();
		UE_LOG(LogTemp, Display, TEXT("TAG pair travel: record (%.0f, %.0f) = %.0f cm, root from %.2f s to %.2f s (%.0f, %.0f) = %.0f cm, sim spot %.0f cm from the carrier, drive (%.0f, %.0f), #%d from (%.0f, %.0f) planned to (%.0f, %.0f), spot (%.0f, %.0f)"),
			EndX, P.EndY, Travel.Size2D(), Start, CSq->GetPlayLength(), RMv.X, RMv.Y, RMv.Size2D(), FVector::Dist2D(SimSpot, H), Drive.X, Drive.Y,
			Cr->Player ? Cr->Player->number : -1, CFrom.X, CFrom.Y, (CTo + Travel).X, (CTo + Travel).Y, SimSpot.X, SimSpot.Y);
	}
	UE_LOG(LogTemp, Display, TEXT("TAG tackle pair %s%s: #%d on #%d (front %.2f right %.2f, yac %.0f cm, slide %.0f cm, cost %.0f, over %.2f s, close %.0f cm, %d candidates%s)"),
		*TClip->GetName(), bMirror ? TEXT(" (mirror)") : TEXT(""), Tk->Player ? Tk->Player->number : -1,
		Cr->Player ? Cr->Player->number : -1, Front, Right, Yac, Drive.Size2D(), C.Resid, DriveT, FVector::Dist2D(Tk->GetActorLocation(), TFrom), Cands.Num(),
		bPile ? TEXT(", moving pile") : TEXT(""));
	if (bRoot) // (the momentum check: capture against game speed at the contact, -TagNoTackleMomentum for the A/B)
		UE_LOG(LogTemp, Display, TEXT("TAG tackle momentum: capture T %.1f C %.1f m/s, game T %.1f C %.1f m/s (%s)"), TackleMomentum::CaptureSpeed(TClip, Start) * 0.01f,
			TackleMomentum::CaptureSpeed(CClip, Start) * 0.01f, VTk * 0.01f, VCr * 0.01f, *TClip->GetName());
	TackleCheckBegin(C.Lib >= 9000 ? TEXT("strip") : C.Lib < 0 ? TEXT("placed") : TEXT("pair"), Tk, Cr, nullptr, TClip, CClip, nullptr, Start, Sim.get(), C.Lib >= 9000,
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

bool AMatchDirector::StartGangTackle(AAthlete* Tk, AAthlete* Cr, AAthlete* As, const FVector& SimSpot)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	if (!Tk || !Cr || !As || As == Tk || A.PackGangs.IsEmpty() || Tk->IsFalling() || Cr->IsFalling() || As->IsFalling()) return false;
	const FVector H = Cr->GetActorLocation();
	FVector Fc = Cr->GetLastVel().GetSafeNormal2D();
	if (Fc.IsNearlyZero()) Fc = Cr->GetActorForwardVector();
	const float Yc0 = Fc.Rotation().Yaw;
	const FVector TkAt = Tk->GetActorLocation(), AsAt = As->GetActorLocation();
	const float VTk = static_cast<float>(Tk->GetLastVel().Size2D()), VCr = static_cast<float>(Cr->GetLastVel().Size2D()); // (momentum, as a pair's)
	// every captured gang tackle, turned by up to 45 deg off the carrier's run: how far each of our two tacklers is
	// from where the capture has its man, and how far the pile would still slide to the sim's spot
	struct FFit { int32 G; float Phi, ET, EA, ER, Cost; };
	TArray<FFit, TInlineAllocator<512>> Fits;
	for (int32 g = 0; g < A.PackGangs.Num(); ++g)
	{
		const FAthleteAssets::FPackGang& G = A.PackGangs[g];
		if (!G.C || !G.T || !G.A) continue;
		FFit Best{g, 0.f, 0.f, 0.f, 0.f, 1e9f};
		// (where the carrier comes to rest by the capture's own root, as the fall plays it: the stored pelvis end was off
		// it, and the carrier of a gang tackle lay 2.5 yd past the scored spot in every game, p60-p72 c002)
		const FVector Trav0 = Cr->FallRootTravel(G.C, G.Start, Yc0);
		for (float Phi = -45.f; Phi <= 45.f; Phi += 7.5f)
		{
			const FRotator R(0, Yc0 + Phi, 0);
			const float ET = FVector::Dist2D(H + R.RotateVector(ClipToActor(G.Tx, G.Ty)), TkAt);
			const float EA = FVector::Dist2D(H + R.RotateVector(ClipToActor(G.Ax, G.Ay)), AsAt);
			const float ER = FVector::Dist2D(SimSpot, H + FRotator(0, Phi, 0).RotateVector(Trav0));
			// (and heavily what the 1.5 m drive cannot take off: it is left on the field past the scored spot)
			const float Cost = ET + 0.6f * EA + 0.3f * FMath::Min(ER, 400.f) + 1.5f * FMath::Max(0.f, ER - 150.f) + FMath::Abs(Phi) * 1.2f;
			if (Cost < Best.Cost) Best = {g, Phi, ET, EA, ER, Cost};
		}
		Best.Cost += TackleMomentum::Cost(G.T, G.C, G.Start, VTk, VCr); // (the tackler and the carrier through the hit)
		Fits.Add(Best);
	}
	if (Fits.IsEmpty()) return false;
	Fits.Sort([](const FFit& X, const FFit& Y) { return X.Cost < Y.Cost; });
	int32 NGood = 1;
	while (NGood < Fits.Num() && NGood < 3 && Fits[NGood].Cost < Fits[0].Cost + 30.f) ++NGood;
	static int32 GGangSeq = 0;
	const FFit F = Fits[(GGangSeq++) % NGood];
	// the first tackler must be nearly where the capture has him (he glides the rest); the second man can come from
	// further, he is running in. Otherwise the pair and a man piling on.
	if (F.ET > 130.f || F.EA > 260.f)
	{
		UE_LOG(LogTemp, Display, TEXT("TAG gang tackle: no capture fits #%d + #%d on #%d (best: tackler off %.0f cm, second %.0f cm)"),
			Tk->Player ? Tk->Player->number : -1, As->Player ? As->Player->number : -1, Cr->Player ? Cr->Player->number : -1, F.ET, F.EA);
		return false;
	}
	const FAthleteAssets::FPackGang& G = A.PackGangs[F.G];
	const float Yc = Yc0 + F.Phi;
	const FRotator R(0, Yc, 0);
	const FVector Travel = Cr->FallRootTravel(G.C, G.Start, Yc);
	// (to 1.5 m, as a pair: at 70 cm the rest was left on the field past the mark)
	const FVector Drive = (FVector(SimSpot.X, SimSpot.Y, H.Z) - (H + Travel)).GetClampedToMaxSize2D(150.f);
	const float DriveT = 0.4f;
	const FVector D(Drive.X, Drive.Y, 0.f);
	const FVector TFrom = H + R.RotateVector(ClipToActor(G.Tx, G.Ty));
	const FVector AFrom = H + R.RotateVector(ClipToActor(G.Ax, G.Ay));
	const float Jit = FMath::Frac(FPlatformTime::Seconds() * 7.13) * 0.35f;
	Cr->BeginPairedFall(G.C, H, H + D, Yc, DriveT, 1.05f + Jit, true, G.Start, true);
	Cr->PileBodyEnd = H + D + Travel;
	Tk->BeginPairedFall(G.T, TFrom, TFrom + D, Yc + G.TYaw * -1.f, DriveT, 0.45f, true, G.Start, true);
	As->BeginPairedFall(G.A, AFrom, AFrom + D, Yc + G.AYaw * -1.f, DriveT, 0.3f, true, G.Start, true);
	// each closes the gap to his place in the capture at a run, not in a fixed 0.2 s
	Tk->SetFallEaseTime(FMath::Clamp(F.ET / 450.f, 0.15f, 0.35f));
	As->SetFallEaseTime(FMath::Clamp(F.EA / 450.f, 0.15f, 0.6f));
	{
		const float Mt = Tk->MassKg(), Mc = Cr->MassKg(), Ma = As->MassKg();
		const FVector Vp = (Tk->GetLastVel() * Mt + Cr->GetLastVel() * Mc + As->GetLastVel() * Ma) / FMath::Max(1.f, Mt + Mc + Ma);
		Tk->SetFallCarry(Vp);
		Cr->SetFallCarry(Vp);
		As->SetFallCarry(Vp);
	}
	PileCarrier = Cr;
	PileTackler = Tk;
	PileSpot = SimSpot;
	UE_LOG(LogTemp, Display, TEXT("TAG gang tackle %s: #%d + #%d on #%d (turned %.0f deg, tackler off %.0f cm, second %.0f cm, slide %.0f cm)"),
		*G.Name, Tk->Player ? Tk->Player->number : -1, As->Player ? As->Player->number : -1, Cr->Player ? Cr->Player->number : -1, F.Phi, F.ET, F.EA, Drive.Size2D());
	TackleCheckBegin(TEXT("gang"), Tk, Cr, As, G.T, G.C, G.A, G.Start, Sim.get(), false, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	return true;
}

FVector AMatchDirector::HuddleSpot(bool bOff, bool bQB, int32 Slot, int32 Count, const FVector& BallW, int32 InDir, float& OutYaw) const
{
	// the huddle: the offense in a ring 7.5 yd behind the ball (1.3 m radius, the quarterback at the open side
	// nearest the line, facing his men), the defense in a looser ring 5.5 yd in front; everyone faces the middle
	const FVector Down = (ToWorld(1, 0, InDir) - ToWorld(0, 0, InDir)).GetSafeNormal();
	const FVector Left = FVector::CrossProduct(FVector::UpVector, Down);
	const FVector C = BallW + Down * (bOff ? -6.f : 5.5f) * YD;
	FVector Target;
	// a ring a man can stand in: ten men on a 1.3 m half circle were 40 cm apart, inside one another (the huddle a user
	// looks at for ten seconds a play was a knot of bodies). Offense: a circle 2.1 m out, open toward the line where the
	// quarterback stands facing them; defense: a full circle.
	if (bOff && bQB) Target = C + Down * 210.f;
	else
	{
		const int32 M = FMath::Max(Count, 1);
		const double Gap = PI / 3;
		const double Ang = bOff ? Gap * 0.5 + (2 * PI - Gap) * (Slot + 0.5) / M : 2 * PI * (Slot + 0.5) / M;
		Target = C + (Down * FMath::Cos(Ang) + Left * FMath::Sin(Ang)) * (bOff ? 210.f : 215.f);
	}
	Target.Z = 0.f;
	OutYaw = (C - Target).Rotation().Yaw;
	return Target;
}

void AMatchDirector::TickPostJog(float Dt)
{
	// after the whistle (a beat for the tackle and the celebration), everyone on his feet jogs to the huddle, 4.2 yd/s
	// slowing into the last yards; men who are down, celebrating or carrying the ball stay as the sim shows them.
	// While the play's sim is up (post-play, banners) the sim's men are moved director-side (PostPos); once it is gone
	// (the play call) the Between list carries them and this drives the athletes itself.
	auto Step = [this, Dt](const tag::Player* Who, FVector& Pos, const FVector& Target) -> FVector
	{
		// each man his own pace back (3.4-4.8 m/s at the most, by who he is): at one pace for all, the search gave every
		// man on a style set the same best loop - 4+ men jogging the one capture in 22 % of frames, p80
		const float Pace = Who ? 0.82f + 0.32f * FMath::Frac(static_cast<float>(Who->id) * 0.6180339f + 0.21f) : 1.f;
		FVector To = Target - Pos;
		To.Z = 0.f;
		const float D = To.Size();
		float& Cur = PostSpd.FindOrAdd(Who);
		if (D < 30.f) { Cur = 0.f; return FVector::ZeroVector; }
		// a jog he works up to (~3 m/s2) and eases out of over the last yards; the old step started at full jog in a frame
		const float Want = FMath::Min(420.f * Pace, 120.f + D * 1.2f);
		Cur = FMath::Min(Want, Cur + 300.f * Dt);
		const float Sp = FMath::Max(Cur, 1.f);
		const FVector V = To / D * Sp;
		Pos += V * FMath::Min(Dt, D / Sp);
		return V;
	};
	// (a help-up in progress is given room: a huddle that forms on the ball formed on the two of them)
	auto Clear = [this](const FVector& T) -> FVector
	{
		const AAthlete* Dn = HelpStage > 0 ? HelpDown.Get() : nullptr;
		if (!Dn) return T;
		FVector Dd = T - Dn->GetActorLocation();
		Dd.Z = 0.f;
		const float L = static_cast<float>(Dd.Size());
		return L >= 380.f ? T : Dn->GetActorLocation() * FVector(1, 1, 0) + (L > 1.f ? Dd / L : FVector(1, 0, 0)) * 380.f + FVector(0, 0, T.Z);
	};
	if (!Sim)
	{
		if (Between.empty() || !Game) return;
		const int32 NewDir = Game->worldDir();
		int32 NOff = 0, NDef = 0;
		for (const FBetweenMan& M : Between) (M.bOff ? NOff : NDef)++;
		int32 SlotOff = 0, SlotDef = 0;
		for (FBetweenMan& M : Between)
		{
			const bool bQB = M.P->pos == tag::Pos::QB;
			const int32 Slot = M.bOff ? (bQB ? 0 : SlotOff++) : SlotDef++;
			float FaceYaw;
			// (around the ball where the next play starts - the hash it was spotted on - not where the last one ended:
			// after a run to the sideline the huddle formed 15 yd away from where the camera and the line were)
			const FVector HuddleAt = ToWorld(Game->ballOn, Game->ballY, NewDir);
			const FVector Target = Clear(HuddleSpot(M.bOff, bQB, Slot, M.bOff ? FMath::Max(NOff - 1, 1) : NDef, HuddleAt, NewDir, FaceYaw));
			const FVector V = Step(M.P, M.Pos, Target);
			AAthlete* A = AthleteFor(M.P, Game->home->hasPlayer(M.P) ? tag::TeamKey::Home : tag::TeamKey::Away);
			if (HelpBusy(A)) { M.Pos = A->GetActorLocation(); M.Yaw = A->GetActorRotation().Yaw; continue; } // (they go on from where it leaves them)
			if (A->IsDown()) continue;
			const bool bMoving = V.Size() > 60.f;
			if (bMoving) M.Yaw = V.Rotation().Yaw;
			else M.Yaw += FMath::FindDeltaAngleDegrees(M.Yaw, FaceYaw) * FMath::Clamp(Dt * 4.f, 0.f, 1.f);
			FAthleteFootball F;
			F.bLook = true;
			F.LookWorld = bMoving ? M.Pos + V.GetSafeNormal() * 800.f + FVector(0, 0, 150) : BetweenBallW + FVector(0, 0, 120);
			A->SetFootball(F, Dt);
			A->Drive(M.Pos, M.Yaw, V, 9.f, bMoving ? TEXT("run") : TEXT("idle"), 0.f, Dt, false);
		}
		return;
	}
	if (Sim->isLive() || Sim->isReturn() || !Sim->result) return;
	PostAge += Dt;
	const tag::Ball& B = Sim->ball;
	const FVector BallW = ToWorld(FMath::Clamp(B.pos.x, 2.0, 98.0), FMath::Clamp(B.pos.y, 10.0, tag::FIELD_W - 10.0), Dir);
	for (tag::Ent* E : Sim->ents)
	{
		AAthlete* A = AthleteFor(E->p, E->side == tag::Side::Off ? Game->poss : tag::other(Game->poss));
		if (HelpBusy(A)) { PostPos.erase(E); continue; }
		const bool bHolds = B.mode == tag::BallMode::Held && B.holder == E;
		// (the man a teammate has just pulled up is on his feet: the sim still has him down with the ball, and sent
		// back to its spot he slid three metres across the turf)
		if ((A->IsDown() || E->anim == tag::Anim::Celebrate || E->anim == tag::Anim::Down || bHolds) && A != HelpWalker.Get())
		{
			// (a man already walking to the huddle who goes down - knocked over late, getting up - stays where the walk had
			// him: dropped from the list he was put back on the sim's spot, a lineman stood up seven metres from where he fell)
			if (!(A->IsDown() && PostPos.find(E) != PostPos.end())) PostPos.erase(E);
			PostVel.erase(E); // (not jogging this frame)
			continue;
		}
		// the whistle is not a freeze: for the first 1.3 s everyone on his feet carries on the way he was going, easing
		// up - pursuers close on the pile and pull up a yard and a half short of it, the rest run out of it. (The tackle
		// was the whistle: 20 men stopped on the frame the tackle started.)
		{
			const float CoastT = 1.3f;
			if (PostAge < CoastT && !A->IsFalling() && !E->engaged && E->engagedBy.empty())
			{
				auto Cv = CoastV.find(E);
				if (Cv == CoastV.end())
				{
					const FVector V0 = Dir > 0 ? FVector(E->vel.x, -E->vel.y, 0.0) * YD : FVector(-E->vel.x, E->vel.y, 0.0) * YD;
					Cv = CoastV.emplace(E, V0).first;
				}
				auto Pj = PostPos.find(E);
				if (Pj == PostPos.end())
				{
					A->AbsorbVisOffset();
					Pj = PostPos.emplace(E, A->GetActorLocation()).first;
				}
				FVector V = Cv->second * (1.f - PostAge / CoastT);
				// (a man heading at the ball within 9 yd: he closes on the pile and stops short of it)
				FVector ToBall = BallW - Pj->second;
				ToBall.Z = 0.f;
				const float Db = static_cast<float>(ToBall.Size());
				if (Db < 9.f * YD && Db > 1.f && FVector::DotProduct(Cv->second, ToBall) > 0.f)
				{
					const float Sp = static_cast<float>(V.Size()) * FMath::Clamp((Db - 140.f) / 150.f, 0.f, 1.f);
					V = ToBall / Db * Sp;
				}
				Pj->second += V * Dt;
				PostVel[E] = V;
				continue;
			}
		}
		if (E->animT < 1.0) continue; // the beat after the whistle: look around, then go
		// (a blocker's or a cover man's anim clock runs from long before the whistle: the beat counts from the whistle too,
		// 0.6-1.2 s by the man, so they do not all leave on the same frame)
		if (PostAge < 0.6f + FMath::Frac(E->p->id * 0.37f) * 0.6f) continue;
		auto It = PostPos.find(E);
		// after the whistle he goes on from where his body got up: the get-up's offset from the sim spot is absorbed (Drive
		// added it on top of the actor's location, a 1.6-4.3 m pop, and easing it off slid him back over the turf)
		if (It == PostPos.end())
		{
			A->AbsorbVisOffset();
			It = PostPos.emplace(E, A->GetActorLocation()).first;
		}
		const bool bOff = E->side == tag::Side::Off;
		const std::vector<tag::Ent*>& SideList = bOff ? Sim->off : Sim->def;
		const int32 N = static_cast<int32>(SideList.size());
		int32 K = static_cast<int32>(std::find(SideList.begin(), SideList.end(), E) - SideList.begin());
		const bool bQB = E->p->pos == tag::Pos::QB;
		if (bOff) for (int32 i = 0; i < K; ++i) if (SideList[i]->p->pos == tag::Pos::QB) --K; // slots skip the QB
		float FaceYaw;
		const FVector Target = Clear(HuddleSpot(bOff, bQB, K, bOff ? FMath::Max(N - 1, 1) : N, BallW, Dir, FaceYaw));
		PostYaw[E] = FaceYaw;
		PostVel[E] = Step(E->p, It->second, Target);
	}
}

int32 AMatchDirector::ScoreShown(tag::TeamKey K, FString* Word) const
{
	if (!Game) return 0;
	const int32 Held = HudScore[K == tag::TeamKey::Home ? 0 : 1];
	int32 Pts = Held >= 0 ? Held : Game->score[K];
	// (through the replay too: it runs between the whistle and the play being applied)
	if ((State == EMatchState::Post || State == EMatchState::Replay) && Held < 0 && Sim && !Sim->isLive() && Sim->result && !Sim->isReturn())
	{
		const tag::PlayResult& R = *Sim->result;
		const tag::TeamKey Off = Game->poss, Def = tag::other(Game->poss);
		if (R.td) { if ((*R.td == tag::Side::Off ? Off : Def) == K) Pts += 6; if (Word) *Word = TEXT("TOUCHDOWN"); }
		else if (R.safety) { if (Def == K) Pts += 2; if (Word) *Word = TEXT("SAFETY"); }
	}
	return Pts;
}

void AMatchDirector::TickUmpire(float Dt)
{
	if (!Umpire || !Game) return;
	const bool bDead = Sim && !Sim->isLive() && Sim->result && !Sim->isReturn();
	const bool bCall = !Sim && !Between.empty();
	const int32 NewDir = Game->worldDir();
	const FVector Spot = ToWorld(Game->ballOn, Game->ballY, NewDir); // the next play's spot (after ApplyPlay)
	const FVector DownNew = (ToWorld(1, 0, NewDir) - ToWorld(0, 0, NewDir)).GetSafeNormal();
	// the referee's spot: 12 yd behind the ball and 5.5 yd wide of the quarterback (NFL mechanics put him 10-12 yd deep,
	// to the side of the passer, clear of the pocket; at 9 yd and 3.5 yd off he ended up in sack piles)
	const FVector Station = Spot - DownNew * 12.f * YD + FVector::CrossProduct(FVector::UpVector, DownNew) * 5.5f * YD;
	FVector Target = UmpPos;
	float Jog = 380.f;
	FVector BallW = bCall ? BetweenBallW : (Sim ? ToWorld(Sim->ball.pos.x, Sim->ball.pos.y, Dir) : Spot);
	const FVector UmpLag = Umpire->GetActorLocation() - UmpPos; // (his body against his spot, before this frame's move)
	// a man standing on the ball (the carrier up, a helper beside him, a man still holding it after the whistle): the
	// referee takes it at a long arm's length (the ball is handed or flipped over), not from inside him (he walked to 12 cm
	// of the carrier, p63 c011; at 105 cm off him the 26 Nov matrix still had him 0.7 m from a player): the spot 1.5 m off
	// that man that keeps as far from every other man there too, the nearest such spot to where he is
	bool bArmGo = false; // (on his way to an arm's-length spot: the dodge keeps no more than that from the man)
	auto ArmSpot = [&](FVector& Out) -> bool
	{
		const AAthlete* Near = nullptr;
		float ND = 140.f;
		for (const auto& KV : Athletes)
			if (const AAthlete* A = KV.Value.Get(); A && !A->IsHidden() && !A->IsDown())
				if (const float Dd = FVector::Dist2D(A->GetActorLocation(), BallW); Dd < ND) { ND = Dd; Near = A; }
		if (!Near) return false;
		const FVector C = Near->GetActorLocation();
		float BestCost = TNumericLimits<float>::Max();
		for (int32 k = 0; k < 16; ++k)
		{
			const FVector P = C + FRotator(0, k * 22.5f, 0).RotateVector(FVector(155.f, 0, 0));
			float Clear = 1e4f;
			for (const auto& KV : Athletes)
				if (const AAthlete* A = KV.Value.Get(); A && A != Near && !A->IsHidden())
					Clear = FMath::Min(Clear, static_cast<float>(FVector::Dist2D(A->GetActorLocation(), P)));
			const float Cost = static_cast<float>(FVector::Dist2D(P, UmpPos)) + (Clear < 155.f ? (155.f - Clear) * 20.f : 0.f);
			if (Cost < BestCost) { BestCost = Cost; Out = FVector(P.X, P.Y, UmpPos.Z); }
		}
		return true;
	};
	if (Sim && Sim->isReturn() && Sim->cfg.kick->punt && Sim->t < Sim->cfg.kick->kickT + 1.0)
	{
		// a punt: the referee belongs behind and wide of the punter, and goes there at a run (walking to the sideline
		// he stood in the punt formation with the ball on its way back)
		const FVector Pp = ToWorld(Sim->cfg.kick->kickX, tag::FIELD_W / 2, Dir);
		const FVector Pf = (ToWorld(Sim->cfg.kick->kickX - 1.0, tag::FIELD_W / 2, Dir) - Pp).GetSafeNormal();
		Target = Pp - Pf * 450.f + FVector::CrossProduct(Pf, FVector::UpVector) * 650.f;
		Jog = 760.f;
		// (the punt team is set the moment its play starts; he is put there with them - left at the ball he had just
		// spotted, he was penned inside the formation through the snap)
		if (Sim->t < 0.05) UmpPos = FVector(Target.X, Target.Y, UmpPos.Z);
	}
	else if (Sim && Sim->isReturn())
	{
		// a kickoff: the referee deep on the receiving team's goal line, wide on the field's edge; then he trails the return
		const tag::KickReturnSpec& K = *Sim->cfg.kick;
		const double X = Sim->ball.holder && Sim->ball.mode == tag::BallMode::Held ? Sim->ball.holder->pos.x - 10.0 : FMath::Min(K.landX - 4.0, 2.0);
		Target = ToWorld(FMath::Clamp(X, -1.0, 100.0), tag::FIELD_W - 3.0, Dir);
		if (Sim->t < 0.05) UmpPos = FVector(Target.X, Target.Y, UmpPos.Z);
	}
	else if (State == EMatchState::Live)
	{
		// holds his position through the play, but gets out of anyone's way (officials are not run through)
		float Urg = 0.f;
		const FVector Away = OfficialDodge(UmpPos, 4.f * YD, 1.6f, Urg);
		Target = UmpPos + Away;
		Jog = Away.IsNearlyZero() ? 0.f : 380.f + 500.f * Urg;
	}
	else if (bDead && State == EMatchState::Post)
	{
		// at the whistle, to the dead ball, but he waits off the pile until the men on the ball are up
		Target = BallW;
		if (Sim)
			for (const tag::Ent* E : Sim->ents)
				if (AAthlete* P = AthleteFor(E->p, E->side == tag::Side::Off ? Game->poss : tag::other(Game->poss)); P && P->IsDown() && FVector::Dist2D(P->GetActorLocation(), BallW) < 1.8f * YD)
				{
					FVector Back = UmpPos - BallW;
					Back.Z = 0.f;
					Target = BallW + Back.GetSafeNormal() * 2.6f * YD;
					break;
				}
		// a man standing on the ball: he takes it at arm's length (ArmSpot)
		const bool bArm = Target.Equals(BallW, 1.f) && ArmSpot(Target);
		bArmGo = bArm && !bUmpHasBall;
		if (!bUmpHasBall && !bUmpSpotted && FVector::Dist2D(UmpPos, BallW) < (bArm ? 320.f : 75.f) && FVector::Dist2D(UmpPos, Target) < (bArm ? 70.f : 1e4f)) bUmpHasBall = true;
	}
	else if ((bDead && (State == EMatchState::Wait || State == EMatchState::Call)) || bCall)
	{
		if (!bUmpHasBall && !bUmpSpotted)
		{
			// (a man still on the ball after the whistle: from arm's length here too, the post-play rule)
			Target = BallW;
			const bool bArm = ArmSpot(Target);
			bArmGo = bArm;
			if (bArm ? FVector::Dist2D(UmpPos, BallW) < 320.f && FVector::Dist2D(UmpPos, Target) < 70.f : FVector::Dist2D(UmpPos, BallW) < 75.f) bUmpHasBall = true;
		}
		else if (bUmpHasBall)
		{
			// carry it to the new spot and set it down
			Target = Spot;
			if (FVector::Dist2D(UmpPos, Spot) < 60.f) { bUmpHasBall = false; bUmpSpotted = true; }
		}
		else Target = Station;
	}
	else if (Sim)
	{
		// before the snap: the referee's spot, 12 yd deep and 5.5 yd wide of the quarterback
		const FVector Down = (ToWorld(1, 0, Dir) - ToWorld(0, 0, Dir)).GetSafeNormal();
		Target = ToWorld(Sim->los, Sim->by, Dir) - Down * 12.f * YD + FVector::CrossProduct(FVector::UpVector, Down) * 5.5f * YD;
		// a cut onto the formation (the teams put on their spots behind it: after a break, a hurry-up): he is put on his
		// with them (left where he was, the 26 Nov matrix had an official standing on the new ball spot inside the line)
		if (bCamCutPending && State == EMatchState::Presnap && !Sim->isReturn()) UmpPos = FVector(Target.X, Target.Y, UmpPos.Z);
	}
	// in every phase he walks around players, never through them (presnap through the huddle and the formation was the
	// worst of it); the last yard into a loose ball he has to pick up is exempt
	if (Sim && State != EMatchState::Live && FVector::Dist2D(Target, BallW) > 0.9f * YD)
	{
		float Urg = 0.f;
		const FVector Away = OfficialDodge(UmpPos, (bArmGo ? 1.8f : 3.f) * YD, 1.4f, Urg);
		if (!Away.IsNearlyZero())
		{
			Target += Away;
			Jog = FMath::Max(Jog, 340.f + 450.f * Urg);
		}
	}
	FVector To = Target - UmpPos;
	To.Z = 0.f;
	const float D = To.Size();
	FVector Vel = FVector::ZeroVector;
	if (Jog > 0.f && D > 25.f)
	{
		float Sp = FMath::Min(Jog, 110.f + D * 3.f); // a short sidestep is quick, not a stroll
		// between plays he goes to his spot round the men, not through them (OfficialSteer): the way to go this frame,
		// and at a run when the formation is coming to the line across his path
		FVector Way = To / D;
		const bool bToBall = bUmpHasBall || FVector::Dist2D(Target, BallW) < 1.2f * YD || FVector::Dist2D(Target, Spot) < 1.2f * YD;
		if (State != EMatchState::Live && !(Sim && Sim->isReturn()))
		{
			float Crowd = 0.f;
			const FVector Go = OfficialSteer(UmpPos, Target, FMath::Max(Sp, 380.f), UmpSteerSide, bToBall, &Crowd);
			FVector G2 = Go - UmpPos;
			G2.Z = 0.f;
			if (G2.Size() > 5.f) Way = G2.GetSafeNormal();
			if (State == EMatchState::Presnap && D > 3.f * YD) Sp = FMath::Max(Sp, FMath::Min(620.f, 110.f + D * 3.f));
		}
		Vel = Way * Sp;
		UmpPos += Vel * FMath::Min(Dt, D / Sp);
	}
	// (at a loose ball he picks it up: the man beside it may be close. Not while the ball is live - a carrier coming at him
	// is the one man to keep clear of - nor on his way to an arm's-length spot, which keeps him off the man anyway.)
	const bool bPickUp = State != EMatchState::Live && !bArmGo && !bUmpHasBall && !bUmpSpotted && FVector::Dist2D(UmpPos, BallW) < 1.5f * YD;
	if (!bPickUp) OfficialPushOut(UmpPos, Dt, UmpLag);
	// facing: the way he goes, or the ball when he stands
	float Yaw = Umpire->GetActorRotation().Yaw;
	if (!Vel.IsNearlyZero()) Yaw = Vel.Rotation().Yaw;
	else
	{
		const FVector At = bUmpSpotted || bCall ? Spot : BallW;
		FVector F = At - UmpPos;
		F.Z = 0.f;
		if (F.Size() > 30.f) Yaw = F.Rotation().Yaw;
	}
	// a touchdown: the referee and the wing nearer the ball give the signal - the pack's Ref_Touchdown_Signal (officials
	// had no capture of their own but locomotion; arm sweep 2)
	if (!AAthlete::ArmSweepOff() && Sim && Sim->result && Sim->result->td && State == EMatchState::Post && RefTdSerial != Sim->serial)
		if (UAnimationAsset* Sig = FAthleteAssets::Get().RefTdSignal)
		{
			RefTdSerial = Sim->serial;
			Umpire->PlayOneShot(Sig);
			AAthlete* Wing = nullptr;
			for (int32 R = 1; R <= 2 && R < Crew.Num(); ++R)
				if (Crew[R] && (!Wing || FVector::Dist2D(Crew[R]->GetActorLocation(), BallW) < FVector::Dist2D(Wing->GetActorLocation(), BallW))) Wing = Crew[R];
			if (Wing) Wing->PlayOneShot(Sig);
			UE_LOG(LogTemp, Display, TEXT("TAG referee touchdown signal (and the nearer wing)"));
		}
	FAthleteFootball F;
	F.bLook = true;
	F.LookWorld = bUmpHasBall ? UmpPos + Vel.GetSafeNormal() * 800.f + FVector(0, 0, 150) : (bUmpSpotted || bCall ? Spot : BallW) + FVector(0, 0, 20);
	Umpire->SetFootball(F, Dt);
	Umpire->Drive(UmpPos, Yaw, Vel, 7.f, Vel.IsNearlyZero() ? TEXT("idle") : TEXT("run"), 0.f, Dt, false);
	// without a sim nothing else places the ball: in his hand, or spotted for the next play
	if (!Sim && Ball)
	{
		FVector HoldLoc;
		FRotator HoldRot;
		if (bUmpHasBall && Umpire->GetBallHold(HoldLoc, HoldRot)) { Ball->SetActorLocation(HoldLoc); Ball->SetActorRotation(HoldRot); }
		else if (bUmpSpotted) { Ball->SetActorLocation(Spot + FVector(0, 0, 9)); Ball->SetActorRotation(FRotator(0, NewDir > 0 ? 0 : 180, 0)); }
	}
}

void AMatchDirector::GatherOfficialBodies()
{
	OffBodies.Reset();
	if (!Game) return;
	if (Sim)
	{
		const bool bPre = State == EMatchState::Presnap;
		const bool bSimMoves = State == EMatchState::Live || bPre;
		const FVector Down = ToWorld(1, 0, Dir) - ToWorld(0, 0, Dir);
		const FVector Left = ToWorld(0, 1, Dir) - ToWorld(0, 0, Dir);
		for (const tag::Ent* E : Sim->ents)
		{
			AAthlete* A = AthleteFor(E->p, Game->home->hasPlayer(E->p) ? tag::TeamKey::Home : tag::TeamKey::Away);
			if (!A || A->IsHidden()) continue;
			FOffBody B;
			B.P = A->GetActorLocation();
			B.P.Z = 0.f;
			if (bSimMoves) B.V = Down * E->vel.x + Left * E->vel.y;
			else if (auto It = PostVel.find(E); It != PostVel.end()) B.V = FVector(It->second.X, It->second.Y, 0.f);
			// a man walking to his spot: where he will be is known (the formation he is lining up in)
			if (bPre && E->shiftTo)
			{
				B.bDest = true;
				B.Dest = ToWorld(E->shiftTo->x, E->shiftTo->y, Dir);
			}
			OffBodies.Add(B);
		}
	}
	else
		for (const FBetweenMan& M : Between)
		{
			AAthlete* A = AthleteFor(M.P, Game->home->hasPlayer(M.P) ? tag::TeamKey::Home : tag::TeamKey::Away);
			FOffBody B;
			B.P = A && !A->IsHidden() ? A->GetActorLocation() : M.Pos;
			B.P.Z = 0.f;
			OffBodies.Add(B);
		}
}

FVector AMatchDirector::OfficialSteer(const FVector& Pos, const FVector& Target, float Pace, float& Turn, bool bCoreOk, float* OutCrowd) const
{
	// An official on his way to a spot looks down a fan of headings (straight at it, and up to 105 degrees either side)
	// a few strides ahead, with every player moved on to where he will be by then - his sim velocity, or his walk to
	// the place he is lining up - and takes the heading that still makes ground without crossing anyone's path or
	// standing where a man is about to arrive. Before the snap the heart of the formation (the two lines and the
	// backfield around the ball) is out of bounds to him altogether: he goes round behind it. He keeps going round
	// the way he started (Turn) rather than dithering in front of a moving group. The reactive dodge and the push-out
	// stay as the last resort; this is what stops him needing them.
	if (OutCrowd) *OutCrowd = 0.f;
	static const bool bNoSteer = FParse::Param(FCommandLine::Get(), TEXT("TagNoRefSteer")); // (the straight line, for the comparison)
	FVector To = Target - Pos;
	To.Z = 0.f;
	const float D = static_cast<float>(To.Size());
	if (bNoSteer || D < 140.f || OffBodies.Num() == 0) { Turn = 0.f; return Target; }
	const FVector Fwd = To / D;
	const float Sp = FMath::Max(Pace, 260.f);
	const float Y = static_cast<float>(YD);
	const bool bCore = !bCoreOk && Sim && State == EMatchState::Presnap && !Sim->isReturn();
	FVector CoreC = FVector::ZeroVector;
	if (bCore) CoreC = ToWorld(Sim->los - 1.0, Sim->by, Dir);
	static const float Angles[] = {0.f, 20.f, -20.f, 40.f, -40.f, 60.f, -60.f, 80.f, -80.f, 105.f, -105.f};
	float BestS = -1e9f, BestA = 0.f;
	FVector BestDir = Fwd;
	for (const float Ang : Angles)
	{
		const FVector Dv = Fwd.RotateAngleAxis(Ang, FVector::UpVector);
		float Danger = 0.f;
		for (int32 k = 1; k <= 4; ++k)
		{
			const float S = k * 1.6f * Y;
			if (Ang == 0.f && S > D + 0.5f * Y) break; // (straight at it: no further than the spot)
			const FVector Pt = Pos + Dv * S;
			const float Tk = S / Sp, Wk = 1.f / FMath::Sqrt(static_cast<float>(k));
			for (const FOffBody& B : OffBodies)
			{
				FVector Bp = B.P;
				if (B.bDest)
				{
					FVector Td = B.Dest - B.P;
					Td.Z = 0.f;
					const float Ld = static_cast<float>(Td.Size());
					if (Ld > 1.f) Bp += Td / Ld * FMath::Min(Ld, 4.5f * Y * Tk);
					const float De = static_cast<float>(FVector::Dist2D(B.Dest, Pt));
					if (De < 3.2f * Y) Danger += 0.5f * FMath::Exp(-(De * De) / (2.f * FMath::Square(1.3f * Y))) * Wk;
				}
				else Bp += B.V * Tk;
				const float Dd = static_cast<float>(FVector::Dist2D(Bp, Pt));
				if (Dd < 4.f * Y) Danger += FMath::Exp(-(Dd * Dd) / (2.f * FMath::Square(1.3f * Y))) * Wk;
			}
			if (bCore && FMath::Abs(static_cast<float>(Pt.X - CoreC.X)) < 6.5f * Y && FMath::Abs(static_cast<float>(Pt.Y - CoreC.Y)) < 8.f * Y) Danger += 2.5f * Wk;
			// (the benches and the wall: nowhere to go round out there)
			if (FMath::Abs(static_cast<float>(Pt.Y)) > (tag::FIELD_W / 2 + 4.0) * Y || Pt.X < -12.5 * Y || Pt.X > 112.5 * Y) Danger += 3.f;
		}
		float Sc = FMath::Cos(FMath::DegreesToRadians(Ang)) * 1.2f - Danger * 1.5f;
		if (Ang != 0.f && Turn != 0.f && (Ang > 0.f) == (Turn > 0.f)) Sc += 0.25f;
		if (Ang == 0.f && OutCrowd) *OutCrowd = Danger;
		if (Sc > BestS)
		{
			BestS = Sc;
			BestA = Ang;
			BestDir = Dv;
		}
	}
	Turn = BestA == 0.f ? 0.f : FMath::Sign(BestA);
	if (BestA == 0.f) return Target;
	return Pos + BestDir * FMath::Min(D, 4.f * Y);
}

FVector AMatchDirector::OfficialDodge(const FVector& Pos, float Keep, float Look, float& Urgency)
{
	Urgency = 0.f;
	FVector Push = FVector::ZeroVector;
	if (!Sim || !Game) return Push;
	const FVector Down = (ToWorld(1, 0, Dir) - ToWorld(0, 0, Dir));
	const FVector Left = (ToWorld(0, 1, Dir) - ToWorld(0, 0, Dir));
	for (const tag::Ent* E : Sim->ents)
	{
		AAthlete* P = AthleteFor(E->p, E->side == tag::Side::Off ? Game->poss : tag::other(Game->poss));
		if (!P || P->IsHidden()) continue;
		FVector R = Pos - P->GetActorLocation();
		R.Z = 0.f;
		if (R.SizeSquared() > FMath::Square(Keep + 12.f * YD)) continue;
		// his velocity in the world (the sim's, a man walking to his spot included); after the whistle the director jogs
		// the men itself, so a dead play's men count as standing
		const bool bSimMoves = State == EMatchState::Live || State == EMatchState::Presnap;
		const FVector V = bSimMoves ? Down * E->vel.x + Left * E->vel.y : FVector::ZeroVector;
		const float V2 = V.SizeSquared();
		const float Tc = V2 > 1.f ? FMath::Clamp(static_cast<float>(FVector::DotProduct(R, V) / V2), 0.f, Look) : 0.f;
		FVector Rc = R - V * Tc; // where he passes us, relative
		float Dc = Rc.Size();
		if (Dc >= Keep) continue;
		if (Dc < 5.f) { Rc = FVector::CrossProduct(FVector::UpVector, V.IsNearlyZero() ? R : V).GetSafeNormal() * 5.f; Dc = 5.f; }
		const float W = 1.f / (1.f + Tc * 2.f); // the man arriving now matters more than the one a second away
		// (before the snap, out of the lane of a man walking at him to the side away from the ball, as OfficialPushOut: a
		// line walking up abreast pushed him both ways at once, and he stood until it reached him)
		const bool bFlow = State == EMatchState::Presnap && V2 > FMath::Square(60.f) && FVector::DotProduct(R, V) > 0.f;
		Push += (bFlow ? FlowSide(Pos, V, R) : Rc / Dc) * (Keep - Dc) * W * 2.4f;
		Urgency = FMath::Max(Urgency, (Keep - Dc) / Keep * W);
	}
	return Push;
}

FVector AMatchDirector::FlowSide(const FVector& Pos, const FVector& V, const FVector& R) const
{
	// square to the man's walk: on the side of his lane the official is already on (R: the official off the man), or when
	// he is square in it, the side away from the ball (out of the formation the man is walking into, never ahead of him)
	FVector Side = FVector::CrossProduct(FVector::UpVector, FVector(V.X, V.Y, 0.f)).GetSafeNormal();
	const float Lat = static_cast<float>(FVector::DotProduct(Side, FVector(R.X, R.Y, 0.f)));
	if (FMath::Abs(Lat) > 40.f)
	{
		if (Lat < 0.f) Side = -Side;
		return Side;
	}
	FVector Out = Pos - (Sim ? ToWorld(Sim->los, Sim->by, Dir) : Pos);
	Out.Z = 0.f;
	if (FVector::DotProduct(Side, Out) < 0.f) Side = -Side;
	return Side;
}

void AMatchDirector::OfficialPushOut(FVector& Pos, float Dt, const FVector& Lag)
{
	if (!Sim || !Game) return;
	// (a man at a sprint closes 13-15 cm a frame: at 6 m/s the push lost to him and the p39 game had an official inside
	// a metre of a player in 0.9 % of frames, most of them the back judge with a receiver or a safety running at him)
	// (measured from his body as shown, not his spot - the drive's follower lags the spot by up to 30 cm standing, more on
	// the move - and 1.55 m kept: the rule is an official 1.5 m clear of any player but at the ball)
	const float Min = 155.f;
	FVector LagXY(Lag.X, Lag.Y, 0.f);
	if (LagXY.SizeSquared() > FMath::Square(150.f)) LagXY = FVector::ZeroVector; // (he was just put somewhere: no lag to speak of)
	const FVector Shown = Pos + LagXY.GetClampedToMaxSize(80.f);
	// before the snap, a man walking up to the line is not pushed ahead of: the official steps out of his lane to the side
	// he is already on (FlowSide). Pushed straight away from them, the line walking up from the huddle bulldozed the umpire
	// 8 m to the ball, where he was penned between the two lines through the snap (the 26 Nov matrix, cells 03 and 09).
	const bool bPre = State == EMatchState::Presnap;
	const FVector Down = ToWorld(1, 0, Dir) - ToWorld(0, 0, Dir);
	const FVector Left = ToWorld(0, 1, Dir) - ToWorld(0, 0, Dir);
	FVector Out = FVector::ZeroVector;
	float Worst = 0.f;
	for (const tag::Ent* E : Sim->ents)
	{
		AAthlete* P = AthleteFor(E->p, E->side == tag::Side::Off ? Game->poss : tag::other(Game->poss));
		if (!P || P->IsHidden()) continue;
		FVector R = Shown - P->GetActorLocation();
		R.Z = 0.f;
		const float L = R.Size();
		if (L >= Min) continue;
		FVector U = L > 1.f ? R / L : FVector(1, 0, 0);
		float Need = Min - L;
		if (bPre)
		{
			const FVector V = Down * E->vel.x + Left * E->vel.y;
			if (V.SizeSquared2D() > FMath::Square(60.f) && FVector::DotProduct(R, V) > 0.f)
			{
				// (out of his lane: what is missing across it, not to his body - a man jogging straight at an official
				// standing on the edge of the keep came through it to 42-49 cm, each frame's push only the frame's step)
				U = FlowSide(Shown, V, R);
				Need = Min - FMath::Abs(static_cast<float>(FVector::DotProduct(R, U)));
			}
		}
		Out += U * Need;
		Worst = FMath::Max(Worst, Need);
	}
	if (Out.IsNearlyZero()) return;
	Pos += Out.GetSafeNormal() * FMath::Min(Worst, 1100.f * Dt);
}

void AMatchDirector::TickCrew(float Dt)
{
	if (!Game || Crew.Num() == 0) return;
	const double FW = tag::FIELD_W;
	const bool bReturn = Sim && Sim->isReturn();
	const bool bDead = Sim && !Sim->isLive() && Sim->result && !bReturn;
	const bool bLiveNow = Sim && State == EMatchState::Live && !bReturn;
	// the frame the crew lines up in: the play on the field (presnap, live, the dead ball), else the next play's spot
	const bool bPlayFrame = Sim && !bReturn && (State == EMatchState::Presnap || State == EMatchState::Live || State == EMatchState::Post);
	const int32 D = bPlayFrame ? Dir : Game->worldDir();
	// line of scrimmage and the ball's lateral spot in the offense frame (x toward the defense's goal)
	const double L = bPlayFrame ? Sim->los : Game->ballOn;
	const double By = bPlayFrame ? Sim->by : Game->ballY;
	// where the ball is now (offense frame) and the deepest man downfield
	double BallX = L, BallY = By, Deepest = L;
	if (bPlayFrame)
	{
		const tag::V2 Bp = Sim->ball.holder ? Sim->ball.holder->pos : tag::V2{Sim->ball.pos.x, Sim->ball.pos.y};
		BallX = Bp.x;
		BallY = Bp.y;
		for (const tag::Ent* E : Sim->ents)
			if (E->state != tag::EntState::Down) Deepest = FMath::Max(Deepest, E->pos.x);
	}
	const FVector Down = (ToWorld(1, 0, D) - ToWorld(0, 0, D)).GetSafeNormal();
	const FVector Across = FVector::CrossProduct(FVector::UpVector, Down);
	if (CrewVel.Num() != Crew.Num())
	{
		CrewVel.Init(FVector::ZeroVector, Crew.Num());
		CrewTgt = CrewPos;
		CrewYaw.Init(0.f, Crew.Num());
		CrewGo.Init(false, Crew.Num());
		CrewSteerSide.Init(0.f, Crew.Num());
		for (int32 R = 0; R < Crew.Num(); ++R) if (Crew[R]) CrewYaw[R] = Crew[R]->GetActorRotation().Yaw;
	}
	// a kick play (a live return) starts: the crew is put on its kick stations at once (they ran across the field from
	// where the last play had left them at the opening kickoff)
	const bool bNewKick = bReturn && Sim->serial != CrewKickSerial;
	if (bNewKick) CrewKickSerial = Sim->serial;
	for (int32 R = 0; R < Crew.Num(); ++R)
	{
		AAthlete* A = Crew[R];
		if (!A) continue;
		FVector& Pos = CrewPos[R];
		const FVector Lag = A->GetActorLocation() - Pos; // (his body against his spot, before this frame's move)
		const bool bWing = R == 1 || R == 2;
		// his sideline is his for the game (in the world: the wings and the side judges did not swap sides at every change
		// of possession), as an offense-frame y for this frame's direction
		const bool bWorldLeft = R == 1 || R == 3 || R == 5;
		const double Side = (bWorldLeft == (D > 0)) ? FW + 1.2 : -1.2;
		const double DeepX = R == 5 ? FMath::Min(L + 25.0, 110.5) : FMath::Min(L + 20.0, 100.5);
		FVector Target = Pos;
		float Jog = 380.f;
		if (bReturn)
		{
			// a kick: each on his station for it, on his own sideline (the return play's frame: the receiving team attacks +x)
			const tag::KickReturnSpec& K = *Sim->cfg.kick;
			const bool bAir = Sim->ball.mode == tag::BallMode::Air || Sim->ball.mode == tag::BallMode::Dead;
			double X = K.landX;
			if (K.punt)
				X = R == 0 ? K.coverX - 1.0 : (R == 1 || R == 2) ? K.coverX : (R == 3 || R == 4) ? K.landX + 3.0 : K.landX - 6.0;
			else
				X = R == 0 ? K.kickX : (R == 1 || R == 2) ? 20.0 : (R == 3 || R == 4) ? K.coverX - 5.0 : 1.0;
			// once the returner has it: the wings run their sidelines with the ball, the others trail it
			if (!bAir && Sim->ball.holder)
			{
				const double Bx = Sim->ball.holder->pos.x;
				X = bWing ? Bx : (R == 3 || R == 4) ? Bx - 6.0 : Bx - 12.0;
			}
			Target = ToWorld(FMath::Clamp(X, -1.0, 101.0), R == 0 ? (bWorldLeft == (Dir > 0) ? FW + 2.5 : -2.5) : (bWorldLeft == (Dir > 0) ? FW + 1.2 : -1.2), Dir);
			Jog = 600.f;
			if (bNewKick) { Pos = FVector(Target.X, Target.Y, Pos.Z); CrewTgt[R] = Pos; CrewVel[R] = FVector::ZeroVector; }
		}
		else if (State == EMatchState::Kick)
		{
			// a staged kick (a field goal, a touchback): out of the way on their own sidelines
			if (R == 0) Target = ToWorld(FMath::Clamp(Game->ballOn, 5.0, 95.0), FW + 2.5, Game->worldDir());
			else Target.Y = ToWorld(0, Side, D).Y;
			Jog = 600.f;
		}
		else if (bLiveNow)
		{
			const bool bPast = BallX > L + 0.5 || Sim->passThrown;
			if (R == 0) { Target = Pos; Jog = 0.f; }                                   // holds, gives ground below
			else if (bWing) { Target = ToWorld(bPast ? BallX : L, Side, D); Jog = 600.f; } // the forward progress line
			else
			{
				// the deep men hold their depth and give ground only when the play comes to them: 10 yd off the ball once
				// it is past the line, 4 yd off a man about to run by. (They kept 6 yd beyond the deepest man at all
				// times, so every release and every safety's drop had three officials running.)
				double X = DeepX;
				if (bPast) X = FMath::Max(X, BallX + 10.0);
				if (Deepest > DeepX - 4.0) X = FMath::Max(X, Deepest + 4.0);
				const double Y = R == 5 ? FW / 2 + (BallY - FW / 2) * 0.3 : Side;
				Target = ToWorld(FMath::Min(X, 111.0), Y, D);
				Jog = 620.f;
			}
		}
		else if (bDead && State == EMatchState::Post)
		{
			// the whistle: the wings mark the spot on their sideline, the others close in on the ball (not onto the pile)
			const FVector BallW = ToWorld(BallX, BallY, D);
			if (bWing) Target = ToWorld(BallX, Side, D);
			else
			{
				FVector Off = Pos - BallW;
				Off.Z = 0.f;
				const float Want = (R == 0 ? 6.f : 10.f) * YD;
				Target = Off.Size() > Want ? BallW + Off.GetSafeNormal() * Want : Pos;
			}
			Jog = 450.f;
		}
		else
		{
			// lined up for the play: the umpire 12 yd deep opposite the referee, the wings at the line, the deep men off the ball
			const FVector LosW = ToWorld(L, By, D);
			if (R == 0) Target = LosW - Down * 12.f * YD - Across * 5.5f * YD;
			else if (bWing) Target = ToWorld(L, Side, D);
			// the deep men stand off the deepest man (a safety at 15-18 yd, a defense walking back from its huddle)
			// the deep men at their depth for the down (20 and 25 yd off the ball: behind any safety), wherever the
			// defense is walking
			else Target = ToWorld(FMath::Min(DeepX, 111.0), R == 5 ? FW / 2 + (By - FW / 2) * 0.3 : Side, D);
			// (a long way to go - the last play ended 30 yd from here, or the offense is in a hurry - and he runs: at a
			// jog the back judge was still among the safeties at the snap. A few yards he walks.)
			Jog = FMath::Clamp(static_cast<float>(FVector::Dist2D(Target, Pos)) * 0.5f, 170.f, 700.f);
			// a cut onto the formation (the teams put on their spots behind it): the crew is put on its spots with them (the
			// 26 Nov matrix had the umpire left on the new ball spot, inside the line, as the formation appeared round him)
			if (bCamCutPending && Sim && State == EMatchState::Presnap)
			{
				Pos = FVector(Target.X, Target.Y, Pos.Z);
				CrewTgt[R] = Pos;
				CrewVel[R] = FVector::ZeroVector;
				CrewGo[R] = false;
			}
		}
		// no official is run through, and none walks through a player: give ground to anyone near (3 yd while the ball is
		// live, 2 yd otherwise)
		if (Sim)
		{
			float Urg = 0.f;
			const FVector Away = OfficialDodge(Pos, (bLiveNow ? 3.f : 1.8f) * YD, bLiveNow ? 1.6f : 1.4f, Urg);
			if (!Away.IsNearlyZero())
			{
				Target += Away;
				Jog = FMath::Max(Jog, 380.f + 500.f * Urg);
			}
		}
		// the spot he goes to moves on only when the new one is a yard and a half off it (it followed every step of the
		// deepest man and the ball carrier: the crew twitched about the field)
		if (FVector::Dist2D(Target, CrewTgt[R]) > 1.5f * YD || !CrewGo[R]) CrewTgt[R] = Target;
		FVector To = CrewTgt[R] - Pos;
		To.Z = 0.f;
		const float Dd = To.Size();
		// an official stands until there is somewhere to go (his spot more than 1.5 m off), then accelerates to his pace and
		// eases into the spot (he went from standing to a sprint in a frame, and back)
		if (Dd > 150.f && Jog > 0.f) CrewGo[R] = true;
		else if (Dd < 30.f || Jog <= 0.f) CrewGo[R] = false;
		const float Arrive = FMath::Sqrt(2.f * 450.f * FMath::Max(0.f, Dd - 20.f));
		// between plays the way to his spot goes round the men (OfficialSteer): behind the formation as it comes to the
		// line, outside the huddle - and he hustles when there is a way to go before the snap
		FVector Way = Dd > 1.f ? To / Dd : FVector::ZeroVector;
		if (!bLiveNow && !bReturn && CrewGo[R] && Dd > 1.f && CrewSteerSide.IsValidIndex(R))
		{
			if (State == EMatchState::Presnap && Dd > 4.f * YD) Jog = FMath::Max(Jog, 560.f);
			const FVector Go = OfficialSteer(Pos, CrewTgt[R], FMath::Max(Jog, 300.f), CrewSteerSide[R], false);
			FVector G2 = Go - Pos;
			G2.Z = 0.f;
			if (G2.Size() > 5.f) Way = G2.GetSafeNormal();
		}
		const FVector Want = CrewGo[R] && Dd > 1.f ? Way * FMath::Min(Jog, Arrive) : FVector::ZeroVector;
		CrewVel[R] = FMath::VInterpConstantTo(CrewVel[R], Want, Dt, 650.f);
		FVector Vel = CrewVel[R];
		Pos += Vel * Dt;
		const FVector Was = Pos;
		OfficialPushOut(Pos, Dt, Lag);
		if (Dt > 0.f) Vel += (Pos - Was) / Dt; // (a push out of a player's way is motion too: he steps, he does not skate)
		// facing: the ball, unless he is running somewhere (and a wing on the move watches the ball as he goes), turned at
		// a man's pace (it flipped 180 degrees between running and standing)
		const FVector BallNow = bPlayFrame ? ToWorld(BallX, BallY, D) : ToWorld(Game->ballOn, Game->ballY, Game->worldDir());
		FVector Fb = BallNow - Pos;
		Fb.Z = 0.f;
		float WantYaw = CrewYaw[R];
		if (Vel.Size2D() > 350.f && !(bWing && (bLiveNow || bReturn))) WantYaw = Vel.Rotation().Yaw;
		else if (Fb.Size() > 30.f) WantYaw = Fb.Rotation().Yaw;
		CrewYaw[R] = FMath::FixedTurn(CrewYaw[R], WantYaw, 300.f * Dt);
		const float Yaw = CrewYaw[R];
		FAthleteFootball F;
		F.bLook = true;
		F.LookWorld = BallNow + FVector(0, 0, 60);
		A->SetFootball(F, Dt);
		A->Drive(Pos, Yaw, Vel, 7.f, Vel.Size2D() < 40.f ? TEXT("idle") : TEXT("run"), 0.f, Dt, false);
	}
}

void AMatchDirector::StartPileOn(AAthlete* As, AAthlete* Cr)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	if (!As || !Cr || As->IsFalling()) return;
	// the man joining a tackle already going down: the third man of the pack's gang tackles (wrapping the carrier from
	// his side and going down on the pile, root driven), the capture whose man is nearest where ours is relative to the
	// carrier. The keyed pile-on clip (a dive onto bodies) somersaulted head first into the turf when it landed short.
	{
		const FVector H = Cr->GetActorLocation();
		const float Yc0 = Cr->GetActorRotation().Yaw;
		const FVector AsAt = As->GetActorLocation();
		const double NowT = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		const bool bAnother = LastPileCarrier.Get() == Cr && NowT - LastPileT < 1.5; // a second man in: not on the first one
		int32 BestG = -1;
		float BestCost = 1e9f, BestPhi = 0.f, BestEA = 0.f;
		for (int32 g = 0; g < A.PackGangs.Num(); ++g)
		{
			const FAthleteAssets::FPackGang& G = A.PackGangs[g];
			if (!G.A) continue;
			for (float Phi = -180.f; Phi < 180.f; Phi += 15.f)
			{
				const FVector From = H + FRotator(0, Yc0 + Phi, 0).RotateVector(ClipToActor(G.Ax, G.Ay));
				const float EA = FVector::Dist2D(From, AsAt);
				// (turned freely: the carrier is already down in his own capture; nearer his own heading is a little better)
				float Cost = EA + FMath::Abs(Phi) * 0.15f;
				if (bAnother && FVector::Dist2D(From, LastPileSpot) < 90.f) Cost += 300.f;
				if (Cost < BestCost) { BestCost = Cost; BestG = g; BestPhi = Phi; BestEA = EA; }
			}
		}
		if (BestG >= 0 && BestEA < 220.f)
		{
			const FAthleteAssets::FPackGang& G = A.PackGangs[BestG];
			const float Yc = Yc0 + BestPhi;
			const FVector AFrom = H + FRotator(0, Yc, 0).RotateVector(ClipToActor(G.Ax, G.Ay));
			As->BeginPairedFall(G.A, AFrom, AFrom, Yc - G.AYaw, 0.3f, 0.3f, true, G.Start, true);
			As->SetFallEaseTime(FMath::Clamp(BestEA / 450.f, 0.15f, 0.45f));
			As->SetFallCarry(As->GetLastVel() * 0.5f); // half his run into a pile that is already going down
			LastPileCarrier = Cr;
			LastPileT = NowT;
			LastPileSpot = AFrom;
			LastPileDir = FRotator(0, Yc - G.AYaw, 0).Vector();
			UE_LOG(LogTemp, Display, TEXT("TAG pile-on #%d on #%d: %s third man (off %.0f cm, turned %.0f)"), As->Player ? As->Player->number : -1,
				Cr->Player ? Cr->Player->number : -1, *G.Name, BestEA, BestPhi);
			return;
		}
	}
	if (!A.PileOn) return;
	// he lands across the pile: chest over the carrier's body (the carrier's anchor end, pelvis a little past it), in a
	// slot of his own. The tackler already lies on one side of the carrier: the first man in lands on the far side of
	// the carrier from him, a second one beside the first (two men landing their chests on the same point from two sides
	// lay inside each other, 33 cm of torso overlap, -TagTackleCam)
	// (a pile spot more than 4 m off the carrier is not his: the actor then - see AAthlete::BeginPairedFall's PileBodyEnd)
	const bool bPileFar = Cr->IsFalling() && FVector::Dist2D(Cr->PileBodyEnd, Cr->GetActorLocation()) >= 400.f;
	if (bPileFar)
		UE_LOG(LogTemp, Warning, TEXT("TAG pile-on #%d on #%d: the carrier's pile spot %.0f cm off him, landing on his body instead"), As->Player ? As->Player->number : -1,
			Cr->Player ? Cr->Player->number : -1, FVector::Dist2D(Cr->PileBodyEnd, Cr->GetActorLocation()));
	const FVector Pile0 = Cr->IsFalling() && !bPileFar ? Cr->PileBodyEnd : Cr->GetActorLocation();
	FVector ToPile = Pile0 - As->GetActorLocation();
	ToPile.Z = 0.f;
	if (!ToPile.Normalize()) ToPile = As->GetActorForwardVector();
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const bool bSecond = LastPileCarrier.Get() == Cr && Now - LastPileT < 1.5;
	FVector Away = FVector::ZeroVector; // from the tackler's body across the carrier's
	if (AAthlete* PT = PileTackler.Get(); PT && PileCarrier.Get() == Cr && PT->IsFalling())
		Away = (Pile0 - PT->BoneWorld(TEXT("spine_03"))).GetSafeNormal2D();
	FVector Pile = Pile0 + Away * 28.f;
	if (bSecond)
	{
		// beside the first man, on the side he comes from, landing a little short (hands on the pile) along his own line
		// (turning him to lie parallel swung his diving chest through his neighbour on the way in)
		const FVector Side = FVector::CrossProduct(FVector::UpVector, LastPileDir).GetSafeNormal2D();
		const float Sgn = FVector::DotProduct(As->GetActorLocation() - Pile0, Side) >= 0.f ? 1.f : -1.f;
		Pile = LastPileSpot + Side * Sgn * 75.f - ToPile * 40.f;
		if (FVector::DotProduct(ToPile, LastPileDir) > 0.5f) ToPile = LastPileDir; // nearly the same line: alongside him
	}
	LastPileCarrier = Cr;
	LastPileT = Now;
	LastPileDir = ToPile;
	LastPileSpot = Pile;
	const FVector At = Pile - ToPile * 165.f; // the pile-on clip lands his chest 1.65 m ahead of where it starts
	As->BeginPairedFall(A.PileOn, At, At, ToPile.Rotation().Yaw, 0.3f, 0.25f, true);
	// from a few yards off he covers the ground at a sprinter's ~4.5 m/s, not in the fixed 0.2 s ease (3 m in six frames)
	As->SetFallEaseTime(FMath::Clamp(FVector::Dist2D(As->GetActorLocation(), At) / 450.f, 0.2f, 0.7f));
}

void AMatchDirector::TickTackleTest(float Dt)
{
	// a back running at 7 m/s along +X; the tackler arrives from the rep's angle. Reps (5 s): 0 head on (no gain),
	// 1 head on (falls forward for yards), 2 from behind, 3 from his right, 4 from his left, 5 shoestring dive,
	// 6 head on with a second man piling on
	if (Lineup.Num() < 5) return;
	// (once the pack library's first draw is in: the reps play its two-man captures, not only the placed pairs)
	static float TStart = -1.f;
	if (TStart < 0.f)
	{
		if (FAthleteAssets::Get().bPackLoading || FAthleteAssets::Get().PackBatch == 0) return;
		TStart = LineupT;
	}
	const float T = LineupT - TStart;
	AAthlete* Cr = Lineup[1];
	AAthlete* Tk = Lineup[3];
	AAthlete* As = Lineup[4];
	const int32 Rep = static_cast<int32>(T / 5.f);
	const float R = T - Rep * 5.f;
	if (Rep > 6)
	{
		FPlatformMisc::RequestExit(false);
		return;
	}
	static int32 LastRep = -1;
	static bool bHit = false, bPile = false;
	if (Rep != LastRep)
	{
		LastRep = Rep;
		bHit = bPile = false;
		Cr->ResetPose();
		Tk->ResetPose();
		As->ResetPose();
	}
	for (int32 i = 0; i < Lineup.Num(); ++i) Lineup[i]->SetActorHiddenInGame(!(i == 1 || i == 3 || (i == 4 && Rep == 6)));
	const FVector O = TagCoords::ToWorld(40, tag::FIELD_W * 0.5, 1);
	const float HitT = 1.2f, RunV = 700.f;
	const float Run = FMath::Min(R, HitT);
	const FVector CrP = O + FVector(-RunV * (HitT - Run), 0.f, 0.f);
	// where the tackler comes from (relative to the hit point), and how fast
	FVector From(260.f, 0.f, 0.f);
	if (Rep == 2) From = FVector(-260.f, 0.f, 0.f);
	else if (Rep == 3) From = FVector(40.f, 300.f, 0.f);
	else if (Rep == 4) From = FVector(40.f, -300.f, 0.f);
	else if (Rep == 5) From = FVector(-320.f, 60.f, 0.f);
	const float Approach = FMath::Clamp(R / HitT, 0.f, 1.f);
	const FVector TkP = O + From * (1.f - Approach) * 1.6f + From.GetSafeNormal() * 55.f;
	const FVector TkV = R < HitT ? -From.GetSafeNormal() * 600.f : FVector::ZeroVector;
	auto Foot = [&](AAthlete* Who) { FAthleteFootball F; F.bHasBall = Who == Cr; Who->SetFootball(F, Dt); };
	Foot(Cr);
	Foot(Tk);
	Foot(As);
	if (!bHit && R >= HitT)
	{
		bHit = true;
		const FVector Spot = O + FVector(Rep == 1 ? 230.f : Rep == 0 || Rep == 6 ? -40.f : 120.f, 0.f, 0.f);
		const int32 Pairs[] = {FAthleteAssets::PairDrive, FAthleteAssets::PairOver, FAthleteAssets::PairBehind,
			FAthleteAssets::PairSide, FAthleteAssets::PairSide, FAthleteAssets::PairDive, FAthleteAssets::PairDrive};
		StartPairedTackle(Tk, Cr, Spot, Pairs[Rep], Rep == 5);
	}
	if (Rep == 6 && bHit && !bPile && R >= HitT + 0.25f)
	{
		bPile = true;
		StartPileOn(As, Cr);
	}
	// sim-side positions (the falls own the bodies once they start)
	const FVector Spot = O;
	Cr->Drive(R < HitT ? CrP : Spot, 0.f, R < HitT ? FVector(RunV, 0, 0) : FVector::ZeroVector, 9.f, R < HitT ? TEXT("run") : TEXT("down"), R, Dt, false);
	Tk->Drive(R < HitT ? TkP : O + From.GetSafeNormal() * 60.f, TkV.IsNearlyZero() ? (-From).Rotation().Yaw : TkV.Rotation().Yaw, TkV, 9.f,
		R < HitT ? TEXT("run") : TEXT("tackle"), R, Dt, false);
	if (Rep == 6)
	{
		const FVector AsFrom = O + FVector(300.f, -220.f, 0.f);
		const FVector AsP = R < HitT + 0.25f ? FMath::Lerp(AsFrom + FVector(400.f, -300.f, 0.f), AsFrom, FMath::Clamp(R / (HitT + 0.25f), 0.f, 1.f)) : AsFrom;
		As->Drive(AsP, (O - AsFrom).Rotation().Yaw, R < HitT + 0.25f ? (AsFrom - (AsFrom + FVector(400.f, -300.f, 0.f))) : FVector::ZeroVector, 9.f,
			TEXT("run"), R, Dt, false);
	}
	// side on from the field side, low, 30 mm
	const FVector At = O + FVector(60.f, -720.f, 170.f), Look = O + FVector(60.f, 0.f, 60.f);
	Camera->SetActorLocation(At);
	Camera->SetActorRotation((Look - At).Rotation());
	if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
	{
		C->CurrentFocalLength = 30.f;
		C->FocusSettings.ManualFocusDistance = (Look - At).Size();
	}
	static int32 Shot = 0;
	if (T >= 0.1f * Shot && Shot < 360)
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_tackletest_r%d_%03d.png"), Rep, FMath::RoundToInt(R * 100.f)), false, false), ++Shot;
}

void AMatchDirector::TickLineTest(float Dt)
{
	// a right tackle (home, +X) against the end over him (away, -X), filmed side on. Each 4 s rep: stance, get-off and
	// the punch at contact, the hand fight, then the rep's outcome:
	//   0 bull rush: the end walks him into the pocket (bull drive / anchor), finishes with the long arm
	//   1 run block won: drive / driven back, pancake (the end goes down, the tackle finishes over him)
	//   2 speed rush off the edge: dip and rip, the tackle beaten on the outside
	//   3 swim inside   4 spin   5 club   6 rip
	if (Lineup.Num() < 2) return;
	const float T = LineupT;
	AAthlete* OL = Lineup[0];
	AAthlete* DL = Lineup[1];
	for (int32 i = 0; i < Lineup.Num(); ++i) Lineup[i]->SetActorHiddenInGame(i > 1);
	const int32 Rep = static_cast<int32>(T / 4.f);
	const float R = T - Rep * 4.f;
	static const TCHAR* Moves[] = {TEXT("longarm"), TEXT(""), TEXT("diprip"), TEXT("swim"), TEXT("spin"), TEXT("club"), TEXT("rip")};
	if (Rep > 6)
	{
		double GazeMean, GazeMin;
		int32 GazeN;
		AAthlete::GazeStats(GazeMean, GazeMin, GazeN, true);
		UE_LOG(LogTemp, Display, TEXT("TAG presnap gaze: set linemen look %.1f deg (lowest %.1f) over %d samples (0 = level, - = down)"), GazeMean, GazeMin, GazeN);
		FPlatformMisc::RequestExit(false);
		return;
	}
	static int32 LastRep = -1;
	static bool bResolved = false;
	if (Rep != LastRep)
	{
		LastRep = Rep;
		bResolved = false;
		OL->ResetPose();
		DL->ResetPose();
	}
	const FVector O = TagCoords::ToWorld(40, tag::FIELD_W * 0.5, 1);
	const bool bRun = Rep == 1;
	const float Wide = Rep == 2 ? 70.f : 0.f; // the speed rusher lines up wide of the tackle
	const float Snap = 0.8f, Contact = 1.15f, Resolve = 2.6f;
	// pile travel: toward the quarterback (-X) on the bull rush, off the ball (+X) on the run block
	const float PileV = Rep == 0 ? -110.f : bRun ? 150.f : 0.f;
	const float Fought = FMath::Clamp(R - (Contact + 0.6f), 0.f, Resolve - Contact - 0.6f);
	const FVector Pile = O + FVector(PileV * Fought, 0.f, 0.f);
	FVector OLp = Pile + FVector(-55.f, 0.f, 0.f), DLp = Pile + FVector(55.f, Wide, 0.f);
	FVector OLv = FVector(PileV, 0, 0), DLv = OLv;
	FString OLa = TEXT("block"), DLa = TEXT("block");
	bool bEngaged = R >= Contact && R < Resolve;
	if (R < Snap)
	{
		OLp = O + FVector(-125.f, 0, 0);
		DLp = O + FVector(125.f, Wide, 0);
		OLv = DLv = FVector::ZeroVector;
		OLa = DLa = TEXT("idle");
	}
	else if (R < Contact)
	{
		const float S = (R - Snap) / (Contact - Snap);
		OLp = FMath::Lerp(O + FVector(-125.f, 0, 0), O + FVector(-55.f, 0, 0), S);  // pass set / fire out
		DLp = FMath::Lerp(O + FVector(125.f, Wide, 0), O + FVector(55.f, Wide, 0), S);
		OLv = FVector(bRun ? 80.f : -60.f, 0, 0);
		DLv = FVector(-80.f, 0, 0);
	}
	else if (R >= Resolve)
	{
		const float After = R - Resolve;
		if (!bResolved)
		{
			bResolved = true;
			if (bRun)
			{
				OL->PlayPancakeFinish();
				DL->SetPancaked(FVector(1, 0, 0));
				UE_LOG(LogTemp, Display, TEXT("TAG line test rep %d: pancake"), Rep);
			}
			else
			{
				DL->PlayRushMove(Moves[Rep], OL);
				OL->PlayBeaten(DL);
				UE_LOG(LogTemp, Display, TEXT("TAG line test rep %d: %s"), Rep, Moves[Rep]);
			}
		}
		const FVector End = Pile;
		OLp = End + FVector(-55.f, 0.f, 0.f);
		OLv = FVector::ZeroVector;
		OLa = TEXT("idle");
		if (bRun)
		{
			DLp = End + FVector(60.f + 40.f * FMath::Min(After, 0.5f), 0.f, 0.f);
			DLv = FVector(120.f, 0, 0);
			DLa = TEXT("down");
		}
		else
		{
			DLp = End + FVector(48.f - 380.f * After, Wide + (Rep == 3 || Rep == 6 ? -90.f : 90.f) * FMath::Min(After, 0.6f), 0.f);
			DLv = FVector(-380.f, 0, 0);
			DLa = TEXT("run");
		}
	}
	// hand fight while engaged; the rep's winner after the punch has settled
	const float Push = !bEngaged || R < Contact + 0.6f ? 0.f : (Rep == 0 ? 0.85f : bRun ? -0.85f : 0.f);
	OL->SetBlockPartner(bEngaged ? DL : nullptr);
	DL->SetBlockPartner(bEngaged ? OL : nullptr);
	OL->SetLinePush(Push, false, !bRun);
	DL->SetLinePush(Push, true, !bRun);
	auto Feed = [&](AAthlete* Me, AAthlete* Him)
	{
		FAthleteFootball F;
		F.bPreSnap = R < Snap;
		static const bool bNoLook = FParse::Param(FCommandLine::Get(), TEXT("TagNoPresnapLook")); // the stance as captured
		if (F.bPreSnap && !bNoLook)
		{
			// set in the stance: eyes up the field, as the match feeds every set lineman
			F.bLook = true;
			F.LookWorld = Me->GetActorLocation() + Me->GetActorForwardVector() * 1500.f + FVector(0, 0, 160);
		}
		if (bEngaged)
		{
			const FVector Chest = Him->BoneWorld(TEXT("spine_04"));
			F.bReach = true;
			F.ReachWorld = Chest + (Me->GetActorLocation() - Chest).GetSafeNormal2D() * 16.f;
			F.ReachSpread = 13.f;
			F.Lean = 0.9f;
			F.bLook = true;
			F.LookWorld = Him->BoneWorld(TEXT("head"));
		}
		Me->SetFootball(F, Dt);
	};
	Feed(OL, DL);
	Feed(DL, OL);
	OL->Drive(OLp, 0.f, OLv, 7.f, OLa, R, Dt, true);
	DL->Drive(DLp, 180.f, DLv, 7.f, DLa, R, Dt, true);
	ResolveEngagedOverlap();
	// side on from the offense's right, low, a 35 mm lens on the pair
	const FVector At = O + FVector(0.f, -560.f, 150.f), Look = O + FVector(0.f, 0.f, 95.f);
	Camera->SetActorLocation(At);
	Camera->SetActorRotation((Look - At).Rotation());
	if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
	{
		C->CurrentFocalLength = 35.f;
		C->FocusSettings.ManualFocusDistance = (Look - At).Size();
	}
	static int32 Shot = 0;
	if (T >= 0.2f * Shot && Shot < 150)
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_linetest_%03d.png"), Shot++), false, false);
}

void AMatchDirector::TickGapTest(float Dt)
{
	if (Lineup.Num() < 5) return;
	const float T = LineupT;
	AAthlete* QB = Lineup[0];
	AAthlete* RB = Lineup[1];
	AAthlete* WR = Lineup[2];
	AAthlete* LB = Lineup[3];
	AAthlete* CB = Lineup[4];
	const FVector O = TagCoords::ToWorld(40, tag::FIELD_W * 0.5, 1); // the spot, offense going +X (yaw 0)
	auto Show = [&](std::initializer_list<AAthlete*> Who)
	{
		for (AAthlete* A : Lineup) A->SetActorHiddenInGame(true);
		for (AAthlete* A : Who) A->SetActorHiddenInGame(false);
	};
	auto Foot = [&](AAthlete* A, bool bPre, bool bUnder)
	{
		FAthleteFootball F;
		F.bPreSnap = bPre;
		F.bUnderCenter = bUnder;
		A->SetFootball(F, Dt);
	};
	auto Cam = [&](const FVector& At, const FVector& Look, float Focal)
	{
		Camera->SetActorLocation(At);
		Camera->SetActorRotation((Look - At).Rotation());
		if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
		{
			C->CurrentFocalLength = Focal;
			C->FocusSettings.ManualFocusDistance = (Look - At).Size();
		}
	};
	static bool bSnap = false, bPitch = false, bDive = false, bTackle = false;
	if (T < 6.f)
	{
		// 1) under center, the snap, the toss to the back sweeping right (-Y)
		Show({QB, RB});
		const bool bPre = T < 1.5f;
		Foot(QB, bPre, true);
		Foot(RB, bPre, false);
		QB->Drive(O, 0.f, FVector::ZeroVector, 9.f, TEXT("idle"), T, Dt, false);
		if (!bPre && !bPitch && T > 2.2f) { QB->PlayOneShot(FAthleteAssets::Get().Pitch); bPitch = true; }
		const float Rt = FMath::Max(0.f, T - 1.6f);
		const FVector RbV = Rt > 0.f ? FVector(Rt > 1.6f ? 450.f : 120.f, -650.f, 0.f) : FVector::ZeroVector;
		const FVector RbP = O + FVector(-450.f, -60.f, 0.f) + FVector(Rt > 1.6f ? 120.f * 1.6f + 450.f * (Rt - 1.6f) : 120.f * Rt, -650.f * Rt, 0.f);
		RB->Drive(RbP, RbV.IsNearlyZero() ? 0.f : RbV.Rotation().Yaw, RbV, 9.f, RbV.IsNearlyZero() ? TEXT("idle") : TEXT("run"), T, Dt, false);
		(void)bSnap;
		Cam(O + FVector(-700.f, 420.f, 230.f), O + FVector(-120.f, -120.f, 100.f), 30.f);
	}
	else if (T < 11.f)
	{
		// 2) the layout catch: a receiver at full speed, the ball just out of reach
		Show({WR});
		Foot(WR, false, false);
		const float Wt = T - 6.f;
		const float StopT = 2.4f;
		const float Run = FMath::Min(Wt, StopT);
		const FVector P = O + FVector(-1200.f + 800.f * Run, 900.f, 0.f);
		if (!bDive && Wt >= StopT) { WR->PlayDiveAttempt(); bDive = true; }
		const FVector V = Wt < StopT ? FVector(800.f, 0.f, 0.f) : FVector::ZeroVector;
		WR->Drive(P, 0.f, V, 9.f, Wt < StopT ? TEXT("run") : TEXT("catch"), Wt, Dt, false);
		Cam(P + FVector(250.f, -650.f, 140.f), P + FVector(150.f, 0.f, 70.f), 30.f);
	}
	else if (T >= 22.f && T < 38.f)
	{
		// 4) the throwing family, one every 2 s: quick, deep, fade, sidearm, touch (feet set), then on the run both ways
		Show({QB});
		Foot(QB, false, false);
		const float Tt = T - 22.f;
		const int32 Kinds[] = {FAthleteAssets::ThrowQuickK, FAthleteAssets::ThrowDeepK, FAthleteAssets::ThrowFadeK,
			FAthleteAssets::ThrowSidearmK, FAthleteAssets::ThrowTouchK, FAthleteAssets::ThrowRunRK, FAthleteAssets::ThrowRunLK};
		const int32 Idx = FMath::Clamp(static_cast<int32>(Tt / 2.f), 0, 6);
		static int32 GapLastThrow = -1;
		const bool bRun = Kinds[Idx] == FAthleteAssets::ThrowRunRK || Kinds[Idx] == FAthleteAssets::ThrowRunLK;
		const FVector V = bRun ? FVector(400.f, 0.f, 0.f) : FVector::ZeroVector;
		QB->Drive(O + FVector(-600.f, 0.f, 0.f), 0.f, V, 9.f, TEXT("throw"), Tt, Dt, false);
		if (Idx != GapLastThrow)
		{
			GapLastThrow = Idx;
			QB->PlayThrow(Kinds[Idx]);
			UE_LOG(LogTemp, Display, TEXT("TAG gap test: throw kind %d"), Kinds[Idx]);
		}
		const FVector At = O + FVector(-600.f, 0.f, 0.f);
		Cam(At + FVector(250.f, -520.f, 160.f), At + FVector(0.f, 0.f, 120.f), 30.f);
	}
	else if (T >= 51.5f)
	{
		// 6) the quarterback's drops, filmed from the side: under center for a beat, the five step drop at 4 m/s and
		//    the pocket; then the shotgun three step at 2.75 m/s and the pocket again
		Show({QB});
		const float Qt = T - 51.5f;
		const bool bUnderPhase = Qt < 4.5f;
		const float Lt = Qt - (bUnderPhase ? 0.f : 4.5f);
		const bool bPre = Lt < 0.5f;
		const float DropLen = bUnderPhase ? 1.5f : 0.66f, Sp = bUnderPhase ? 400.f : 275.f;
		const float Run = FMath::Clamp(Lt - 0.5f, 0.f, DropLen);
		const FVector Start = O + FVector(bUnderPhase ? 0.f : -500.f, 200.f, 0.f);
		const FVector P = Start - FVector(Sp * Run, 0.f, 0.f);
		const FVector V = (Lt >= 0.5f && Lt < 0.5f + DropLen) ? FVector(-Sp, 0.f, 0.f) : FVector::ZeroVector;
		FAthleteFootball F;
		F.bPreSnap = bPre;
		F.bUnderCenter = bUnderPhase;
		F.bHasBall = !bPre;
		F.bLive = !bPre;
		F.bLook = true;
		F.LookWorld = P + FVector(1500.f, 0.f, 160.f);
		QB->SetFootball(F, Dt);
		QB->Drive(P, 0.f, V, 9.f, V.IsNearlyZero() ? TEXT("idle") : TEXT("run"), Lt, Dt, false);
		Cam(P + FVector(-40.f, -640.f, 160.f), P + FVector(-60.f, 0.f, 100.f), 30.f);
	}
	else if (T >= 38.f)
	{
		// 5) the catching family, one every 2.2 s, each in its situation: in stride, back shoulder, one hand, scoop at
		//    speed; the high point jump ball and the comeback from a stand
		Show({WR});
		Foot(WR, false, false);
		const float Ct = T - 38.f;
		const FAthleteAssets& AA = FAthleteAssets::Get();
		UAnimationAsset* Catches[] = {AA.CatchInStride.Get(), AA.CatchBackShoulder.Get(), AA.CatchOneHand.Get(), AA.CatchScoop.Get(),
			AA.CatchHighPoint.Get(), AA.CatchComeback.Get()};
		const int32 Idx = FMath::Clamp(static_cast<int32>(Ct / 2.2f), 0, 5);
		static int32 LastCatch = -1;
		const bool bRun = Idx < 4;
		const FVector V = bRun ? FVector(650.f, 0.f, 0.f) : FVector::ZeroVector;
		const FVector At = O + FVector(-800.f, 700.f, 0.f);
		WR->Drive(At, 0.f, V, 9.f, bRun ? TEXT("run") : TEXT("catch"), Ct, Dt, false);
		if (Idx != LastCatch && Catches[Idx])
		{
			LastCatch = Idx;
			WR->PlayOneShot(Catches[Idx]);
		}
		Cam(At + FVector(260.f, -560.f, 150.f), At + FVector(0.f, 0.f, 110.f), 30.f);
	}
	else
	{
		// 3) gang tackle: the linebacker squares up and wraps, the corner comes in second, they go down in a pile,
		//    the back is hurt and stays down, then gets up slowly
		Show({RB, LB, CB});
		Foot(RB, false, false);
		Foot(LB, false, false);
		Foot(CB, false, false);
		const float Kt = T - 11.f;
		const float HitT = 1.8f;
		static FVector RbAt = FVector::ZeroVector;
		if (Kt < HitT)
		{
			RB->bForceInjury = true;
			RbAt = O + FVector(-900.f + 700.f * Kt, -300.f, 0.f);
			RB->Drive(RbAt, 0.f, FVector(700.f, 0.f, 0.f), 9.f, TEXT("run"), Kt, Dt, false);
			const FVector LbAt = O + FVector(900.f - 300.f * Kt, -360.f, 0.f);
			LB->Drive(LbAt, 180.f, FVector(-300.f, 0.f, 0.f), 9.f, TEXT("run"), Kt, Dt, false);
			// the corner closes from the sideline to arrive just behind the linebacker, a beat later
			const FVector CbTgt = O + FVector(-900.f + 700.f * HitT + 160.f, -300.f + 110.f, 0.f);
			const FVector CbAt = CbTgt + FVector(0.f, 420.f * (HitT + 0.25f - Kt), 0.f);
			CB->Drive(CbAt, -90.f, FVector(0.f, -420.f, 0.f), 9.f, TEXT("run"), Kt, Dt, false);
		}
		else
		{
			// the sim resolves the tackle in one tick and moves the carrier forward by the yards after contact
			const FVector Spot = RbAt + FVector(140.f, 0.f, 0.f);
			RB->Drive(Spot, 0.f, FVector::ZeroVector, 9.f, TEXT("down"), Kt - HitT, Dt, false);
			if (!bTackle) { LB->BeginTackleOn(RB); bTackle = true; }
			LB->Drive(O + FVector(900.f - 300.f * HitT, -360.f, 0.f), 180.f, FVector::ZeroVector, 9.f, TEXT("tackle"), Kt - HitT, Dt, false);
			const float Ct = Kt - HitT - 0.25f;
			const FVector CbTgt = O + FVector(-900.f + 700.f * HitT + 160.f, -300.f + 110.f, 0.f);
			const FVector CbAt = CbTgt + FVector(0.f, 420.f * FMath::Max(0.f, HitT + 0.25f - Kt), 0.f);
			if (Ct < 0.f) CB->Drive(CbAt, -90.f, FVector(0.f, -420.f, 0.f), 9.f, TEXT("run"), Kt, Dt, false);
			else
			{
				if (Ct < Dt * 1.5f) CB->BeginTackleOn(RB);
				CB->Drive(CbAt, -90.f, FVector::ZeroVector, 9.f, TEXT("tackle"), Ct, Dt, false);
			}
		}
		Cam(RbAt + FVector(100.f, -900.f, 170.f), RbAt + FVector(160.f, 0.f, 60.f), 32.f);
	}
	const float ShotT = 0.33f * GapShot;
	if (T >= ShotT && GapShot < 200)
	{
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_gap_%02d.png"), GapShot), false, false);
		++GapShot;
	}
	PerfT += Dt; ++PerfFrames;
	if (T > 61.f)
	{
		UE_LOG(LogTemp, Display, TEXT("TAG perf avg %.1f fps over %d frames"), PerfFrames / FMath::Max(PerfT, 0.001f), PerfFrames);
		{
			double GazeMean, GazeMin;
			int32 GazeN;
			AAthlete::GazeStats(GazeMean, GazeMin, GazeN, true);
			UE_LOG(LogTemp, Display, TEXT("TAG presnap gaze: set linemen look %.1f deg (lowest %.1f) over %d samples (0 = level, - = down)"), GazeMean, GazeMin, GazeN);
		}
		FPlatformMisc::RequestExit(false);
	}
}

static bool GHudShot = false;  // automation: one screenshot with the UI
static bool GAutoBoard = false;
static bool GAutoChains = false;
static bool GAutoCoach = false; // automation: the establishing shot turns to the end-zone video board

void AMatchDirector::TickAutomation(float Dt)
{
	if (!bAuto) return;
#if WITH_EDITOR
	if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0) return; // wait for shaders
#endif
	if (FAssetCompilingManager::Get().GetNumRemainingAssets() > 0) return; // and textures/meshes
	AutoT += Dt;
	if (GHudShot && !FScreenshotRequest::IsScreenshotRequested())
	{
		GHudShot = false;
		FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_hud.png"), true, false);
	}
	static const float Times[] = {3.f, 9.f, 14.f, 20.f, 27.f, 34.f, 42.f, 50.f};
	if (AutoShot < static_cast<int32>(UE_ARRAY_COUNT(Times)) && AutoT >= Times[AutoShot])
	{
		if (AutoShot == 4) CamMode = ECamMode::Sideline;
		if (AutoShot == 6) CamMode = ECamMode::Skycam;
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_auto_%02d.png"), AutoShot), false, false);
		if (AutoShot == 3) GHudShot = true; // next frame: the same view with the HUD (score bug, hints)
		int32 NLog = 0;
		for (auto& KV : Athletes)
		{
			if (!KV.Value->IsHidden() && NLog++ < 3) UE_LOG(LogTemp, Display, TEXT("TAG athlete %d: %s"), KV.Key, *KV.Value->DebugAnimState());
		}
		UE_LOG(LogTemp, Display, TEXT("TAG auto screenshot %d state=%d cam %s look %s focus %s dir %d ballOn %.1f"), AutoShot, static_cast<int32>(State),
			*Camera->GetActorLocation().ToCompactString(), *CamLook.ToCompactString(), *FocusPoint().ToCompactString(), Dir, Game ? Game->ballOn : -1.0);
		++AutoShot;
	}
	// final shot: establishing view of the far stands (verifies crowd/stadium rendering)
	if (AutoShot == static_cast<int32>(UE_ARRAY_COUNT(Times)) && AutoT > 52.f && AutoT < 54.f)
	{
		bAutoEstablish = true;
	}
	if (bAutoEstablish && AutoShot == static_cast<int32>(UE_ARRAY_COUNT(Times)) && AutoT >= 53.5f)
	{
		FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_stands.png"), false, false);
		++AutoShot;
	}
	// then the end-zone video board above the stands (verifies the bowl's end and the live score)
	if (AutoShot == static_cast<int32>(UE_ARRAY_COUNT(Times)) + 1 && AutoT > 53.9f && AutoT < 55.f && !GAutoBoard)
		GAutoBoard = true;
	if (GAutoBoard && !GAutoChains && AutoT >= 54.6f && AutoT < 55.f)
	{
		FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_board.png"), false, false);
		GAutoChains = true; // then the chain crew and the down box on the far sideline
		AutoT = 54.0f;
	}
	if (GAutoChains && !GAutoCoach && AutoT >= 54.6f && AutoT < 55.f)
	{
		FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_chains.png"), false, false);
		GAutoCoach = true; // then the home head coach in the coaching box
		AutoT = 54.0f;
	}
	if (GAutoCoach && AutoT >= 54.6f && AutoT < 55.f)
	{
		FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_coach.png"), false, false);
		AutoT = 55.f;
	}
	// close-up of one athlete (verifies character, uniform and equipment fit)
	if (AutoShot == static_cast<int32>(UE_ARRAY_COUNT(Times)) + 1 && AutoT > 55.f)
	{
		bAutoEstablish = false;
		GAutoBoard = false;
		GAutoChains = false;
		GAutoCoach = false;
		for (auto& KV : Athletes)
		{
			if (!KV.Value->IsHidden())
			{
				CloseupTarget = KV.Value;
				break;
			}
		}
		if (AutoT >= 57.5f)
		{
			if (const AAthlete* CA = Cast<AAthlete>(CloseupTarget)) UE_LOG(LogTemp, Display, TEXT("TAG closeup athlete: %s"), *CA->DebugAnimState());
			FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_closeup.png"), false, false);
			++AutoShot;
		}
	}
	else if (AutoShot == static_cast<int32>(UE_ARRAY_COUNT(Times)) + 2 && AutoT >= 59.f)
	{
		CloseupView = 1; // full body
		if (AutoT >= 60.5f)
		{
			FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_closeup_full.png"), false, false);
			++AutoShot;
		}
	}
	else if (AutoShot == static_cast<int32>(UE_ARRAY_COUNT(Times)) + 3 && AutoT >= 61.f)
	{
		CloseupView = 2; // from behind
		if (AutoT >= 62.5f)
		{
			FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_closeup_back.png"), false, false);
			++AutoShot;
		}
	}
	PerfT += Dt; ++PerfFrames;
	static const bool bLineCamAuto = FParse::Param(FCommandLine::Get(), TEXT("TagLineCam"));
	if (AutoT > (bLineCamAuto ? 240.f : 63.5f))
	{
		UE_LOG(LogTemp, Display, TEXT("TAG perf avg %.1f fps over %d frames"), PerfFrames / FMath::Max(PerfT, 0.001f), PerfFrames);
		{
			double GazeMean, GazeMin;
			int32 GazeN;
			AAthlete::GazeStats(GazeMean, GazeMin, GazeN, true);
			UE_LOG(LogTemp, Display, TEXT("TAG presnap gaze: set linemen look %.1f deg (lowest %.1f) over %d samples (0 = level, - = down)"), GazeMean, GazeMin, GazeN);
		}
		FPlatformMisc::RequestExit(false);
	}
}

void AMatchDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	FWorldDelegates::OnWorldPostActorTick.Remove(PostTickHandle);
	BroadcastEndPlay(); // (the wipe off the viewport, the store of recent shots written: MatchBroadcast.cpp)
	if (GEngine && GEngine->GameViewport && Overlay.IsValid()) GEngine->GameViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
	FTagInputHub::Shutdown();
	Super::EndPlay(Reason);
}

void AMatchDirector::OnPostActorTick(UWorld* InWorld, ELevelTick InTickType, float InDt)
{
	if (InWorld != GetWorld()) return;
	CommitCameraFrame(InDt); // (the view is final for the frame: a cut is told to the renderer, MatchBroadcast.cpp)
	FTagLatency::Get().EndFrame(); // (-TagLatencyLog: the presses drawn this frame, the frame's timings)
	if (!bOverlayDue) return;
	bOverlayDue = false;
	UpdateIcons();
	UpdateHighlight();
	UpdateLineBars();
}

void AMatchDirector::BuildWorld(const tag::Team& Home, const tag::Team& Away, const tag::Weather& W, bool bNight)
{
	if (Stadium) Stadium->Destroy();
	for (auto& KV : Athletes)
		if (KV.Value) KV.Value->Destroy();
	Athletes.Empty();
	// model pack people dress for the weather: coaches' polo / windbreaker / rain jacket, officials' sleeves, fans
	FAthleteAssets::Get().HeroWeather = W.precip != tag::Precip::None ? 2 : W.tempF < 50.0 ? 1 : 0;
	Stadium = GetWorld()->SpawnActor<AStadium>(AStadium::StaticClass(), FTransform::Identity);
	Stadium->Build(Home, Away, W, bNight);
	SpawnSidelines(Home, Away);
}

static FUniform UniformFor(const tag::Team& T, bool bHome)
{
	FUniform U;
	if (bHome)
	{
		U.Jersey = Hex(T.colors.primary);
		U.Pants = Hex(T.colors.secondary);
		U.Helmet = Hex(T.colors.primary);
		U.Trim = Hex(T.colors.secondary);
		U.Number = Hex(T.colors.accent);
	}
	else
	{
		U.Jersey = FLinearColor(0.72f, 0.72f, 0.72f); // white cloth reflects ~3/4 of the light: at 0.92 the road whites clipped to flat white in daylight (no folds, no shading)
		U.Pants = FLinearColor(0.68f, 0.68f, 0.68f);
		U.Helmet = Hex(T.colors.primary);
		U.Trim = Hex(T.colors.primary);
		U.Number = Hex(T.colors.primary);
	}
	U.TeamId = T.id;
	U.Logo = Hex(T.colors.secondary);
	return U;
}

void AMatchDirector::SpawnSidelines(const tag::Team& Home, const tag::Team& Away)
{
	for (AAthlete* A : Sideline)
		if (A) A->Destroy();
	Sideline.Empty();
	for (AAthlete* A : Officials)
		if (A) A->Destroy();
	Officials.Empty();
	for (AAthlete* A : Crew)
		if (A) A->Destroy();
	Crew.Empty();
	CrewPos.Empty();
	SidelineIsHome.Empty();
	SidelineScore[0] = SidelineScore[1] = 0;
	SidelineCheerLeft[0] = SidelineCheerLeft[1] = 0.f;
	static int32 PerTeam = 14;
	FParse::Value(FCommandLine::Get(), TEXT("TagSideline="), PerTeam); // 0 turns the benches off
	if (PerTeam <= 0 || bLineup) return;
	// chain crew (orange shirts, black trousers) on the rods and the down box, and a head coach in each team's colours
	{
		OfficialPeople.clear();
		auto Staff = [&](int32 Id, double HeightIn, double WeightLb, const FLinearColor& Top, const FLinearColor& Bottom,
			AAthlete::ECivilian Who, int32 Index)
		{
			auto P = std::make_unique<tag::Player>();
			P->id = 900000 + Id;
			P->pos = tag::Pos::WR;
			P->heightIn = HeightIn;
			P->weightLb = WeightLb;
			P->number = 0;
			FUniform U{Top, Bottom, Top, Top, Top}; // number painted in the shirt colour: none shows
			AAthlete* A = GetWorld()->SpawnActor<AAthlete>(AAthlete::StaticClass(), FTransform::Identity);
			A->TickAfter(this);
			A->Setup(P.get(), U);
			A->SetCivilian(Who, Index);
			OfficialPeople.push_back(std::move(P));
			Officials.Add(A);
			return A;
		};
		const FLinearColor Orange(0.9f, 0.28f, 0.02f), Black(0.03f, 0.03f, 0.035f), Khaki(0.55f, 0.47f, 0.33f);
		for (int32 k = 0; k < 3; ++k)
			if (USceneComponent* Post = Stadium ? Stadium->CrewPost(k) : nullptr)
			{
				AAthlete* A = Staff(k, 70 + k * 2, 185 + k * 15, Orange, Black, AAthlete::ECivilian::Crew, k);
				A->StandOnSideline(Post->GetComponentTransform().TransformPosition(FVector(-40, 14, 0)), -90.f, 3 * k);
				A->AttachToComponent(Post, FAttachmentTransformRules::KeepWorldTransform);
			}
		AAthlete* HC = Staff(5, 72, 210, Hex(Home.colors.primary) * 0.8f, Khaki, AAthlete::ECivilian::Coach, Home.id);
		HC->StandOnSideline(FVector(46 * YD, (tag::FIELD_W / 2 + 1.7) * YD, 0), -90.f, 1);
		AAthlete* AC = Staff(6, 70, 200, Hex(Away.colors.primary) * 0.8f, Khaki, AAthlete::ECivilian::Coach, Away.id);
		AC->StandOnSideline(FVector(54 * YD, -(tag::FIELD_W / 2 + 1.7) * YD, 0), 90.f, 4);
		// each team area also has an assistant coach by the head coach and a trainer at the end of the bench (the
		// model pack's sideline people; the stand-in bodies have nobody to show)
		if (FAthleteAssets::Get().bHero)
			for (int32 T = 0; T < 2; ++T)
			{
				const double Side = T == 0 ? 1.0 : -1.0;
				const double CoachX = T == 0 ? 46.0 : 54.0;
				const FLinearColor Col = Hex((T == 0 ? Home : Away).colors.primary) * 0.8f;
				AAthlete* As = Staff(10 + T, 71, 205, Col, Khaki, AAthlete::ECivilian::Assistant, T);
				As->StandOnSideline(FVector((CoachX - Side * 3.0) * YD, Side * (tag::FIELD_W / 2 + 2.2) * YD, 0), Side > 0 ? -100.f : 80.f, 6 + T);
				AAthlete* Tr = Staff(12 + T, 70, 190, Col, Khaki, AAthlete::ECivilian::Trainer, T);
				Tr->StandOnSideline(FVector((T == 0 ? 31.0 : 69.0) * YD, Side * (tag::FIELD_W / 2 + 3.4) * YD, 0), Side > 0 ? -60.f : 120.f, 8 + T);
			}
		// the umpire (white cap, striped shirt reads as white at distance) works from the offensive backfield
		Umpire = Staff(7, 71, 195, FLinearColor(0.85f, 0.85f, 0.85f), Black, AAthlete::ECivilian::Official, 0);
		UmpPos = FVector(50 * YD, 8 * YD, 0);
		Umpire->StandOnSideline(UmpPos, -90.f, 2);
		for (int32 R = 0; R < 6; ++R)
		{
			AAthlete* A = Staff(20 + R, 69 + (R * 5) % 6, 185 + R * 7, FLinearColor(0.85f, 0.85f, 0.85f), Black, AAthlete::ECivilian::Official, 1 + R);
			Officials.Remove(A); // driven by TickCrew, not a sideline figure
			Crew.Add(A);
			CrewPos.Add(FVector((38.0 + R * 5.0) * YD, (R % 2 ? 1.0 : -1.0) * (tag::FIELD_W / 2 + 1.2) * YD, 0));
			A->StandOnSideline(CrewPos.Last(), R % 2 ? -90.f : 90.f, 3 + R);
		}
	}
	// the team areas: home on the far sideline (the broadcast side looks at it), visitors on the near one, each between
	// the 32s, standing in two loose rows just outside the 6 ft border in front of the bench
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const bool bHome = Team == 0;
		const tag::Team& T = bHome ? Home : Away;
		std::vector<tag::Player*> Bench;
		for (int32 Depth = 2; Depth < 6 && static_cast<int32>(Bench.size()) < PerTeam; ++Depth)
			for (int32 Pi = 0; Pi <= static_cast<int32>(tag::Pos::P) && static_cast<int32>(Bench.size()) < PerTeam; ++Pi)
			{
				const std::vector<tag::Player*> D = tag::depth(T, static_cast<tag::Pos>(Pi));
				if (Depth < static_cast<int32>(D.size())) Bench.push_back(D[Depth]);
			}
		const double SideSign = bHome ? 1.0 : -1.0;
		FRandomStream Rand(T.id * 31 + 7);
		const FUniform U = UniformFor(T, bHome);
		for (int32 i = 0; i < static_cast<int32>(Bench.size()); ++i)
		{
			const int32 Row = i % 3 == 2 ? 1 : 0;
			const double X = (33.0 + 34.0 * (i + Rand.FRandRange(-0.3f, 0.3f)) / FMath::Max(1, static_cast<int32>(Bench.size()) - 1)) * YD;
			const double Y = SideSign * (tag::FIELD_W / 2 + 2.4 + Row * 1.3 + Rand.FRandRange(-0.25f, 0.25f)) * YD;
			float Yaw = SideSign > 0 ? -90.f : 90.f;            // facing the field
			Yaw += Rand.FRandRange(-28.f, 28.f) + (i % 5 == 4 ? (Rand.FRand() < 0.5f ? 70.f : -70.f) : 0.f); // some talk to a teammate
			AAthlete* A = GetWorld()->SpawnActor<AAthlete>(AAthlete::StaticClass(), FTransform::Identity);
			A->TickAfter(this);
			A->Setup(Bench[i], U);
			A->StandOnSideline(FVector(X, Y, 0), Yaw, i + T.id * 5);
			Sideline.Add(A);
			SidelineIsHome.Add(bHome);
		}
	}
}

void AMatchDirector::TickSidelines(float Dt)
{
	// the benches and the staff animate only while they are in shot (the umpire works the play: he is driven)
	for (AAthlete* A : Sideline)
		if (A) A->SleepWhenOffscreen(Dt);
	for (AAthlete* A : Officials)
		if (A && A != Umpire) A->SleepWhenOffscreen(Dt);
	if (!Game || Sideline.Num() == 0) return;
	// a score sets the scoring team's bench off for a few seconds (longer for a touchdown)
	const int32 Now[2] = {Game->score.home, Game->score.away};
	for (int32 k = 0; k < 2; ++k)
	{
		if (Now[k] > SidelineScore[k])
		{
			SidelineCheerLeft[k] = Now[k] - SidelineScore[k] >= 6 ? 7.f : 4.f;
			for (int32 i = 0; i < Sideline.Num(); ++i)
				if (Sideline[i] && SidelineIsHome[i] == (k == 0)) Sideline[i]->SidelineCheer(true);
		}
		else if (Now[k] < SidelineScore[k])
			SidelineCheerLeft[k] = 0.f; // new game
		SidelineScore[k] = Now[k];
		if (SidelineCheerLeft[k] > 0.f && (SidelineCheerLeft[k] -= Dt) <= 0.f)
			for (int32 i = 0; i < Sideline.Num(); ++i)
				if (Sideline[i] && SidelineIsHome[i] == (k == 0)) Sideline[i]->SidelineCheer(false);
	}
}

void AMatchDirector::ShowMainMenu()
{
	State = EMatchState::Menu;
	Sim.reset();
	Game.reset();
	ShowOnly({});
	bLobby = false;
	bMulti = false;
	SetMenuOwner(-1);
	if (Overlay) Overlay->SetCallWho(FString(), FLinearColor::White);
	if (Overlay) Overlay->ShowMainMenu();
}

void AMatchDirector::ShowTeamSelect(bool bUserPlays)
{
	if (Overlay) Overlay->ShowTeamSelect(bUserPlays);
}

void AMatchDirector::QuitToMenu()
{
	if (Booth) { Booth->Destroy(); Booth = nullptr; }
	if (Audio) Audio->MatchEnd();
	ShowMainMenu();
}

void AMatchDirector::StartMatch(int HomeIdx, int AwayIdx, bool bUserPlays, bool bUserHome, int WeatherKind, bool bNight, double QuarterLen)
{
	if (Audio) Audio->Hush(); // (the loading reel has its own sound; the menu's music must not play over it)
	if (Audio) Audio->PreloadMatchBanks(); // (the match's sound banks stream in behind the loading screen, not on first use)
	bMulti = bMultiNext; // (StartMultiMatch; the one-player screens start a one-player game)
	bMultiNext = false;
	bLobby = false;
	SetMenuOwner(-1);
	for (FTagHuman& H : Humans) { H.Slot = -1; H.Last = nullptr; H.Ctl = FHumanCtl(); }
	// -TagWeatherSeed=<n>: the same weather every run (like-for-like performance comparisons)
	int32 WeatherSeed = -1;
	FParse::Value(FCommandLine::Get(), TEXT("TagWeatherSeed="), WeatherSeed);
	tag::Rng R(static_cast<uint32_t>(WeatherSeed >= 0 ? WeatherSeed : FMath::Rand()) * 2654435761u + 17u);
	tag::Weather W = tag::randomWeather(R);
	UE_LOG(LogTemp, Display, TEXT("TAG weather: %s, %.0f F, wind %.1f yd/s"), W.precip == tag::Precip::Rain ? TEXT("rain") : W.precip == tag::Precip::Snow ? TEXT("snow") : TEXT("dry"),
		W.tempF, FMath::Sqrt(W.windX * W.windX + W.windY * W.windY));
	switch (WeatherKind)
	{
	case 1: W.precip = tag::Precip::None; W.windX *= 0.4; W.windY *= 0.4; W.tempF = FMath::Max(50.0, W.tempF); break;
	case 2: { W.precip = tag::Precip::None; const double A = R.range(0, 6.283); W.windX = FMath::Cos(A) * 9; W.windY = FMath::Sin(A) * 9; break; }
	case 3: W.precip = tag::Precip::Rain; W.tempF = FMath::Clamp(W.tempF, 40.0, 65.0); break;
	case 4: W.precip = tag::Precip::Snow; W.tempF = R.range(18, 31); break;
	default: break;
	}
	tag::Team& Home = *League[HomeIdx];
	tag::Team& Away = *League[AwayIdx];
	for (tag::Team* T : {&Home, &Away})
		for (auto& P : T->roster) P->injuredWeeks = 0;
	tag::GameOptions O;
	O.seed = static_cast<uint32_t>(R.next() * 4294967295.0);
	O.quarterLen = QuarterLen;
	O.weather = W;
	if (bUserPlays) O.userTeam = bUserHome ? tag::TeamKey::Home : tag::TeamKey::Away;
	O.abilities = !FParse::Param(FCommandLine::Get(), TEXT("TagNoAbilities"));  // (Superstar / X-Factor abilities: A/B)
	Game = std::make_unique<tag::Game>(Home, Away, O);
	BroadcastSeed = O.seed.value_or(0u); // (the presentation's tie-breaks: the same game, the same shots for the same saved history)
	UserTeam = O.userTeam;
	// the broadcast booth for this match (not on the highlight reel, not with -TagNoBooth)
	if (Booth) { Booth->Destroy(); Booth = nullptr; }
	bBoothNight = bNight;
	if (!bHighlights && !FParse::Param(FCommandLine::Get(), TEXT("TagNoBooth"))) Booth = GetWorld()->SpawnActor<ATagBooth>();
	if (Booth) Booth->SetCaptionsShown(bOptCaptions);
	bWatch = !bUserPlays;
	Speed = 1.f;
	LastQ = 1;
	PackQ = 1;
	const bool bLoadScreen = BeginLoadingScreen(Home, Away, W, bNight);
	BuildWorld(Home, Away, W, bNight);
	// while the loading screen is up: the men both teams can field, and what the first plays would otherwise load
	WarmMatchAssets();
	PrespawnAthletes();
	if (bLoadScreen) GetMoviePlayer()->WaitForMovieToFinish(); // the reel runs its minimum, then the game is back
	if (Overlay)
	{
		Overlay->ClearModal();
		Overlay->SetHudVisible(true);
	}
	// a game nobody plays is watched the way TV shows it (the game camera on the near side); a played game from behind
	// the offense. C / the pad's view button steps through the cameras either way.
	CamMode = bWatch ? ECamMode::Sideline : ECamMode::Broadcast;
	CamStationX = -1.f;
	ResetReplayBudget(); // (each game earns its own replays: MatchBroadcast.cpp)
	if (Audio) Audio->MatchStart(*Game);
	BoothSkipUntil = 0.0;
	// the pregame open (a crane over the stadium, the matchup, both kickoff units on the field: MatchBroadcast.cpp), the
	// booth talking over it; without it (-TagNoOpen, automation), with the booth, a short wait while the crew opens the
	// broadcast (BoothHold: the camera on an empty field)
	if (BeginPregameOpen()) {}
	else if (Booth) Wait(0.5f);
	else Next();
}

bool AMatchDirector::BeginLoadingScreen(const tag::Team& Home, const tag::Team& Away, const tag::Weather& W, bool bNight)
{
	if (bAuto || bCallShot || bMoveShot || bArtTest || bMultiTest || bLineup || bHighlights || bTackleCam || SoakSecs > 0.f || FParse::Param(FCommandLine::Get(), TEXT("TagNoLoad"))) return false;
	if (!IsMoviePlayerEnabled() || !GetMoviePlayer()->IsInitialized() || GetMoviePlayer()->IsMovieCurrentlyPlaying()) return false;
	const FString Reel = FPaths::ProjectContentDir() / TEXT("Movies/TAG_Highlights.mp4");
	const bool bReel = FPaths::FileExists(Reel);
	auto Tm = [](const tag::Team& T)
	{
		FLoadingTeam L;
		L.City = S(T.city);
		L.Name = S(T.name);
		L.Abbr = S(T.abbr);
		L.Color = Hex(T.colors.primary);
		return L;
	};
	TArray<FString> Tips;
	{ const TagBind::FScope Tip(TagBind::ECtx::PreOff); Tips.Add(PadHint(TEXT("Hold L2 before the snap to see the play drawn on the field."))); }
	{ const TagBind::FScope Tip(TagBind::ECtx::Passing); Tips.Add(PadHint(TEXT("Hold a receiver's button for a bullet, tap it for a lob, tap it twice to pump fake; L1 throws high, L2 low. R3 throws it away."))); }
	Tips.Add(TEXT("Dynamic kickoff: nobody on the coverage moves until the returner has the ball."));
	{ const TagBind::FScope Tip(TagBind::ECtx::Defense); Tips.Add(PadHint(TEXT("On defense, CIRCLE switches you to the man nearest the ball, R2 sprints, SQUARE dives for the tackle."))); }
	{ const TagBind::FScope Tip(TagBind::ECtx::Defense); Tips.Add(PadHint(TEXT("Engaged with a blocker: R-STICK up rips, down bull rushes, to a side swims or clubs; R2 is the speed rush."))); }
	{ const TagBind::FScope Tip(TagBind::ECtx::Kicking); Tips.Add(PadHint(TEXT("Kicking: aim with the L-STICK, pull the R-STICK back for power and push it straight up."))); }
	{ const TagBind::FScope Tip(TagBind::ECtx::PreOff); Tips.Add(PadHint(TEXT("Hot routes: hold TRIANGLE at the line, pick a receiver, give him a new route."))); }
	Tips.Add(TEXT("The pass rush wins with speed outside and power inside; the protection slide answers one of them."));
	Tips.Add(TEXT("Fourth and short near midfield: the coach's chart says go more often than you'd think."));
	Tips.Add(PadHint(TEXT("OPTIONS pauses the game; the box score is a tap away from there.")));
	FLoadingScreenAttributes A;
	if (bReel) A.MoviePaths.Add(TEXT("TAG_Highlights"));
	A.PlaybackType = EMoviePlaybackType::MT_Looped;
	// (2 s at least, reel or not: the match's own work - the world, every man both teams can field - is done under it,
	// and the screen stays as long as that takes; it was 8 s of reel over a third of a second of loading)
	float Secs = 2.f;
	FParse::Value(FCommandLine::Get(), TEXT("TagLoadSecs="), Secs);
	A.MinimumLoadingScreenDisplayTime = Secs;
	A.bAutoCompleteWhenLoadingCompletes = true;
	A.bMoviesAreSkippable = true;
	A.bWaitForManualStop = false;
	const FString Cond = FString::Printf(TEXT("%s  ·  %s"), Game ? *S(Game->weatherText()) : TEXT(""), bNight ? TEXT("NIGHT GAME") : TEXT("DAY GAME"));
	A.WidgetLoadingScreen = SNew(SLoadingScreen).Away(Tm(Away)).Home(Tm(Home)).Conditions(Cond).Tips(Tips).bHasMovie(bReel);
	GetMoviePlayer()->SetupLoadingScreen(A);
	const bool bPlaying = GetMoviePlayer()->PlayMovie();
	UE_LOG(LogTemp, Display, TEXT("TAG loading screen: %s, reel %s, %.0f s minimum"), bPlaying ? TEXT("up") : TEXT("refused"), bReel ? TEXT("found") : TEXT("missing"), Secs);
	return bPlaying;
}

void AMatchDirector::TickHighlights(float Dt)
{
	if (!bHighlights) return;
	bool bBusy = FAssetCompilingManager::Get().GetNumRemainingAssets() > 0;
	// (and the first pack draw, so the opening plays have the whole library)
	if (bHlFull && HlClip == 0 && FAthleteAssets::Get().bPackLoading) bBusy = true;
#if WITH_EDITOR
	bBusy = bBusy || (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0);
#endif
	if (bHlFull && bBusy != bHlHeld)
	{
		// the whole game is filmed: hold the world (engine pause, the director's Tick stops at bEnginePaused) while
		// shaders / assets compile, so no snap happens off camera
		bHlHeld = bBusy;
		bEnginePaused = bBusy;
		UGameplayStatics::SetGamePaused(this, bBusy);
		UE_LOG(LogTemp, Display, TEXT("TAG hl %s for compiles at clip %d"), bBusy ? TEXT("hold") : TEXT("resume"), HlClip);
	}
	if (bBusy) return;
	if (Overlay) Overlay->SetHint(TEXT(""));
	const bool bRec = State == EMatchState::Live || State == EMatchState::Post || State == EMatchState::Replay ||
		(bHlFull && (State == EMatchState::Presnap || State == EMatchState::Kick)) ||
		(bHlFull && (State == EMatchState::Wait || State == EMatchState::Call) && bHlInClip && FCString::Strifind(FCommandLine::Get(), TEXT("TagHuddleSecs="))) || // (the huddle too, when it is held)
		(State == EMatchState::Wait && BroadcastFilming()); // (the shots after the replay, with their play; the open with -TagHlOpen: MatchBroadcast.cpp)
	// the pose the last screenshot showed is final now (the bodies evaluate after the director): its telemetry row
	if (bHlFull && bHlInClip && HlFrame > 0 && !bHlSkipClip) HlTelemetry(HlFrame - 1);
	if (bRec && !bHlInClip)
	{
		bHlInClip = true;
		bHlReplayLogged = false;
		++HlClip;
		HlFrame = 0;
		// -TagHlFromQ=<q>: the game plays through (same plays, same clip numbers: the sim is deterministic) unfilmed until then
		static int32 FromQ = FParse::Value(FCommandLine::Get(), TEXT("TagHlFromQ="), FromQ) ? FromQ : 0;
		bHlSkipClip = bHlFull && Game && Game->q < FromQ;
		static const bool bHlBroadcast = FParse::Param(FCommandLine::Get(), TEXT("TagHlBroadcast")); // (the player's default camera, for checking what he sees)
		CamMode = bHlBroadcast ? ECamMode::Broadcast : bHlFull || HlClip % 4 == 2 ? ECamMode::Sideline : ECamMode::Broadcast; // (a filmed full game: the TV camera throughout)
		UE_LOG(LogTemp, Display, TEXT("TAG hl clip %d start cam %d gf %llu"), HlClip, static_cast<int32>(CamMode), static_cast<unsigned long long>(GFrameCounter));
	}
	if (!bRec && bHlInClip)
	{
		bHlInClip = false;
		const tag::PlayLog* L = Game && !Game->log.empty() ? &Game->log.back() : nullptr;
		UE_LOG(LogTemp, Display, TEXT("TAG hl clip %d end frames %d big %d desc \"%s\""), HlClip, HlFrame, L ? static_cast<int32>(L->big) : 0, L ? *S(L->desc) : TEXT(""));
		if (bHlFull)
		{
			if (!bHlSkipClip) HlFlush();
			HlTele.Reset();
			// -TagHlKeepTurnovers: a whole game filmed for its natural turnovers - every play's telemetry is kept, the frames
			// only of plays with a fumble or an interception in them (a full game of frames is ~80 GB). Deleted one clip late:
			// the last screenshots of a clip are still being written when it ends.
			static const bool bKeepTO = FParse::Param(FCommandLine::Get(), TEXT("TagHlKeepTurnovers"));
			if (bKeepTO)
			{
				static int32 PendingDel = 0;
				FString FrameDir;
				if (!FParse::Value(FCommandLine::Get(), TEXT("TagHlDir="), FrameDir)) FrameDir = FPaths::ProjectSavedDir() / TEXT("Highlights/frames");
				if (PendingDel > 0)
				{
					TArray<FString> Files;
					IFileManager::Get().FindFiles(Files, *FString::Printf(TEXT("%s/c%03d_*.png"), *FrameDir, PendingDel), true, false);
					for (const FString& F : Files) IFileManager::Get().Delete(*(FrameDir / F));
				}
				const FString Desc = L ? FString(S(L->desc)) : FString();
				const bool bTO = (Sim && Sim->result && Sim->result->turnover) || (L && L->big == tag::LogTag::Turnover) ||
					Desc.Contains(TEXT("intercept")) || Desc.Contains(TEXT("fumble"));
				PendingDel = bTO ? 0 : HlClip;
				if (bTO) UE_LOG(LogTemp, Display, TEXT("TAG hl turnover clip %d kept: %s"), HlClip, *Desc);
			}
			UE_LOG(LogTemp, Display, TEXT("TAG hl clip %d game q%d clock %.0f score %d-%d gf %llu"), HlClip, Game ? Game->q : 0, Game ? Game->clock : 0.0, Game ? Game->score.away : 0, Game ? Game->score.home : 0,
				static_cast<unsigned long long>(GFrameCounter));
		}
	}
	if (bRec && !bHlSkipClip)
	{
		if (State == EMatchState::Replay && !bHlReplayLogged)
		{
			bHlReplayLogged = true;
			UE_LOG(LogTemp, Display, TEXT("TAG hl clip %d replay from frame %d"), HlClip, HlFrame);
		}
		// (a name with a directory in it is written as given; a bare name would go under Saved/Screenshots)
		static const FString HlDir = []
		{
			FString D;
			if (FParse::Value(FCommandLine::Get(), TEXT("TagHlDir="), D) && !D.IsEmpty()) return D;
			return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Highlights/frames"));
		}();
		// -TagHlNoFrames (with -TagHlFull, 07 A6): every frame counted and its telemetry row written, no screenshot taken
		static const bool bHlNoFrames = FParse::Param(FCommandLine::Get(), TEXT("TagHlNoFrames"));
		static const bool bHlCamOnly = FParse::Param(FCommandLine::Get(), TEXT("TagHlCamOnly")); // (frames only while a film close shot is on: -TagThrowCam / -TagCatchCam)
		if (bHlFull && (bHlNoFrames || (bHlCamOnly && !bFilmCloseShot))) ++HlFrame;
		else FScreenshotRequest::RequestScreenshot(bHlFull ? FString::Printf(TEXT("%s/c%03d_%05d.png"), *HlDir, HlClip, HlFrame++)
			: FString::Printf(TEXT("%s/c%02d_%05d.png"), *HlDir, HlClip, HlFrame++), true, false);
	}
	const int32 MaxClips = 18;
	const bool bFinal = Game && Game->phase == tag::GamePhase::Final;
	// -TagHlMaxClips=<n>: a filmed full game stops after n clips (the sim is deterministic: the same n plays every run,
	// for like-for-like measurements in a third of the time)
	static int32 FullMax = FParse::Value(FCommandLine::Get(), TEXT("TagHlMaxClips="), FullMax) ? FullMax : 0;
	if (bHlFull ? ((bFinal || (FullMax > 0 && HlClip >= FullMax)) && !bHlInClip) : ((HlClip >= MaxClips && !bHlInClip) || FPlatformTime::Seconds() - HlStart > 1500.0 || bFinal))
	{
		if (bHlFull) HlFlush();
		UE_LOG(LogTemp, Display, TEXT("TAG hl done: %d clips%s"), HlClip, bFinal ? TEXT(" (final)") : TEXT(""));
		if (bHlFull && Game)
			UE_LOG(LogTemp, Display, TEXT("TAG hl final score %d-%d after %.0f s"), Game->score.away, Game->score.home, FPlatformTime::Seconds() - HlStart);
		FPlatformMisc::RequestExit(false);
	}
}

void AMatchDirector::HlTelemetry(int32 Frame)
{
	if (!Sim || !Game) return;
	auto Bone = [](USkeletalMeshComponent* M, const TCHAR* Name, FVector& Out)
	{
		if (!M || M->GetBoneIndex(Name) == INDEX_NONE) return false;
		Out = M->GetSocketLocation(Name);
		return true;
	};
	const tag::TeamKey OffK = Game->poss;
	const FVector BallW = Ball && !Ball->IsHidden() ? Ball->GetActorLocation() : FVector(NAN, NAN, NAN);
	const TCHAR* St = State == EMatchState::Presnap ? TEXT("presnap") : State == EMatchState::Live ? TEXT("live") : State == EMatchState::Post ? TEXT("post")
		: State == EMatchState::Replay ? TEXT("replay") : State == EMatchState::Kick ? TEXT("kick") : TEXT("other");
	if (HlTele.IsEmpty())
		HlTele = TEXT("frame,state,simt,ballmode,bx,by,bz,bwx,bwy,bwz,pid,num,pos,side,role,sstate,sanim,sx,sy,svx,svy,face,eng,ball,stun,hidden,wx,wy,wz,yaw,down,")
			TEXT("flx,fly,flz,frx,fry,frz,pz,hdz,hrx,hry,hrz,hlx,hly,hlz,scrx,scry,refd,refw,slr,sls,dev,dfb,dor,dst,dln,dxf,dft,mmeff,mmstr,mmang,mmor,pvx,pvy,hdx,hdy,voff,rro,plf,")
			TEXT("ahl,ahr,afl,afr,ael,aer,awl,awr,acl,acr,ajit,lod,twl,twr,tul,tur,hws,hwc,anim,")
			TEXT("camx,camy,camz,camfx,camfy,camfz,camfoc,camang,camcut,camnear,camhold\n");
	// the camera's columns, the same on every man's row (MatchBroadcast.cpp: CameraTelemetryCols)
	const FString CamCols = CameraTelemetryCols();
	// where each man is on screen (0..1, for the framing check) and how close the referee is to anyone (cm)
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	FVector2D VP(1.0, 1.0);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(VP);
	// refd: the closest any official on the field (the referee and the crew) comes to a player
	float RefD = NAN;
	TArray<AAthlete*, TInlineAllocator<8>> Refs;
	if (Umpire && !Umpire->IsHidden()) Refs.Add(Umpire);
	for (AAthlete* C : Crew)
		if (C && !C->IsHidden()) Refs.Add(C);
	int32 RefW = -1; // which official: 0 the referee, 1 umpire, 2 down judge, 3 line judge, 4 side judge, 5 field judge, 6 back judge
	for (int32 Oi = 0; Oi < Refs.Num(); ++Oi)
		for (tag::Ent* E : Sim->ents)
			if (AAthlete* P = AthleteFor(E->p, E->side == tag::Side::Off ? OffK : tag::other(OffK)); P && !P->IsHidden())
			{
				const float Dd = FVector::Dist2D(P->GetActorLocation(), Refs[Oi]->GetActorLocation());
				if (FMath::IsNaN(RefD) || Dd < RefD) { RefD = Dd; RefW = Refs[Oi] == Umpire ? 0 : 1 + Crew.IndexOfByKey(Refs[Oi]); }
			}
	for (tag::Ent* E : Sim->ents)
	{
		AAthlete* A = AthleteFor(E->p, E->side == tag::Side::Off ? OffK : tag::other(OffK));
		USkeletalMeshComponent* M = A ? A->Visible() : nullptr;
		const FVector W = A ? A->GetActorLocation() : FVector::ZeroVector;
		FVector FL(NAN), FR(NAN), Pv(NAN), Hd(NAN), HR(NAN), HL(NAN);
		if (!Bone(M, TEXT("ball_l"), FL)) Bone(M, TEXT("foot_l"), FL);
		if (!Bone(M, TEXT("ball_r"), FR)) Bone(M, TEXT("foot_r"), FR);
		Bone(M, TEXT("pelvis"), Pv);
		Bone(M, TEXT("head"), Hd);
		// -TagStallDbg: a body that holds still for a frame while its actor moves on (the head steps < 1/4 of the actor's
		// step): where the visible mesh component went, and how its pose moved inside it
		static const bool bStallDbg = FParse::Param(FCommandLine::Get(), TEXT("TagStallDbg"));
		if (bStallDbg && A && M)
		{
			struct FStallPrev { FVector W, Hd, C, HdCS, RootCS, PelCS, SrcPel, SrcC, FtCS, RD; int32 Frame; bool bRD; };
			static TMap<const AAthlete*, FStallPrev> GStallPrev;
			const int32 Hi = M->GetBoneIndex(TEXT("head"));
			const TArray<FTransform>& CS = M->GetComponentSpaceTransforms();
			const FVector HdCS = CS.IsValidIndex(Hi) ? CS[Hi].GetLocation() : FVector::ZeroVector;
			const FVector RootCS = CS.Num() ? CS[0].GetLocation() : FVector::ZeroVector;
			const FVector C = M->GetComponentLocation();
			auto CSOf = [](const USkeletalMeshComponent* K, const TCHAR* N)
			{
				const int32 I = K ? K->GetBoneIndex(N) : INDEX_NONE;
				return I != INDEX_NONE && K->GetComponentSpaceTransforms().IsValidIndex(I) ? K->GetComponentSpaceTransforms()[I].GetLocation() : FVector::ZeroVector;
			};
			const FVector PelCS = CSOf(M, TEXT("pelvis")), FtCS = (CSOf(M, TEXT("foot_l")) + CSOf(M, TEXT("foot_r"))) * 0.5f;
			const USkeletalMeshComponent* Src = A->Body.Get();
			const FVector SrcPel = Src && Src != M ? CSOf(Src, TEXT("pelvis")) : FVector::ZeroVector, SrcC = Src ? Src->GetComponentLocation() : FVector::ZeroVector;
			const UTagMotionMatchInstance* MMI = Src ? Cast<UTagMotionMatchInstance>(Src->GetAnimInstance()) : nullptr;
			const bool bRD = MMI && MMI->bRootDeltaValid;
			const FVector RD = bRD ? MMI->RootDelta : FVector::ZeroVector;
			static int32 TraceNum = FParse::Value(FCommandLine::Get(), TEXT("TagStallTrace="), TraceNum) ? TraceNum : -1;
			if (E->p && E->p->number == TraceNum && State == EMatchState::Live)
				UE_LOG(LogTemp, Display, TEXT("TAG stalltrace #%d f%d srcPel %s mmPel %s root %s actor %s | rm %.1f at %.3f %s %s"), TraceNum, Frame, *SrcPel.ToCompactString(),
					*PelCS.ToCompactString(), *RootCS.ToCompactString(), *W.ToCompactString(), RD.Size(), MMI ? MMI->MatchedTime : -1.f,
					MMI ? (MMI->bMatchedContinuing ? TEXT("cont") : TEXT("new")) : TEXT("-"), MMI ? *GetNameSafe(MMI->GetMatchedAsset()) : TEXT("-"));
			if (const FStallPrev* Pp = GStallPrev.Find(A); Pp && Pp->Frame == Frame - 1)
			{
				const float Da = FVector::Dist2D(W, Pp->W), Dh = FVector::Dist2D(Hd, Pp->Hd);
				if (Da > 20.f && Dh < 0.25f * Da)
					UE_LOG(LogTemp, Display, TEXT("TAG stall #%d f%d gf%llu actor %.0f head %.0f comp %.0f | headCS d %s rootCS d %s | tick %d vis %d rendered %.3f s ago | %s"),
						E->p ? E->p->number : -1, Frame, (unsigned long long)GFrameCounter, Da, Dh, FVector::Dist2D(C, Pp->C), *(HdCS - Pp->HdCS).ToCompactString(),
						*(RootCS - Pp->RootCS).ToCompactString(), M->IsComponentTickEnabled() ? 1 : 0, M->IsVisible() ? 1 : 0,
						GetWorld()->GetTimeSeconds() - M->GetLastRenderTimeOnScreen(), *A->DebugAnimState());
				if (Da > 20.f && Dh < 0.25f * Da)
					UE_LOG(LogTemp, Display, TEXT("TAG stall2 #%d f%d pelCS d %s feetCS d %s srcPel d %s srcComp d %.0f | root motion %d %.1f cm (prev %d %.1f) at %.2f %s"), E->p ? E->p->number : -1, Frame,
						*(PelCS - Pp->PelCS).ToCompactString(), *(FtCS - Pp->FtCS).ToCompactString(), *(SrcPel - Pp->SrcPel).ToCompactString(), FVector::Dist2D(SrcC, Pp->SrcC), bRD ? 1 : 0, RD.Size(), Pp->bRD ? 1 : 0, Pp->RD.Size(), MMI ? MMI->MatchedTime : -1.f,
						MMI ? (MMI->bMatchedContinuing ? TEXT("cont") : TEXT("new")) : TEXT("-"));
			}
			GStallPrev.Add(A, FStallPrev{W, Hd, C, HdCS, RootCS, PelCS, SrcPel, SrcC, FtCS, RD, Frame, bRD});
		}
		Bone(M, TEXT("hand_r"), HR);
		Bone(M, TEXT("hand_l"), HL);
		FVector2D Scr(NAN, NAN);
		if (PC && A && VP.X > 0 && VP.Y > 0)
		{
			FVector2D S2;
			if (PC->ProjectWorldLocationToScreen(W + FVector(0, 0, 90), S2, false)) Scr = S2 / VP;
		}
		HlTele += FString::Printf(TEXT("%d,%s,%.3f,%s,%.2f,%.2f,%.2f,%.1f,%.1f,%.1f,%d,%d,%s,%s,%s,%s,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%s,%d,%.2f,%d,%.1f,%.1f,%.1f,%.1f,%d,")
			TEXT("%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.3f,%.3f,%.1f,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.0f,%.2f,%.0f,%.0f,%.1f,%.1f,%.1f,%.1f,%.1f,%.0f,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.0f,%.0f,%.0f,%.0f,%.2f,%d,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%s%s\n"),
			Frame, St, Sim->t, UTF8_TO_TCHAR(tag::ballModeName(Sim->ball.mode)), Sim->ball.pos.x, Sim->ball.pos.y, Sim->ball.pos.z, BallW.X, BallW.Y, BallW.Z,
			E->p->id, E->p->number, UTF8_TO_TCHAR(tag::posName(E->p->pos)), UTF8_TO_TCHAR(tag::sideName(E->side)), UTF8_TO_TCHAR(E->role.c_str()),
			UTF8_TO_TCHAR(E->stateName()), UTF8_TO_TCHAR(E->animName()), E->pos.x, E->pos.y, E->vel.x, E->vel.y, E->face,
			E->engaged ? UTF8_TO_TCHAR(E->engaged->role.c_str()) : TEXT(""), E->hasBall ? 1 : 0, E->stun, A && A->IsHidden() ? 1 : 0,
			W.X, W.Y, W.Z, A ? A->GetActorRotation().Yaw : 0.f, A && A->IsDown() ? 1 : 0,
			FL.X, FL.Y, FL.Z, FR.X, FR.Y, FR.Z, Pv.Z, Hd.Z, HR.X, HR.Y, HR.Z, HL.X, HL.Y, HL.Z, Scr.X, Scr.Y, RefD, RefW, A ? A->LastSlideRaw : 0.f, A ? A->LastSlideShown : 0.f,
			A ? A->LastDev[0] : 0.f, A ? A->LastDev[1] : 0.f, A ? A->LastDev[2] : 0.f, A ? A->LastDev[3] : 0.f, A ? A->LastDev[4] : 0.f, A ? A->LastDev[5] : 0.f,
			A ? A->LastDev[6] : 0.f, A ? A->TelEff : 0.f, A ? A->TelStride : 1.f, A ? A->TelAng : 0.f, A ? A->TelOrient : 0.f, Pv.X, Pv.Y, Hd.X, Hd.Y, A ? A->VisOffsetCm() : 0.f, A ? A->TelRootOff : 0.f, A ? A->LastPlantFeet : 0,
			A ? A->LastArm[0] : 0.f, A ? A->LastArm[1] : 0.f, A ? A->LastArm[2] : 0.f, A ? A->LastArm[3] : 0.f, A ? A->LastArm[4] : 0.f, A ? A->LastArm[5] : 0.f,
			A ? A->LastArm[6] : 0.f, A ? A->LastArm[7] : 0.f, A ? A->LastArm[8] : 0.f, A ? A->LastArm[9] : 0.f, A ? A->LastArm[10] : 0.f,
			M ? M->GetPredictedLODLevel() : -1, A ? A->LastArm[11] : 0.f, A ? A->LastArm[12] : 0.f, A ? A->LastArm[13] : 0.f, A ? A->LastArm[14] : 0.f, A ? A->LastArm[15] : 0.f, A ? A->LastArm[16] : 0.f,
			A ? *A->TelemetryAnim() : TEXT(""), *CamCols);
	}
}

void AMatchDirector::HlFlush()
{
	static const FString TeleDir = []
	{
		FString D;
		if (FParse::Value(FCommandLine::Get(), TEXT("TagHlDir="), D) && !D.IsEmpty()) return FPaths::GetPath(D) / TEXT("telemetry");
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Highlights/telemetry"));
	}();
	if (!HlTele.IsEmpty())
	{
		FFileHelper::SaveStringToFile(HlTele, *FString::Printf(TEXT("%s/c%03d.csv"), *TeleDir, HlClip));
		HlTele.Reset();
	}
	if (Sim)
	{
		FString Ev = TEXT("t,type,a_role,a_num,b_role,b_num,x,note\n");
		for (const tag::PlayEvent& V : Sim->events)
			Ev += FString::Printf(TEXT("%.3f,%s,%s,%d,%s,%d,%.2f,\"%s\"\n"), V.t, UTF8_TO_TCHAR(V.type.c_str()),
				V.a ? UTF8_TO_TCHAR(V.a->role.c_str()) : TEXT(""), V.a ? V.a->p->number : -1,
				V.b ? UTF8_TO_TCHAR(V.b->role.c_str()) : TEXT(""), V.b ? V.b->p->number : -1, V.x, UTF8_TO_TCHAR(V.note.c_str()));
		Ev += FString::Printf(TEXT("# end \"%s\"\n"), UTF8_TO_TCHAR(Sim->endReason.c_str()));
		FFileHelper::SaveStringToFile(Ev, *FString::Printf(TEXT("%s/ev%03d.csv"), *TeleDir, HlClip));
	}
}

// ------------------------------------------------------------------ tackle cam (-TagTackleCam)
namespace
{
	const TCHAR* GTcBones[] = {TEXT("pelvis"), TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"), TEXT("spine_04"), TEXT("neck_01"), TEXT("head"),
		TEXT("clavicle_l"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"), TEXT("clavicle_r"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"),
		TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("ball_l"), TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), TEXT("ball_r")};
	FString TcDir()
	{
		FString D;
		if (FParse::Value(FCommandLine::Get(), TEXT("TagTcDir="), D) && !D.IsEmpty()) return D;
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("TackleCam/frames"));
	}
}

void AMatchDirector::TcRecord()
{
	if (!Game) return;
	{
		// hold the world (engine pause) while shaders / assets compile and the first pack draw streams in, so every
		// filmed frame is a game frame and the tackles have the whole library
		bool bBusy = FAssetCompilingManager::Get().GetNumRemainingAssets() > 0 || (TcClip == 0 && FAthleteAssets::Get().bPackLoading);
#if WITH_EDITOR
		bBusy = bBusy || (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0);
#endif
		static bool bHeld = false;
		if (bBusy != bHeld)
		{
			bHeld = bBusy;
			bEnginePaused = bBusy;
			UGameplayStatics::SetGamePaused(this, bBusy);
			UE_LOG(LogTemp, Display, TEXT("TAG tc %s for compiles at clip %d"), bBusy ? TEXT("hold") : TEXT("resume"), TcClip);
		}
		if (bBusy) return;
	}
	// the pose the last screenshot showed (the bodies evaluated after the director's last tick): every man near the ball
	if (!TcCarrier.IsValid() || TcFrame == 0 || !Sim || !Game) return;
	const tag::TeamKey OffK = Game->poss;
	AAthlete* Cr = TcCarrier.Get();
	if (TcTele.IsEmpty())
	{
		TcTele = TEXT("frame,simt,state,pid,num,side,carrier,sanim,sstate,sx,sy,wx,wy,wz,yaw,down,anim");
		for (const TCHAR* B : GTcBones) TcTele += FString::Printf(TEXT(",%s_x,%s_y,%s_z"), B, B, B);
		TcTele += TEXT("\n");
	}
	const TCHAR* St = State == EMatchState::Live ? TEXT("live") : State == EMatchState::Post ? TEXT("post") : State == EMatchState::Wait ? TEXT("wait") : TEXT("other");
	for (tag::Ent* E : Sim->ents)
	{
		AAthlete* A = AthleteFor(E->p, E->side == tag::Side::Off ? OffK : tag::other(OffK));
		if (!A || A->IsHidden()) continue;
		if (A != Cr && FVector::Dist2D(A->GetActorLocation(), Cr->GetActorLocation()) > 450.f) continue;
		const FVector W = A->GetActorLocation();
		TcTele += FString::Printf(TEXT("%d,%.3f,%s,%d,%d,%s,%d,%s,%s,%.2f,%.2f,%.1f,%.1f,%.1f,%.1f,%d,%s"), TcFrame - 1, Sim->t, St, E->p->id, E->p->number,
			UTF8_TO_TCHAR(tag::sideName(E->side)), A == Cr ? 1 : 0, UTF8_TO_TCHAR(E->animName()), UTF8_TO_TCHAR(E->stateName()), E->pos.x, E->pos.y,
			W.X, W.Y, W.Z, A->GetActorRotation().Yaw, A->IsDown() ? 1 : 0, *A->TelemetryAnim().Replace(TEXT(","), TEXT(";")));
		USkeletalMeshComponent* M = A->Visible();
		for (const TCHAR* B : GTcBones)
		{
			if (M && M->GetBoneIndex(B) != INDEX_NONE)
			{
				const FVector P = M->GetSocketLocation(B);
				TcTele += FString::Printf(TEXT(",%.1f,%.1f,%.1f"), P.X, P.Y, P.Z);
			}
			else TcTele += TEXT(",,,");
		}
		TcTele += TEXT("\n");
	}
}

void AMatchDirector::TcFlush()
{
	const FString DataDir = FPaths::GetPath(TcDir()) / TEXT("tcdata");
	if (!TcTele.IsEmpty()) FFileHelper::SaveStringToFile(TcTele, *FString::Printf(TEXT("%s/t%03d.csv"), *DataDir, TcClip));
	if (Sim)
	{
		FString Ev = TEXT("t,type,a_num,a_side,b_num,b_side,note,a_pid,b_pid\n");
		for (size_t i = TcEv0; i < Sim->events.size(); ++i)
		{
			const tag::PlayEvent& V = Sim->events[i];
			Ev += FString::Printf(TEXT("%.3f,%s,%d,%s,%d,%s,\"%s\",%d,%d\n"), V.t, UTF8_TO_TCHAR(V.type.c_str()), V.a ? V.a->p->number : -1,
				V.a ? UTF8_TO_TCHAR(tag::sideName(V.a->side)) : TEXT(""), V.b ? V.b->p->number : -1, V.b ? UTF8_TO_TCHAR(tag::sideName(V.b->side)) : TEXT(""),
				UTF8_TO_TCHAR(V.note.c_str()), V.a ? V.a->p->id : -1, V.b ? V.b->p->id : -1);
		}
		FFileHelper::SaveStringToFile(Ev, *FString::Printf(TEXT("%s/tev%03d.csv"), *DataDir, TcClip));
	}
	UE_LOG(LogTemp, Display, TEXT("TAG tc clip %d end: %d frames, %s"), TcClip, TcFrame, TcAfter >= 0.f ? TEXT("tackle") : TEXT("no tackle"));
	TcTele.Reset();
	TcCarrier = nullptr;
	if (TcClip >= TcMaxClips || (Game && Game->phase == tag::GamePhase::Final))
	{
		UE_LOG(LogTemp, Display, TEXT("TAG tc done: %d clips"), TcClip);
		FPlatformMisc::RequestExit(false);
	}
}

void AMatchDirector::TickTackleCam(float Dt)
{
	if (!bTackleCam || !Game || bEnginePaused) return;
	const bool bPlayOn = Sim && (State == EMatchState::Live || State == EMatchState::Post || State == EMatchState::Wait);
	if (TcCarrier.IsValid() && !bPlayOn) TcFlush();
	if (Game->phase == tag::GamePhase::Final && !TcCarrier.IsValid()) { TcFlush(); return; }
	if (!bPlayOn) return;
	const tag::TeamKey OffK = Game->poss;
	auto AthOf = [&](const tag::Ent* X) { return AthleteFor(X->p, X->side == tag::Side::Off ? OffK : tag::other(OffK)); };
	if (!TcCarrier.IsValid())
	{
		// a defender closing on the ball carrier (within 3 yd, not blocked): a new clip
		const tag::Ball& B = Sim->ball;
		if (State != EMatchState::Live || !Sim->isLive() || B.mode != tag::BallMode::Held || !B.holder) return;
		const tag::Ent* Near = nullptr;
		for (const tag::Ent* D : Sim->ents)
			if (D->side != B.holder->side && !D->engaged && D->state != tag::EntState::Down && tag::dist(D->pos, B.holder->pos) < 3.0) { Near = D; break; }
		if (!Near) return;
		AAthlete* Cr = AthOf(B.holder);
		if (!Cr) return;
		TcCarrier = Cr;
		++TcClip;
		TcFrame = 0;
		TcT = 0.f;
		TcAfter = -1.f;
		TcEv0 = Sim->events.size();
		TcTele.Reset();
		// side on to the carrier's run, from the broadcast side
		FVector V = Cr->GetLastVel().GetSafeNormal2D();
		if (V.IsNearlyZero()) V = Cr->GetActorForwardVector();
		TcSide = FVector(-V.Y, V.X, 0.f);
		if (FVector::DotProduct(Camera->GetActorLocation() - Cr->GetActorLocation(), TcSide) < 0.f) TcSide = -TcSide;
		TcFocus = (Cr->GetActorLocation() + AthOf(Near)->GetActorLocation()) * 0.5f;
		UE_LOG(LogTemp, Display, TEXT("TAG tc clip %d start: #%d carrier, #%d closing at %.1f yd, sim t %.2f"), TcClip, B.holder->p->number, Near->p->number,
			tag::dist(Near->pos, B.holder->pos), Sim->t);
	}
	// the ball changed hands (a handoff, a catch): the clip follows the new carrier, the one who gets tackled
	if (Sim->ball.mode == tag::BallMode::Held && Sim->ball.holder)
		if (AAthlete* H = AthOf(Sim->ball.holder); H && H != TcCarrier.Get()) TcCarrier = H;
	AAthlete* Cr = TcCarrier.Get();
	TcT += Dt;
	for (size_t i = TcEv0; i < Sim->events.size(); ++i)
		if (TcAfter < 0.f && (Sim->events[i].type == "tackle" || Sim->events[i].type == "fumble")) TcAfter = 0.f;
	if (TcAfter >= 0.f) TcAfter += Dt;
	if (TcAfter > 3.2f || (TcAfter < 0.f && TcT > 3.0f) || TcFrame > 400)
	{
		TcFlush();
		return;
	}
	// the camera: 6 m off the pile, chest high, following the middle of the men around the carrier
	FVector Mid = Cr->GetActorLocation();
	int32 N = 1;
	for (tag::Ent* E : Sim->ents)
		if (AAthlete* A = AthOf(E); A && A != Cr && !A->IsHidden() && FVector::Dist2D(A->GetActorLocation(), Cr->GetActorLocation()) < 250.f) { Mid += A->GetActorLocation(); ++N; }
	Mid /= N;
	TcFocus = FMath::VInterpTo(TcFocus, Mid, Dt, 5.f);
	// (high enough to look over the men walking past between the lens and the pile)
	const FVector Look(TcFocus.X, TcFocus.Y, 55.f);
	const FVector At = Look + TcSide * 540.f + FVector(0, 0, 250.f);
	Camera->SetActorLocation(At);
	Camera->SetActorRotation((Look - At).Rotation());
	if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
	{
		C->CurrentFocalLength = 30.f;
		C->FocusSettings.ManualFocusDistance = (Look - At).Size();
	}
	FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("%s/t%03d_%04d.png"), *TcDir(), TcClip, TcFrame++), false, false);
}

bool AMatchDirector::UserOff() const { return bMulti ? Game && TeamHuman(Game->poss) : UserTeam.has_value() && Game && *UserTeam == Game->poss; }
bool AMatchDirector::UserDef() const { return bMulti ? Game && TeamHuman(tag::other(Game->poss)) : UserTeam.has_value() && Game && *UserTeam == tag::other(Game->poss); }

// ------------------------------------------------------------------ flow
void AMatchDirector::Next()
{
	tag::Game& G = *Game;
	Labels.clear();
	if (Overlay) Overlay->SetFlag(false);
	if (G.phase == tag::GamePhase::Final) return ShowFinal();
	if (G.q == 3 && LastQ == 2)
	{
		LastQ = 3;
		if (BeginHalftimeShow()) return; // (the halftime show, then the second half's kickoff: MatchBroadcast.cpp)
		if (bHighlights || !UserTeam.has_value())
		{
			// a CPU-vs-CPU game (watched or filmed) has nobody to press Start 2nd Half: show halftime and go on
			Overlay->Banner(TEXT("HALFTIME"), FString::Printf(TEXT("%s %d  -  %s %d"), *FString(UTF8_TO_TCHAR(G.away->abbr.c_str())),
				G.score.away, *FString(UTF8_TO_TCHAR(G.home->abbr.c_str())), G.score.home), FLinearColor::White);
			return Wait(bHighlights ? 3.f : 6.f);
		}
		State = EMatchState::Paused;
		Overlay->ShowBoxScore(TEXT("Halftime"), TEXT("Start 2nd Half"), [this] { State = EMatchState::Wait; Timer = 0; });
		return;
	}
	LastQ = G.q;
	if (G.phase != tag::GamePhase::Scrimmage) bHurryNext = bSpikeNext = false; // (a kick or a try: the tempo was for a down)
	if (G.phase == tag::GamePhase::Kickoff)
	{
		if (G.clock <= 0)
		{
			G.simStep();
			return Next();
		}
		const tag::TeamKey KT = G.kicking;
		const int D = G.worldDir(KT);
		auto KickU = Pick11(G.teamOf(KT), "K");
		auto RecvU = Pick11(G.teamOf(tag::other(KT)), "KR");
		HudDown = TEXT("KICKOFF");
		HoldHudScore();
		auto Go = [this, D, KickU, RecvU]()
		{
			tag::Game& GG = *Game;
			const tag::SpecialResult R = GG.kickoff(false, true);
			if (GG.pendingReturn) return StartLiveReturn(KickU, RecvU);
			StartKick(R, D, 0, KickU, RecvU, 35);
		};
		// the user's team kicking: aimed and swung on the meter first (Game/MatchKicking.cpp)
		if (UserKicks(KT) && !G.safetyKick) return BeginUserKick(0, D, KickU, RecvU, 35, false, Go);
		return Go();
	}
	if (G.phase == tag::GamePhase::Pat)
	{
		auto Decide = [this](bool bTwo)
		{
			tag::Game& GG = *Game;
			HudDown = bTwo ? TEXT("2-PT TRY") : TEXT("EXTRA POINT");
			if (!bTwo)
			{
				const int D = GG.worldDir();
				auto KickU = tag::tune().fgPlay > 0 ? GG.fgUnit(GG.poss, true) : Pick11(GG.offense(), "K");
				auto DefU = tag::tune().fgPlay > 0 ? GG.fgUnit(tag::other(GG.poss), false) : Pick11(GG.defense(), "D");
				HoldHudScore();
				auto Go = [this, D, KickU, DefU]()
				{
					// (played as a field goal from the 15: StartPlaceKick)
					if (tag::tune().fgPlay > 0) return StartPlaceKick(true, KickU, DefU, FgUserLook >= 0 ? 0.5 : 1.0, FgUserLook);
					const tag::SpecialResult R = Game->extraPoint();
					StartKick(R, D, 2, KickU, DefU, 85);
				};
				if (UserKicks(GG.poss)) return BeginUserKick(2, D, KickU, DefU, 85, true, Go);
				return Go();
			}
			bTwoPoint = true;
			CallPlays(true);
		};
		if (UserOff() && !bPadPlayTest && !bFeelTest)
		{
			State = EMatchState::Call;
			if (bMulti) SetMenuOwner(CallerOf(G.poss));
			Overlay->ShowPatChoice([this, Decide](bool bTwo) { SetMenuOwner(-1); Decide(bTwo); });
			return;
		}
		static const bool bFgFilm = FParse::Param(FCommandLine::Get(), TEXT("TagFgTest")); // (-TagFgTest films the kick, never the try for two)
		return Decide(bFgFilm ? false : G.goForTwo());
	}
	const int QBefore = G.q;
	const double ClockBefore = G.clock;
	if (!G.preSnap())
	{
		// the period is over: the clock stops at the break and starts again at the next snap (what had run off since
		// the whistle kept counting down on the scorebug, into the new quarter's 15:00)
		HudPreRun = 0.0;
		bPreRun = false;
		HudRunoffLeft = 0.0;
		bClockLive = false;
		bPlayClockOn = false;
		if (G.q != QBefore && G.phase != tag::GamePhase::Final)
		{
			// the break: the score panel and a wide shot of the stadium while the teams change ends (a CPU game watched
			// or filmed keeps its beat short)
			static const TCHAR* Ord[] = {TEXT("1ST"), TEXT("2ND"), TEXT("3RD"), TEXT("4TH")};
			const FString Title = G.q == 5 ? FString(TEXT("OVERTIME")) : FString::Printf(TEXT("END OF %s QUARTER"), QBefore >= 1 && QBefore <= 4 ? Ord[QBefore - 1] : TEXT(""));
			const float Secs = bHighlights || bAuto ? 1.5f : 3.4f;
			bSetAfterBreak = true;
			BeginBreak(Title, FString::Printf(TEXT("%s %d  -  %s %d"), *FString(UTF8_TO_TCHAR(G.away->abbr.c_str())), G.score.away,
				*FString(UTF8_TO_TCHAR(G.home->abbr.c_str())), G.score.home), Secs);
			return Wait(Secs);
		}
		return Wait(1.2f);
	}
	// the clock between the plays: what came off it now winds down on the scorebug through the call and the pre-snap,
	// and the play clock runs with it (a stopped clock: the offense still takes 24-32 s of the 40)
	HudRunoff = G.q == QBefore ? FMath::Max(0.0, ClockBefore - G.clock) : 0.0;
	HudRunoffLeft = FMath::Max(0.0, HudRunoff - HudPreRun); // what the scorebug already took off since the whistle stays off
	bRealClock = UserTeam.has_value() && !bAuto;
	bClockLive = HudRunoff > 1.0 && G.clockRunning; // (a timeout, or the two-minute warning, stopped it)
	if (bRealClock)
	{
		// real seconds: the play clock keeps running from the whistle, the game clock with it while it runs. What the
		// scorebug shows stays (a timeout called after it ran; more than the drawn runoff), except at the two-minute
		// warning, which stops it at 2:00
		const bool bTwoMinStop = HudRunoff > 0.0 && !G.clockRunning;
		HudRunoffLeft = bTwoMinStop ? 0.0 : FMath::Max(HudRunoff - HudPreRun, -FMath::Max(0.0, G.clock - 1.0));
		if (!bPlayClockOn) PlayClockF = 40.f;
		PlayClockSnap = 0.f;
		PlayClockRate = 1.f;
		HudRunoff = HudRunoffLeft;
		bPlayClockOn = true;
	}
	HudPreRun = 0.0;
	bPreRun = false;
	if (!bRealClock)
	{
		const float Use = HudRunoff > 1.0 ? static_cast<float>(HudRunoff) : FMath::FRandRange(24.f, 32.f);
		PlayClockSnap = FMath::Clamp(40.f - Use, 1.f, 30.f);
		if (!bPlayClockOn) PlayClockF = 40.f;
		PlayClockF = FMath::Max(PlayClockF, PlayClockSnap);
		PlayClockRate = FMath::Max(1.f, (PlayClockF - PlayClockSnap) / 4.f); // there by the snap (~4 s of call and pre-snap)
		HudRunoff = HudRunoffLeft;
		bPlayClockOn = true;
	}
	// the two-minute warning: the clock stopped at 2:00 - the break, as at the end of a quarter, before the next call
	if ((G.q == 2 || G.q == 4) && G.twoMin[G.q] && !bTwoMinShown[G.q] && G.phase == tag::GamePhase::Scrimmage)
	{
		bTwoMinShown[G.q] = true;
		const float Secs = bHighlights || bAuto ? 1.5f : 3.0f;
		BeginBreak(TEXT("TWO-MINUTE WARNING"), FString::Printf(TEXT("%s %d  -  %s %d     %s QUARTER"), *FString(UTF8_TO_TCHAR(G.away->abbr.c_str())), G.score.away,
			*FString(UTF8_TO_TCHAR(G.home->abbr.c_str())), G.score.home, G.q == 2 ? TEXT("2ND") : TEXT("4TH")), Secs);
		// (the game's clock already stands at 2:00; this call comes back here when the break is over)
		HudRunoffLeft = 0.0;
		return Wait(Secs);
	}
	// -TagFgTest: every possession kicks a field goal at once, from a new distance each time (look at the operation)
	static const bool bFgTest = FParse::Param(FCommandLine::Get(), TEXT("TagFgTest"));
	static const bool bPuntTest = FParse::Param(FCommandLine::Get(), TEXT("TagPuntTest"));
	if (bPuntTest)
	{
		static int32 PuntN = 0;
		G.ballOn = 28.0 + (PuntN++ * 11) % 30;
		G.down = 4;
		return DoPunt();
	}
	if (bFgTest)
	{
		// (every third one an extra point: the try is played the same way, from the 15)
		static int32 FgN = 0;
		if (FgN++ % 3 == 2)
		{
			G.phase = tag::GamePhase::Pat;
			G.ballOn = 85;
			return Next();
		}
		G.ballOn = 62.0 + (FgN * 9) % 30;
		G.down = 4;
		return DoFG();
	}
	if (!UserOff())
	{
		if (G.down == 4)
		{
			const tag::FourthDownCall C = G.fourthDownCall();
			if (C == tag::FourthDownCall::Punt) return DoPunt();
			if (C == tag::FourthDownCall::Fg) return DoFG();
		}
		else if ((G.q == 2 || G.q >= 4) && G.clock <= 6 && 100 - G.ballOn + 17 <= 56 && G.margin() >= -3)
			return DoFG();
	}
	bTwoPoint = false;
	CallPlays(false);
}

void AMatchDirector::CallPlays(bool bTwo)
{
	tag::Game& G = *Game;
	State = EMatchState::Call;
	if (Overlay) Overlay->SetHint(TEXT("")); // (the post-play's hurry-up / spike line)
	// the play's sim goes, the men stay where the whistle (and the jog since) left them, in the huddle or on the way
	Between.clear();
	if (Sim)
	{
		const tag::TeamKey OffWas = Sim->isReturn() ? G.poss : G.poss; // the sim's own side labels are the last play's
		for (tag::Ent* E : Sim->ents)
		{
			AAthlete* A = AthleteFor(E->p, Game->home->hasPlayer(E->p) ? tag::TeamKey::Home : tag::TeamKey::Away);
			FBetweenMan M;
			M.P = E->p;
			auto PJ = PostPos.find(E);
			M.Pos = PJ != PostPos.end() ? PJ->second : A->GetActorLocation();
			M.Pos.Z = 0.f;
			M.Yaw = A->GetActorRotation().Yaw;
			M.bOff = G.teamOf(G.poss).hasPlayer(E->p);
			Between.push_back(M);
		}
		BetweenBallW = ToWorld(Sim->ball.pos.x, Sim->ball.pos.y, Dir);
		(void)OffWas;
	}
	PostPos.clear();
	PostStay.clear();
	PostVel.clear();
	PostYaw.clear();
	SyncHumanLast(); // (each player gets his man back next play)
	Sim.reset();
	{
		std::vector<tag::Player*> Keep;
		for (const FBetweenMan& M : Between) Keep.push_back(M.P);
		ShowOnly(Keep);
	}
	Dir = G.worldDir();
	IdleFocus = ToWorld(bTwo ? 98 : G.ballOn, G.ballY, Dir);
	Stadium->SetLines(ToWorld(bTwo ? 98 : G.ballOn, 0, Dir).X, bTwo ? -1 : ToWorld(FMath::Min(100.0, G.ballOn + G.toGo), 0, Dir).X);
	{
		Stadium->SetDown(bTwo || G.phase != tag::GamePhase::Scrimmage ? 0 : G.down);
		static const TCHAR* QName[] = {TEXT("1ST"), TEXT("2ND"), TEXT("3RD"), TEXT("4TH")};
		const int32 Secs = FMath::Max(0, FMath::CeilToInt(G.clock));
		Stadium->SetScoreboard(FString::Printf(TEXT("%s %d   %s %d"), UTF8_TO_TCHAR(G.away->abbr.c_str()), G.score.away,
			UTF8_TO_TCHAR(G.home->abbr.c_str()), G.score.home),
			FString::Printf(TEXT("%s  %d:%02d"), G.q >= 1 && G.q <= 4 ? QName[G.q - 1] : TEXT("OT"), Secs / 60, Secs % 60));
		Stadium->SetGameClock(G.q, G.clock, G.quarterLen); // (the weather and the field's wear build over the game: visual only)
	}
	if (!bTwo) HudDown.Reset(); // (the scorebug still read KICKOFF through the first call of a drive)
	auto Finish = [this](const tag::OffPlay* Op, const tag::DefCall* Dc) { StartPresnap(Op, Dc); };
	if (bArtTest && !bTwo)
	{
		// -TagArtTest: the user's side of the ball is the one still to test; the calls are made for it
		UserTeam = !bArtDidOff ? G.poss : tag::other(G.poss);
		const tag::OffPlay* Op = UserOff() ? tag::playById("trips_flood") : G.cpuOffense();
		return Finish(Op, UserOff() ? G.cpuDefense(*Op) : tag::defById("base_c3"));
	}
	if (bMoveShot && !bTwo)
	{
		// -TagMoveShot: the user has the ball every snap: a run from his book (power and inside zone first)
		UserTeam = G.poss;
		bMoveShotHlThisPlay = false;
		if (++MoveShotPlays > 14 || MoveShotsMv >= 8)
		{
			UE_LOG(LogTemp, Display, TEXT("TAG moveshot done: %d plays, %d cue shots, %d move shots"), MoveShotPlays - 1, MoveShotsHl, MoveShotsMv);
			FPlatformMisc::RequestExit(false);
			return;
		}
		std::vector<const tag::OffPlay*> Runs;
		for (const tag::OffPlay* P : tag::offBookFor(G.offense()).plays)
			if (P->run && P->run->carrier == "RB" && P->run->style != tag::RunStyle::Draw && tag::formationById(P->formation)->personnel != "22") Runs.push_back(P);
		const tag::OffPlay* Op = Runs.empty() ? G.cpuOffense() : Runs[(MoveShotPlays * 7) % Runs.size()];
		UE_LOG(LogTemp, Display, TEXT("TAG moveshot play %d: %s (%s)"), MoveShotPlays, UTF8_TO_TCHAR(Op->name.c_str()), UTF8_TO_TCHAR(Op->formation.c_str()));
		return Finish(Op, G.cpuDefense(*Op));
	}
	if (bCallShot && !bTwo)
	{
		// -TagCallShot: the call screen itself, offense first and then defense, each shot with its cards up; the play
		// then runs on the coach's pick
		UserTeam = CallShotStep == 0 ? G.poss : tag::other(G.poss);
		const bool bOff = UserOff();
		Overlay->ShowPlayCall(bOff, false, false, [](FCallChoice) {});
		FTimerHandle H;
		GetWorld()->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, Finish, bOff]
		{
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_call_%s.png"), bOff ? TEXT("off") : TEXT("def")), true, false);
			FTimerHandle H2;
			GetWorld()->GetTimerManager().SetTimer(H2, FTimerDelegate::CreateWeakLambda(this, [this, Finish]
			{
				Overlay->ClearModal();
				if (++CallShotStep >= 2) { FPlatformMisc::RequestExit(false); return; }
				const tag::OffPlay* Op = Game->cpuOffense();
				Finish(Op, Game->cpuDefense(*Op));
			}), 0.6f, false);
		}), 3.f, false);
		return;
	}
	// the defense's call (a user defense: the call screen, with what the offense has come out in)
	std::function<void(const tag::OffPlay*)> DefCall = [this, Finish](const tag::OffPlay* Op)
	{
		// what the offense has come out in (its formation and the men on the field), as a defense sees before it
		// calls: "SHOTGUN - Trips TE   11 personnel (1 RB, 1 TE, 3 WR)"
		FString LookStr;
		if (const tag::Formation* F = Op ? tag::formationById(Op->formation) : nullptr)
		{
			int32 Rb = 0, Te = 0, Wr = 0;
			for (const tag::FormationSlot& Sl : F->slots)
			{
				if (Sl.pos.empty()) continue;
				const tag::Pos Ps = Sl.pos.front();
				if (Ps == tag::Pos::RB || Ps == tag::Pos::FB) ++Rb;
				else if (Ps == tag::Pos::TE) ++Te;
				else if (Ps == tag::Pos::WR) ++Wr;
			}
			const FString Fam = FString(UTF8_TO_TCHAR(tag::formationFamily(*F).c_str())).ToUpper();
			const FString Name = FString(UTF8_TO_TCHAR(F->name.c_str()));
			LookStr = FString::Printf(TEXT("OFFENSE IN  %s%s     %d%d PERSONNEL  (%d RB, %d TE, %d WR)"), *Fam,
				Name.Equals(Fam, ESearchCase::IgnoreCase) ? TEXT("") : *(TEXT("  -  ") + Name), Rb, Te, Rb, Te, Wr);
			UE_LOG(LogTemp, Display, TEXT("TAG defensive call sees: %s"), *LookStr);
		}
		Overlay->SetCallOffenseLook(LookStr);
		Overlay->SetCallOffenseForm(Op ? FString(UTF8_TO_TCHAR(Op->formation.c_str())) : FString()); // (the coach's suggestions read the offense's tendencies from this look: UI/CallCoach.cpp)
		if (bMulti) SetMenuOwner(CallerOf(tag::other(Game->poss)));
		Overlay->ShowPlayCall(false, false, false, [this, Op, Finish](FCallChoice C)
		{
			SetMenuOwner(-1);
			Finish(Op, C.Kind == FCallChoice::Def && C.Call ? C.Call : Game->cpuDefense(*Op));
		});
	};
	if ((UserOff() || TempoTest()) && !bTwo && G.phase == tag::GamePhase::Scrimmage && (bHurryNext || bSpikeNext) && !bMulti)
	{
		// hurry up: no huddle - the last play called again, at the line now; or the spike
		const tag::OffPlay* Op = bSpikeNext ? tag::playById("spike") : LastUserPlay;
		const bool bSpk = bSpikeNext;
		bHurryNext = bSpikeNext = false;
		if (!Op) Op = G.cpuOffense();
		UE_LOG(LogTemp, Display, TEXT("TAG user tempo: %s (%s)"), bSpk ? TEXT("spike") : TEXT("hurry up"), UTF8_TO_TCHAR(Op->name.c_str()));
		Overlay->Toast(bSpk ? TEXT("SPIKE") : FString::Printf(TEXT("HURRY UP  -  %s"), UTF8_TO_TCHAR(Op->name.c_str())).ToUpper());
		return Finish(Op, G.cpuDefense(*Op));
	}
	bHurryNext = bSpikeNext = false;
	if (bPadPlayTest && !bTwo)
	{
		// (the pad test's calls: a run for the user's offense, Cover 3 for his defense)
		const tag::OffPlay* Op = UserOff() ? tag::playById("i_power") : G.cpuOffense();
		if (UserOff()) LastUserPlay = Op;
		return Finish(Op, UserDef() ? tag::defById("base_c3") : G.cpuDefense(*Op));
	}
	if (bFeelTest)
	{
		// (-TagFeelTest's calls: the side and the play its next check wants, Game/MatchFeelTest.cpp; a try for two is the CPU's call)
		const tag::OffPlay* FOp = nullptr;
		const tag::DefCall* FDc = nullptr;
		if (bTwo || !FeelTestCall(FOp, FDc))
		{
			FOp = bTwo ? G.cpuTwoPointPlay() : G.cpuOffense();
			FDc = G.cpuDefense(*FOp);
		}
		if (UserOff() && !bTwo) LastUserPlay = FOp;
		return Finish(FOp, FDc);
	}
	if (UserOff())
	{
		// (multiplayer: the offense's caller works the screen; a defense with people on it calls next)
		if (bMulti) SetMenuOwner(CallerOf(G.poss));
		Overlay->ShowPlayCall(true, G.down == 4 && !bTwo, bTwo, [this, bTwo, Finish, DefCall](FCallChoice C)
		{
			SetMenuOwner(-1);
			tag::Game& GG = *Game;
			if (C.Kind == FCallChoice::Punt) return DoPunt();
			if (C.Kind == FCallChoice::FG) return DoFG();
			const tag::OffPlay* Op = C.Kind == FCallChoice::Off && C.Play ? C.Play : bTwo ? GG.cpuTwoPointPlay() : GG.cpuOffense();
			if (!bTwo && Op->type != tag::PlayType::Kneel && Op->type != tag::PlayType::Spike) LastUserPlay = Op;
			if (bMulti && UserDef()) return DefCall(Op);
			Finish(Op, GG.cpuDefense(*Op));
		});
	}
	else
	{
		const tag::OffPlay* Op = bTwo ? G.cpuTwoPointPlay() : G.cpuOffense();
		if (TempoTest() && !bTwo) LastUserPlay = Op; // (the test's hurry-up runs the CPU's last call again)
		// -TagForcePlay=<id>[,<id>...]: a filmed check of particular plays - the CPU offense calls these in turn on its
		// scrimmage downs (e.g. pistol_pa,ace_pa to see the play-action fakes)
		{
			static TArray<FString> Force = [] { FString S; TArray<FString> L; if (FParse::Value(FCommandLine::Get(), TEXT("TagForcePlay="), S, false)) S.ParseIntoArray(L, TEXT(",")); return L; }();
			static int32 ForceN = 0;
			if (!bTwo && Force.Num())
				if (const tag::OffPlay* F = tag::playById(TCHAR_TO_UTF8(*Force[ForceN++ % Force.Num()]))) Op = F;
		}
		// -TagForceDef=<id>[,<id>...]: the CPU defense calls these in turn on those downs (a filmed check of fronts and
		// pressures: nk33__mugbluff,dime32__c3,...)
		auto DefFor = [](tag::Game& GG, const tag::OffPlay& P) -> const tag::DefCall*
		{
			static TArray<FString> ForceD = [] { FString S; TArray<FString> L; if (FParse::Value(FCommandLine::Get(), TEXT("TagForceDef="), S, false)) S.ParseIntoArray(L, TEXT(",")); return L; }();
			static int32 ForceDN = 0;
			if (ForceD.Num() && P.type != tag::PlayType::Kneel && P.type != tag::PlayType::Spike)
				if (const tag::DefCall* D = tag::defById(TCHAR_TO_UTF8(*ForceD[ForceDN++ % ForceD.Num()]))) return D;
			return GG.cpuDefense(P);
		};
		if (UserDef()) DefCall(Op);
		else
		{
			// -TagHuddleSecs=<n>: a watched or filmed game takes this long over its call, as a user picking his play does
			// (the films cut straight to the next snap, and what the men do between plays was never on them)
			float HuddleSecs = 0.f;
			FParse::Value(FCommandLine::Get(), TEXT("TagHuddleSecs="), HuddleSecs);
			if (HuddleSecs > 0.f && !bTwo)
			{
				FTimerHandle H;
				GetWorld()->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, Op, Finish, DefFor] { Finish(Op, DefFor(*Game, *Op)); }), HuddleSecs, false);
			}
			else Finish(Op, DefFor(G, *Op));
		}
	}
}

void AMatchDirector::StartPresnap(const tag::OffPlay* Op, const tag::DefCall* Dc)
{
	tag::Game& G = *Game;
	if (!bTwoPoint) HudDown.Reset();
	if (!bTwoPoint && Op->type != tag::PlayType::Kneel && Op->type != tag::PlayType::Spike)
	{
		std::optional<tag::Penalty> Pen = G.rollPresnap();
		if (Pen && bRealClock && UserOff() && Pen->name == "Delay of Game") Pen.reset(); // the user's own play clock calls that
		// (not called here: the flag came out as the play was picked, before anyone was at the line. It waits for the
		// formation to set, as a false start or an offside does - CallPresnapFlag)
		PendingPreFlag = Pen;
		PreFlagAt = 0.25f + FMath::FRand() * 0.6f; // (inside the CPU's one-second set)
	}
	else PendingPreFlag.reset();
	std::optional<tag::Side> US;
	if (UserOff()) US = tag::Side::Off;
	else if (UserDef()) US = tag::Side::Def;
	// where the last play left everyone (world), so they jog back to the new formation instead of appearing on it
	std::map<tag::Player*, FVector> From;
	for (const FBetweenMan& M : Between) From[M.P] = M.Pos;
	if (Sim)
		for (tag::Ent* E : Sim->ents)
		{
			auto PJ = PostPos.find(E);
			From[E->p] = PJ != PostPos.end() ? PJ->second : ToWorld(E->pos.x, E->pos.y, Dir);
		}
	Between.clear();
	PostPos.clear();
	PostStay.clear();
	PostVel.clear();
	PostYaw.clear();
	bUmpHasBall = false;
	bUmpSpotted = false;
	Sim = bTwoPoint ? G.twoPointSim(*Op, *Dc, US) : G.makePlay(*Op, *Dc, US);
	SetupHumans();
	Dir = G.worldDir();
	double Far = 0.0;
	int32 Walkers = 0, Known = 0;
	// (a substitute walks on from his sideline as the rest walk back: as far as the play clock leaves time for)
	const double SubMax = bRealClock && bPlayClockOn ? FMath::Max(0.0, (PlayClockF - 4.0) * 4.2) : 32.0;
	// (substitutions, a few men: a whole unit coming on - the offense after a kickoff - takes its spots as before; the
	// quarterback walking 25 yd on from the sideline had no audible to play at the line)
	int32 SubsComing = 0;
	for (const tag::Ent* E : Sim->ents) SubsComing += From.count(E->p) ? 0 : 1;
	for (tag::Ent* E : Sim->ents)
	{
		auto It = From.find(E->p);
		if (It == From.end())
		{
			// a substitute (not on the field for the last play): in from his sideline (MatchBroadcast.cpp; -TagNoSubJog)
			tag::V2 SubFrom;
			if (From.size() >= 11 && SubsComing <= 6 && !bSetAfterBreak && SubEntrySpot(E, SubMax, SubFrom))
			{
				const double Ds = tag::dist(SubFrom, E->pos);
				Sim->walkFrom(E, SubFrom);
				Far = FMath::Max(Far, Ds);
				++Walkers;
			}
			continue;
		}
		++Known;
		const tag::V2 P = TagCoords::FromWorld(It->second, Dir);
		const double D = tag::dist(P, E->pos);
		// (after a quarter's break everyone is on his spot when the camera comes back: the teams changed ends while the
		// broadcast was on the stadium - the offense far away was put there and the defense jogged 30 yd in after the cut)
		if (D > 0.3 && D < 32.0 && !bSetAfterBreak) // a man far downfield (a long touchdown) takes his spot; the rest jog in
		{
			Sim->walkFrom(E, P);
			Far = FMath::Max(Far, D);
			++Walkers;
		}
	}
	if (bSetAfterBreak) bCutNext = true; // (from the stadium shot to the teams, set)
	bSetAfterBreak = false;
	UE_LOG(LogTemp, Verbose, TEXT("TAG presnap walk: %d of %d known men walk in, farthest %.1f yd"), Walkers, Known, Far);
	if (bAuto) UE_LOG(LogTemp, Display, TEXT("TAG presnap walk: %d of %d known men walk in, farthest %.1f yd"), Walkers, Known, Far);
	std::vector<tag::Player*> Ps;
	for (tag::Ent* E : Sim->ents) Ps.push_back(E->p);
	ShowOnly(Ps);
	const float SubsOffSecs = BeginSubsOff(From); // (the men going off jog to their benches: MatchBroadcast.cpp)
	CapturePrev();
	Stadium->SetLines(ToWorld(Sim->los, 0, Dir).X, bTwoPoint ? -1 : ToWorld(FMath::Min(100.0, G.ballOn + G.toGo), 0, Dir).X);
	Labels.clear();
	if (US && *US == tag::Side::Off) AssignLabels();
	State = EMatchState::Presnap;
	// the user's offense snaps when ready; against the user's defense the CPU offense gives it a few seconds to
	// make its calls (and waits while a pre-snap menu is open, up to the play clock)
	Timer = bWatch ? 1.1f / Speed : (US && *US == tag::Side::Off) ? 999.f : US ? 7.f : 1.6f;
	// ...and nobody snaps it before the men jogging back have set (4.2 yd/s is their jog; at most a few seconds)
	if (!(US && *US == tag::Side::Off)) Timer = FMath::Max(Timer, static_cast<float>(FMath::Min(6.0, Far / 4.2 + 0.7)) / Speed);
	// ...or before the men going off are off the field
	if (!(US && *US == tag::Side::Off)) Timer = FMath::Max(Timer, SubsOffSecs / Speed);
	// the pre-snap panel lists the calls; the hint is for the play itself
	bCarrierHint = false;
	const TagBind::FScope HintScope(US && *US == tag::Side::Off ? TagBind::ECtx::Passing : US ? TagBind::ECtx::Defense : TagBind::ECtx::None);
	if (PadUI())
		Overlay->SetHint(PadHint(US && *US == tag::Side::Off ? TEXT("Throw: hold the receiver's button bullet, tap it lob, tap twice pump fake    L1 high   L2 low    R3 throw away    R2 sprint")
			: US ? TEXT("Defending    CIRCLE switch    R2 sprint    SQUARE dive tackle    R-STICK up hit stick / down cut    TRIANGLE (ball in air) play the ball")
			: TEXT("CREATE camera    OPTIONS pause")));
	else
		Overlay->SetHint(TagBind::TranslateHint(US && *US == tag::Side::Off ? TEXT("Throw: the receiver's key 1-5 bullet (SHIFT lob, CTRL high, ALT low)   P pump fake   T throw away   WASD move")
			: US ? TEXT("Defending   TAB switch player   WASD move   SPACE dive tackle   F hit stick   V cut   (rushing: J finesse, K power)")
			: TEXT("1/2/4 game speed   C camera   ESC pause"), false));
	BeginPresnapUI();
	SyncPlay(0.f, 1.f);
}

void AMatchDirector::SyncRealClock()
{
	// the scorebug showed the game clock as the real seconds took it down: the game's clock becomes that (more off if
	// the offense took longer than the drawn runoff, some back if it snapped sooner)
	if (!bRealClock || !Game || FMath::Abs(HudRunoffLeft) < 0.01) return;
	Game->adjustBetweenPlays(-HudRunoffLeft);
	HudRunoffLeft = 0.0;
}

void AMatchDirector::DelayOfGame()
{
	tag::Game& G = *Game;
	SyncRealClock();
	bPlayClockOn = false;
	if (Overlay->HasModal()) Overlay->ClearModal();
	if (State == EMatchState::Presnap)
	{
		EndPresnapUI();
		Overlay->SetHint(TEXT(""));
	}
	G.delayOfGame();
	Overlay->SetFlag(true);
	if (Audio) Audio->Cue(TEXT("flag"), Ball ? Ball->GetActorLocation() : FVector::ZeroVector);
	Overlay->Banner(TEXT("FLAG"), FString(TEXT("Delay of Game  -  ")) + S(G.offense().abbr), Flag);
	Wait(2.f);
}

void AMatchDirector::CallPresnapFlag()
{
	if (!PendingPreFlag || !Game) return;
	tag::Game& G = *Game;
	const tag::Penalty Pen = *PendingPreFlag;
	PendingPreFlag.reset();
	// the man who jumped, in the pack's own false start / offside: a lineman flinching out of his stance, a back or a
	// receiver jumping in his two-point, a D-lineman across the ball standing up to argue it
	if (Sim)
	{
		int32 Num = -1;
		const size_t H = Pen.name.find("(#");
		if (H != std::string::npos) Num = std::atoi(Pen.name.c_str() + H + 2);
		const bool bFalse = Pen.name.find("False Start") != std::string::npos;
		const bool bOff = Pen.name.find("Offside") != std::string::npos || Pen.name.find("Encroachment") != std::string::npos ||
			Pen.name.find("Neutral Zone") != std::string::npos;
		if (Num >= 0 && (bFalse || bOff))
			for (const tag::Ent* E : Sim->ents)
			{
				if (!E->p || E->p->number != Num || (E->side == tag::Side::Off) != (Pen.on == tag::Side::Off)) continue;
				AAthlete* A = AthleteFor(E->p, E->side == tag::Side::Off ? G.poss : tag::other(G.poss));
				if (!A) break;
				const bool bLine = E->p->pos == tag::Pos::OT || E->p->pos == tag::Pos::OG || E->p->pos == tag::Pos::C;
				const bool b4pt = E->p->pos == tag::Pos::DT;
				const TCHAR* N = PreFlagClips[bFalse ? (bLine ? 0 : 1) : (b4pt ? 2 : 3)];
				// (loaded under the loading screen, WarmMatchAssets: here it is a lookup)
				if (UAnimSequence* Clip = WarmClip(FString::Printf(TEXT("/Game/TAG/AnimH/Pack/PrePlay/%s.%s"), N, N)))
				{
					A->PlayOneShot(Clip);
					UE_LOG(LogTemp, Display, TEXT("TAG presnap flag: #%d %s"), Num, *Clip->GetName());
				}
				break;
			}
	}
	SyncRealClock();
	bPlayClockOn = false;
	if (Overlay->HasModal()) Overlay->ClearModal();
	EndPresnapUI();
	Overlay->SetHint(TEXT(""));
	G.applyPresnap(Pen);
	Overlay->SetFlag(true);
	if (Audio) Audio->Cue(TEXT("flag"), Ball ? Ball->GetActorLocation() : FVector::ZeroVector);
	Overlay->Banner(TEXT("FLAG"), S(Pen.name) + TEXT("  -  ") + S((Pen.on == tag::Side::Off ? G.offense() : G.defense()).abbr), Flag);
	Wait(2.5f);
}

void AMatchDirector::AssignLabels()
{
	// Madden's buttons by where each man lines up (ArtLabelOrder: TE cross, right wideout circle, left square, slot
	// triangle, tailback R1); men kept in to block keep theirs until the snap (EndPresnapUI), so they can be hot routed
	Labels.clear();
	const std::vector<tag::Ent*> Order = ArtLabelOrder(*Sim);
	for (size_t i = 0; i < Order.size() && i < 5; ++i)
		if (Order[i]) Labels[Order[i]] = FString::FromInt(static_cast<int>(i) + 1);
}

void AMatchDirector::Wait(float Sec)
{
	State = EMatchState::Wait;
	Timer = Sec;
}

void AMatchDirector::ApplyPlay()
{
	tag::Game& G = *Game;
	tag::PlaySim& P = *Sim;
	const tag::TeamKey PossBefore = G.poss;
	if (bLiveReturn)
	{
		bLiveReturn = false;
		const tag::PlayLog L = G.finishKickReturn(P);
		UE_LOG(LogTemp, Display, TEXT("TAG live return result: %s (caught %d, end %.1f yd, %s)"), *S(L.desc), P.catcher ? 1 : 0, P.result ? P.result->endX : 0.0, *S(P.endReason));
		if (L.big == tag::LogTag::Turnover) Overlay->Banner(TEXT("FUMBLE"), S(L.desc), Bad);
		else if (L.desc.find("Touchback") != std::string::npos) Overlay->Toast(TEXT("TOUCHBACK"));
		State = EMatchState::Wait;
		Timer = BroadcastAfterPlay(); // (the shots after the play, if it earned any: MatchBroadcast.cpp)
		return;
	}
	if (bPlaceKick)
	{
		// a field goal / extra point played: into the game (the score, the stats, the ball after a block)
		bPlaceKick = false;
		const tag::SpecialResult R = G.finishPlaceKick(P);
		ReleaseHudScore();
		UE_LOG(LogTemp, Display, TEXT("TAG place kick result: %s (%s, look %d, blocked %d by %s, tipped %d, %.1f s)"), *S(R.desc), *S(P.endReason), P.placeLook,
			P.placeBlocked ? 1 : 0, P.placeBlockBy ? *S(P.placeBlockBy->role) : TEXT("-"), P.placeTipped ? 1 : 0, P.t);
		const tag::PlayLog* L = G.log.empty() ? nullptr : &G.log.back();
		if (L && L->big == tag::LogTag::Td) { Overlay->Banner(TEXT("TOUCHDOWN"), S(L->desc), Good); HomeCheer(1.f, tag::other(PossBefore)); } // (the block team's)
		else if (P.placeBlocked && L) Overlay->Banner(TEXT("BLOCKED"), S(L->desc), Bad);
		State = EMatchState::Wait;
		Timer = BroadcastAfterPlay();
		return;
	}
	if (bTwoPoint)
	{
		G.finishTwoPoint(P);
		bTwoPoint = false;
	}
	else
	{
		const tag::PlayLog L = G.finishPlay(P);
		if (L.big == tag::LogTag::Penalty)
		{
			// the call as the referee gives it: the foul, the team and the man ("Offensive Holding (#72 J.Smith)")
			FString Call = TEXT("Penalty on the play");
			const FString Desc = S(L.desc);
			const int32 At = Desc.Find(TEXT("PENALTY on "));
			if (At != INDEX_NONE)
			{
				Call = Desc.Mid(At + 11);
				const int32 End = Call.Find(TEXT(", enforced"));
				if (End != INDEX_NONE) Call.LeftInline(End);
			}
			Overlay->Banner(TEXT("FLAG"), Call, Flag);
		}
		else if (P.result && (P.result->td || P.result->yards >= 20)) { if (P.result->yards >= 20) HomeCheer(0.6f, PossBefore); } // (its stat line went up at the whistle)
		else if (G.poss == PossBefore && G.phase == tag::GamePhase::Scrimmage && G.down == 1 && P.result && !P.result->td && P.result->yards > 0)
			Overlay->Toast(TEXT("FIRST DOWN"));
		else if (L.desc.find("Turnover on downs") != std::string::npos) Overlay->Banner(TEXT("TURNOVER ON DOWNS"), TEXT(""), Bad);
	}
	State = EMatchState::Wait;
	Timer = BroadcastAfterPlay(); // (the cappers and the wide reset after the play, by the pace: MatchBroadcast.cpp)
}

void AMatchDirector::StartCutIn(const tag::Ent* Who, float Seconds)
{
	static const bool bForce = FParse::Param(FCommandLine::Get(), TEXT("TagCutIns")); // review cut-ins under automation
	if (!Who || ((bAuto || bLineup) && !bForce)) return; // automation drives its own shots
	UE_LOG(LogTemp, Display, TEXT("TAG cut-in: #%d %s for %.1f s"), Who->p->number, ANSI_TO_TCHAR(tag::posName(Who->p->pos)), Seconds);
	CutInTarget = AthleteFor(Who->p, Who->side == tag::Side::Off ? Game->poss : tag::other(Game->poss));
	CutInLeft = Seconds;
	bCutInPlaced = false;
	CutRecuts = 0;
	CutBlockedT = 0.f;
	Timer = FMath::Max(Timer, Seconds); // hold the post-play beat long enough for the shot
}

void AMatchDirector::OnPlayDead()
{
	const tag::PlayResult& R = *Sim->result;
	// the whistle: the play clock starts again at 40; a clock still running (a tackle in bounds) shows its seconds go by
	PlayClockF = 40.f;
	bPlayClockOn = true;
	SprintStamina = 1.f; // (his wind is back by the next snap)
	CoastV.clear();
	bSprintSpent = false;
	for (FTagHuman& H : Humans) { H.Ctl.SprintStamina = 1.f; H.Ctl.bSprintSpent = false; }
	bRealClock = UserTeam.has_value() && !bAuto;
	PostAge = 0.f;
	// a help-up: on an ordinary tackle, one time in four, the nearest teammate on his feet comes over for the carrier
	HelpStage = 0;
	HelpWalker = nullptr;
	{
		const FAthleteAssets& HA = FAthleteAssets::Get();
		static const bool bNoHelp = FParse::Param(FCommandLine::Get(), TEXT("TagNoHelpUp"));
		static const bool bAllHelp = FParse::Param(FCommandLine::Get(), TEXT("TagHelpUpAll"));
		const tag::Ent* C = Sim->ball.holder;
		if (!bNoHelp && HA.HelpUpDown.IsValid() && HA.HelpUpBy.IsValid() && C && C->side == tag::Side::Off && !Sim->isReturn() && !R.td && R.yards < 20 &&
			R.kind != tag::PlayKind::Sack && R.kind != tag::PlayKind::Kneel && R.kind != tag::PlayKind::Spike && !R.turnover && Game &&
			(bAllHelp || (C->p->id * 31 + static_cast<int32>(Game->log.size()) * 17) % 4 == 0))
		{
			AAthlete* Dn = AthleteFor(C->p, Game->poss);
			AAthlete* Best = nullptr;
			float BestD = 9.f * YD;
			for (const tag::Ent* E : Sim->off)
			{
				if (E == C || E->p->pos == tag::Pos::QB) continue;
				AAthlete* A = AthleteFor(E->p, Game->poss);
				const float Dd = A && !A->IsDown() && !A->IsHidden() ? static_cast<float>(FVector::Dist2D(A->GetActorLocation(), Dn->GetActorLocation())) : 1e9f;
				if (Dd < BestD) { BestD = Dd; Best = A; }
			}
			if (Dn && Best)
			{
				HelpDown = Dn;
				HelpBy = Best;
				HelpStage = 1;
				HelpAge = HelpT = HelpSpd = 0.f;
				HelpByPos = Best->GetActorLocation();
				UE_LOG(LogTemp, Display, TEXT("TAG help-up picked: #%d for #%d, %.0f cm away, down %d"), Best->Player ? Best->Player->number : -1, Dn->Player ? Dn->Player->number : -1, BestD, Dn->IsDown() ? 1 : 0);
			}
		}
	}
	// a big play: the broadcast's lower third with the man's game so far, this play counted (the book adds it later)
	if ((R.td == tag::Side::Off || R.yards >= 20) && (R.receiver || R.rusher) && !Sim->isReturn() && Game)
	{
		const bool bRec = R.receiver != nullptr && R.complete;
		const tag::Player* Who = bRec ? R.receiver : R.rusher;
		if (Who)
		{
			const tag::PlayerLine& Ln = Game->stats.line(*Who, Game->offense().abbr);
			const int32 Td = (bRec ? Ln.recTD : Ln.rushTD) + (R.td == tag::Side::Off ? 1 : 0);
			const FString Name = FString::Printf(TEXT("#%d %s. %s"), Who->number, *FString(1, UTF8_TO_TCHAR(Who->first.empty() ? "?" : Who->first.substr(0, 1).c_str())), UTF8_TO_TCHAR(Who->last.c_str()));
			const FString TdS = Td ? FString::Printf(TEXT(", %d TD"), Td) : FString();
			Overlay->Toast(bRec ? FString::Printf(TEXT("%s   %d rec, %d yds%s"), *Name, Ln.rec + 1, Ln.recYds + R.yards, *TdS)
								: FString::Printf(TEXT("%s   %d car, %d yds%s"), *Name, Ln.rushAtt + 1, Ln.rushYds + R.yards, *TdS));
		}
	}
	PostSpd.Empty();
	HudPreRun = 0.0;
	// (from the result: the game's own clock flag is set when the play is applied, later)
	bPreRun = Game && !R.incomplete && !R.outOfBounds && !R.td && !R.turnover && !R.safety && Game->tempo() != tag::Tempo::Hurry;
	// the broadcast at the whistle: who made the play (the ball carrier on a score, takeaway or big gain; the sacker),
	// whether it earns a replay, the lead shot (Game/MatchBroadcast.cpp; -TagShotsOld: the one cut-in as before)
	{
		const tag::Ent* Star = Sim->ball.holder;
		if (R.kind == tag::PlayKind::Sack && !R.tacklers.empty())
			for (const tag::Ent* E : Sim->ents)
				if (E->p == R.tacklers.front()) Star = E;
		DecideReplay(R); // (a score against the game's replay budget)
		BroadcastPlayDead(R, Star);
	}
	for (const auto& KV : Athletes) if (AAthlete* X = KV.Value.Get()) X->CelebPick = nullptr;
	TurnoverCelebMan = nullptr;
	UE_LOG(LogTemp, Display, TEXT("TAG contacts this play: %d (bodies meeting at speed; the strongest jolt %.0f cm/s)"), ContactsThisPlay, MaxJoltThisPlay);
	ContactsThisPlay = 0;
	MaxJoltThisPlay = 0.f;
	if (R.td)
	{
		// (the stands follow the home crowd's mood: TickStands; without the sound, the home side's score only)
		const tag::Ent* TdBy = Sim->ball.holder;
		HomeCheer(1.f, (TdBy ? TdBy->side : *R.td) == tag::Side::Off ? Game->poss : tag::other(Game->poss));
		ShakeCam(25.f, 0.5f, true); // (a slow sway: Game/MatchFeel.cpp)
		if (tag::Ent* Scorer = Sim->ball.holder)
		{
			// (a scorer tackled into the end zone falls first: the celebrate anim would skip the fall and jump him onto the spot.
			// He celebrates once he is up (TickPost's pending celebration), the shot held for it)
			if (Scorer->state != tag::EntState::Down)
			{
				Scorer->anim = tag::Anim::Celebrate;
				Scorer->animT = 0;
			}
			else
			{
				TurnoverCelebMan = Scorer;
				TurnoverCelebSerial = Sim->serial;
				Timer = FMath::Max(Timer, 3.6f);
			}
			// the teammates around him celebrate with him (a touchdown is a group celebration, not one man's: those within 25 yd
			// run in to it - at 12 yd a long touchdown left the scorer alone)
			for (tag::Ent* E : Sim->ents)
				if (E != Scorer && E->side == Scorer->side && E->state != tag::EntState::Down && tag::dist(E->pos, Scorer->pos) < 25.0)
				{
					E->anim = tag::Anim::Celebrate;
					E->animT = 0;
				}
		}
		Overlay->Banner(TEXT("TOUCHDOWN"), TEXT(""), Good);
	}
	// a takeaway: the man who came up with it holds the ball up (the pack's fumble-recovery and interception celebrations),
	// the two teammates nearest him point the way it is going. One on the turf celebrates once he is up (TickPost).
	if ((R.fumbleLost || R.interception) && !R.td && Sim->ball.holder && Game)
	{
		const FAthleteAssets& AA = FAthleteAssets::Get();
		tag::Ent* Star = Sim->ball.holder;
		static int32 GTakeSeq = 0;
		const int32 Seq = GTakeSeq++;
		const TArray<TObjectPtr<UAnimationAsset>>& Own = R.interception ? AA.PackCelebInt : AA.PackCelebFumbleBC;
		const TArray<TObjectPtr<UAnimationAsset>>& Mates = R.interception ? AA.PackCelebIntMates : AA.PackCelebFumblePoint;
		if (AAthlete* SA = AthleteFor(Star->p, Star->side == tag::Side::Off ? Game->poss : tag::other(Game->poss)); SA && Own.Num())
		{
			SA->CelebPick = Own[Seq % Own.Num()];
			TurnoverCelebMan = Star;
			TurnoverCelebSerial = Sim->serial;
		}
		TArray<TPair<double, tag::Ent*>, TInlineAllocator<12>> Near;
		for (tag::Ent* E : Sim->ents)
			if (E != Star && E->side == Star->side && E->state != tag::EntState::Down && tag::dist(E->pos, Star->pos) < 15.0)
				Near.Add(TPair<double, tag::Ent*>(tag::dist(E->pos, Star->pos), E));
		Near.Sort([](const TPair<double, tag::Ent*>& X, const TPair<double, tag::Ent*>& Y) { return X.Key < Y.Key; });
		for (int32 k = 0; k < Near.Num() && k < 2 && Mates.Num(); ++k)
			if (AAthlete* MA = AthleteFor(Near[k].Value->p, Near[k].Value->side == tag::Side::Off ? Game->poss : tag::other(Game->poss)))
			{
				MA->CelebPick = Mates[(Seq + 1 + k) % Mates.Num()];
				Near[k].Value->anim = tag::Anim::Celebrate;
				Near[k].Value->animT = 0;
			}
		UE_LOG(LogTemp, Display, TEXT("TAG takeaway celebration: #%d (%s), %d teammates"), Star->p ? Star->p->number : -1, R.interception ? TEXT("interception") : TEXT("fumble"),
			FMath::Min(2, Near.Num()));
	}
	if (R.td) {}
	else if (R.interception) Overlay->Banner(TEXT("INTERCEPTED"), TEXT(""), Bad);
	else if (R.fumbleLost) Overlay->Banner(TEXT("FUMBLE"), TEXT("Defense recovers"), Bad);
	else if (R.kind == tag::PlayKind::Sack) { Overlay->Banner(TEXT("SACK"), TEXT(""), Flag); ShakeCam(12.f, 0.3f, false); }
	else if (R.safety) Overlay->Banner(TEXT("SAFETY"), TEXT(""), Bad);
}

void AMatchDirector::RecordReplayFrame(float Dt)
{
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("TagNoReplay"));
	if (bOff || bLineup) return;
	ReplayClock += Dt;
	if (ReplayBuf.Num() && ReplayClock - ReplayBuf.Last().T < 1.f / 30.f) return;
	if (ReplayBuf.Num() >= 30 * 40) return; // 40 s cap
	// (a frame of the last play's film is written over where there is one: its per-man arrays are kept, not allocated
	// again - 44 arrays a frame, all freed at the next snap)
	if (ReplayPool.Num()) ReplayBuf.Add(ReplayPool.Pop(EAllowShrinking::No));
	else ReplayBuf.AddDefaulted();
	FReplayFrame& F = ReplayBuf.Last();
	F.T = ReplayClock;
	F.SimT = Sim ? static_cast<float>(Sim->t) : ReplayClock; // (the sim's events are on its clock: the replay's key moment)
	F.Ball = FTransform::Identity;
	F.bBall = false;
	if (Ball)
	{
		F.Ball = Ball->GetActorTransform();
		F.bBall = !Ball->IsHidden();
	}
	TArray<FTransform>& Local = ReplayScratch; // (free while recording: TickReplay's own scratch)
	int32 NumAth = 0;
	for (const auto& KV : Athletes)
	{
		const AAthlete* A = KV.Value;
		FTransform W;
		if (!A || A->IsHidden() || !A->CaptureReplayPose(W, Local)) continue;
		if (NumAth == F.Ath.Num()) F.Ath.AddDefaulted();
		FReplayAth& R = F.Ath[NumAth++];
		R.Id = KV.Key;
		R.World = W;
		R.Rot.SetNumUninitialized(Local.Num(), EAllowShrinking::No);
		R.Pos.SetNumUninitialized(Local.Num(), EAllowShrinking::No);
		for (int32 i = 0; i < Local.Num(); ++i)
		{
			R.Rot[i] = FQuat4f(Local[i].GetRotation());
			R.Pos[i] = FVector3f(Local[i].GetTranslation());
		}
	}
	F.Ath.SetNum(NumAth, EAllowShrinking::No);
}

void AMatchDirector::ResetReplayBuf()
{
	// the film is dropped; up to 300 of its frames (10 s) keep their arrays for the next play's
	ReplayPool.Reserve(300);
	while (ReplayBuf.Num() && ReplayPool.Num() < 300) ReplayPool.Add(ReplayBuf.Pop(EAllowShrinking::No));
	ReplayBuf.Reset();
}

// (StartReplay / TickReplay: the replay package, Game/MatchBroadcast.cpp)

void AMatchDirector::ShowFinal()
{
	if (BeginPostgameShow()) return; // (the postgame show first; it comes back here when it is over: MatchBroadcast.cpp)
	State = EMatchState::Final;
	Overlay->SetHint(TEXT(""));
	LogReplaySummary();
	LogBroadcastSummary(); // (the shots, the replay package, the pace's timings; the store written: MatchBroadcast.cpp)
	const tag::Game& G = *Game;
	const FString W = G.score.home == G.score.away ? FString(TEXT("Tie game"))
		: G.score.home > G.score.away ? S(G.home->city) + TEXT(" ") + S(G.home->name) + TEXT(" win")
		: S(G.away->city) + TEXT(" ") + S(G.away->name) + TEXT(" win");
	Overlay->ShowBoxScore(TEXT("Final  -  ") + W, TEXT("Main Menu"), [this] { ShowMainMenu(); });
}

void AMatchDirector::SetInvertRightY(bool b)
{
	bOptInvertRY = b;
	if (GConfig)
	{
		GConfig->SetBool(TEXT("TAG.Settings"), TEXT("InvertRightY"), b, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

float AMatchDirector::RightStickY(APlayerController* PC) const
{
	const float Y = PC ? PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY) : 0.f;
	return bOptInvertRY ? -Y : Y;
}

float AMatchDirector::RightStickY(const FTagReader& R) const
{
	const float Y = R ? R->GetInputAnalogKeyState(EKeys::Gamepad_RightY) : 0.f;
	return bOptInvertRY ? -Y : Y;
}

void AMatchDirector::SetCaptionsOn(bool b)
{
	bOptCaptions = b;
	if (Booth) Booth->SetCaptionsShown(b);
	if (GConfig)
	{
		GConfig->SetBool(TEXT("TAG.Settings"), TEXT("Captions"), b, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void AMatchDirector::TogglePause()
{
	if (bEnginePaused) return Unpause();
	if (State == EMatchState::Paused || State == EMatchState::Menu || State == EMatchState::Final) return;
	ResumeState = State;
	State = EMatchState::Paused;
	// the whole world holds (every actor's tick, the animation with it); the menu is Slate and keeps working. The
	// screen the pause came over (a play call, the try choice) is kept and comes back with Resume.
	bEnginePaused = UGameplayStatics::SetGamePaused(this, true);
	Overlay->ShowPause(
		[this] { Unpause(); },
		[this]
		{
			Overlay->DropSuspended();
			UGameplayStatics::SetGamePaused(this, false);
			bEnginePaused = false;
			if (Sim && Sim->isLive()) Sim->runToEnd();
			if (Sim && Sim->result && (ResumeState == EMatchState::Live || ResumeState == EMatchState::Post || ResumeState == EMatchState::Presnap))
			{
				if (bTwoPoint) Game->finishTwoPoint(*Sim); else Game->finishPlay(*Sim);
			}
			Sim.reset();
			Game->simToEnd();
			ShowFinal();
		},
		[this]
		{
			Overlay->DropSuspended();
			UGameplayStatics::SetGamePaused(this, false);
			bEnginePaused = false;
			ShowMainMenu();
		});
}

void AMatchDirector::Unpause()
{
	if (State != EMatchState::Paused) return;
	UGameplayStatics::SetGamePaused(this, false);
	bEnginePaused = false;
	State = ResumeState;
	Overlay->RestoreSuspended(); // the play call / try choice the pause came over, or nothing
}

void AMatchDirector::TickPauseTest()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	FViewport* VP = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
	// keys pressed last tick are released now (the pad test's own release runs on game time, which the pause holds)
	for (int32 i = PadHeld.Num() - 1; i >= 0; --i)
	{
		if (PC) PC->InputKey(FInputKeyEventArgs(VP, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), PadHeld[i].Key, IE_Released, 0.f, false, FPlatformTime::Cycles64()));
		PadHeld.RemoveAt(i);
	}
	const double Now = FPlatformTime::Seconds();
	auto Probe = [this](FVector* Out)
	{
		int32 n = 0;
		for (auto& KV : Athletes)
		{
			if (n >= 3) break;
			if (KV.Value->IsHidden()) continue;
			USkeletalMeshComponent* M = KV.Value->FindComponentByClass<USkeletalMeshComponent>();
			Out[n++] = M ? M->GetSocketLocation(TEXT("hand_r")) : KV.Value->GetActorLocation();
		}
		while (n < 3) Out[n++] = FVector::ZeroVector;
	};
	switch (PauseStep)
	{
	case 0:
		if (State == EMatchState::Call && Overlay && Overlay->HasModal())
		{
			UE_LOG(LogTemp, Display, TEXT("TAG pause test: play call up, pressing Esc"));
			PadPress(EKeys::Escape);
			PauseT = Now;
			PauseStep = 1;
		}
		break;
	case 1:
		if (Now - PauseT > 0.7)
		{
			PadCheck(bEnginePaused && State == EMatchState::Paused, TEXT("Esc pauses the engine"));
			FScreenshotRequest::RequestScreenshot(TEXT("tag_pause_menu.png"), true, false);
			Probe(PauseProbe);
			PauseT = Now;
			PauseStep = 2;
		}
		break;
	case 2:
		if (Now - PauseT > 1.2)
		{
			FVector P2[3];
			Probe(P2);
			float Moved = 0.f;
			for (int32 i = 0; i < 3; ++i) Moved = FMath::Max(Moved, static_cast<float>(FVector::Dist(P2[i], PauseProbe[i])));
			PadCheck(Moved < 0.01f, *FString::Printf(TEXT("the players hold still while paused (hand moved %.3f cm in 1.2 s)"), Moved));
			FScreenshotRequest::RequestScreenshot(TEXT("tag_pause_hold.png"), true, false);
			PadPress(EKeys::Escape);
			PauseT = Now;
			PauseStep = 3;
		}
		break;
	case 3:
		if (Now - PauseT > 0.7)
		{
			PadCheck(!bEnginePaused && State == EMatchState::Call && Overlay && Overlay->HasModal(), TEXT("Esc again resumes with the play call back up"));
			FScreenshotRequest::RequestScreenshot(TEXT("tag_pause_resume.png"), true, false);
			PauseT = Now;
			PauseStep = 4;
		}
		break;
	case 4:
		if (Now - PauseT > 0.5)
		{
			UE_LOG(LogTemp, Display, TEXT("TAG pause test done: %d pass %d fail"), PadPasses, PadFails);
			TagResult::Finish(TEXT("TagPauseTest"), PadPasses, PadFails); // (the result line and the exit status, Game/TagResult.h)
			PauseStep = 5;
		}
		break;
	default: break;
	}
}

void AMatchDirector::TrackInputDevice(APlayerController* PC)
{
	if (!PC) return;
	// a pad plugged in (or paired) after the game started: look again every two seconds, so the buttons on screen are
	// the ones in the player's hands (a DualSense connected at the menu was drawn as an Xbox pad all game)
	{
		// (the device list is read on a worker thread and its answer taken here a frame or so later: read on the game
		// thread it was a hitch every two seconds. -TagFakePad has nothing to read.)
		static double NextScan = 0.0;
		static TFuture<int32> PadScan;
		static const bool bFakePad = FCString::Strifind(FCommandLine::Get(), TEXT("TagFakePad=")) != nullptr;
		const double Now = FPlatformTime::Seconds();
		const bool bWasXbox = bXboxPad;
		bool bScanned = false, bSony = false;
		if (PadScan.IsValid())
		{
			if (PadScan.IsReady())
			{
				const int32 Bits = PadScan.Get();
				PadScan = TFuture<int32>(); // (taken: the next scan may start)
				bSony = (Bits & 1) != 0;
				bXboxPad = (Bits & 2) != 0;
				bScanned = true;
			}
		}
		else if (Now >= NextScan)
		{
			NextScan = Now + 2.0;
			if (bFakePad)
			{
				bSony = FindPlayStationPad();
				bScanned = true;
			}
			else PadScan = Async(EAsyncExecution::ThreadPool, []() -> int32 { return TagScanPads(); });
		}
		if (bScanned)
		{
			// the buttons drawn are the pad's in the player's hands: PlayStation while a Sony pad is connected, Xbox
			// names (A B X Y, LB RB LT RT, VIEW / MENU) for an Xbox pad or any other XInput pad
			if (bSony != bPlayStationPad)
			{
				bPlayStationPad = bSony;
				if (Overlay) Overlay->SetPadKind(TagBind::Sony(bSony));
				if (bSony) { bPadActive = true; if (Overlay) Overlay->Toast(TEXT("PS5 controller connected: PlayStation buttons")); }
				else if (bXboxPad && Overlay) Overlay->Toast(TEXT("Xbox buttons"));
				UE_LOG(LogTemp, Display, TEXT("TAG controllers: PlayStation pad %s"), bSony ? TEXT("connected") : TEXT("gone"));
			}
			if (bXboxPad && !bWasXbox)
			{
				bPadActive = true;
				if (Overlay && !bSony) Overlay->Toast(TEXT("Xbox controller connected: Xbox buttons"));
				UE_LOG(LogTemp, Display, TEXT("TAG controllers: Xbox pad connected"));
			}
		}
	}
	bool bPad = false, bKbd = false;
	for (const FKey& K : PadKeys)
		if (PC->WasInputKeyJustPressed(K)) { bPad = true; break; }
	if (!bPad)
	{
		const float LX = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX), LY = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
		const float RX = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX), RY = RightStickY(PC);
		bPad = FMath::Abs(LX) + FMath::Abs(LY) + FMath::Abs(RX) + FMath::Abs(RY) > 0.35f;
	}
	if (!bPad)
		for (const FKey& K : KeyboardKeys)
			if (PC->WasInputKeyJustPressed(K)) { bKbd = true; break; }
	if (bPad) bPadActive = true;
	else if (bKbd) bPadActive = false;
}

FString AMatchDirector::PadHint(const TCHAR* PS) const
{
	// the buttons the player put these actions on (Settings > Controls), in the part of the game being read
	FString H = TagBind::TranslateHint(PS, true);
	if (TagBind::Sony(bPlayStationPad)) return H;
	// an Xbox-style pad: its names for the same buttons
	static const TCHAR* Map[][2] = {{TEXT("L1"), TEXT("LB")}, {TEXT("R1"), TEXT("RB")}, {TEXT("L2"), TEXT("LT")}, {TEXT("R2"), TEXT("RT")},
		{TEXT("CREATE"), TEXT("VIEW")}, {TEXT("OPTIONS"), TEXT("MENU")}, {TEXT("CROSS"), TEXT("A")}, {TEXT("CIRCLE"), TEXT("B")},
		{TEXT("SQUARE"), TEXT("X")}, {TEXT("TRIANGLE"), TEXT("Y")}};
	for (const auto& M : Map) H.ReplaceInline(M[0], M[1], ESearchCase::CaseSensitive);
	return H;
}

// ------------------------------------------------------------------ special teams
// the pack's field goal capture (FG_Normal_Kick_v1), seconds into it: the ball leaves the snapper's hands, reaches the
// holder's, stands on the spot, and the foot meets it
static constexpr float FgSnapAt = 0.10f, FgCatchAt = 0.62f, FgPlaceAt = 1.0f, FgKickAt = 1.667f;
// and its punt capture (Punt_Normal_Kick_v1): the snap, the punter's catch, the foot; the punter starts 12.06 m behind
// the snapper; the fair catch capture's catch
static constexpr float PuntSnapAt = 0.20f, PuntCatchAt = 0.92f, PuntKickAt = 1.933f, PuntBack = 1206.f, FairCatchAt = 2.27f;
std::vector<tag::Player*> AMatchDirector::Pick11(tag::Team& T, const char* RoleName)
{
	std::vector<tag::Player*> Healthy;
	for (auto& P : T.roster)
		if (P->injuredWeeks <= 0) Healthy.push_back(P.get());
	auto By = [&](std::initializer_list<tag::Pos> Ps)
	{
		std::vector<tag::Player*> Out;
		for (tag::Player* P : Healthy)
			for (tag::Pos Q : Ps)
				if (P->pos == Q) { Out.push_back(P); break; }
		std::stable_sort(Out.begin(), Out.end(), [](tag::Player* A, tag::Player* B) { return A->r.speed > B->r.speed; });
		return Out;
	};
	std::vector<tag::Player*> Out;
	const FString R(RoleName);
	if (R == TEXT("K") || R == TEXT("P"))
	{
		// the man the game kicks with (the depth chart's kicker, punter or kickoff specialist where it is set)
		const bool bKickoff = R == TEXT("K") && Game->phase == tag::GamePhase::Kickoff;
		tag::Player* Spec = nullptr;
		if (Game && (Game->home == &T || Game->away == &T))
		{
			const tag::TeamKey Key = Game->home == &T ? tag::TeamKey::Home : tag::TeamKey::Away;
			Spec = R == TEXT("P") ? Game->punter(Key) : bKickoff ? tag::chartSpecialist(T, tag::DSlot::KOS) : Game->kicker(Key);
			if (!Spec && bKickoff) Spec = Game->kicker(Key);
			if (Spec && Spec->injuredWeeks > 0) Spec = nullptr;
		}
		if (Spec) Out.push_back(Spec);
		else
		{
			auto L = By({R == TEXT("K") ? tag::Pos::K : tag::Pos::P});
			if (!L.empty()) Out.push_back(L[0]);
		}
		if (R == TEXT("P") || !bKickoff)
		{
			for (tag::Player* P : By({tag::Pos::OT, tag::Pos::OG, tag::Pos::C, tag::Pos::TE}))
				if (Out.size() < 9 && std::find(Out.begin(), Out.end(), P) == Out.end()) Out.push_back(P);
			// the long snapper in the snapper's spot (the punt's man 4, the kick's man 5)
			const size_t SnapI = R == TEXT("P") ? 4 : 5;
			if (tag::Player* LS = tag::chartSpecialist(T, tag::DSlot::LS))
				if (SnapI < Out.size() && LS != Out[0])
				{
					auto It = std::find(Out.begin(), Out.end(), LS);
					if (It != Out.end()) std::iter_swap(It, Out.begin() + SnapI);
					else Out[SnapI] = LS;
				}
		}
	}
	if (R == TEXT("KR") || R == TEXT("PR"))
	{
		// the depth chart's returner, else the fastest of the receivers, corners and backs
		tag::Player* Ret = tag::chartSpecialist(T, R == TEXT("PR") ? tag::DSlot::PR : tag::DSlot::KR);
		if (Ret) Out.push_back(Ret);
		else
		{
			auto L = By({tag::Pos::WR, tag::Pos::CB, tag::Pos::RB});
			if (!L.empty()) Out.push_back(L[0]);
		}
	}
	auto Rest = R == TEXT("D") ? By({tag::Pos::DE, tag::Pos::DT, tag::Pos::LB}) : By({tag::Pos::LB, tag::Pos::S, tag::Pos::CB, tag::Pos::TE, tag::Pos::WR, tag::Pos::RB, tag::Pos::FB, tag::Pos::DE});
	for (tag::Player* P : Rest)
		if (Out.size() < 11 && std::find(Out.begin(), Out.end(), P) == Out.end()) Out.push_back(P);
	return Out;
}

void AMatchDirector::DoPunt()
{
	tag::Game& G = *Game;
	const int D = G.worldDir();
	HudDown = FString(UTF8_TO_TCHAR(G.downText().c_str())) + TEXT("  ·  ") + FString(UTF8_TO_TCHAR(G.ydLine().c_str()));
	auto KickU = Pick11(G.offense(), "P");
	auto RecvU = Pick11(G.defense(), "PR");
	const double Los = G.ballOn;
	HoldHudScore();
	auto Go = [this, D, KickU, RecvU, Los]()
	{
		tag::SpecialResult R = Game->punt(true);
		if (Game->pendingReturn) return StartLiveReturn(KickU, RecvU);
		if (R.flight) R.flight->fromX = Los - 14;
		StartKick(R, D, 1, KickU, RecvU, Los);
	};
	if (UserKicks(G.poss)) return BeginUserKick(1, D, KickU, RecvU, Los, false, Go);
	Go();
}

void AMatchDirector::DoFG()
{
	tag::Game& G = *Game;
	const int D = G.worldDir();
	HudDown = FString(UTF8_TO_TCHAR(G.downText().c_str())) + TEXT("  ·  ") + FString(UTF8_TO_TCHAR(G.ydLine().c_str()));
	auto KickU = Pick11(G.offense(), "K");
	auto DefU = Pick11(G.defense(), "D");
	const double Los = G.ballOn;
	HoldHudScore();
	// (the holder is the punter, as on most teams: Pick11 fills his place with a lineman)
	if (KickU.size() > 8)
		for (auto& P : G.offense().roster)
			if (P->pos == tag::Pos::P && P->injuredWeeks <= 0) { KickU[8] = P.get(); break; }
	// a field goal played: the game's own units (the punter holding, the long snapper over the ball), live from the set
	if (tag::tune().fgPlay > 0)
	{
		KickU = G.fgUnit(G.poss, true);
		DefU = G.fgUnit(tag::other(G.poss), false);
	}
	auto Go = [this, D, KickU, DefU, Los]()
	{
		if (tag::tune().fgPlay > 0) return StartPlaceKick(false, KickU, DefU, FgUserLook >= 0 ? 0.5 : 1.0, FgUserLook);
		const tag::SpecialResult R = Game->fieldGoal();
		StartKick(R, D, 2, KickU, DefU, Los);
	};
	if (UserKicks(G.poss)) return BeginUserKick(2, D, KickU, DefU, Los, false, Go);
	Go();
}

void AMatchDirector::StartLiveReturn(const std::vector<tag::Player*>& KickTeam, const std::vector<tag::Player*>& RecvTeam)
{
	tag::Game& G = *Game;
	ReleaseHudScore(); // a live return scores when its play ends, like any other
	// the game gave the ball to the receiving team: they are the sim's offense, attacking their way
	std::optional<tag::Side> US;
	if (UserOff()) US = tag::Side::Off;
	else if (UserDef()) US = tag::Side::Def;
	if (Sim) SyncHumanLast();
	// (the post-whistle maps are keyed by the last sim's men: a new man allocated where an old one was took his jog spot -
	// a kickoff returner after a field goal ran 25 yd in the sim while his body stood on the old spot, off the screen)
	PostPos.clear();
	PostStay.clear();
	PostVel.clear();
	PostYaw.clear();
	// (a live return is a kickoff too: the break's hold ends here as in StartKick - the second-half kickoff's whole
	// return had stayed on the crane shot, cd2 log "after 1" from the kick to the tackle)
	if (bSetAfterBreak)
	{
		bSetAfterBreak = false;
		if (BreakLeft <= 0.f) bCutNext = true;
	}
	Sim = G.kickReturnSim(RecvTeam, KickTeam, US);
	SetupHumans();
	// (the umpire's ball from the last play is not this one: still marked spotted, the kick was drawn on the tee through its
	// whole flight and the returner's catch pulled it 60-110 m into his hands - 26 Nov overnight, 7 held-ball jumps of 4-11 m)
	bUmpHasBall = false;
	bUmpSpotted = false;
	bLiveReturn = true;
	bKickCatchLogged = false;
	Dir = G.worldDir();
	std::vector<tag::Player*> Ps;
	for (tag::Ent* E : Sim->ents) Ps.push_back(E->p);
	ShowOnly(Ps);
	CapturePrev();
	Stadium->SetLines(-1, -1);
	Labels.clear();
	State = EMatchState::Live;
	Acc = 0;
	ResetReplayBuf();
	ReplayClock = 0.f;
	ReplayPostRec = 0.f;
	bReplayWorthy = false;
	const TagBind::FScope HintScope(US && *US == tag::Side::Off ? TagBind::ECtx::Carrier : TagBind::ECtx::Defense);
	Overlay->SetHint(US && *US == tag::Side::Off ? (PadUI() ? PadHint(TEXT("Returning the kick    L-STICK run    R2 sprint    in the end zone: leave the stick alone to take a knee")) : FString(TEXT("Returning the kick    WASD run    SPACE juke    in the end zone: stand still to take a knee")))
		: US ? (PadUI() ? PadHint(TEXT("Kick coverage    nobody moves until the catch    L-STICK run    L1 switch    R2 dive tackle")) : FString(TEXT("Kick coverage    nobody moves until the catch    WASD run    TAB switch    SPACE dive tackle")))
		: FString());
	UE_LOG(LogTemp, Display, TEXT("TAG live %s return: land %.1f, hang %.1f s"), Sim->cfg.kick->punt ? TEXT("punt") : TEXT("kickoff"),
		Sim->cfg.kick->landX, Sim->cfg.kick->hang);
	SyncPlay(0.f, 1.f);
}

void AMatchDirector::StartKick(const tag::SpecialResult& R, int D, int Kind, const std::vector<tag::Player*>& KickTeam, const std::vector<tag::Player*>& RecvTeam, double Los)
{
	Sim.reset();
	// the break's stadium shot holds until the teams are set at the new end, and the presnap walk ends that hold - but
	// the second half (and overtime) opens with a kickoff, which has no walk: the whole kick and return were filmed from
	// the crane in the corner of the bowl on a 22 mm lens (to7 c085, the returner 76 frames out of the picture). The
	// kicking team lined up is the teams set: back to the game camera (a cut, or one when the break's own beat runs out).
	if (bSetAfterBreak)
	{
		bSetAfterBreak = false;
		if (BreakLeft <= 0.f) bCutNext = true;
	}
	PostPos.clear(); // (keyed by the old sim's men: see StartLiveReturn)
	PostStay.clear();
	PostVel.clear();
	PostYaw.clear();
	Kick = std::make_unique<FKick>();
	Kick->Res = R;
	Kick->Dir = D;
	Kick->Kind = Kind;
	Kick->Kick = KickTeam;
	Kick->Recv = RecvTeam;
	if (Kind == 2 && tag::tune().fgPlay > 0 && Game)
	{
		// (a played field goal staged while the user aims: everyone where its play lines him up)
		Game->orderPlaceUnits(Game->poss, Kick->Kick, Kick->Recv);
		Kick->bPlaceOrder = true;
		Kick->PlaceLook = FMath::Max(0, FgUserLook);
	}
	Kick->bBlocked = Kind != 0 && FCStringAnsi::Strstr(R.desc.c_str(), "BLOCKED") != nullptr;
	tag::KickFlight F;
	if (R.flight) F = *R.flight;
	else { F.fromX = Los; F.toX = Los + 40; }
	Kick->FromX = F.fromX;
	Kick->ToX = FMath::Min(F.toX, 112.0);
	Kick->Dur = static_cast<float>(F.hang) + 1.6f;
	Kick->Lead = Kind == 0 ? 0.35f : Kind == 1 ? 1.0f : 0.9f;
	if (Kind == 0 && FMath::Abs(F.fromX - 35.0) < 0.5 && F.toX - F.fromX > 40.0 && !FParse::Param(FCommandLine::Get(), TEXT("TagNoFgScene")))
	{
		// the whole kickoff clip: the ball leaves at its foot contact
		Kick->bDynamicKo = true;
		Kick->Lead = FAthleteAssets::KickoffContact;
		Kick->Dur = Kick->Lead + static_cast<float>(F.hang) + 1.6f;
	}
	static const bool bNoFgScene = FParse::Param(FCommandLine::Get(), TEXT("TagNoFgScene"));
	if (Kind == 2 && !bNoFgScene && FAthleteAssets::Get().FgScene.Ok() && KickTeam.size() >= 11)
	{
		// the field goal as the pack captured it: a beat set, the snap, the hold, the kick 1.57 s after the snap
		Kick->SceneT0 = 1.3f;
		Kick->Lead = Kick->SceneT0 + FgKickAt;
		Kick->Dur = Kick->Lead + static_cast<float>(F.hang) + 1.4f;
	}
	Kick->Los = Los;
	if (const FAthleteAssets::FKickScene& Ps = FAthleteAssets::Get().PuntScene; Kind == 1 && !bNoFgScene && Ps.Snap.IsValid() && Ps.Kick.IsValid() && KickTeam.size() >= 11)
	{
		// the punt as captured: the punter's own steps decide where the ball is kicked from (his start 12 m behind the
		// snapper, less what he walks into it, his foot a stride ahead of him)
		Kick->SceneT0 = 1.3f;
		Kick->Lead = Kick->SceneT0 + PuntKickAt;
		Kick->Dur = Kick->Lead + static_cast<float>(F.hang) + 1.6f;
		const float Walk = static_cast<float>(Ps.Kick->ExtractRootMotionFromRange(0.0, PuntKickAt, FAnimExtractContext(0.0, true)).GetTranslation().Size2D());
		Kick->FromX = Los - (55.f + PuntBack - Walk - 90.f) / YD;
	}
	{
		// the flight: launch speed and angle solved (shooting) so the ball comes down at the sim's spot after the sim's
		// hang time, with quadratic drag on a tumbling / spiralling ball (k ~ 0.0045 per yard, cold air denser)
		const double G = 10.72, R = FMath::Max(1.0, Kick->ToX - Kick->FromX), T = FMath::Max(0.6, F.hang);
		const double Z0 = Kind == 1 ? 1.0 : 0.25, Zend = Kind == 2 ? 0.0 : 1.2, Hs = 1.0 / 60.0;
		const double K = 0.0045 * (Game ? 1 + (60 - Game->weather.tempF) * 0.0015 : 1.0);
		auto Fly = [&](double Vx, double Vz, std::vector<FVector2D>* Out, double& Land, double& Tl)
		{
			double X = 0, Z = Z0, Ux = Vx, Uz = Vz, Tt = 0;
			if (Out) Out->clear();
			while (Tt < T * 2.5)
			{
				if (Out) Out->push_back(FVector2D(X, Z));
				const double S = FMath::Sqrt(Ux * Ux + Uz * Uz);
				Ux -= K * S * Ux * Hs;
				Uz -= (G + K * S * Uz) * Hs;
				X += Ux * Hs;
				Z += Uz * Hs;
				Tt += Hs;
				if (Uz < 0 && Z <= Zend) break;
			}
			Land = X;
			Tl = Tt;
		};
		double Vx = R / T * 1.2, Vz = (Zend - Z0 + 0.5 * G * T * T) / T, Land = 0, Tl = 0;
		if (F.vx > 0)
		{
			// a played place kick: the sim's own strike, flown as the sim flew it (and on past the posts into the net)
			Vx = F.vx;
			Vz = F.vz;
		}
		else
			for (int It = 0; It < 12; ++It)
			{
				Fly(Vx, Vz, nullptr, Land, Tl);
				Vx *= R / FMath::Max(0.5, Land);
				Vz *= FMath::Pow(T / FMath::Max(0.2, Tl), 1.3);
			}
		Fly(Vx, Vz, &Kick->Path, Land, Tl);
		if (F.vx > 0)
			for (FVector2D& P : Kick->Path)
				if (P.X > R + 6.0) P.X = R + 6.0 + (P.X - R - 6.0) * 0.12; // the net behind the posts
	}
	std::vector<tag::Player*> All = KickTeam;
	All.insert(All.end(), RecvTeam.begin(), RecvTeam.end());
	ShowOnly(All);
	Stadium->SetLines(-1, -1);
	State = EMatchState::Kick;
	Overlay->SetHint(TEXT(""));
}

void AMatchDirector::UpdateKick(float Dt)
{
	FKick& K = *Kick;
	tag::KickFlight F;
	if (K.Res.flight) F = *K.Res.flight;
	// a user kick being aimed: the team set, the clock held short of the snap (the snapper's and holder's waits loop)
	K.T = K.bAwait ? FMath::Min(K.T + Dt, K.HoldT) : K.T + Dt;
	if (K.bAwait) TickUserKick(Dt);
	const float Lead = K.Lead;
	const float Ft = FMath::Clamp((K.T - Lead) / FMath::Max(0.1f, static_cast<float>(F.hang)), 0.f, 1.f);
	double BallX = K.T < Lead ? K.FromX : K.FromX + (K.ToX - K.FromX) * Ft;
	double BallZ = K.T < Lead ? 0.3 : 0.3 + 4.0 * F.height * Ft * (1 - Ft);
	if (K.T >= Lead && K.Path.size() > 1)
	{
		// along the solved flight (it ends where the sim put the ball; held there once it is down or caught)
		const float Fi = (K.T - Lead) * 60.f;
		const int32 I0 = FMath::Min(static_cast<int32>(Fi), static_cast<int32>(K.Path.size()) - 1);
		const int32 I1 = FMath::Min(I0 + 1, static_cast<int32>(K.Path.size()) - 1);
		const FVector2D P = FMath::Lerp(K.Path[I0], K.Path[I1], FMath::Clamp(Fi - I0, 0.f, 1.f));
		BallX = K.FromX + P.X;
		BallZ = FMath::Max(0.12, P.Y);
	}
	double BallY = F.endY ? F.y + (*F.endY - F.y) * FMath::Clamp((BallX - K.FromX) / FMath::Max(1.0, K.ToX - K.FromX), 0.0, 1.15) : F.y;
	// down and not caught (kickoffs and punts: a field goal's ball stays in the net): it bounces on, a football's way -
	// high, short and off line - and rolls to a stop
	if (K.Kind != 2 && K.T > Lead + static_cast<float>(F.hang) && K.Path.size() > 2)
	{
		if (!K.bBounce)
		{
			K.bBounce = true;
			const FVector2D A = K.Path[K.Path.size() - 2], B2 = K.Path.back();
			K.BounceP = FVector(BallX, BallY, 0.15);
			// (a football comes down nose first and kicks up short: the first hop is a couple of yards high and a few
			// long - at 40% of its fall it sailed 10 yd, out of the back of the end zone)
			K.BounceV = FVector((B2.X - A.X) * 60.0 * 0.35, 0.0, FMath::Abs((B2.Y - A.Y) * 60.0) * 0.28);
			if (F.endY) K.BounceV.Y = K.BounceV.X * (*F.endY - F.y) / FMath::Max(1.0, K.ToX - K.FromX);
			K.Hops = 0;
			UE_LOG(LogTemp, Display, TEXT("TAG kick bounce 0 (lands) at %.1f, %.1f yd: up %.1f yd/s, along %.1f, across %.1f"), K.BounceP.X, K.BounceP.Y,
				K.BounceV.Z, K.BounceV.X, K.BounceV.Y);
		}
		const float Sd = FMath::Min(Dt, 0.05f);
		K.BounceV.Z -= 10.72f * Sd;
		K.BounceP += K.BounceV * Sd;
		if (K.BounceP.Z <= 0.12 && K.BounceV.Z < 0.f)
		{
			// a bounce: the nose catches the turf - it kicks up, skips off to a side, loses most of its speed
			++K.Hops;
			const float Side = FMath::Frac(K.Hops * 0.618f + K.FromX * 0.13f) * 2.f - 1.f;
			const float Sp = static_cast<float>(FVector2D(K.BounceV.X, K.BounceV.Y).Size());
			K.BounceP.Z = 0.12;
			K.BounceV.Z = K.Hops > 4 ? 0.f : -K.BounceV.Z * (0.35f + 0.3f * FMath::Abs(Side));
			K.BounceV.X *= 0.6f;
			K.BounceV.Y = K.BounceV.Y * 0.5f + Side * Sp * 0.45f;
			if (K.Hops <= 5) UE_LOG(LogTemp, Display, TEXT("TAG kick bounce %d at %.1f, %.1f yd: up %.1f yd/s, along %.1f, across %.1f"), K.Hops, K.BounceP.X,
				K.BounceP.Y, K.BounceV.Z, K.BounceV.X, K.BounceV.Y);
		}
		if (K.Hops > 4) { K.BounceV.X *= FMath::Exp(-3.f * Sd); K.BounceV.Y *= FMath::Exp(-3.f * Sd); } // rolling out
		// over the end line or a sideline: it is dead there (it rolled on, held at the clamp, sliding along the line)
		if ((K.BounceP.X > 110.5 || K.BounceP.Y < -1.0 || K.BounceP.Y > tag::FIELD_W + 1.0) && K.Hops <= 4)
		{
			K.Hops = 5;
			K.BounceV = FVector(K.BounceV.X * 0.2f, K.BounceV.Y * 0.2f, 0.f);
			K.BounceP.Z = 0.12;
		}
		BallX = FMath::Clamp(static_cast<double>(K.BounceP.X), -11.0, 111.0);
		BallY = FMath::Clamp(static_cast<double>(K.BounceP.Y), -3.0, tag::FIELD_W + 3.0);
		BallZ = K.BounceP.Z;
	}
	Ball->SetActorLocation(ToWorld(BallX, BallY, K.Dir, BallZ));
	if (K.bBounce) Ball->SetActorRotation(FRotator(K.Hops * 37.f + K.T * 220.f, K.Hops * 61.f, K.T * 400.f)); // tumbling
	// filmed checks: where the bouncing ball is on each captured frame (normalised screen), to crop the footage to it
	if (K.bBounce && bHlFull && bHlInClip)
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			FVector2D Scr, VP(1, 1);
			if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(VP);
			if (PC->ProjectWorldLocationToScreen(Ball->GetActorLocation(), Scr, true))
				UE_LOG(LogTemp, Display, TEXT("TAG kick ball on screen: clip %d frame %d at %.3f %.3f, z %.2f yd, hops %d"), HlClip, HlFrame, Scr.X / FMath::Max(1.0, VP.X),
					Scr.Y / FMath::Max(1.0, VP.Y), BallZ, K.Hops);
		}
	if (K.Kind == 2 && F.vx > 0 && !K.bPlaneLogged && BallX >= 110.0)
	{
		// the shown ball at the posts (it must agree with the call: over 3.33 yd, inside 3.08 yd either way)
		K.bPlaneLogged = true;
		UE_LOG(LogTemp, Display, TEXT("TAG place kick at the posts: across %.2f yd, height %.2f yd, called %s"), BallY - tag::FIELD_W / 2, BallZ, F.good.value_or(false) ? TEXT("good") : TEXT("no good"));
	}
	Ball->AddActorLocalRotation(FRotator(0, 0, 900 * Dt));
	const double W = tag::FIELD_W;
	auto Pose = [&](tag::Player* P, double X, double Y, double FaceX, const TCHAR* Anim, float Sp)
	{
		AAthlete* A = AthleteFor(P, Game->home->hasPlayer(P) ? tag::TeamKey::Home : tag::TeamKey::Away);
		A->SetDormant(false);
		A->SetActorHiddenInGame(false);
		const float Yw = FaceToYaw(FaceX > 0 ? 0 : PI, K.Dir);
		const FVector VelW = FRotator(0, Yw, 0).Vector() * Sp * YD;
		// (aiming: the kicker stands at the top of his approach)
		if (K.bAwait && (FCString::Strcmp(Anim, TEXT("kickoff")) == 0 || FCString::Strcmp(Anim, TEXT("punt")) == 0 || FCString::Strcmp(Anim, TEXT("fgkick")) == 0 || FCString::Strcmp(Anim, TEXT("run")) == 0))
		{
			Anim = TEXT("idle");
			Sp = 0.f;
		}
		// kick clips: AnimT is where to start in the clip so its foot contact lands on the ball leaving (Lead)
		float AnimT = K.T;
		if (FCString::Strcmp(Anim, TEXT("kickoff")) == 0) AnimT = FMath::Max(0.f, FAthleteAssets::KickoffContact - Lead);
		else if (FCString::Strcmp(Anim, TEXT("punt")) == 0 || FCString::Strcmp(Anim, TEXT("fgkick")) == 0) AnimT = 0.f;
		A->Drive(ToWorld(X, Y, K.Dir), Yw, VelW, 9.f, Anim, AnimT, Dt, false);
		A->SetSelected(false);
		A->SetLabel(TEXT(""));
	};
	if (K.Kind == 0 && K.bDynamicKo)
	{
		// a kickoff that is not returned, lined up as the returned ones are (the dynamic kickoff): the kicker alone at
		// his 35 with his whole approach, the other ten on the receiving 40, nine receivers in the setup zone, and
		// nobody but the kicker and the returners moving until the ball is down
		const bool bDown = K.T > Lead + static_cast<float>(F.hang);
		const float After = FMath::Max(0.f, K.T - Lead - static_cast<float>(F.hang));
		for (size_t i = 0; i < K.Kick.size(); ++i)
		{
			if (i == 0)
			{
				const double X = K.FromX - 0.5 - FMath::Max(0.f, Lead - K.T) * 4.6 + FMath::Clamp(K.T - Lead, 0.f, 1.2f) * 2.5;
				Pose(K.Kick[i], X, W / 2, 1, K.T < Lead + 0.9f ? TEXT("kickoff") : TEXT("idle"), K.T < Lead + 0.9f ? 4.6f : 0.f);
				continue;
			}
			const double Y = 4 + (static_cast<double>(i) - 1) / 9.0 * (W - 8);
			Pose(K.Kick[i], 60.0 + FMath::Min(After * 3.0, 6.0), Y, 1, bDown && After < 2.f ? TEXT("run") : TEXT("idle"), bDown && After < 2.f ? 3.f : 0.f);
		}
		for (size_t i = 0; i < K.Recv.size(); ++i)
		{
			// (a kick nobody fields - over the end line, out of bounds, short: the returner still goes to where it is
			// coming down, on its own line across the field; he stood in the middle while it landed 15 yd from him)
			const double RetY = FMath::Clamp(F.endY ? *F.endY : F.y, 3.0, W - 3.0);
			const double RetX = FMath::Min(K.ToX, 108.0);
			if (i == 0) { Pose(K.Recv[i], RetX, RetY, -1, TEXT("idle"), 0.f); continue; }
			const int32 j = static_cast<int32>(i) - 1;
			if (j < 9) Pose(K.Recv[i], 67.0 - (j % 2) * 1.5, 4 + j * (W - 8) / 8.0, -1, TEXT("idle"), 0.f);
			else Pose(K.Recv[i], FMath::Min(K.ToX, 104.0) - 6.0, FMath::Clamp(RetY - 3.0, 4.0, W - 4.0), -1, TEXT("idle"), 0.f);
		}
	}
	else if (K.Kind == 0)
	{
		for (size_t i = 0; i < K.Kick.size(); ++i)
		{
			const double Y = i == 0 ? W / 2 : 4 + (static_cast<double>(i) - 1) / 9.0 * (W - 8);
			const double X = K.FromX - (i == 0 ? 7 : 1) + FMath::Max(0.f, K.T - 0.2f) * (i == 0 ? 4 : 8.5);
			const TCHAR* An = K.T > 0.2f ? TEXT("run") : TEXT("idle");
			if (i == 0 && K.T < Lead + 0.5f) An = TEXT("kickoff");
			Pose(K.Kick[i], FMath::Min(X, BallX + 2), Y, 1, An, K.T > 0.2f ? 8.5f : 0.f);
		}
		for (size_t i = 0; i < K.Recv.size(); ++i)
		{
			const double Y = i == 0 ? F.y : 6 + (static_cast<double>(i) - 1) / 9.0 * (W - 12);
			const double X = i == 0 ? FMath::Min(K.ToX, 100.0) : K.FromX + 12 + (i % 3) * 6 + FMath::Max(0.f, K.T - 1) * 2;
			Pose(K.Recv[i], X, Y, -1, i == 0 ? TEXT("idle") : TEXT("run"), i == 0 ? 0.f : 3.f);
		}
	}
	else if (K.Kind == 1 && K.SceneT0 >= 0.f)
	{
		// the pack's punt: snapper and punter on their two-man capture, the line set and blocking until the ball is
		// away, the gunners off at the snap; a fair catch is the returner's own capture
		const FAthleteAssets& AA = FAthleteAssets::Get();
		const FAthleteAssets::FKickScene& Sc = AA.PuntScene;
		const double Los = K.Los;
		// (a blocked punt comes down behind the line: the coverage still goes for where it would have landed)
		const double CovX = K.bBlocked ? Los + 40.0 : K.ToX;
		const FVector SnapW = ToWorld(Los - 0.6, F.y, K.Dir);
		const FVector FwdW = (ToWorld(Los + 0.4, F.y, K.Dir) - SnapW).GetSafeNormal();
		const FVector LeftW = FVector::CrossProduct(FwdW, FVector::UpVector);
		const float FwdYaw = FwdW.Rotation().Yaw;
		const bool bStart = !K.bSceneOn && K.T >= K.SceneT0;
		const bool bFirst = K.T <= Dt * 1.5f;
		const float Tc = K.T - K.SceneT0;
		const bool bSnapped = Tc >= PuntSnapAt;
		const bool bAway = K.T > Lead + 0.15f && !K.bBlocked; // (blocked: the line stays on its blocks)
		const size_t SnapI = 4;
		AAthlete* Punter = nullptr;
		struct FPart { size_t Who; FVector At; UAnimSequence* Clip; UAnimSequence* Wait; };
		const FPart Parts[2] = {
			{SnapI, SnapW, Sc.Snap.Get(), Sc.SnapWait.Get()},
			{0, SnapW - FwdW * PuntBack + LeftW * 9.6f, Sc.Kick.Get(), Sc.KickWait.Get()}};
		for (const FPart& P : Parts)
		{
			AAthlete* A = AthleteFor(K.Kick[P.Who], Game->home->hasPlayer(K.Kick[P.Who]) ? tag::TeamKey::Home : tag::TeamKey::Away);
			A->SetDormant(false);
			A->SetActorHiddenInGame(false);
			// (the snapper runs down under the punt with the rest once his capture has him up)
			if (P.Who == SnapI && K.T > Lead + 0.6f)
			{
				const double X = FMath::Min(Los - 0.6 + (K.T - Lead - 0.6f) * 7.0, CovX - 4.0);
				Pose(K.Kick[P.Who], X, F.y, 1, TEXT("run"), 7.f);
				continue;
			}
			if (bFirst && P.Wait) A->BeginScene(P.Wait, P.At, FwdYaw, FMath::Frac(P.Who * 0.37f + 0.2f) * P.Wait->GetPlayLength(), true);
			if (bStart || (bFirst && !P.Wait)) A->BeginScene(P.Clip, P.At, FwdYaw, 0.f, false);
			if (K.bSceneOn && K.T >= K.SceneT0 + P.Clip->GetPlayLength() - 0.05f) A->Drive(A->GetActorLocation(), A->GetActorRotation().Yaw, FVector::ZeroVector, 9.f, TEXT("idle"), 0.f, Dt, false);
			else A->Drive(P.At, FwdYaw, FVector::ZeroVector, 9.f, TEXT("scene"), 0.f, Dt, false);
			A->SetSelected(false);
			A->SetLabel(TEXT(""));
			if (P.Who == 0) Punter = A;
		}
		if (bStart) K.bSceneOn = true;
		auto Set = [&](tag::Player* Pl, bool bPre, uint8 Job)
		{
			AAthlete* A = AthleteFor(Pl, Game->home->hasPlayer(Pl) ? tag::TeamKey::Home : tag::TeamKey::Away);
			FAthleteFootball Fb;
			Fb.bPreSnap = Fb.bStance = bPre;
			Fb.GetOffKind = Job;
			A->SetFootball(Fb, Dt);
		};
		for (size_t i = 1; i < K.Kick.size(); ++i)
		{
			if (i == SnapI) continue;
			const float Down = bAway ? (K.T - Lead - 0.15f) : 0.f; // seconds running down under it
			if (i <= 7)
			{
				// the line, seven across: set, the block, then down the field
				Set(K.Kick[i], !bSnapped, 1);
				const double X = FMath::Min(Los - 0.75 + Down * 7.0, CovX - 3.0 - (i % 3));
				Pose(K.Kick[i], X, F.y + (static_cast<double>(i) - 4) * 1.3 + Down * (static_cast<double>(i) - 4) * 0.5, 1, bAway ? TEXT("run") : bSnapped ? TEXT("block") : TEXT("idle"), bAway ? 7.f : 0.f);
			}
			else if (i <= 9)
			{
				// the gunners, split wide: off at the snap
				const float Run = bSnapped ? Tc - PuntSnapAt : 0.f;
				Set(K.Kick[i], !bSnapped, 0);
				const double Y0 = i == 8 ? 5.0 : W - 5.0;
				const double X = FMath::Min(Los - 0.6 + FMath::Max(0.f, Run - 0.15f) * 8.3, CovX - 2.5);
				const bool bThere = X >= CovX - 2.6;
				Pose(K.Kick[i], X, Y0 + (F.y - Y0) * FMath::Clamp((X - Los) / FMath::Max(10.0, CovX - Los), 0.0, 0.85), 1, bSnapped && !bThere ? TEXT("run") : TEXT("idle"), bSnapped && !bThere ? 8.3f : 0.f);
			}
			else
			{
				// the personal protector, 6 yd back off the snapper's hip
				const double X = FMath::Min(Los - 6.0 + Down * 6.5, CovX - 6.0);
				Pose(K.Kick[i], X, F.y + 1.4, 1, bAway ? TEXT("run") : bSnapped ? TEXT("block") : TEXT("idle"), bAway ? 6.5f : 0.f);
			}
		}
		const bool bFair = FCStringAnsi::Strstr(K.Res.desc.c_str(), "fair catch") != nullptr && AA.FairCatch.IsValid();
		for (size_t i = 0; i < K.Recv.size(); ++i)
		{
			if (i == 0)
			{
				// the returner under it; a fair catch from the pack's capture, its catch on the ball's arrival
				const double X = FMath::Min(CovX, 100.0);
				const FVector At = ToWorld(X, F.y, K.Dir);
				AAthlete* A = AthleteFor(K.Recv[0], Game->home->hasPlayer(K.Recv[0]) ? tag::TeamKey::Home : tag::TeamKey::Away);
				if (bFair && !K.bFairOn && K.T >= Lead + static_cast<float>(F.hang) - FairCatchAt)
				{
					K.bFairOn = true;
					A->SetDormant(false);
					A->SetActorHiddenInGame(false);
					A->BeginScene(AA.FairCatch.Get(), At, FwdYaw + 180.f, FMath::Max(0.f, FairCatchAt - static_cast<float>(F.hang) - Lead + K.T), false);
				}
				if (K.bFairOn && A->SceneClipTime() >= AA.FairCatch->GetPlayLength() - 0.05f) A->Drive(A->GetActorLocation(), A->GetActorRotation().Yaw, FVector::ZeroVector, 9.f, TEXT("idle"), 0.f, Dt, false);
				else if (K.bFairOn) { A->SetDormant(false); A->SetActorHiddenInGame(false); A->Drive(At, FwdYaw + 180.f, FVector::ZeroVector, 9.f, TEXT("scene"), 0.f, Dt, false); }
				else Pose(K.Recv[0], X, F.y, -1, TEXT("idle"), 0.f);
				continue;
			}
			const float Back = K.T > Lead + 0.3f && !K.bBlocked ? K.T - Lead - 0.3f : 0.f; // seconds turned and running back with the coverage
			if (i == 4 && K.bBlocked && AA.FgBlockRush.IsValid())
			{
				// the block: the middle rusher through clean from the snap, at the punter's foot as it meets the ball, laid
				// out across it (the field goal block's layout, from just before he leaves his feet)
				UAnimSequence* Bc = AA.FgBlockRush.Get();
				AAthlete* A = AthleteFor(K.Recv[i], Game->home->hasPlayer(K.Recv[i]) ? tag::TeamKey::Home : tag::TeamKey::Away);
				const float DiveFrom = FAthleteAssets::FgBlockAt - 0.15f;
				const double X0 = Los + 1.0, Y0 = F.y - 0.65;
				const FVector Bw = ToWorld(K.FromX + 1.5, F.y, K.Dir);
				const FVector Dive = A->SceneTravel(Bc, DiveFrom, FAthleteAssets::FgBlockAt + 0.15f, 0.f);
				if (!K.bBlockOn && K.T >= Lead - 0.15f)
				{
					K.bBlockOn = true;
					const FVector Here = A->GetActorLocation();
					const float Yaw = static_cast<float>((Bw - Here).Rotation().Yaw - Dive.Rotation().Yaw);
					A->BeginScene(Bc, Here, Yaw, DiveFrom, false);
					UE_LOG(LogTemp, Display, TEXT("TAG punt block: #%d laid out %.0f cm from the foot"), K.Recv[i]->number, FVector::Dist2D(Here, Bw));
				}
				if (K.bBlockOn)
				{
					if (A->SceneClipTime() >= Bc->GetPlayLength() - 0.05f) A->Drive(A->GetActorLocation(), A->GetActorRotation().Yaw, FVector::ZeroVector, 9.f, TEXT("idle"), 0.f, Dt, false);
					else A->Drive(A->GetActorLocation(), A->GetActorRotation().Yaw, FVector::ZeroVector, 9.f, TEXT("scene"), 0.f, Dt, false);
					continue;
				}
				Set(K.Recv[i], !bSnapped, 3);
				// (from the line to a dive's length short of the ball, between the get-off and the dive)
				const double Xd = K.FromX + 1.5 + Dive.Size2D() / YD, T0 = PuntSnapAt + 0.15, T1 = Lead - 0.15f - K.SceneT0;
				const double Al = FMath::Clamp((Tc - T0) / FMath::Max(0.3, T1 - T0), 0.0, 1.0);
				const double Sp = (X0 - Xd) / FMath::Max(0.3, T1 - T0); // yd/s
				Pose(K.Recv[i], X0 + (Xd - X0) * Al, Y0 + (F.y - Y0) * Al, -1, Al > 0.0 ? TEXT("run") : bSnapped ? TEXT("block") : TEXT("idle"), Al > 0.0 ? static_cast<float>(Sp) : 0.f);
				continue;
			}
			if (i <= 8)
			{
				// the rush: set across from the line, into it at the snap, then back with the coverage
				Set(K.Recv[i], !bSnapped, 3);
				const double In = bSnapped ? FMath::Min(0.35, (Tc - PuntSnapAt) * 1.5) : 0.0;
				const double X = FMath::Min(Los + 1.0 - In + Back * 6.0, CovX - 7.0 - (i % 4) * 1.5);
				Pose(K.Recv[i], X, F.y + (static_cast<double>(i) - 4.5) * 1.3 + Back * (static_cast<double>(i) - 4.5) * 0.6, Back > 0.f ? 1 : -1, Back > 0.f ? TEXT("run") : bSnapped ? TEXT("block") : TEXT("idle"), Back > 0.f ? 6.f : 0.f);
			}
			else
			{
				// the men over the gunners: run with them
				const float Run = bSnapped ? Tc - PuntSnapAt : 0.f;
				const double Y0 = i == 9 ? 5.0 : W - 5.0;
				const double X = FMath::Min(Los + 1.5 + FMath::Max(0.f, Run - 0.3f) * 8.0, CovX - 4.5);
				const bool bThere = X >= CovX - 4.6;
				Pose(K.Recv[i], X, Y0 + (F.y - Y0) * FMath::Clamp((X - Los) / FMath::Max(10.0, CovX - Los), 0.0, 0.8) + (i == 9 ? 1.2 : -1.2), Run > 0.3f ? 1 : -1, Run > 0.3f && !bThere ? TEXT("run") : TEXT("idle"), Run > 0.3f && !bThere ? 8.f : 0.f);
			}
		}
		// the ball: under the snapper, back to the punter, in his hands, dropped onto the foot
		if (K.T < Lead)
		{
			const FVector Turf = ToWorld(Los - 0.15, F.y, K.Dir, 0.12);
			const FVector Foot = ToWorld(K.FromX, F.y, K.Dir, 1.0);
			FVector Hands = SnapW - FwdW * PuntBack + FVector(0, 0, 110);
			if (USkeletalMeshComponent* M = Punter ? Punter->Visible() : nullptr)
				if (M->GetBoneIndex(TEXT("hand_l")) != INDEX_NONE && M->GetBoneIndex(TEXT("hand_r")) != INDEX_NONE)
					Hands = 0.5 * (M->GetBoneLocation(TEXT("hand_l")) + M->GetBoneLocation(TEXT("hand_r")));
			FVector B = Turf;
			FRotator R(0, FwdYaw, 0);
			if (Tc >= PuntSnapAt && Tc < PuntCatchAt)
			{
				const float Al = (Tc - PuntSnapAt) / (PuntCatchAt - PuntSnapAt);
				B = FMath::Lerp(Turf + FVector(0, 0, 30), Hands, Al) + FVector(0, 0, 55.f * FMath::Sin(PI * Al));
				R = FRotator(0, FwdYaw, Tc * 1400.f);
			}
			else if (Tc >= PuntCatchAt)
			{
				// held out, then the drop: the last fifth of a second it leaves the hands for the foot
				const float Al = FMath::SmoothStep(0.f, 1.f, (Tc - (PuntKickAt - 0.2f)) / 0.2f);
				B = FMath::Lerp(Hands, Foot, Al);
				R = FRotator(-12.f, FwdYaw, 0);
			}
			Ball->SetActorLocationAndRotation(B, R);
		}
	}
	else if (K.Kind == 1)
	{
		const double Los = K.FromX + 14;
		for (size_t i = 0; i < K.Kick.size(); ++i)
		{
			const double Y = i == 0 ? W / 2 : i <= 6 ? W / 2 + (static_cast<double>(i) - 3.5) * 1.4 : i == 7 ? 4 : i == 8 ? W - 4 : W / 2 + (static_cast<double>(i) - 9.5) * 6;
			const double X = (i == 0 ? K.FromX : Los - 0.6) + FMath::Max(0.f, K.T - 1.1f) * (i == 0 ? 1 : 8);
			const TCHAR* An = K.T > 1.1f ? TEXT("run") : TEXT("idle");
			if (i == 0 && K.T > Lead - FAthleteAssets::PuntContact && K.T < Lead + 0.5f) An = TEXT("punt"); // the punter
			Pose(K.Kick[i], X, Y, 1, An, K.T > 1.1f ? 8.f : 0.f);
		}
		for (size_t i = 0; i < K.Recv.size(); ++i)
		{
			const double Y = i == 0 ? F.y : W / 2 + (static_cast<double>(i) - 5.5) * 2.2;
			const double X = i == 0 ? FMath::Min(K.ToX, 100.0) : Los + 1 - FMath::Max(0.f, K.T - 1) * 3;
			Pose(K.Recv[i], X, Y, -1, i == 0 ? TEXT("idle") : TEXT("block"), 0.f);
		}
	}
	else if (K.SceneT0 >= 0.f)
	{
		// the pack's field goal: snapper, holder and kicker on their three-man capture, placed so the spot the capture
		// kicks from is the spot the ball is kicked from here; the two lines set, then into their blocks at the snap
		const FAthleteAssets::FKickScene& Sc = FAthleteAssets::Get().FgScene;
		const double Los = K.FromX + 7;
		const FVector SpotW = ToWorld(K.FromX, F.y, K.Dir);
		const FVector FwdW = (ToWorld(K.FromX + 1, F.y, K.Dir) - SpotW).GetSafeNormal();
		const FVector LeftW = FVector::CrossProduct(FwdW, FVector::UpVector);
		const float FwdYaw = FwdW.Rotation().Yaw;
		// where each man starts in the capture (m; x left, y back of the kick spot; heading turned left of downfield)
		static float Mirror = 1.f, SpotX = 0.f, SpotY = 6.55f;
		static const bool bOnce = [&]()
		{
			FParse::Value(FCommandLine::Get(), TEXT("TagFgMirror="), Mirror);
			FParse::Value(FCommandLine::Get(), TEXT("TagFgSpotX="), SpotX);
			FParse::Value(FCommandLine::Get(), TEXT("TagFgSpotY="), SpotY);
			return true;
		}();
		struct FPart { size_t Who; float X, Y, H; UAnimSequence* Clip; UAnimSequence* Wait; };
		// (the played place kick's order: K, H, LS first; the director's Pick11 had the snapper 5th and the holder 8th)
		const size_t SnapI = K.bPlaceOrder ? 2 : 5, HoldI = K.bPlaceOrder ? 1 : 8;
		const FPart Parts[3] = {
			{SnapI, -0.003f, 0.792f, 0.f, Sc.Snap.Get(), Sc.SnapWait.Get()},
			{HoldI, -0.558f, 6.563f, 90.f, Sc.Hold.Get(), Sc.HoldWait.Get()},
			{0, 1.881f, 8.688f, -40.3f, Sc.Kick.Get(), Sc.KickWait.Get()}};
		const bool bStart = !K.bSceneOn && K.T >= K.SceneT0;
		const bool bFirst = K.T <= Dt * 1.5f;
		AAthlete* Holder = nullptr;
		for (const FPart& P : Parts)
		{
			AAthlete* A = AthleteFor(K.Kick[P.Who], Game->home->hasPlayer(K.Kick[P.Who]) ? tag::TeamKey::Home : tag::TeamKey::Away);
			A->SetDormant(false);
			A->SetActorHiddenInGame(false);
			const FVector At = SpotW + LeftW * (Mirror * (P.X - SpotX) * 100.f) - FwdW * ((P.Y - SpotY) * 100.f);
			const float Yaw = FwdYaw - Mirror * P.H;
			if (bFirst && P.Wait) A->BeginScene(P.Wait, At, Yaw, FMath::Frac(P.Who * 0.37f) * P.Wait->GetPlayLength(), true);
			if (bStart || (bFirst && !P.Wait)) A->BeginScene(P.Clip, At, Yaw, 0.f, false);
			// (the capture over, he stands where it left him)
			if (K.bSceneOn && K.T >= K.SceneT0 + P.Clip->GetPlayLength() - 0.05f) A->Drive(A->GetActorLocation(), A->GetActorRotation().Yaw, FVector::ZeroVector, 9.f, TEXT("idle"), 0.f, Dt, false);
			else A->Drive(At, Yaw, FVector::ZeroVector, 9.f, TEXT("scene"), 0.f, Dt, false);
			A->SetSelected(false);
			A->SetLabel(TEXT(""));
			if (P.Who == HoldI) Holder = A;
		}
		if (bStart) K.bSceneOn = true;
		const bool bSnapped = K.T >= K.SceneT0 + FgSnapAt;
		if (K.bPlaceOrder)
		{
			// a played field goal being aimed: the two units where its play lines them up, set in their stances (the play
			// itself starts from these spots when the swing is made: StartPlaceKick)
			for (int32 Unit = 0; Unit < 2; ++Unit)
			{
				const std::vector<tag::Player*>& Us = Unit == 0 ? K.Kick : K.Recv;
				for (size_t i = Unit == 0 ? 3 : 0; i < Us.size() && i < 11; ++i)
				{
					AAthlete* A = AthleteFor(Us[i], Game->home->hasPlayer(Us[i]) ? tag::TeamKey::Home : tag::TeamKey::Away);
					FAthleteFootball Fb;
					Fb.bPreSnap = Fb.bStance = true;
					Fb.GetOffKind = Unit == 0 ? 1 : 3;
					A->SetFootball(Fb, Dt);
					const tag::V2 At = tag::PlaySim::placeAlign(Unit == 0, i, K.PlaceLook, Los, K.FromX, F.y);
					Pose(Us[i], At.x, At.y, Unit == 0 ? 1 : -1, TEXT("idle"), 0.f);
				}
			}
		}
		else
		{
		// the line: nine across, a yard and a bit apart, the snapper in the middle (his spot is the capture's); the rush
		// across from them. Set until the snap, then the block.
		int32 Slot = 0;
		for (size_t i = 1; i < K.Kick.size(); ++i)
		{
			if (i == 8) continue;
			const int32 S0 = Slot++;
			if (i == 5) continue; // (the snapper: his slot, his own capture)
			AAthlete* A = AthleteFor(K.Kick[i], Game->home->hasPlayer(K.Kick[i]) ? tag::TeamKey::Home : tag::TeamKey::Away);
			FAthleteFootball Fb;
			Fb.bPreSnap = Fb.bStance = !bSnapped;
			Fb.GetOffKind = 1;
			A->SetFootball(Fb, Dt);
			Pose(K.Kick[i], Los - 0.75, F.y + (S0 - 4) * 1.15, 1, bSnapped ? TEXT("block") : TEXT("idle"), 0.f);
		}
		// a blocked kick: the edge man (the first of the rush) on Madden's field goal block, from the snap - the capture
		// turned so its run ends where the ball is kicked, its layout at the foot's contact, and he lines up where the
		// turned capture starts (off the edge, a yard over the ball)
		UAnimSequence* BlockClip = FAthleteAssets::Get().FgBlockRush.Get();
		AAthlete* Rusher = K.bBlocked && BlockClip && !K.Recv.empty() ? AthleteFor(K.Recv[0], Game->home->hasPlayer(K.Recv[0]) ? tag::TeamKey::Home : tag::TeamKey::Away) : nullptr;
		const float BlockFrom = FMath::Max(0.f, FAthleteAssets::FgBlockAt - (FgKickAt - FgSnapAt));
		FVector BlockAnchor = FVector::ZeroVector;
		float BlockYaw = 0.f;
		double BlockX = Los + 1.0, BlockY = F.y;
		if (Rusher)
		{
			const FVector Bw = ToWorld(K.FromX + 1.2, F.y, K.Dir);
			const FVector Tv0 = Rusher->SceneTravel(BlockClip, BlockFrom, FAthleteAssets::FgBlockAt, 0.f);
			const double Ly = Tv0.Size2D() / YD, Dx = (Los + 1.0) - (K.FromX + 1.2);
			BlockY = F.y - FMath::Sqrt(FMath::Max(0.0, Ly * Ly - Dx * Dx));
			const FVector Sw = ToWorld(BlockX, BlockY, K.Dir);
			BlockYaw = static_cast<float>((Bw - Sw).Rotation().Yaw - Tv0.Rotation().Yaw);
			BlockAnchor = Bw - Rusher->SceneTravel(BlockClip, BlockFrom, FAthleteAssets::FgBlockAt, BlockYaw);
		}
		for (size_t i = 0; i < K.Recv.size(); ++i)
		{
			AAthlete* A = AthleteFor(K.Recv[i], Game->home->hasPlayer(K.Recv[i]) ? tag::TeamKey::Home : tag::TeamKey::Away);
			FAthleteFootball Fb;
			Fb.bPreSnap = Fb.bStance = !bSnapped;
			Fb.GetOffKind = 3;
			A->SetFootball(Fb, Dt);
			if (A == Rusher)
			{
				if (bSnapped && !K.bBlockOn)
				{
					K.bBlockOn = true;
					A->BeginScene(BlockClip, BlockAnchor, BlockYaw, BlockFrom, false);
					UE_LOG(LogTemp, Display, TEXT("TAG kick block: #%d off the edge %.1f yd wide, laid out at the kick"), K.Recv[0]->number, FMath::Abs(BlockY - F.y));
				}
				if (!K.bBlockOn) Pose(K.Recv[i], BlockX, BlockY, -1, TEXT("idle"), 0.f);
				else if (A->SceneClipTime() >= BlockClip->GetPlayLength() - 0.05f) A->Drive(A->GetActorLocation(), A->GetActorRotation().Yaw, FVector::ZeroVector, 9.f, TEXT("idle"), 0.f, Dt, false);
				else A->Drive(BlockAnchor, BlockYaw, FVector::ZeroVector, 9.f, TEXT("scene"), 0.f, Dt, false);
				continue;
			}
			// (they come off the ball half a yard into the blockers)
			const double In = bSnapped ? FMath::Min(0.35, (K.T - K.SceneT0 - FgSnapAt) * 1.5) : 0.0;
			Pose(K.Recv[i], Los + 1.0 - In, F.y + (static_cast<double>(i) - 5) * 1.2, -1, bSnapped ? TEXT("block") : TEXT("idle"), 0.f);
		}
		} // (the staged lines)
		// the ball: on the turf under the snapper, back to the holder's hands, set on the spot, kicked
		if (K.T < Lead)
		{
			const float Tc = K.T - K.SceneT0;
			const FVector Turf = ToWorld(Los - 0.15, F.y, K.Dir, 0.12);
			const FVector Tee = SpotW + FVector(0, 0, 15);
			FVector Hands = SpotW + FVector(0, 0, 50);
			if (USkeletalMeshComponent* M = Holder ? Holder->Visible() : nullptr)
				if (M->GetBoneIndex(TEXT("hand_l")) != INDEX_NONE && M->GetBoneIndex(TEXT("hand_r")) != INDEX_NONE)
					Hands = 0.5 * (M->GetBoneLocation(TEXT("hand_l")) + M->GetBoneLocation(TEXT("hand_r")));
			FVector B = Turf;
			FRotator R(0, FwdYaw, 0);
			if (Tc >= FgSnapAt && Tc < FgCatchAt)
			{
				// the snap: a flat spiral, 7 yd in half a second
				const float Al = (Tc - FgSnapAt) / (FgCatchAt - FgSnapAt);
				B = FMath::Lerp(Turf + FVector(0, 0, 25), Hands, Al) + FVector(0, 0, 22.f * FMath::Sin(PI * Al));
				R = FRotator(0, FwdYaw, Tc * 1400.f);
			}
			else if (Tc >= FgCatchAt)
			{
				// down onto the spot, stood on its point, the laces away
				const float Al = FMath::SmoothStep(0.f, 1.f, (Tc - FgCatchAt) / (FgPlaceAt - FgCatchAt));
				B = FMath::Lerp(Hands, Tee, Al);
				R = FRotator(82.f * Al, FwdYaw, 0);
			}
			Ball->SetActorLocationAndRotation(B, R);
		}
	}
	else
	{
		const double Los = K.FromX + 7;
		for (size_t i = 0; i < K.Kick.size(); ++i)
		{
			double X = Los - 0.6, Y = W / 2 + (static_cast<double>(i) - 5) * 1.3;
			const TCHAR* Anim = TEXT("block");
			float Sp = 0;
			if (i == 0)
			{
				X = K.FromX - 1.5 + FMath::Min(1.f, K.T / 0.9f) * 1.5; Y = W / 2 + 0.5; Anim = K.T > 0.9f ? TEXT("idle") : TEXT("run"); Sp = K.T < 0.9f ? 4.f : 0.f;
				if (K.T > Lead - FAthleteAssets::FieldGoalContact && K.T < Lead + 0.6f) Anim = TEXT("fgkick");
			}
			if (i == 8) { X = K.FromX; Y = W / 2 - 0.4; Anim = TEXT("idle"); }
			Pose(K.Kick[i], X, Y, 1, Anim, Sp);
		}
		for (size_t i = 0; i < K.Recv.size(); ++i) Pose(K.Recv[i], Los + 0.8, W / 2 + (static_cast<double>(i) - 5) * 1.35, -1, TEXT("block"), 0.f);
	}
	UpdateCamera(Dt, ToWorld(BallX, F.y, K.Dir));
	if (K.T > Lead + F.hang && !K.bAnnounced)
	{
		K.bAnnounced = true;
		ReleaseHudScore(); // the points go up with the call
		const tag::PlayLog* L = Game->log.empty() ? nullptr : &Game->log.back();
		if (K.Kind == 2) Overlay->Banner(F.good.value_or(false) ? TEXT("IT'S GOOD") : TEXT("NO GOOD"), L ? S(L->desc) : FString(), F.good.value_or(false) ? Good : Bad);
		else if (L && L->big == tag::LogTag::Td) { Overlay->Banner(TEXT("TOUCHDOWN"), S(L->desc), Good); HomeCheer(1.f, Game->poss); } // (the scorers have the try)
		else if (L && L->big == tag::LogTag::Turnover) Overlay->Banner(TEXT("TURNOVER"), S(L->desc), Bad);
	}
	if (K.T >= K.Dur)
	{
		ReleaseHudScore();
		Kick.reset();
		State = EMatchState::Wait;
		Timer = 0;
	}
}

// ------------------------------------------------------------------ world sync
AAthlete* AMatchDirector::AthleteFor(tag::Player* P, tag::TeamKey Side)
{
	if (TObjectPtr<AAthlete>* Found = Athletes.Find(P->id)) return *Found;
	AAthlete* A = GetWorld()->SpawnActor<AAthlete>(AAthlete::StaticClass(), FTransform::Identity);
			A->TickAfter(this);
	const tag::Team& T = Game ? Game->teamOf(Side) : *League[0];
	FUniform U;
	if (Side == tag::TeamKey::Home)
	{
		U.Jersey = Hex(T.colors.primary);
		U.Pants = Hex(T.colors.secondary);
		U.Helmet = Hex(T.colors.primary);
		U.Trim = Hex(T.colors.secondary);
		U.Number = Hex(T.colors.accent);
	}
	else
	{
		U.Jersey = FLinearColor(0.72f, 0.72f, 0.72f); // white cloth reflects ~3/4 of the light: at 0.92 the road whites clipped to flat white in daylight (no folds, no shading)
		U.Pants = FLinearColor(0.68f, 0.68f, 0.68f);
		U.Helmet = Hex(T.colors.primary);
		U.Trim = Hex(T.colors.primary);
		U.Number = Hex(T.colors.primary);
	}
	U.TeamId = T.id;
	U.Logo = Hex(T.colors.secondary);
	A->Setup(P, U);
	Athletes.Add(P->id, A);
	return A;
}

void AMatchDirector::ShowOnly(const std::vector<tag::Player*>& Ps)
{
	TSet<int32> Ids;
	for (tag::Player* P : Ps)
	{
		Ids.Add(P->id);
		AthleteFor(P, Game && Game->home->hasPlayer(P) ? tag::TeamKey::Home : tag::TeamKey::Away);
	}
	for (auto& KV : Athletes)
	{
		// the men off the field sleep (no animation, no pose search) until they are next shown; a man coming on is woken
		// before anything is asked of him
		const bool bOn = Ids.Contains(KV.Key);
		if (bOn) KV.Value->SetDormant(false);
		KV.Value->ResetPose(); // stand tackled players back up for the next snap
		KV.Value->SetActorHiddenInGame(!bOn);
		if (!bOn) KV.Value->SetDormant(true);
	}
}

void AMatchDirector::PrespawnAthletes()
{
	// (the lineup and test-course modes show and hide their own few men themselves, without ShowOnly)
	static const bool bNoPrespawn = FParse::Param(FCommandLine::Get(), TEXT("TagNoPrespawn"));
	if (!Game || bLineup || bNoPrespawn) return;
	const double T0 = FPlatformTime::Seconds();
	int32 Made = 0;
	auto Make = [&](tag::Player* P, tag::TeamKey Side)
	{
		if (!P || Athletes.Contains(P->id)) return;
		AAthlete* A = AthleteFor(P, Side);
		if (!A) return;
		A->SetActorHiddenInGame(true);
		A->SetDormant(true);
		++Made;
	};
	// first the men who are sure to play: the kicking units, the returners, the depth chart's starters at every slot
	// (the situational subs and the specialists with them)
	for (const tag::TeamKey Key : {tag::TeamKey::Home, tag::TeamKey::Away})
	{
		tag::Team& T = Game->teamOf(Key);
		for (const char* Unit : {"K", "KR", "P", "PR", "D"})
			for (tag::Player* P : Pick11(T, Unit)) Make(P, Key);
		for (int32 s = 0; s < tag::DSLOTS; ++s)
			for (tag::Player* P : tag::chartStarters(T, static_cast<tag::DSlot>(s))) Make(P, Key);
	}
	const int32 First = Made;
	// then everyone else who is fit (the rotation's backups, the heavy and the spread sets' extra men), a man from each
	// side in turn, for as long as it has not taken 2.5 s in all: whoever is left is made when he first comes on
	bool bAll = true;
	const size_t Most = FMath::Max(Game->teamOf(tag::TeamKey::Home).roster.size(), Game->teamOf(tag::TeamKey::Away).roster.size());
	for (size_t i = 0; i < Most && bAll; ++i)
		for (const tag::TeamKey Key : {tag::TeamKey::Home, tag::TeamKey::Away})
		{
			tag::Team& T = Game->teamOf(Key);
			if (i >= T.roster.size() || T.roster[i]->injuredWeeks > 0) continue;
			if (FPlatformTime::Seconds() - T0 > 2.5) { bAll = false; break; }
			Make(T.roster[i].get(), Key);
		}
	UE_LOG(LogTemp, Display, TEXT("TAG prespawn: %d men made under the loading screen in %.2f s (%d in the units and the starters, the rest of the rosters %s)"),
		Made, FPlatformTime::Seconds() - T0, First, bAll ? TEXT("too") : TEXT("cut short at 2.5 s"));
}

UAnimSequence* AMatchDirector::WarmClip(const FString& Path)
{
	if (const TObjectPtr<UAnimSequence>* Have = WarmClips.Find(Path)) return Have->Get();
	UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Clip) WarmClips.Add(Path, Clip);
	return Clip;
}

void AMatchDirector::WarmMatchAssets()
{
	for (const TCHAR* N : PreFlagClips) WarmClip(FString::Printf(TEXT("/Game/TAG/AnimH/Pack/PrePlay/%s.%s"), N, N));
	WarmPresnapAssets(); // (the hard counts, the play art: Game/MatchPresnap.cpp)
}

void AMatchDirector::CapturePrev()
{
	if (!Sim) return;
	for (tag::Ent* E : Sim->ents) Prev[E] = FVector2D(E->pos.x, E->pos.y);
}

void AMatchDirector::ReadTempoInput(float Dt)
{
	// (not after a score, a takeaway or a safety: the ball goes to a kickoff or the other team, and a hurry-up or spike
	// asked for then was carried over to the next possession's first snap)
	if (Sim && Sim->result && (Sim->result->td || Sim->result->turnover || Sim->result->safety))
	{
		HurryHoldT = SpikeHoldT = 0.f;
		return;
	}
	// -TagTempoTest: a filmed check of the hurry-up and the spike in a CPU game - after every odd play the hurry-up is
	// asked for, every fourth the spike, as a held button would ask (the same path from here)
	if (TempoTest() && Game && Game->phase == tag::GamePhase::Scrimmage && State == EMatchState::Post && Sim && !Sim->isReturn() && !bHurryNext && !bSpikeNext)
	{
		static unsigned long long Seen = 0;
		static int32 N = 0;
		if (Seen != Sim->serial)
		{
			Seen = Sim->serial;
			if (++N % 4 == 0) bSpikeNext = true;
			else if (N % 2 == 1) bHurryNext = true;
			if (bHurryNext || bSpikeNext)
			{
				if (Overlay) Overlay->Toast(bSpikeNext ? TEXT("SPIKE THE BALL") : TEXT("HURRY UP"));
				UE_LOG(LogTemp, Display, TEXT("TAG tempo test asks for the %s"), bSpikeNext ? TEXT("spike") : TEXT("hurry-up"));
			}
		}
		return;
	}
	// only for a user's offense on a scrimmage down, between the whistle and the call (not a kick, not a try, not a game
	// several people play: their caller's screen decides)
	if (!Game || !UserOff() || bMulti || bTwoPoint || Game->phase != tag::GamePhase::Scrimmage || bWatch || bAuto)
	{
		HurryHoldT = SpikeHoldT = 0.f;
		return;
	}
	if (Sim && Sim->isReturn()) return;
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return;
	// a hold, not a tap (CROSS also skips the post-play shot): 0.35 s
	auto Held = [&](float& T, std::initializer_list<FKey> Keys)
	{
		bool bDown = false;
		for (const FKey& K : Keys) bDown = bDown || PC->IsInputKeyDown(K);
		const float Was = T;
		T = bDown ? T + Dt : 0.f;
		return Was < 0.35f && T >= 0.35f;
	};
	if (Held(HurryHoldT, {EKeys::Gamepad_FaceButton_Bottom, EKeys::N}) && !bHurryNext)
	{
		bHurryNext = true;
		bSpikeNext = false;
		if (Overlay) Overlay->Toast(TEXT("HURRY UP"));
		UE_LOG(LogTemp, Display, TEXT("TAG user asks for the hurry-up"));
	}
	if (Held(SpikeHoldT, {EKeys::Gamepad_FaceButton_Top, EKeys::B}) && !bSpikeNext)
	{
		bSpikeNext = true;
		bHurryNext = false;
		if (Overlay) Overlay->Toast(TEXT("SPIKE THE BALL"));
		UE_LOG(LogTemp, Display, TEXT("TAG user asks for the spike"));
	}
	// the choices, said while they can be made (the clock running, or always once the half is in its last two minutes)
	if (Overlay && State == EMatchState::Post && !bHurryNext && !bSpikeNext)
		Overlay->SetHint(bPadActive ? PadHint(TEXT("Hold CROSS: hurry up     Hold TRIANGLE: spike")) : FString(TEXT("Hold N: hurry up     Hold B: spike")));
}

void AMatchDirector::BeginBreak(const FString& Title, const FString& Sub, float Secs)
{
	BreakLeft = Secs;
	BreakT = 0.f;
	bCutNext = true; // (a cut to the wide shot)
	if (Overlay)
	{
		Overlay->SetHint(TEXT(""));
		Overlay->Banner(Title, Sub, FLinearColor::White);
	}
	UE_LOG(LogTemp, Display, TEXT("TAG broadcast break: %s (%s) %.1f s"), *Title, *Sub, Secs);
}

bool AMatchDirector::BreakCamera(FVector& T, FVector& Look) const
{
	// (after a quarter it holds through the play call until the teams are set at the new end: the game camera had come back
	// to an empty stretch of field with the men still at the old one)
	if (BreakLeft <= 0.f && !bSetAfterBreak) return false;
	// high in the far corner of the bowl, craning slowly across the field toward the near end zone (the game camera's
	// side stays out of it): the stadium, the crowd and the field in one frame
	const double Y = YD;
	const double A = FMath::DegreesToRadians(30.0 + 14.0 * BreakT);
	T = FVector((50.0 + 62.0 * FMath::Cos(A)) * Y, (48.0 * FMath::Sin(A)) * Y, (24.0 + 1.5 * BreakT) * Y);
	Look = FVector(50.0 * Y, 0.0, 2.0 * Y);
	return true;
}

void AMatchDirector::FireHeldTackle(const FHeldTackle& H)
{
	AAthlete* Tk = H.Tk.Get();
	AAthlete* Cr = H.Cr.Get();
	if (!Tk || !Cr || Cr->IsFalling()) return;
	UE_LOG(LogTemp, Display, TEXT("TAG catch, then the hit: #%d on #%d"), Tk->Player ? Tk->Player->number : -1, Cr->Player ? Cr->Player->number : -1);
	if (H.bWrap)
	{
		StartPairedTackle(Tk, Cr, H.Spot, -1, H.bDive, H.PileSecs);
		return;
	}
	GangAssist = nullptr;
	AAthlete* As = H.As.Get();
	if (As && !H.bDive && !H.bBig && !H.Strip && StartGangTackle(Tk, Cr, As, H.Spot)) GangAssist = As;
	else
	{
		StartPairedTackle(Tk, Cr, H.Spot, -1, H.bDive, 0.f, H.bBig, H.Strip);
		if (As && As != Tk) StartPileOn(As, Cr);
	}
	if (!Cr->IsFalling()) Cr->FallAlone(H.Spot);
}

void AMatchDirector::SyncPlay(float Dt, float Alpha)
{
	if (!Sim) return;
	const tag::TeamKey OffK = Game->poss;
	// a punt with a live return: the snap and the punt are the pack's capture (as on the staged punts), placed so its
	// foot meets the ball where and when the sim kicks it
	static const bool bNoKickScene = FParse::Param(FCommandLine::Get(), TEXT("TagNoFgScene"));
	const FAthleteAssets::FKickScene& PuntSc = FAthleteAssets::Get().PuntScene;
	const bool bLivePunt = !bNoKickScene && Sim->isReturn() && Sim->cfg.kick->punt && PuntSc.Snap.IsValid() && PuntSc.Kick.IsValid();
	FVector PuntPt = FVector::ZeroVector, PuntFwd = FVector::ForwardVector;
	float PuntWalk = 0.f, PuntT0 = 0.f;
	if (bLivePunt)
	{
		if (PuntSceneSim != Sim->serial)
		{
			PuntSceneSim = Sim->serial;
			PuntStage[0] = PuntStage[1] = 0;
			PuntSnapEnt = nullptr;
			double Best = 1e9;
			for (const tag::Ent* E : Sim->def)
				if (E != Sim->kickerEnt && FMath::Abs(E->pos.x - (Sim->cfg.kick->coverX + 0.6)) < 0.5 && FMath::Abs(E->pos.y - tag::FIELD_W / 2) < Best)
				{
					Best = FMath::Abs(E->pos.y - tag::FIELD_W / 2);
					PuntSnapEnt = E;
				}
		}
		PuntPt = ToWorld(Sim->cfg.kick->kickX, tag::FIELD_W / 2, Dir);
		PuntFwd = (ToWorld(Sim->cfg.kick->kickX - 1.0, tag::FIELD_W / 2, Dir) - PuntPt).GetSafeNormal();
		PuntWalk = static_cast<float>(PuntSc.Kick->ExtractRootMotionFromRange(0.0, PuntKickAt, FAnimExtractContext(0.0, true)).GetTranslation().Size2D());
		PuntT0 = static_cast<float>(Sim->cfg.kick->kickT) - PuntKickAt;
	}
	AAthlete* PuntMan = nullptr;
	FVector PuntSnapW = FVector::ZeroVector;
	for (tag::Ent* E : Sim->ents)
	{
		const FVector2D* PV = nullptr;
		auto It = Prev.find(E);
		if (It != Prev.end()) PV = &It->second;
		const double X = PV ? FMath::Lerp(PV->X, E->pos.x, static_cast<double>(Alpha)) : E->pos.x;
		const double Y = PV ? FMath::Lerp(PV->Y, E->pos.y, static_cast<double>(Alpha)) : E->pos.y;
		AAthlete* A = AthleteFor(E->p, E->side == tag::Side::Off ? OffK : tag::other(OffK));
		if (!Sim->isLive() && HelpBusy(A)) { PostPos.erase(E); continue; } // (a help-up: TickHelpUp has the two of them)
		FVector VelW = Dir > 0 ? FVector(E->vel.x, -E->vel.y, 0) * YD : FVector(-E->vel.x, E->vel.y, 0) * YD;
		FString AnimName(E->animName());
		// a ball carrier's highlight-stick move: the clip of exactly that move, to his side (sim +1 = his left)
		if (E->anim == tag::Anim::Juke && E->move != tag::Move::None && E->move != tag::Move::Hit && E->move != tag::Move::Cut)
			AnimName = FString::Printf(TEXT("mv_%s_%s"), UTF8_TO_TCHAR(tag::moveName(E->move)), E->moveSide > 0 ? TEXT("l") : TEXT("r"));
		float AnimT = static_cast<float>(E->animT);
		FVector Loc = ToWorld(X, Y, Dir);
		float Yaw = FaceToYaw(E->face, Dir);
		if (auto PJ = PostPos.find(E); PJ != PostPos.end())
		{
			// jogging toward the ball after the whistle (TickPostJog): the director's position and velocity
			// (the jog's own velocity: the gap from the actor over one frame told a man held a few cm off his spot to run
			// at a steady pace he never moved at, and a dozen such men stood on the one matched idle - p66-p71)
			auto PVel = PostVel.find(E);
			VelW = PVel != PostVel.end() ? FVector(PVel->second.X, PVel->second.Y, 0.f) : FVector::ZeroVector;
			if (VelW.Size() > 60.f) { AnimName = TEXT("run"); Yaw = VelW.Rotation().Yaw; }
			else
			{
				AnimName = TEXT("idle");
				VelW = FVector::ZeroVector;
				if (auto PY = PostYaw.find(E); PY != PostYaw.end()) Yaw = PY->second;
			}
			Loc = PJ->second;
		}
		else if (!Sim->isLive() && Sim->result && !Sim->isReturn() && (PostStay.count(E) || (!A->IsDown() && FVector::Dist2D(A->GetActorLocation(), Loc) > 150.f)))
		{
			// (and stays left: let go the next frame, inside 1.5 m, he was put on the sim spot at once - 89 cm, p65 dv c012)
			// (through a get-up too: a diving catch ended on the turf 2.6 m from the sim's spot, the get-up counts as down,
			// and the man was driven onto the spot the frame he started up off the turf - to11 c148)
			PostStay.insert(E);
			// after the whistle nobody is put back on the sim's spot: a man a capture left somewhere else (a blocker knocked
			// down while the sim ran his man on downfield) goes on from where he is. (He was stood up seven metres away.)
			A->AbsorbVisOffset(); // (first: it takes any travel a capture just handed over)
			Loc = A->GetActorLocation();
			Loc.Z = 0.f;
			VelW = FVector::ZeroVector;
		}
		else if (!Sim->isLive() && Sim->result && !Sim->isReturn() && (!PV || (PV->X == E->pos.x && PV->Y == E->pos.y)))
		{
			// the sim stopped at the whistle but keeps each man's last velocity: a man it no longer moves is standing (on
			// "run" at his last speed he never counted as still, and a dozen men stood on the one matched idle - p68)
			VelW = FVector::ZeroVector;
			if (AnimName == TEXT("run")) AnimName = TEXT("idle");
		}
		// a field goal's snapper, holder and kicker: the pack's operation, timed to the sim's snap and foot (MatchKicking.cpp)
		if (Sim->isPlaceKick() && SyncPlaceOperation(E, A, Dt)) continue;
		if (bLivePunt && (E == Sim->kickerEnt || E == PuntSnapEnt))
		{
			const bool bP = E == Sim->kickerEnt;
			UAnimSequence* Clip = bP ? PuntSc.Kick.Get() : PuntSc.Snap.Get();
			UAnimSequence* Wait = bP ? PuntSc.KickWait.Get() : PuntSc.SnapWait.Get();
			const float Tc = static_cast<float>(Sim->t) - PuntT0;
			// (the punter plays his capture out; the snapper is off with the coverage when the sim lets it go)
			const float EndT = bP ? Clip->GetPlayLength() - 0.05f : PuntKickAt + 0.5f;
			const FVector At = bP ? PuntPt - PuntFwd * (PuntWalk + 90.f) : Loc;
			const float FwdYaw = PuntFwd.Rotation().Yaw;
			int32& Stage = PuntStage[bP ? 0 : 1];
			if (!bP) PuntSnapW = At;
			if (Tc < EndT && (Wait || Tc >= 0.f))
			{
				if (Tc < 0.f && Stage == 0) { A->BeginScene(Wait, At, FwdYaw, 0.f, true); Stage = 1; }
				if (Tc >= 0.f && Stage < 2) { A->BeginScene(Clip, At, FwdYaw, Tc, false); Stage = 2; }
				A->Drive(At, FwdYaw, FVector::ZeroVector, static_cast<float>(E->vmax), TEXT("scene"), 0.f, Dt, false);
				A->SetSelected(false); // (the user's man is marked by the overlay's ring and chevron: UpdateLineBars)
				A->SetLabel(FString());
				if (bP) PuntMan = A;
				continue;
			}
		}
		else if (Sim->isReturn() && E == Sim->kickerEnt && Sim->t < Sim->cfg.kick->kickT + 0.6)
		{
			// the kick: start the clip so its foot contact lands when the sim kicks the ball
			const bool bPunt = Sim->cfg.kick->punt;
			AnimName = bPunt ? TEXT("punt") : TEXT("kickoff");
			AnimT = FMath::Max(0.f, (bPunt ? FAthleteAssets::PuntContact : FAthleteAssets::KickoffContact) - static_cast<float>(Sim->cfg.kick->kickT));
		}
		// where the sim steers him (while it is the sim that moves him: not on the post-whistle jog)
		if (Sim->isLive() && PostPos.find(E) == PostPos.end() && !E->engaged && E->stun <= 0)
			A->SetIntent(Dir > 0 ? FVector(E->want.x, -E->want.y, 0) * YD : FVector(-E->want.x, E->want.y, 0) * YD);
		if (Sim->isLive() && PostPos.find(E) == PostPos.end() && !E->engaged && E->stun <= 0) A->SetSimFuture(*Sim, *E, Dir, Alpha); // (and his next 1.6 s as the sim moves him: motion matching's query)
		A->Drive(Loc, Yaw, VelW, static_cast<float>(E->vmax), AnimName, AnimT, Dt, E->isOL || E->isDL);
		A->SetSelected(false); // (the user's man is marked by the overlay ring and chevron: UpdateLineBars)
		A->SetLabel(FString()); // receivers' buttons are drawn by the overlay (UpdateIcons)
	}
	// ball
	const tag::Ball& B = Sim->ball;
	auto AthOf = [&](const tag::Ent* X) { return AthleteFor(X->p, X->side == tag::Side::Off ? OffK : tag::other(OffK)); };
	// paired blocks: who each lineman is locked up with (the sim's engagement), for alignment
	{
		double SumD = 0.0;
		int32 NPairs = 0;
		for (tag::Ent* E : Sim->ents)
		{
			AAthlete* Me = AthOf(E);
			AAthlete* Him = E->engaged && E->anim == tag::Anim::Block ? AthOf(E->engaged) : nullptr;
			Me->SetBlockPartner(Him);
			// who is winning the rep (the engaged loops follow it); pass or run blocking from the blocker's state
			const tag::Ent* Blk = E->side == tag::Side::Off ? E : E->engaged;
			Me->SetLinePush(Him ? static_cast<float>(E->linePush) : 0.f, E->side == tag::Side::Def,
				!Blk || Blk->state != tag::EntState::RunBlock);
			if (Him) { SumD += FVector::Dist2D(Me->GetActorLocation(), Him->GetActorLocation()); ++NPairs; }
		}
		static double LastBlockLog = 0.0;
		const double NowT = GetWorld()->GetTimeSeconds();
		// double teams: two pass blockers on one rusher - the three on a three-man capture of it, again when it runs out
		{
			static TMap<const tag::Ent*, double> DblUntil;
			static unsigned long long DblSim = 0;
			static int32 GDblSeq = 0;
			if (DblSim != Sim->serial) { DblSim = Sim->serial; DblUntil.Reset(); }
			for (const tag::Ent* E : Sim->def)
			{
				if (E->engagedBy.size() >= 2 && !DblUntil.Contains(E))
					UE_LOG(LogTemp, Display, TEXT("TAG two on one: #%d (%s) by %s / %s, ball %d"), E->p->number, UTF8_TO_TCHAR(E->role.c_str()),
						UTF8_TO_TCHAR(tag::entStateName(E->engagedBy[0]->state)), UTF8_TO_TCHAR(tag::entStateName(E->engagedBy[1]->state)), static_cast<int32>(Sim->ball.mode));
				const bool bDbl = E->engagedBy.size() == 2 && E->engagedBy[0]->state == tag::EntState::PassBlock && E->engagedBy[1]->state == tag::EntState::PassBlock &&
					Sim->ball.mode != tag::BallMode::Snap;
				if (!bDbl) { DblUntil.Remove(E); continue; }
				const double* Until = DblUntil.Find(E);
				if (Until && NowT < *Until) continue;
				AAthlete* Ru = AthOf(E);
				AAthlete* Ba = AthOf(E->engagedBy[0]);
				AAthlete* Bb = AthOf(E->engagedBy[1]);
				if (!Ru || !Ba || !Bb || Ru->IsFalling() || Ba->IsFalling() || Bb->IsFalling()) continue;
				const float Len = AAthlete::PlayRepDouble(Ru, Ba, Bb, GDblSeq++);
				DblUntil.Add(E, NowT + FMath::Max(0.5f, Len - 0.1f));
			}
		}
		if (NPairs && NowT - LastBlockLog > 3.0)
		{
			LastBlockLog = NowT;
			UE_LOG(LogTemp, Verbose, TEXT("TAG blocks: %d engaged, mean chest to chest %.0f cm"), NPairs, SumD / NPairs);
		}
	}
	// paired tackles: a defender whose tackle just started squares up to the carrier he is hitting (the man down or
	// still holding the ball within 3 yd)
	for (tag::Ent* E : Sim->ents)
	{
		if (E->anim != tag::Anim::Tackle || E->animT > 0.06 || AthOf(E)->IsFalling()) continue;
		const tag::Ent* Best = nullptr;
		double BestD = 3.0;
		for (const tag::Ent* C : Sim->ents)
			if (C->side != E->side && (C->anim == tag::Anim::Down || C->hasBall) && tag::dist(C->pos, E->pos) < BestD)
			{
				Best = C;
				BestD = tag::dist(C->pos, E->pos);
			}
		if (Best)
		{
			AAthlete* Tk = AthOf(E);
			Tk->BeginTackleOn(AthOf(Best));
			if (Best->anim == tag::Anim::Down && !AthOf(Best)->IsFalling())
			{
				const FVector TvW = Dir > 0 ? FVector(E->vel.x, -E->vel.y, 0) * YD : FVector(-E->vel.x, E->vel.y, 0) * YD;
				AthOf(Best)->ApplyHit(TvW, static_cast<float>(E->p->weightLb) * 0.4536f, Tk->GetActorLocation());
			}
		}
	}
	// keyed one-shots on sim events (new events only; a new play restarts the list)
	if (Sim.get() != EventSim || Sim->events.size() < SeenEvents)
	{
		EventSim = Sim.get();
		SeenEvents = 0;
		HeldTackles.Reset();
		LastCatcher = nullptr;
		LastCatchT = -10.0;
	}
	// a tackle held for the catch: the hit once the catch has the ball in (see FHeldTackle)
	for (int32 i = HeldTackles.Num() - 1; i >= 0; --i)
		if ((HeldTackles[i].Left -= Dt) <= 0.f)
		{
			const FHeldTackle H = HeldTackles[i];
			HeldTackles.RemoveAt(i);
			FireHeldTackle(H);
		}
	// (a catch takes ~0.35 s from the ball in his hands to it tucked away, in the pack's catch captures)
	constexpr double CatchSecure = 0.35;
	// -TagCatchCheck: every contact with the ball measured (see the ball's drawing below)
	static const bool bCatchCheckOn = FParse::Param(FCommandLine::Get(), TEXT("TagCatchCheck"));
	for (; SeenEvents < Sim->events.size(); ++SeenEvents)
	{
		const tag::PlayEvent& Ev = Sim->events[SeenEvents];
		if (bCatchCheckOn && Ev.a && (Ev.type == "catch" || Ev.type == "divecatch" || Ev.type == "int" || Ev.type == "pbu" || Ev.type == "drop" ||
			Ev.type == "contested_inc" || Ev.type == "batted" || Ev.type == "recover" || Ev.type == "oobcatch"))
			CatchCheckNew.Emplace(Ev.a, FString(UTF8_TO_TCHAR(Ev.type.c_str())));
		// (telemetry runs: the ball's moment against the capture's - a throw's release, a catch's or a play on the ball's
		// hands on it - as the shown man has it: + the capture's moment still to come, - already past; "none" when no
		// capture with a known moment is playing on him then. Tools/Realism/arm_audit.py reads these.)
		if (bHlFull && Ev.a && (Ev.type == "throw" || Ev.type == "catch" || Ev.type == "divecatch" || Ev.type == "int" || Ev.type == "pbu" || Ev.type == "drop" ||
			Ev.type == "contested_inc" || Ev.type == "batted" || Ev.type == "oobcatch" || Ev.type == "toss" || Ev.type == "pump"))
			if (const AAthlete* Ka = AthOf(Ev.a))
			{
				const float In = Ka->OneShotKeyIn();
				UE_LOG(LogTemp, Display, TEXT("TAG keytime %s #%d clip %d frame %d: %s %.3f s (%s, at %.2f, key %.2f)"), UTF8_TO_TCHAR(Ev.type.c_str()), Ev.a->p ? Ev.a->p->number : -1,
					HlClip, HlFrame, FMath::IsNaN(In) ? TEXT("none") : TEXT("in"), FMath::IsNaN(In) ? 0.f : In, *Ka->TelemetryAnim(), Ka->OneShotClipT(), Ka->OneShotKeyT);
			}
		if ((Ev.type == "catch" || Ev.type == "int") && Ev.a)
		{
			LastCatcher = Ev.a;
			LastCatchT = Ev.t;
		}
		const double SinceCatch = Ev.b && Ev.b == LastCatcher ? Ev.t - LastCatchT : 99.0;
		if (Ev.type == "handoff" && Ev.a)
		{
			AAthlete* Q = AthOf(Ev.a);
			// a toss on stretch runs (the back is out wide), a handoff otherwise
			const bool bPitch = Ev.b && tag::dist(Ev.a->pos, Ev.b->pos) > 2.5 && FAthleteAssets::Get().Pitch;
			// toss / hand to the side the back is on (the keyed clips go to the QB's right; mirrored to his left)
			bool bLeft = false;
			if (Q && Ev.b)
				if (AAthlete* Rb = AthOf(Ev.b)) bLeft = FVector::DotProduct(Rb->GetActorLocation() - Q->GetActorLocation(), Q->GetActorRightVector()) < 0.f;
			const FAthleteAssets& AA = FAthleteAssets::Get();
			// a second exchange behind the line (a reverse: the back hands it on to the receiver coming back): its own keyed
			// handoff and hand-to-hand transfer - the first exchange's serials would otherwise leave the ball to jump across
			if (Ev.note == "reverse" && Q && Ev.b)
			{
				Q->PlayOneShot(AA.Side(AA.Handoff.Get(), bLeft));
				HandXferSerial = Sim->serial;
				HandXferT = Sim->t;
				HandXferFrom = Q;
				UE_LOG(LogTemp, Display, TEXT("TAG reverse handoff: %.2f s, #%d to #%d"), Sim->t, Ev.a->p ? Ev.a->p->number : -1, Ev.b->p ? Ev.b->p->number : -1);
				continue;
			}
			// (the two-man handoff is already playing, started before the exchange: no keyed clip over it)
			// (nor after a toss: that was thrown at its release, the "toss" event below, and this is the back catching it)
			if (Q && !(HandoffSerial == Sim->serial && !bPitch) && TossSerial != Sim->serial) Q->PlayOneShot(AA.Side(bPitch ? AA.Pitch.Get() : AA.Handoff.Get(), bLeft));
			// the ball goes from his hand into the back's arms over the next few frames (drawn below)
			// (unless the capture's hands already began it: see the ball's drawing)
			// (nor yet when the capture's contact frame is still to come: the ball waits in his hand for the back's hands)
			AAthlete* EvRb = Ev.b ? AthOf(Ev.b) : nullptr;
			if (Q && EvRb && TossSerial != Sim->serial && HandXferSerial != Sim->serial && HandoffSerial == Sim->serial && EvRb->MeshContactIn() > 0.03f)
			{
				HandDeferSerial = Sim->serial;
				HandDeferT = Sim->t;
			}
			else if (Q && Ev.b && TossSerial != Sim->serial && HandXferSerial != Sim->serial)
			{
				HandXferSerial = Sim->serial;
				HandXferT = Sim->t;
				HandXferFrom = Q;
			}
			if (Q && Ev.b)
				if (AAthlete* Rb = AthOf(Ev.b))
					UE_LOG(LogTemp, Display, TEXT("TAG handoff: %.2f s after the snap, #%d to #%d %s, bodies %.0f cm apart, ball %.0f cm from the back's hands, back %.1f m/s, quarterback %.1f m/s%s"),
						Sim->t, Ev.a->p ? Ev.a->p->number : -1, Ev.b->p ? Ev.b->p->number : -1, TossSerial == Sim->serial ? TEXT("(toss)") : TEXT("(handoff)"),
						FVector::Dist2D(Q->GetActorLocation(), Rb->GetActorLocation()), FVector::Dist(Ball->GetActorLocation(), Rb->HandsMid()),
						tag::len(Ev.b->vel) * 0.9144, tag::len(Ev.a->vel) * 0.9144, HandoffSerial == Sim->serial ? TEXT(", two-man capture") : TEXT(""));
		}
		else if (Ev.type == "toss" && Ev.a)
		{
			// the toss leaves his hands: the ball is drawn through the air to the back from here (the sim has it change
			// hands when it gets there), and his toss is thrown now if it was not started ahead of the release
			if (AAthlete* Q = AthOf(Ev.a))
			{
				TossSerial = Sim->serial;
				TossHandW = Q->HandsMid();
				bool bLeft = false;
				if (Ev.b)
					if (AAthlete* Rb = AthOf(Ev.b)) bLeft = FVector::DotProduct(Rb->GetActorLocation() - Q->GetActorLocation(), Q->GetActorRightVector()) < 0.f;
				const FAthleteAssets& AA = FAthleteAssets::Get();
				if (TossClipSerial != Sim->serial && AA.Pitch) Q->PlayOneShot(AA.Side(AA.Pitch.Get(), bLeft));
				TossClipSerial = Sim->serial;
				// the back takes it with the pack's pitch catch for where it comes from, its hands on the ball as it gets there
				// (the sim's toss flies 0.28-0.3 s: PlaySim mesh tossDur). He ran on with nothing in his arms before.
				if (Ev.b)
					if (AAthlete* Rb = AthOf(Ev.b))
					{
						const FVector Fly = (Rb->GetActorLocation() - Q->GetActorLocation()).GetSafeNormal2D() * 900.f;
						Rb->PlayPitchCatch(Fly, 0.29f);
					}
			}
		}
		else if (Ev.type == "throw" && Ev.a && Ev.b)
		{
			// (where the ball leaves his hand: the ball is drawn from there into its flight, not from his body's centre)
			if (AAthlete* Q = AthOf(Ev.a))
			{
				FRotator Rr;
				FVector Hand;
				if (Q->GetBallHold(Hand, Rr)) { ReleaseHandW = Hand; ReleaseSerial = Sim->serial; }
			}
			// the throw that fits the pass (already under way when the windup was timed to this release)
			if (AAthlete* Q = ThrowTimedSerial == Sim->serial ? nullptr : AthOf(Ev.a))
			{
				const double D = tag::dist(Ev.a->pos, Ev.b->pos);
				const double Sp = tag::len(Ev.a->vel);
				double Rush = 99.0;
				for (const tag::Ent* O : Sim->ents)
					if (O->side != Ev.a->side) Rush = FMath::Min(Rush, tag::dist(O->pos, Ev.a->pos));
				int32 K = FAthleteAssets::ThrowSet;
				static int32 GPressSeq = 0;
				if (Sp > 2.5)
				{
					const FVector VW = Dir > 0 ? FVector(Ev.a->vel.x, -Ev.a->vel.y, 0) : FVector(-Ev.a->vel.x, Ev.a->vel.y, 0);
					K = FVector::DotProduct(VW, Q->GetActorRightVector()) > 0.f ? FAthleteAssets::ThrowRunRK : FAthleteAssets::ThrowRunLK;
				}
				else if (Rush < 1.6) K = (GPressSeq++ % 2) ? FAthleteAssets::ThrowSidearmK : FAthleteAssets::ThrowFadeK;
				else if (Sim->ball.lob) K = FAthleteAssets::ThrowTouchK;
				else if (D > 28.0) K = FAthleteAssets::ThrowDeepK;
				else if (D < 10.0) K = FAthleteAssets::ThrowQuickK;
				Q->PlayThrow(K);
				UE_LOG(LogTemp, Verbose, TEXT("TAG throw kind %d (%.0f yd, qb %.1f yd/s, rush %.1f yd%s)"), K, D, Sp, Rush, Sim->ball.lob ? TEXT(", lob") : TEXT(""));
			}
		}
		else if (Ev.type == "shed" && Ev.a && Ev.b)
		{
			// the rusher beat his blocker: his move (picked by the sim from his build and leverage) to the side he
			// went, and the beaten blocker's lunge after him
			AAthlete* Ru = AthOf(Ev.a);
			AAthlete* Bl = AthOf(Ev.b);
			if (Ru && Bl)
			{
				// a pass rush win: the pack's two-man capture of one, each man his own part (the bull rush that runs the
				// blocker over for a bull's win); a run block shed keeps the single moves
				const bool bPassRep = Ev.b->state != tag::EntState::RunBlock; // (as SetLinePush reads it: the shed has already taken him out of his block)
				static int32 GRepSeq = 0;
				if (!(bPassRep && AAthlete::PlayRepWin(Ru, Bl, GRepSeq++, Ev.note == "bull")))
				{
					Ru->PlayRushMove(UTF8_TO_TCHAR(Ev.note.c_str()), Bl);
					Bl->PlayBeaten(Ru);
				}
			}
		}
		else if (Ev.type == "pump" && Ev.a)
		{
			if (AAthlete* Q = AthOf(Ev.a)) Q->PlayPumpFake();
		}
		else if ((Ev.type == "int" || Ev.type == "pbu") && Ev.a)
		{
			// the defender's play on the ball: an interception's catch; a breakup's swat if he was not already in one
			if (AAthlete* Df = AthOf(Ev.a); Df && !Df->IsFalling())
			{
				if (Ev.type == "int") { if (!(bIntPaired && FatePrimedSerial == Sim->serial) && !(IntPrimedSerial == Sim->serial && SwatBy == Ev.a)) Df->PlayBallDefense(0, true); } // (the two-man capture already has it, or his own started before the ball got there)
				else if (!(SwatSerial == Sim->serial && SwatBy == Ev.a))
				{
					const double Sp = tag::len(Ev.a->vel);
					Df->PlayBallDefense(Sp < 1.5 ? 2 : 0, false);
				}
			}
		}
		else if (Ev.type == "rushfail" && Ev.a && Ev.b)
		{
			// a move stoned: the pack's two-man capture of a rusher who does not get by (a bull rush anchored against)
			AAthlete* Ru = AthOf(Ev.a);
			AAthlete* Bl = AthOf(Ev.b);
			static int32 GHoldSeq = 0;
			// (a double-teamed rusher is on the three-man capture: a two-man one would leave the second blocker out)
			if (Ru && Bl && !Ru->IsFalling() && !Bl->IsFalling() && Ev.a->engagedBy.size() < 2)
				AAthlete::PlayRepHold(Ru, Bl, GHoldSeq++, Ev.note == "bull" || Ev.note == "longarm");
		}
		else if (Ev.type == "wrap" && Ev.a && Ev.b)
		{
			// a pile on the move: the paired tackle starts at the hit, not where the drive ends. The pair's capture (one
			// whose carrier carries on for yards, picked by its travel) plays from the contact while the sim drives the
			// pile; before, the carrier jogged on in his locomotion with the tackler's solo capture falling beside him,
			// and both popped back up into the paired fall at the end.
			AAthlete* Tk = AthOf(Ev.a);
			AAthlete* Cr = AthOf(Ev.b);
			tag::V2 End;
			double Secs = 0.0;
			if (Tk && Cr && Tk != Cr && Sim->wrapEnd(End, Secs))
			{
				if (SinceCatch < CatchSecure)
				{
					FHeldTackle H;
					H.Tk = Tk;
					H.Cr = Cr;
					H.Spot = ToWorld(End.x, End.y, Dir);
					H.bDive = Ev.note == "dive";
					H.bWrap = true;
					H.PileSecs = FMath::Max(0.05f, static_cast<float>(Secs - (CatchSecure - SinceCatch)));
					H.Left = static_cast<float>(CatchSecure - SinceCatch);
					HeldTackles.Add(H);
				}
				else
					StartPairedTackle(Tk, Cr, ToWorld(End.x, End.y, Dir), -1, Ev.note == "dive", FMath::Max(0.05f, static_cast<float>(Secs)));
			}
		}
		else if (Ev.type == "tackle" && Ev.a && Ev.b)
		{
			// the sim's tackle: both men to the ground together, the carrier's body from the hit to the spot
			AAthlete* Tk = AthOf(Ev.a);
			AAthlete* Cr = AthOf(Ev.b);
			const FVector Spot = ToWorld(Ev.b->pos.x, Ev.b->pos.y, Dir);
			if (Tk && Cr && Tk != Cr && Cr->IsFalling() && PileCarrier.Get() == Cr)
			{
				// the pile started at the wrap: it ends where the sim stopped it (help arrived, the sideline)
				const FVector Delta = (Spot - PileSpot).GetClampedToMaxSize2D(150.f);
				Cr->ShiftFallTarget(Delta);
				if (AAthlete* PT = PileTackler.Get()) PT->ShiftFallTarget(Delta);
				PileSpot = Spot;
			}
			else if (Tk && Cr && Tk != Cr && SinceCatch < CatchSecure && !Cr->IsFalling())
			{
				// hit as he caught it: the catch first, then the tackle (the pair, the gang or the strip, picked then)
				FHeldTackle H;
				H.Tk = Tk;
				H.Cr = Cr;
				H.Spot = Spot;
				H.bDive = Ev.note == "dive";
				for (size_t k = SeenEvents + 1; k < Sim->events.size(); ++k)
				{
					if (Sim->events[k].type == "assist" && Sim->events[k].b == Ev.b && Sim->events[k].a && !H.As.IsValid()) H.As = AthOf(Sim->events[k].a);
					if (Sim->events[k].type == "fumble" && Sim->events[k].a == Ev.b) H.Strip = Sim->events[k].note == "sack" ? 2 : 1;
				}
				for (size_t k = SeenEvents; k-- > 0 && Sim->events[k].t >= Ev.t - 1e-6;)
					if (Sim->events[k].type == "bighit" && Sim->events[k].b == Ev.b) H.bBig = true;
				H.Left = static_cast<float>(CatchSecure - SinceCatch);
				HeldTackles.Add(H);
				Cr->HoldForCatch(H.Left + 0.1f); // (on the catch, where the hit landed, until the pair takes him)
				continue;
			}
			else if (Tk && Cr && Tk != Cr)
			{
				// a gang tackle: a second man in on it at the same moment (the sim's assist, this step): the three go down
				// together in a captured three-man tackle when one fits where the two are, else the pair and a pile-on
				AAthlete* As = nullptr;
				for (size_t k = SeenEvents + 1; k < Sim->events.size(); ++k)
					if (Sim->events[k].type == "assist" && Sim->events[k].b == Ev.b && Sim->events[k].a) { As = AthOf(Sim->events[k].a); break; }
				GangAssist = nullptr;
				// the hit stick's big hit (the sim's "bighit" just before this tackle): a hit-stick capture
				bool bBig = false;
				for (size_t k = SeenEvents; k-- > 0 && Sim->events[k].t >= Ev.t - 1e-6;)
					if (Sim->events[k].type == "bighit" && Sim->events[k].b == Ev.b) bBig = true;
				// the ball comes out at this tackle (the sim's fumble, this step): one of the pack's strip captures, the
				// ball punched out of the carrying arm as captured (a sack's strip on a sack)
				int32 StripK = 0;
				for (size_t k = SeenEvents + 1; k < Sim->events.size(); ++k)
					if (Sim->events[k].type == "fumble" && Sim->events[k].a == Ev.b) StripK = Sim->events[k].note == "sack" ? 2 : 1;
				if (As && Ev.note != "dive" && !bBig && !StripK && StartGangTackle(Tk, Cr, As, Spot)) GangAssist = As;
				else StartPairedTackle(Tk, Cr, Spot, -1, Ev.note == "dive", 0.f, bBig, StripK);
			}
			// the sim's tackle always puts the carrier on the ground: when no pair could start (the tackler already falling
			// from a dive or a block), he falls on his own rather than standing there tackled
			if (Cr && !Cr->IsFalling()) Cr->FallAlone(Spot);
		}
		else if (Ev.type == "assist" && Ev.a && Ev.b)
		{
			// (a tackle held for the catch takes its assist with it, as a gang tackle or a pile-on, when it goes)
			bool bHeld = false;
			for (const FHeldTackle& H : HeldTackles) bHeld = bHeld || H.Cr.Get() == AthOf(Ev.b);
			if (AAthlete* As = AthOf(Ev.a); As && As != GangAssist.Get() && !bHeld) StartPileOn(As, AthOf(Ev.b));
		}
		else if (Ev.type == "fumble" && Ev.a)
		{
			// the ball out: kept in the carrier's arm until a strip capture's punch (started by the tackle this same step),
			// then flown by the sim's rigid ball. A second man's strip of a carrier already going down in the pile: his punch.
			AAthlete* Cr = AthOf(Ev.a);
			FumbleSerial = Sim->serial;
			FumbleWT = GetWorld()->GetTimeSeconds();
			FumbleCarrier = Cr;
			FumbleOutIn = 0.f;
			if (Cr && LastStripOut >= 0.f && PileCarrier.Get() == Cr)
			{
				FumbleOutIn = LastStripOut;
				Cr->ForceBallHand(bLastStripRight, LastStripOut + 0.1f);
			}
			LastStripOut = -1.f;
			if (Ev.note == "strip" && Ev.b)
				if (AAthlete* Sp = AthOf(Ev.b); Sp && Sp != Cr && !Sp->IsFalling()) Sp->PlayStripPunch(Cr);
			FRotator Hr;
			if (!Cr || !Cr->GetBallHold(FumbleHandW, Hr)) FumbleHandW = ToWorld(Sim->ball.pos.x, Sim->ball.pos.y, Dir, Sim->ball.pos.z);
			UE_LOG(LogTemp, Display, TEXT("TAG fumble: #%d (%s) forced by #%d, ball out in %.2f s"), Ev.a->p ? Ev.a->p->number : -1, UTF8_TO_TCHAR(Ev.note.c_str()),
				Ev.b && Ev.b->p ? Ev.b->p->number : -1, FumbleOutIn);
		}
		else if (Ev.type == "dive" && Ev.a)
		{
			// going for the loose ball: a dive whose hands reach the turf where the sim has him land, when it has him land
			if (AAthlete* Dv = AthOf(Ev.a); Dv && !Dv->IsFalling())
				Dv->PlayLooseDive(static_cast<float>(FMath::Max(0.05, Ev.a->looseDiveT)), ToWorld(Ev.a->looseDiveAt.x, Ev.a->looseDiveAt.y, Dir));
		}
		else if (Ev.type == "recover" && Ev.a)
		{
			// the ball is his: scooped up on the run, or he is on it on the turf (his dive already playing; a man the ball
			// came to without one goes down on it now) and the men around pile on
			AAthlete* Rc = AthOf(Ev.a);
			if (Rc && Ev.note == "scoop") Rc->PlayScoop();
			else if (Rc)
			{
				bool bDove = false;
				for (size_t k = 0; k < SeenEvents; ++k)
					if (Sim->events[k].type == "dive" && Sim->events[k].a == Ev.a && Ev.t - Sim->events[k].t < 1.0) bDove = true;
				if (!bDove) Rc->PlayLooseDive(0.12f, ToWorld(Sim->ball.pos.x, Sim->ball.pos.y, Dir));
				TArray<TPair<double, const tag::Ent*>, TInlineAllocator<8>> Near;
				for (const tag::Ent* E : Sim->ents)
					if (E != Ev.a && E->state != tag::EntState::Down && E->looseDiveT < 0 && tag::dist(E->pos, Ev.a->pos) < 2.6)
						Near.Add(TPair<double, const tag::Ent*>(tag::dist(E->pos, Ev.a->pos), E));
				Near.Sort([](const TPair<double, const tag::Ent*>& X, const TPair<double, const tag::Ent*>& Y) { return X.Key < Y.Key; });
				for (int32 k = 0; k < Near.Num() && k < 2; ++k)
					if (AAthlete* As = AthOf(Near[k].Value); As && !As->IsFalling()) StartPileOn(As, Rc);
			}
			UE_LOG(LogTemp, Display, TEXT("TAG fumble recovered by #%d (%s, %s)"), Ev.a->p ? Ev.a->p->number : -1, UTF8_TO_TCHAR(Ev.note.c_str()),
				Ev.a->side == tag::Side::Def ? TEXT("defense") : TEXT("offense"));
		}
		else if (Ev.type == "pancake" && Ev.a && Ev.b)
		{
			// a run block won decisively: the defender goes down on his back (the sim's down state: hit, fall, get up)
			// and the blocker finishes over him
			if (AAthlete* Bl = AthOf(Ev.b))
			{
				Bl->PlayPancakeFinish();
				if (AAthlete* Df = AthOf(Ev.a))
				{
					// knocked onto his back away from the blocker (the carrier's head-on fall, about a metre back)
					const FVector Here = Df->GetActorLocation();
					const float Yaw = (Bl->GetActorLocation() - Here).Rotation().Yaw;
					if (UAnimationAsset* Fall = FAthleteAssets::Get().PairC[FAthleteAssets::PairDrive])
						// (eased in: put on the capture's heading in a frame, a safety pancaked out of a double-team rep turned 95 deg
						// at once and his feet swung 74 cm, to10 c020)
						Df->BeginPairedFall(Fall, Here, Here, Yaw, 0.3f, 0.9f, true);
				}
			}
			UE_LOG(LogTemp, Display, TEXT("TAG pancake: #%d puts #%d down"), Ev.b->p ? Ev.b->p->number : -1, Ev.a->p ? Ev.a->p->number : -1);
		}
		else if ((Ev.type == "broken") && Ev.a && Ev.b)
		{
			// broken tackle: the defender whiffs, the carrier bounces off and keeps going (a move that beat him plays
			// on: the spin, the stiff arm, the hurdle carry the carrier; a truck puts the tackler down, "trucked")
			AAthlete* Cr = AthOf(Ev.a);
			AAthlete* Df = AthOf(Ev.b);
			if (Cr && Df && Ev.note == "wrap")
			{
				// torn out of a wrap: the carrier comes off the paired capture and stumbles on, the tackler goes to the turf
				// with what is left of his
				Cr->BreakFree();
				Cr->PlayBrokenTackle(FVector::DotProduct(Df->GetActorLocation() - Cr->GetActorLocation(), Cr->GetActorRightVector()) > 0.f);
				if (PileCarrier.Get() == Cr) { PileCarrier = nullptr; PileTackler = nullptr; }
				UE_LOG(LogTemp, Display, TEXT("TAG wrap broken: #%d tears free of #%d"), Ev.a->p ? Ev.a->p->number : -1, Ev.b->p ? Ev.b->p->number : -1);
			}
			else if (Cr && Df)
			{
				if (Ev.note.empty())
					Cr->PlayBrokenTackle(FVector::DotProduct(Df->GetActorLocation() - Cr->GetActorLocation(), Cr->GetActorRightVector()) > 0.f);
				if (Ev.note != "truck")
					Df->PlayMissedTackle(FVector::DotProduct(Cr->GetActorLocation() - Df->GetActorLocation(), Df->GetActorRightVector()) < 0.f);
			}
			if (!Ev.note.empty() && Ev.note != "wrap")
				UE_LOG(LogTemp, Display, TEXT("TAG move won: #%d %s beats #%d"), Ev.a->p ? Ev.a->p->number : -1, UTF8_TO_TCHAR(Ev.note.c_str()), Ev.b->p ? Ev.b->p->number : -1);
		}
		else if (Ev.type == "trucked" && Ev.a && Ev.b)
		{
			// run over: knocked onto his back the way the runner was going (the pancake's fall)
			if (AAthlete* Df = AthOf(Ev.b))
				if (AAthlete* Cr = AthOf(Ev.a))
				{
					const FVector Here = Df->GetActorLocation();
					const float Yaw = (Cr->GetActorLocation() - Here).Rotation().Yaw;
					if (UAnimationAsset* Fall = FAthleteAssets::Get().PairC[FAthleteAssets::PairDrive])
						// (eased in: put on the capture's heading in a frame, a safety pancaked out of a double-team rep turned 95 deg
						// at once and his feet swung 74 cm, to10 c020)
						Df->BeginPairedFall(Fall, Here, Here, Yaw, 0.3f, 0.9f, true);
				}
			UE_LOG(LogTemp, Display, TEXT("TAG truck: #%d runs over #%d"), Ev.a->p ? Ev.a->p->number : -1, Ev.b->p ? Ev.b->p->number : -1);
		}
		else if (Ev.type == "move" && Ev.a)
		{
			UE_LOG(LogTemp, Display, TEXT("TAG move: #%d %s%s at %.2f s"), Ev.a->p ? Ev.a->p->number : -1, UTF8_TO_TCHAR(Ev.note.c_str()),
				Ev.a->userControlled ? TEXT(" (user)") : TEXT(""), Ev.t);
			if (bMoveShot && Ev.a->userControlled && MoveShotsMv < 8)
			{
				// the move a beat in (the clip's key pose)
				const FString Name = FString::Printf(TEXT("tag_mv_%02d_%s.png"), MoveShotsMv++, UTF8_TO_TCHAR(Ev.note.c_str()));
				FTimerHandle H;
				GetWorld()->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [Name] { FScreenshotRequest::RequestScreenshot(Name, true, false); }), 0.28f, false);
			}
		}
		else if (Ev.type == "bighit" && Ev.a && Ev.b)
		{
			UE_LOG(LogTemp, Display, TEXT("TAG big hit: #%d lays out #%d"), Ev.a->p ? Ev.a->p->number : -1, Ev.b->p ? Ev.b->p->number : -1);
		}
		else if (Ev.type == "divecatch" && Ev.a)
		{
			// laid out for it: the sim ends the play at the spot, the receiver dives, secures it and gets up
			// (already in the air for it when the catch was rolled a dive before the ball got there: see DivePrimedSerial)
			if (AAthlete* R = AthOf(Ev.a); R && DivePrimedSerial != Sim->serial) R->PlayDiveAttempt();
		}
		else if (Ev.type == "slide" && Ev.a)
		{
			// a scrambling quarterback giving himself up: the sim ends the play where he goes down, he slides in feet first
			if (AAthlete* Q = AthOf(Ev.a)) Q->PlaySlide();
		}
		else if (Ev.type == "incomplete" && Ev.a)
		{
			// a ball just out of reach of a receiver still running at it: he lays out for it
			const tag::Ball& Bi = Sim->ball;
			const double Gap = tag::dist(tag::V2{Bi.pos.x, Bi.pos.y}, Ev.a->pos);
			if (Gap > 1.0 && Gap < 3.5 && tag::len(Ev.a->vel) > 3.0)
				if (AAthlete* R = AthOf(Ev.a))
					// (not over the drop or catch already playing for this ball: cut into, it switched clips mid-reach)
					if (AAthlete::ArmSweepOff() || !(PrimedSerial == Sim->serial && PrimedCatcher == Ev.a && R->OneShotClipT() >= 0.f)) R->PlayDiveAttempt();
		}
		else if (Ev.type == "catch" && Ev.a)
		{
			// catch type from the ball's height, the receiver's speed, the ball's direction and the sideline
			if (AAthlete* R = AthOf(Ev.a))
			{
				const tag::Ball& Bc = Sim->ball;
				const FVector BallVelC = Dir > 0 ? FVector(Bc.vel.x, -Bc.vel.y, Bc.vel.z) * YD : FVector(-Bc.vel.x, Bc.vel.y, Bc.vel.z) * YD;
				const double Yd = Ev.a->pos.y;
				FVector Side = FVector::ZeroVector;
				if (Yd < 1.5 || Yd > tag::FIELD_W - 1.5)
				{
					const FVector In = ToWorld(Ev.a->pos.x, Yd, Dir, 0), Out = ToWorld(Ev.a->pos.x, Yd < 1.5 ? Yd - 1.0 : Yd + 1.0, Dir, 0);
					Side = (Out - In).GetSafeNormal();
				}
				bool bContested = false;
				for (const tag::Ent* O : Sim->ents)
					if (O->side != Ev.a->side && tag::dist(O->pos, Ev.a->pos) < 1.3) bContested = true;
				// the sim takes momentum off the catcher as he catches it (x0.45, x0.3 contested): his speed into the catch
				const double SpeedIn = tag::len(Ev.a->vel) / (bContested ? 0.3 : 0.45);
				// (already in his catch: started before the ball got there, see the football layer below)
				// (primed, but nothing of it playing any more - a ball rolled as a drop and caught after all, a capture that ended
				// before the ball got there: he caught it on his run with the arm layers alone; arm audit, 27 Nov. Now the
				// catch from its hands-on-the-ball frame - CatchIn 0 - not a placed clip from its first frame.)
				const bool bSweep = !AAthlete::ArmSweepOff();
				const bool bPrimedPlaying = PrimedSerial == Sim->serial && PrimedCatcher == Ev.a && (!bSweep || R->OneShotClipT() >= 0.f);
				if (!bPrimedPlaying && !(KickCatchSerial == Sim->serial && Sim->cfg.kick))
					R->PlayCatch(static_cast<float>(Bc.pos.z * YD), static_cast<float>(SpeedIn * YD), BallVelC, Side,
						ToWorld(Bc.pos.x, Bc.pos.y, Dir, Bc.pos.z), bContested, bAggroCatch && Ev.a->side == tag::Side::Off, bSweep ? 0.f : -1.f);
			}
		}
	}
	const FVector BallW = ToWorld(B.pos.x, B.pos.y, Dir, B.pos.z);
	const FVector BallVelW = Dir > 0 ? FVector(B.vel.x, -B.vel.y, B.vel.z) * YD : FVector(-B.vel.x, B.vel.y, B.vel.z) * YD;
	// Seconds until the ball in the air meets a man (the sim's own moment, PlaySim::homeBall): when it is nearest him on
	// the ground plane, or comes down to his hands (a yard up) if that is sooner than it would be at the turf. The
	// catches, swats and dives are timed to it; the flight's planned end (flightT) is where the throw was aimed, up to
	// 0.2 s off when the ball actually gets to him.
	auto CcContactIn = [&B](const tag::Ent* CcE) -> double
	{
		const double CcRx = B.pos.x - CcE->pos.x, CcRy = B.pos.y - CcE->pos.y, CcUx = B.vel.x - CcE->vel.x, CcUy = B.vel.y - CcE->vel.y;
		const double CcUu = CcUx * CcUx + CcUy * CcUy;
		double CcT = CcUu > 0.25 ? -(CcRx * CcUx + CcRy * CcUy) / CcUu : 9.0;
		if (CcT > 0 && B.pos.z + B.vel.z * CcT - 0.5 * tag::G * CcT * CcT < 0.35 && B.pos.z > 1.0)
			CcT = FMath::Min(CcT, (B.vel.z + FMath::Sqrt(B.vel.z * B.vel.z + 2 * tag::G * (B.pos.z - 1.0))) / tag::G);
		return CcT;
	};
	// the man the ball is coming to, when the sim has said so before it gets there (a pass's rolled outcome: the
	// receiver's catch or drop, a defender's breakup or interception; a kick's returner), and how long until it does
	const tag::Ent* CcWho = nullptr;
	double CcWhoIn = -1.0;
	if (B.mode == tag::BallMode::Air && Sim->isLive())
	{
		if (Sim->cfg.kick)
		{
			if (Sim->returner && tag::dist(Sim->returner->pos, tag::V2{Sim->cfg.kick->landX, Sim->cfg.kick->landY}) < 2.2)
			{
				CcWho = Sim->returner;
				CcWhoIn = Sim->cfg.kick->hang - B.t;
			}
		}
		else if (B.fate != 0 && B.target && !B.away && !B.tumble)
		{
			CcWho = B.fate >= 3 ? B.fateBy : B.target;
			if (CcWho) CcWhoIn = CcContactIn(CcWho);
		}
	}

	// the handoff: the quarterback and the back go into one of the pack's two-man handoffs a beat before the sim's
	// exchange, timed so the ball changes hands in the capture as it does in the sim (the keyed QB clip played after the
	// exchange and the back did nothing: the ball jumped to him)
	// The run exchange as the sim now plays it (PlaySim::mesh): the capture is chosen for the play - the quarterback on
	// the side of the back's path the play has him, from the same alignment - and started as soon as the exchange is no
	// further off than the capture has before its own, from the snap if the capture runs that long (a from-the-snap
	// capture then gives the quarterback his whole footwork to the mesh). A toss has no mesh: the quarterback's toss is
	// started a beat ahead of its release.
	if (Sim->meshOn() && Sim->isLive())
	{
		if (const tag::Ent* MRb = Sim->offRole(Sim->cfg.offPlay->run->carrier); MRb && Sim->qb && !Sim->handedOff && Sim->t > 0.02)
		{
			const tag::Ent* Rb = MRb;
			const auto& Mh = Sim->mesh();
			AAthlete* Qa = AthOf(Sim->qb);
			AAthlete* Ra = AthOf(Rb);
			if (!Qa || !Ra) {}
			else if (Mh.toss)
			{
				if (TossClipSerial != Sim->serial && Sim->tossT0 < 0 && Mh.T - Sim->t <= 0.2 && FAthleteAssets::Get().Pitch)
				{
					TossClipSerial = Sim->serial;
					const bool bLeft = FVector::DotProduct(Ra->GetActorLocation() - Qa->GetActorLocation(), Qa->GetActorRightVector()) < 0.f;
					Qa->PlayOneShot(FAthleteAssets::Get().Side(FAthleteAssets::Get().Pitch.Get(), bLeft));
				}
			}
			else if (Sim->qbKept || Sim->rpoThrow)
			{
				// the read said keep it (or throw it): the handoff capture lets the two men go, the quarterback runs or throws
				static unsigned long long BrokeSerial = 0;
				if (HandoffSerial == Sim->serial && BrokeSerial != Sim->serial)
				{
					BrokeSerial = Sim->serial;
					Qa->BreakMesh();
					Ra->BreakMesh();
					UE_LOG(LogTemp, Display, TEXT("TAG option read: %s at %.2f s"), Sim->qbKept ? TEXT("keep") : TEXT("RPO throw"), Sim->t);
				}
			}
			else if (HandoffSerial == Sim->serial)
			{
				// (playing: kept in step with the sim's latest word on when the ball changes hands)
				Qa->MeshRetime(static_cast<float>(Sim->meshIn()));
				Ra->MeshRetime(static_cast<float>(Sim->meshIn()));
			}
			else if (HandoffNoneSerial != Sim->serial)
			{
				const bool bGun = Sim->qb->pos.x < Sim->los - 2.5;
				// (the sim's frame keeps its hand: + y is the offense's left)
				const bool bQbLeft = Mh.dir.x * (Mh.qbAt.y - Mh.at.y) - Mh.dir.y * (Mh.qbAt.x - Mh.at.x) > 0.0;
				bool bNone = false;
				const tag::RunStyle RunSt = Sim->cfg.offPlay->run->style;
				const int32 RunKind = RunSt == tag::RunStyle::Draw ? 2 : RunSt == tag::RunStyle::Stretch ? 1 : 0;
				// (the carry-out the sim gives him - PlaySim::thinkIdle: a boot away from the run on zone and stretch, a drop
				// otherwise - and the two men's speeds as the ball changes hands, m/s: the back at his tempo, the quarterback
				// riding it, about 1.8 under center and 1.3 from the gun - Tools/HandoffCal)
				const int32 FakeSide = RunSt == tag::RunStyle::Zone || RunSt == tag::RunStyle::Stretch ? (Mh.away > 0.0 ? 1 : -1) : 0;
				const float BackMs = static_cast<float>(Rb->vmax * Mh.tempo * 0.9144);
				const float QbMs = bGun ? 1.3f : 1.8f;
				if (AAthlete::PlayRepHandoffFit(Qa, Ra, bGun, bQbLeft, RunKind, static_cast<float>(tag::tune().meshArm * YD), static_cast<float>(Sim->meshIn()), HandoffSeq, bNone,
						FakeSide, BackMs, QbMs))
				{
					HandoffSerial = Sim->serial;
					++HandoffSeq;
				}
				else if (bNone)
				{
					HandoffNoneSerial = Sim->serial;
					UE_LOG(LogTemp, Display, TEXT("TAG handoff mesh: no capture with the quarterback on the back's %s (%s): the keyed handoff"), bQbLeft ? TEXT("left") : TEXT("right"),
						bGun ? TEXT("gun") : TEXT("under center"));
				}
			}
		}
	}
	else if (const tag::OffPlay* Op = Sim->cfg.offPlay; Op && Op->type == tag::PlayType::Run && Op->run && Op->run->carrier != "QB" && Sim->qb &&
		Sim->ball.holder == Sim->qb && HandoffSerial != Sim->serial && Sim->t > 0.05)
	{
		const double In = Op->run->handoffDelay - Sim->t;
		const bool bPitch = Op->run->style == tag::RunStyle::Stretch && Sim->qb->pos.x > Sim->los - 2;
		if (!bPitch && In <= 0.45 && In > -0.2)
			if (const tag::Ent* Rb = Sim->offRole(Op->run->carrier))
			{
				HandoffSerial = Sim->serial;
				static int32 GHandSeq = 0;
				const bool bGun = Sim->qb->pos.x < Sim->los - 2.5;
				AAthlete::PlayRepHandoff(AthOf(Sim->qb), AthOf(Rb), bGun, static_cast<float>(FMath::Max(0.0, In)), GHandSeq++);
			}
	}
	// play action: the quarterback and the back into one of the pack's two-man fakes from the snap - the ball shown in
	// the back's belly at the sim's mesh (Sim->paMeshT(): the back's depth), pulled, and the drop or the boot after it (the quarterback had walked
	// sideways for 0.85 s on his own, the back nowhere near him)
	if (const tag::OffPlay* Op = Sim->cfg.offPlay; Op && Op->type == tag::PlayType::PlayAction && Sim->qb && Sim->ball.holder == Sim->qb &&
		PaFakeSerial != Sim->serial && Sim->t > 0.02 && Sim->t < 0.3)
		if (const tag::Ent* Rb = Sim->offRole("RB"))
		{
			PaFakeSerial = Sim->serial;
			static int32 GPaSeq = 0;
			const bool bGun = Sim->qb->pos.x < Sim->los - 2.5;
			AAthlete::PlayRepPaFake(AthOf(Sim->qb), AthOf(Rb), bGun, Sim->paBoot(), static_cast<float>(FMath::Max(0.0, Sim->paMeshT() - Sim->t)), GPaSeq++);
		}
	// a kick coming down to the returner: he goes into the pack's kick catch a beat before it gets there (the
	// catch event found him standing and the ball arrived in his hands without one)
	if (Sim->cfg.kick && Sim->returner && Sim->ball.mode == tag::BallMode::Air && KickCatchSerial != Sim->serial)
	{
		const double Left = Sim->cfg.kick->hang - Sim->ball.t;
		const tag::Ent* Rt = Sim->returner;
		if (Left > 0.0 && Left <= 0.35 && tag::dist(Rt->pos, tag::V2{Sim->cfg.kick->landX, Sim->cfg.kick->landY}) < 2.2)
			if (AAthlete* R = AthOf(Rt); R && R->PlayKickCatch(tag::len(Rt->vel) > 2.0, static_cast<float>(Left)))
				KickCatchSerial = Sim->serial;
	}
	// the windup: the throw that fits the pass starts as the sim starts the throw, timed so its release is the sim's
	// (it used to start at the release itself, from 7 frames before: the ball was away before the arm came through)
	// (a throw-away too: it had no windup, and its keyed throw played from 7 frames before its release at the release - the
	// arm came through after the ball was gone)
	const bool bWindAway = Sim->pendingThrow && Sim->pendingThrow->away && !AAthlete::ArmSweepOff();
	if (Sim->pendingThrow && !Sim->passThrown && Sim->qb && ThrowTimedSerial != Sim->serial && ((!Sim->pendingThrow->away && Sim->pendingThrow->tgt) || bWindAway))
		if (AAthlete* Q = AthOf(Sim->qb))
		{
			const tag::Ent* QE = Sim->qb;
			const double D = Sim->pendingThrow->tgt ? tag::dist(QE->pos, Sim->pendingThrow->tgt->pos) : 20.0;
			const double Sp = tag::len(QE->vel);
			double Rush = 99.0;
			for (const tag::Ent* O : Sim->ents)
				if (O->side != QE->side) Rush = FMath::Min(Rush, tag::dist(O->pos, QE->pos));
			int32 K = FAthleteAssets::ThrowSet;
			static int32 GPressSeqW = 0;
			const bool bWindTouch = Sim->pendingThrow->lob || Sim->pendingThrow->style == 2;
			bThrowTimedStand = false;
			if (Sp > 2.5)
			{
				const FVector VW = Dir > 0 ? FVector(QE->vel.x, -QE->vel.y, 0) : FVector(-QE->vel.x, QE->vel.y, 0);
				K = FVector::DotProduct(VW, Q->GetActorRightVector()) > 0.f ? FAthleteAssets::ThrowRunRK : FAthleteAssets::ThrowRunLK;
			}
			else if (Rush < 1.6) K = (GPressSeqW++ % 2) ? FAthleteAssets::ThrowSidearmK : FAthleteAssets::ThrowFadeK;
			else
			{
				// (set and clear of the rush: the pass type picks the throw - and a pass settled in the windup re-picks it below)
				bThrowTimedStand = true;
				K = bWindTouch ? FAthleteAssets::ThrowTouchK : D > 28.0 ? FAthleteAssets::ThrowDeepK : D < 10.0 ? FAthleteAssets::ThrowQuickK : FAthleteAssets::ThrowSet;
				// a player's pass whose type is still open (pressed, not let go, no L1 / L2: FeelThrow) starts as the set throw -
				// the touch pass's own family, so a tap that turns out a lob goes on in it unchanged; the quick and the deep
				// throws are a bullet's only (a switch from one of them into the set throws 0.12 s before the release showed:
				// transition jerk 6.7)
				if (QbStyleOpenFrame == GFrameCounter && !bWindTouch && (K == FAthleteAssets::ThrowQuickK || K == FAthleteAssets::ThrowDeepK)) K = FAthleteAssets::ThrowSet;
			}
			ThrowTimedKey = K;
			bThrowTimedTouch = bWindTouch;
			// (a man a few yards off: the pack's shovel - underhand, out of the set or on the move)
			const bool bShovel = Sim->pendingThrow->shovel; // (the sim's call as the throw started: the clip and the flight agree)
			if (bShovel) Q->PlayShovelTimed(Sp > 2.5, static_cast<float>(Sim->pendingThrow->at - Sim->t));
			else Q->PlayThrowTimed(K, static_cast<float>(Sim->pendingThrow->at - Sim->t));
			UE_LOG(LogTemp, Display, TEXT("TAG throw type: style %d lob %d %s, %.1f yd, clip key %d"), Sim->pendingThrow->style, Sim->pendingThrow->lob ? 1 : 0,
				bShovel ? TEXT("shovel") : TEXT("overhand"), D, K);
			if (bShovel) bThrowTimedStand = false;
			ThrowTimedSerial = Sim->serial;
		}
	// the pass settled in the windup (the player's throw starts on the press and a tap turns out a lob, L1 a high one:
	// Game/MatchFeel.cpp): the throw that fits it from here, timed to the same release - a lob does not finish as the quick or
	// the deep throw it started as. (The clip change is dead-blended; too near the release it is left alone.)
	if (Sim->pendingThrow && !Sim->passThrown && Sim->qb && ThrowTimedSerial == Sim->serial && bThrowTimedStand && !Sim->pendingThrow->away && Sim->pendingThrow->tgt)
	{
		const bool bRtTouch = Sim->pendingThrow->lob || Sim->pendingThrow->style == 2;
		const float RtIn = static_cast<float>(Sim->pendingThrow->at - Sim->t);
		AAthlete* RtQ = AthOf(Sim->qb);
		if (bRtTouch != bThrowTimedTouch && RtIn > 0.05f && RtQ)
		{
			const double RtD = tag::dist(Sim->qb->pos, Sim->pendingThrow->tgt->pos);
			const int32 RtK = bRtTouch ? FAthleteAssets::ThrowTouchK : RtD > 28.0 ? FAthleteAssets::ThrowDeepK : RtD < 10.0 ? FAthleteAssets::ThrowQuickK : FAthleteAssets::ThrowSet;
			const int32 XfjBefore = RtQ->ClipXfjN;
			const bool bSwitched = RtQ->RetimeThrow(RtK, RtIn);
			if (bSwitched)
			{
				++ThrowRetimes;
				ThrowRetimeSerial = Sim->serial;
				ThrowRetimeAt = FPlatformTime::Seconds();
				ThrowRetimeXfjN = XfjBefore;
			}
			UE_LOG(LogTemp, Display, TEXT("TAG throw re-picked in the windup: %s, clip key %d -> %d (%s), %.2f s before the release"), bRtTouch ? TEXT("now a lob / high") : TEXT("now driven"),
				ThrowTimedKey, RtK, bSwitched ? TEXT("switched") : TEXT("the clip playing already fits"), RtIn);
			ThrowTimedKey = RtK;
			bThrowTimedTouch = bRtTouch;
		}
	}
	// the captures' key moments on the ball's: the windup's release on the sim's release (it moves - a pass held for the
	// receiver's break), a primed catch's, swat's or interception's hands on the ball when it meets the man (sooner or later
	// than the flight's plan). Each capture's own rate is steered for it (AAthlete::AlignKey, 0.7-1.4); the 27 Nov arm audit
	// had releases and catches up to a quarter second off the ball.
	if (Sim->pendingThrow && !Sim->passThrown && Sim->qb && ThrowTimedSerial == Sim->serial)
		if (AAthlete* Aq = AthOf(Sim->qb); Aq && Aq->IsThrowing()) Aq->AlignKey(static_cast<float>(Sim->pendingThrow->at - Sim->t));
	if (B.mode == tag::BallMode::Air && Sim->isLive() && !Sim->cfg.kick && B.target && !B.away)
	{
		auto AlignOn = [&](const tag::Ent* Who)
		{
			if (!Who) return;
			const double In = CcContactIn(Who);
			if (In > 0.0 && In < 0.8)
				if (AAthlete* Aw = AthOf(Who)) Aw->AlignKey(static_cast<float>(In));
		};
		if (PrimedSerial == Sim->serial) AlignOn(PrimedCatcher);
		// (not the two-man interception: its two parts run in step from one moment of the capture, at the capture's rate)
		if (SwatSerial == Sim->serial && SwatBy != PrimedCatcher && !(bIntPaired && FatePrimedSerial == Sim->serial)) AlignOn(SwatBy);
	}
	else if (B.mode == tag::BallMode::Air && Sim->isLive() && Sim->cfg.kick && KickCatchSerial == Sim->serial && Sim->returner)
		if (AAthlete* Ar = AthOf(Sim->returner)) Ar->AlignKey(static_cast<float>(Sim->cfg.kick->hang - B.t));
	// the pass's outcome, rolled by the sim ~0.4 s before it gets there (PlaySim::preRollCatch): the interaction it is
	// starts now, in time - the defender and the receiver in one of the pack's two-man interceptions, the named defender's
	// breakup, the receiver's drop (a catch is primed below as before). An interception's pair is chosen from where the
	// two men are and the ball's height; both play from as far before its catch as the ball has left to fly.
	// (Timed to when the ball meets the man it names, not the flight's planned end; and started again if the sim rolls
	// a defender's play on it after the receiver's - an underneath man the pass will go within reach of, preRollLane.)
	if (B.mode == tag::BallMode::Air && Sim->isLive() && B.fate != 0 && B.target && !B.tumble &&
		(FatePrimedSerial != Sim->serial || FatePrimedFate != B.fate || FatePrimedBy != B.fateBy))
	{
		const double TArr = CcWho ? CcWhoIn : B.flightT - B.t;
		if (TArr > 0.0 && TArr <= 0.4)
		{
			FatePrimedSerial = Sim->serial;
			FatePrimedFate = B.fate;
			FatePrimedBy = B.fateBy;
			bIntPaired = false;
			const double G = 10.72;
			const double Pz = FMath::Max(0.3, B.pos.z + B.vel.z * TArr - 0.5 * G * TArr * TArr);
			static int32 GIntSeq = 0;
			if (B.fate == 4 && B.fateBy)
			{
				bIntPaired = AAthlete::PlayIntPair(AthOf(B.fateBy), AthOf(B.target), static_cast<float>(TArr), static_cast<float>(Pz * YD), GIntSeq++);
				if (bIntPaired)
				{
					PrimedSerial = Sim->serial;
					PrimedCatcher = B.target;
					SwatSerial = Sim->serial;
					SwatBy = B.fateBy;
				}
				// (no two-man capture fits: his own interception, timed so its hands are up when the ball gets to him. It
				// started at the sim's event, with the ball already his.)
				else if (B.fateBy->free() && AthOf(B.fateBy)->PlayBallDefense(0, true, static_cast<float>(TArr)))
				{
					IntPrimedSerial = Sim->serial;
					SwatSerial = Sim->serial;
					SwatBy = B.fateBy;
				}
			}
			else if (B.fate == 3 && B.fateBy && B.fateBy->free())
			{
				const tag::Ent* E = B.fateBy;
				const double Sp = tag::len(E->vel);
				const double Fwd = Sp > 0.3 ? (E->vel.x * FMath::Cos(E->face) + E->vel.y * FMath::Sin(E->face)) / Sp : 1.0;
				if (AthOf(E)->PlayBallDefense(Sp < 1.5 || Fwd < -0.4 ? 2 : FMath::Abs(Fwd) < 0.5 ? 1 : 0, false, static_cast<float>(TArr)))
				{
					SwatSerial = Sim->serial;
					SwatBy = E;
				}
				// and the receiver: the pack's ball-off-the-hands capture, timed to the ball - his hands up for it as it is
				// knocked away (he went into a full catch and tucked a ball the defender had batted down; arm audit, 27 Nov)
				const double TRecv = CcContactIn(B.target);
				if (!AAthlete::ArmSweepOff() && B.target->free() && TRecv > 0.0 && PrimedSerial != Sim->serial)
					if (AAthlete* Rv = AthOf(B.target); Rv && Rv->PlayDrop(static_cast<float>(TRecv)))
					{
						PrimedSerial = Sim->serial;
						PrimedCatcher = B.target;
					}
			}
			else if (B.fate == 2 && AthOf(B.target)->PlayDrop(static_cast<float>(TArr)))
			{
				PrimedSerial = Sim->serial;
				PrimedCatcher = B.target;
			}
			UE_LOG(LogTemp, Display, TEXT("TAG pass fate %d (%s) for #%d, %.2f s out, defender #%d%s"), B.fate,
				B.fate == 4 ? TEXT("interception") : B.fate == 3 ? TEXT("breakup") : B.fate == 2 ? TEXT("drop") : TEXT("catch"), B.target->p ? B.target->p->number : -1, TArr,
				B.fateBy && B.fateBy->p ? B.fateBy->p->number : -1, bIntPaired ? TEXT(", two-man capture") : TEXT(""));
		}
	}
	// football animation layer: what each player's hands, torso and eyes are doing (spec L2/L4)
	const tag::Ent* Holder = B.mode == tag::BallMode::Held ? B.holder : nullptr;
	for (tag::Ent* E : Sim->ents)
	{
		AAthlete* A = AthOf(E);
		FAthleteFootball F;
		const bool bQB = E->p->pos == tag::Pos::QB;
		F.bHasBall = Holder == E;
		F.bCoverBall = Holder == E && E->protecting;
		F.bLive = State == EMatchState::Live;
		F.bSecure = F.bHasBall && bQB && E->speed() < 5.5 && E->anim != tag::Anim::Run && E->anim != tag::Anim::Throw; // (the throw's capture owns his arms)
		if (Holder && Holder != E && E->side != Holder->side) F.CarrierDist = FVector::Dist2D(A->GetActorLocation(), AthOf(Holder)->GetActorLocation());
		if (E->anim == tag::Anim::Throw) F.ThrowT = FMath::Clamp(static_cast<float>(E->animT) / 0.45f, 0.f, 1.f);
		// (a place kick is set in its play: the units in their stances until its snap)
		F.bStance = (Sim->t < 0.05 || Sim->placeSet()) && (E->isOL || E->isDL);
		F.bPreSnap = (Sim->t < 0.05 && State == EMatchState::Presnap) || Sim->placeSet();
		// the lineman's job at the snap picks his get-off (pass set, run block, or a defensive lineman's)
		if (E->isDL && E->side == tag::Side::Def) F.GetOffKind = 3;
		else if (E->isOL || E->role.rfind("TE", 0) == 0) // (TE, TE2 and the jumbo sets' TE3)
			F.GetOffKind = E->state == tag::EntState::PassBlock ? 1 : E->state == tag::EntState::RunBlock ? 2 : 0;
		if (bQB && F.bPreSnap)
			for (const tag::Ent* C : Sim->ents)
				if (C->role == "C") F.bUnderCenter = tag::dist(C->pos, E->pos) < 1.7; // hands under the center
		const FVector Me = A->GetActorLocation();
		// the catch: the man it is thrown to goes into one of the pack's catches a beat before the ball arrives, picked
		// from where it will meet him, so the captured hands meet the ball (the procedural reach is off: it pulled the
		// arms back behind a man running away from the throw, and every bystander under the ball reached for it too)
		if (B.mode == tag::BallMode::Air && Sim->isLive())
		{
			const float Dist = FVector::Dist(BallW, Me + FVector(0, 0, 120));
			const bool bTarget = B.target == E;
			const double TArr = B.flightT - B.t;
			// the man covering him plays the ball: the defender nearest the target (inside the sim's contest radius) goes up
			// for it a beat before it arrives, in the swat for how he is moving (the sim decides the play at the ball:
			// an interception then plays an interception catch over it)
			if (E->side == tag::Side::Def && B.target && !B.away && TArr > 0.0 && TArr <= 0.3 && SwatSerial != Sim->serial && E->free())
			{
				const double Dt2 = tag::dist(E->pos, B.target->pos);
				bool bNearest = Dt2 < 1.8;
				for (const tag::Ent* O : Sim->def)
					if (O != E && O->free() && tag::dist(O->pos, B.target->pos) < Dt2) bNearest = false;
				if (bNearest)
				{
					SwatSerial = Sim->serial;
					SwatBy = E;
					const double Sp = tag::len(E->vel);
					const double Fwd = Sp > 0.3 ? (E->vel.x * FMath::Cos(E->face) + E->vel.y * FMath::Sin(E->face)) / Sp : 1.0;
					const int32 Gait = Sp < 1.5 || Fwd < -0.4 ? 2 : FMath::Abs(Fwd) < 0.5 ? 1 : 0;
					// (timed to the ball meeting the man he covers: untimed, its hands met the ball 4 frames after it started,
					// up to 0.17 s before the ball was there)
					const double TSwat = CcContactIn(B.target);
					A->PlayBallDefense(Gait, false, !AAthlete::ArmSweepOff() && TSwat > 0.0 ? static_cast<float>(TSwat) : -1.f);
				}
			}
			// (timed to when the ball meets him, CcContactIn, and the capture started so its hands are on the ball then:
			// PlayCatch's CatchIn. A catch the sim rolled as a dive - Ball::fateDive - is the layout, started now, in the air
			// when the ball gets there; it started at the sim's catch, with the ball already his. -TagNoDivePrime: as before.)
			const double TCatch = bTarget ? CcContactIn(E) : -1.0;
			static const bool bNoDivePrime = FParse::Param(FCommandLine::Get(), TEXT("TagNoDivePrime"));
			if (bTarget && E->side == tag::Side::Off && !B.away && TCatch > 0.0 && TCatch <= 0.3 && PrimedSerial != Sim->serial && B.fate == 1 && B.fateDive && !bNoDivePrime)
			{
				PrimedSerial = Sim->serial;
				PrimedCatcher = E;
				DivePrimedSerial = Sim->serial;
				A->PlayDiveAttempt(static_cast<float>(TCatch));
				UE_LOG(LogTemp, Display, TEXT("TAG dive catch primed: #%d, %.2f s out"), E->p->number, TCatch);
			}
			// (as soon as the sim has rolled it a catch - 0.4 s before the flight's end, PlaySim::preRollCatch - not 0.3 s out:
			// the capture runs in to the ball and its time-warp has the time to put the hands on it; arm sweep 2)
			const bool bEarlyCatch = !AAthlete::ArmSweepOff() && B.fate == 1 && !B.fateDive && TCatch <= 0.6;
			if (bTarget && E->side == tag::Side::Off && !B.away && TCatch > 0.0 && (TCatch <= 0.3 || bEarlyCatch) && PrimedSerial != Sim->serial)
			{
				PrimedSerial = Sim->serial;
				PrimedCatcher = E;
				const double G = 10.72;
				const tag::V3 Pa{B.pos.x + B.vel.x * TCatch, B.pos.y + B.vel.y * TCatch, FMath::Max(0.3, B.pos.z + B.vel.z * TCatch - 0.5 * G * TCatch * TCatch)};
				FVector Side = FVector::ZeroVector;
				if (E->pos.y < 1.5 || E->pos.y > tag::FIELD_W - 1.5)
					Side = (ToWorld(E->pos.x, E->pos.y < 1.5 ? E->pos.y - 1.0 : E->pos.y + 1.0, Dir, 0) - ToWorld(E->pos.x, E->pos.y, Dir, 0)).GetSafeNormal();
				bool bContested = false;
				for (const tag::Ent* O : Sim->ents)
					if (O->side != E->side && tag::dist(O->pos, E->pos) < 1.3) bContested = true;
				A->PlayCatch(static_cast<float>(Pa.z * YD), static_cast<float>(tag::len(E->vel) * YD), BallVelW, Side, ToWorld(Pa.x, Pa.y, Dir, Pa.z), bContested,
					bAggroCatch, static_cast<float>(TCatch));
				UE_LOG(LogTemp, Verbose, TEXT("TAG catch primed: #%d, %.2f s out, ball %.0f cm"), E->p->number, TCatch, Pa.z * YD);
			}
			// eyes on the ball in the air: the man it is thrown or kicked to, anyone it is coming down near, the passer
			// and the kicker watching it go. The rest look where it is going (the man it is meant for), at eye level:
			// all 22 tracking a punt's hang overhead read as the whole field staring into the stands
			const FVector Land = B.target ? AthOf(B.target)->BoneWorld(TEXT("spine_04"))
				: B.targetSpot ? ToWorld(B.targetSpot->x, B.targetSpot->y, Dir, 1.5) : FVector(BallW.X + BallVelW.X * 1.5f, BallW.Y + BallVelW.Y * 1.5f, 150.f);
			const bool bOnBall = bTarget || Dist < 1100.f || FVector::Dist2D(Me, Land) < 500.f || B.thrower == E ||
				(E->p->pos == tag::Pos::K || E->p->pos == tag::Pos::P);
			F.bLook = true;
			F.bLookHigh = bOnBall;
			F.LookWorld = bOnBall ? BallW : (B.target && B.target != E ? Land : Me + A->GetActorForwardVector() * 1500.f + FVector(0, 0, 160));
		}
		// a loose ball: every man near it has his eyes on it (low, on the turf)
		if (B.mode == tag::BallMode::Loose && Sim->isLive() && FVector::Dist2D(BallW, Me) < 2200.f)
		{
			F.bLook = true;
			F.bLookHigh = true;
			F.LookWorld = BallW;
		}
		// blocks: hands inside on the opponent's chest plate, drive lean
		const tag::Ent* Opp = E->engaged ? E->engaged : (!E->engagedBy.empty() ? E->engagedBy[0] : nullptr);
		if (Opp && !F.bHasBall)
		{
			AAthlete* O = AthOf(Opp);
			const FVector Chest = O->BoneWorld(TEXT("spine_04"));
			F.bReach = true;
			F.ReachWorld = Chest + (Me - Chest).GetSafeNormal2D() * 16.f;
			F.ReachSpread = 13.f;
			F.Lean = E->engaged ? 0.9f : 0.6f;
			F.bLook = true;
			F.LookWorld = O->BoneWorld(TEXT("head"));
		}
		// tackles: wrap the ball carrier at the waist (only in the attempt itself, at arm's length: a pursuer runs with
		// his arms pumping, and a man still 2 m off with both arms held out at the carrier's hips read as a puppet)
		if (Holder && Holder != E && E->side != Holder->side && (E->anim == tag::Anim::Tackle || E->anim == tag::Anim::Dive) &&
			FVector::Dist2D(Me, AthOf(Holder)->GetActorLocation()) < 160.f)
		{
			AAthlete* H = AthOf(Holder);
			F.bReach = true;
			F.ReachWorld = H->BoneWorld(TEXT("pelvis")) + FVector(0, 0, 18);
			F.ReachSpread = 22.f;
			F.Lean = 0.8f;
		}
		if (!F.bLook)
		{
			F.bLook = true;
			// the run exchange: the quarterback's eyes are on the mesh - the belly the ball is going into - and the back's on
			// his hole, down his path (he looked at the ball in the quarterback's hands, and the quarterback downfield); a
			// back the ball is tossed to watches it in
			const tag::Ent* MeshBack = Sim->meshOn() && Sim->isLive() && !Sim->preSnap() && !Sim->handedOff ?Sim->offRole(Sim->cfg.offPlay->run->carrier) : nullptr;
			if (MeshBack && E == Sim->qb && Holder == E) F.LookWorld = AthOf(MeshBack)->BoneWorld(TEXT("spine_04"));
			else if (MeshBack && E == MeshBack && Sim->mesh().toss) F.LookWorld = Sim->tossT0 >= 0 ? Ball->GetActorLocation() : AthOf(Sim->qb)->HandsMid();
			else if (MeshBack && E == MeshBack)
				F.LookWorld = ToWorld(Sim->mesh().at.x + Sim->mesh().dir.x * 6.0, Sim->mesh().at.y + Sim->mesh().dir.y * 6.0, Dir) + FVector(0, 0, 120);
			else if (F.bHasBall && bQB && B.target) F.LookWorld = AthOf(B.target)->BoneWorld(TEXT("head"));
			else if (Holder && Holder != E) F.LookWorld = AthOf(Holder)->BoneWorld(TEXT("spine_04"));
			else F.LookWorld = Me + A->GetActorForwardVector() * 1500.f + FVector(0, 0, 160);
		}
		// the hands to the ball: in the last 0.2 s before it meets the man who catches it (a rolled catch, drop or
		// interception, a kick's returner) both hands go to where it will meet him - the ball's place then, carried back
		// along his own run to now, so the point holds still against his body while both close on it. The capture has
		// his hands where its own receiver caught his ball; the sim brings this one to within half a yard of him
		// (PlaySim::homeBall), anywhere between his knees and the top of his reach. (A breakup is the swat's own arm; a
		// two-man interception has its hands where the two men put them.)
		if (CcWho == E && CcWhoIn > 0.0 && CcWhoIn <= 0.2 && !F.bReach && (Sim->cfg.kick || B.fate != 3) && !(bIntPaired && FatePrimedSerial == Sim->serial))
		{
			const float CcT = static_cast<float>(CcWhoIn);
			const FVector CcBodyV = Dir > 0 ? FVector(E->vel.x, -E->vel.y, 0) * YD : FVector(-E->vel.x, E->vel.y, 0) * YD;
			// (a kick's flight is the sim's own curve to its landing spot, a yard and a third up: not a thrown ball's fall)
			const FVector CcMeet = Sim->cfg.kick ? ToWorld(Sim->cfg.kick->landX, Sim->cfg.kick->landY, Dir, 1.3)
				: BallW + BallVelW * CcT - FVector(0, 0, 0.5 * tag::G * YD * CcT * CcT);
			// (the sim's ball, not the drawn one: the drawn one is bent onto these hands below, and the two chased each
			// other down to where his hands were before the catch - filmed, a ball the sim brought in at his head met at
			// his waist, the hands then carrying it up into the capture's high catch)
			F.bCatchReach = true;
			F.ReachWorld = CcMeet - CcBodyV * CcT;
			F.ReachSpread = 10.f;
		}
		A->SetFootball(F, Dt);
	}

	// automation: grab the key football moments up close (spec acceptance shots)
	if (bAuto && EventShotFrames == 0)
	{
		for (tag::Ent* E : Sim->ents)
		{
			if (!bShotThrow && E->anim == tag::Anim::Throw && E->animT > 0.14)
			{
				bShotThrow = true;
				EventShotName = TEXT("tag_auto_throw.png");
				CloseupTarget = AthOf(E);
				EventShotFrames = 3;
			}
		}
		if (!bShotGetUp && EventShotFrames == 0)
		{
			for (auto& KV : Athletes)
			{
				if (KV.Value && KV.Value->GetUpElapsed() > 0.4f)
				{
					bShotGetUp = true;
					EventShotName = TEXT("tag_auto_getup.png");
					CloseupTarget = KV.Value;
					EventShotFrames = 3;
					break;
				}
			}
		}
		if (!bShotCatch && B.mode == tag::BallMode::Air && B.target &&
			FVector::Dist(BallW, AthOf(B.target)->GetActorLocation()) < 380.f)
		{
			bShotCatch = true;
			EventShotName = TEXT("tag_auto_catch.png");
			CloseupTarget = AthOf(B.target);
			EventShotFrames = 3;
		}
	}
	if (EventShotFrames > 0 && --EventShotFrames == 0)
	{
		FScreenshotRequest::RequestScreenshot(EventShotName, false, false);
		const AAthlete* CA = Cast<AAthlete>(CloseupTarget);
		UE_LOG(LogTemp, Display, TEXT("TAG event shot %s ball=%s %s"), *EventShotName,
			CA ? *CA->GetActorTransform().InverseTransformPosition(BallW).ToCompactString() : TEXT("-"), CA ? *CA->DebugAnimState() : TEXT(""));
	}
	else if (EventShotFrames == 0 && CloseupTarget && AutoShot < 8)
	{
		CloseupTarget = nullptr;
	}

	// (the ball's drawing against the hands that play it: see BallBend / AttachOff in the header)
	float CcSnapNow = 0.f;
	const float CcBendWas = static_cast<float>(BallBend.Size());
	if (B.mode == tag::BallMode::Held && B.holder)
	{
		AAthlete* A = AthOf(B.holder);
		FVector HoldLoc;
		FRotator HoldRot;
		// (down with it too: the ball stays in the hand through the fall. It used to be set on the turf beside him the
		// moment the tackle began - on a sack it looked like a fumble nobody called)
		if (A->GetBallHold(HoldLoc, HoldRot))
		{
			// The run exchange, hand to belly. Before it the ball is in the quarterback's giving hand - the one nearer the
			// back's belly once he is within two steps, held for the play (the socket's own pick goes by which hand is
			// nearer the quarterback's chest, and changed hands as he reached the ball out). A toss in the air is drawn
			// from where it left his hands to the back's on a low arc, end over end. And from the sim's exchange the ball
			// goes from that hand into the back's arms over five frames, both ends live (it jumped from one man's socket
			// to the other's in a frame, 60-110 cm).
			if (Sim->meshOn() && Sim->isLive())
			{
				const tag::Ent* MeshRb = Sim->offRole(Sim->cfg.offPlay->run->carrier);
				if (!Sim->handedOff && B.holder == Sim->qb && MeshRb && Sim->tossT0 >= 0 && TossSerial == Sim->serial)
				{
					AAthlete* Ra = AthOf(MeshRb);
					FVector To;
					FRotator ToR;
					if (!Ra->GetBallHold(To, ToR)) To = Ra->HandsMid();
					const float S = FMath::Clamp(static_cast<float>((Sim->t - Sim->tossT0) / FMath::Max(0.05, Sim->mesh().tossDur)), 0.f, 1.f);
					HoldLoc = FMath::Lerp(TossHandW, To, S) + FVector(0.f, 0.f, 22.f * FMath::Sin(S * PI));
					HoldRot = (To - TossHandW).Rotation() + FRotator(-300.f * S, 0.f, 0.f);
				}
				else if (!Sim->handedOff && B.holder == Sim->qb && MeshRb && !Sim->mesh().toss)
				{
					AAthlete* Ra = AthOf(MeshRb);
					const FVector Belly = Ra->BoneWorld(TEXT("spine_04"));
					if (HandGiveSerial != Sim->serial && A->MeshPartner() && FVector::Dist2D(Belly, A->GetActorLocation()) < 170.f)
					{
						HandGiveSerial = Sim->serial;
						bHandGiveRight = FVector::DistSquared(A->BoneWorld(TEXT("hand_r")), Belly) < FVector::DistSquared(A->BoneWorld(TEXT("hand_l")), Belly);
					}
					if (HandGiveSerial == Sim->serial)
					{
						A->ForceBallHand(bHandGiveRight, 0.5f);
						A->GetBallHold(HoldLoc, HoldRot);
					}
					// The two-man capture's hands meet when its exchange comes, which can be a few frames ahead of the sim's
					// (filmed h2: the back's hands 27-38 cm from the ball 0.15 s before the sim's exchange, 100 cm off by the
					// time it came, then the ball crossed that in 0.17 s). Once the back's hands are at the ball in the last
					// quarter second before the exchange, the ball goes into them from there, on the same 0.17 s ease the
					// exchange itself uses (both ends live; the sim's exchange then carries on from where it has got to).
					// (and on the capture's own contact frame - the back's hands at the ball as filmed - when that comes first:
					// a draw's back waited in the pocket 0.5 s ahead of the sim's exchange, h6, hands 30 cm off the ball, and
					// the ball left 1.2 m behind when the sim's time came)
					if (HandXferSerial != Sim->serial && A->MeshPartner() == Ra && Sim->meshIn() >= 0.0 &&
						((Sim->meshIn() < 0.25 && FVector::Dist(HoldLoc, Ra->HandsMid()) < 50.f) || (Sim->meshIn() < 0.7 && FVector::Dist(HoldLoc, Ra->HandsMid()) < 35.f) ||
						 (Sim->meshIn() < 0.7 && Ra->MeshContactIn() <= 0.03f && FVector::Dist(HoldLoc, Ra->HandsMid()) < 60.f)))
					{
						HandXferSerial = Sim->serial;
						HandXferT = Sim->t;
						HandXferFrom = A;
					}
					FVector EarlyTo;
					FRotator EarlyToR;
					if (HandXferSerial == Sim->serial && Ra->GetBallHold(EarlyTo, EarlyToR))
					{
						const float S = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(static_cast<float>((Sim->t - HandXferT) / 0.17), 0.f, 1.f));
						HoldLoc = FMath::Lerp(HoldLoc, EarlyTo, S);
						HoldRot = FQuat::Slerp(HoldRot.Quaternion(), EarlyToR.Quaternion(), S).Rotator();
					}
				}
			}
			// the sim's exchange ahead of the capture's contact frame: the ball stays in the quarterback's hand until the
			// back's hands get there (0.3 s at most), then goes in on the same ease
			if (HandDeferSerial == Sim->serial && HandXferSerial != Sim->serial && B.holder != Sim->qb && Sim->qb)
				if (AAthlete* DefQ = AthOf(Sim->qb))
				{
					FVector DefP;
					FRotator DefR;
					const bool bDefGot = DefQ->GetBallHold(DefP, DefR);
					if (!bDefGot || A->MeshContactIn() <= 0.03f || Sim->t - HandDeferT > 0.3 || FVector::Dist(DefP, A->HandsMid()) < 35.f)
					{
						HandXferSerial = Sim->serial;
						HandXferT = Sim->t;
						HandXferFrom = DefQ;
					}
					else
					{
						HoldLoc = DefP;
						HoldRot = DefR;
					}
				}
			if (HandXferSerial == Sim->serial && B.holder != Sim->qb && Sim->t - HandXferT < 0.17)
				if (AAthlete* From = HandXferFrom.Get())
				{
					FVector P0;
					FRotator R0;
					if (From->GetBallHold(P0, R0))
					{
						const float S = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(static_cast<float>((Sim->t - HandXferT) / 0.17), 0.f, 1.f));
						HoldLoc = FMath::Lerp(P0, HoldLoc, S);
						HoldRot = FQuat::Slerp(R0.Quaternion(), HoldRot.Quaternion(), S).Rotator();
					}
				}
			// Out of the air (a pass, a kick) or off the turf (a loose ball) into his hands. The ball was put in his carrying
			// socket in the frame the sim gave it to him, from wherever its flight had got to: up to 1.5 m in one frame, the
			// catch from nowhere near him. What is left between the flight and his hold - from the point on the ball's own
			// path this frame nearest the hold, so a ball flying into his hands has nothing left - is eased out instead,
			// at 3 m/s or faster (0.08-0.35 s: a smooth step's peak is half again its mean, 15 cm a 30 fps frame at 3 m/s).
			static const bool bNoAttachEase = FParse::Param(FCommandLine::Get(), TEXT("TagNoBallBend"));
			if (!bNoAttachEase && BallDrawnSerial == Sim->serial && BallDrawnHolder != B.holder &&
				(BallDrawnMode == static_cast<int32>(tag::BallMode::Air) || BallDrawnMode == static_cast<int32>(tag::BallMode::Loose)))
			{
				const FVector CcAhead = BallDrawnW + BallDrawnVelW * Dt;
				AttachOff = FMath::ClosestPointOnSegment(HoldLoc, BallDrawnW, CcAhead) - HoldLoc;
				AttachDur = FMath::Clamp(static_cast<float>(AttachOff.Size()) / 300.f, 0.08f, 0.35f);
				AttachLeft = AttachDur;
				CcSnapNow = static_cast<float>(AttachOff.Size());
				BallBend = FVector::ZeroVector;
			}
			if (AttachLeft > 0.f)
			{
				HoldLoc += AttachOff * FMath::SmoothStep(0.f, 1.f, AttachLeft / FMath::Max(AttachDur, 0.01f));
				AttachLeft -= Dt;
			}
			Ball->SetActorLocation(HoldLoc);
			Ball->SetActorRotation(HoldRot);
			// -TagHandoffCheck: the run exchange measured frame by frame, from 0.6 s before the ball changes hands to 0.5 s
			// after ("TAG hcheck"): the ball off the quarterback's nearer hand and off the back's hands, the two bodies
			// apart (shown and in the sim), the back's speed as shown, and each man's turn in the frame (a pop is a spike)
			static const bool bHandoffCheck = FParse::Param(FCommandLine::Get(), TEXT("TagHandoffCheck"));
			if (bHandoffCheck && Sim->meshOn() && Sim->isLive() && Sim->qb && Dt > 1e-4f)
				if (const tag::Ent* HcRb = Sim->offRole(Sim->cfg.offPlay->run->carrier))
				{
					const double HcIn = Sim->meshIn();
					const double HcSince = Sim->meshDoneT >= 0 ? Sim->t - Sim->meshDoneT : -1.0;
					AAthlete* HcQ = AthOf(Sim->qb);
					AAthlete* HcB = AthOf(HcRb);
					static unsigned long long HcSerial = 0;
					static FVector HcPrevB = FVector::ZeroVector;
					static float HcPrevQYaw = 0.f, HcPrevBYaw = 0.f;
					if (HcQ && HcB && ((HcIn >= 0.0 && HcIn < 0.6) || (HcSince >= 0.0 && HcSince < 0.5)))
					{
						const bool bHcFirst = HcSerial != Sim->serial;
						HcSerial = Sim->serial;
						const FVector HcBall = Ball->GetActorLocation();
						const double HcQHand = FMath::Sqrt(FMath::Min(FVector::DistSquared(HcBall, HcQ->BoneWorld(TEXT("hand_r"))), FVector::DistSquared(HcBall, HcQ->BoneWorld(TEXT("hand_l")))));
						const FVector HcBLoc = HcB->GetActorLocation();
						const float HcQYaw = static_cast<float>(HcQ->GetActorRotation().Yaw), HcBYaw = static_cast<float>(HcB->GetActorRotation().Yaw);
						UE_LOG(LogTemp, Display, TEXT("TAG hcheck t=%.3f in=%.3f since=%.3f holder=%s ballQhand=%.1f ballBhands=%.1f apart=%.1f simApart=%.1f backV=%.2f backSimV=%.2f qbYawStep=%.1f backYawStep=%.1f toss=%d capture=%d"),
							Sim->t, HcIn, HcSince, B.holder == Sim->qb ? TEXT("qb") : TEXT("back"), HcQHand, FVector::Dist(HcBall, HcB->HandsMid()),
							FVector::Dist2D(HcQ->GetActorLocation(), HcBLoc), tag::dist(Sim->qb->pos, HcRb->pos) * YD,
							bHcFirst ? 0.f : FVector::Dist2D(HcBLoc, HcPrevB) / Dt / 100.f, tag::len(HcRb->vel) * 0.9144,
							bHcFirst ? 0.f : FMath::FindDeltaAngleDegrees(HcPrevQYaw, HcQYaw), bHcFirst ? 0.f : FMath::FindDeltaAngleDegrees(HcPrevBYaw, HcBYaw),
							Sim->mesh().toss ? 1 : 0, HandoffSerial == Sim->serial ? 1 : 0);
						HcPrevB = HcBLoc;
						HcPrevQYaw = HcQYaw;
						HcPrevBYaw = HcBYaw;
					}
				}
		}
		else
		{
			const FVector Fwd = A->GetActorForwardVector();
			const FVector Right = A->GetActorRightVector();
			const bool bDown = B.holder->anim == tag::Anim::Down;
			Ball->SetActorLocation(A->GetActorLocation() + Fwd * 16 + Right * 20 + FVector(0, 0, bDown ? 30 : 108));
			Ball->SetActorRotation(A->GetActorRotation());
		}
	}
	else if (bUmpHasBall && Umpire && !Sim->isLive())
	{
		FVector HoldLoc;
		FRotator HoldRot;
		if (Umpire->GetBallHold(HoldLoc, HoldRot)) { Ball->SetActorLocation(HoldLoc); Ball->SetActorRotation(HoldRot); }
	}
	else if (bUmpSpotted && Game && !Sim->isLive()) // (a live ball is never the umpire's: see StartLiveReturn)
	{
		// spotted for the next play: on the turf at the new line, laces up, nose downfield
		const int32 NewDir = Game->worldDir();
		Ball->SetActorLocation(ToWorld(Game->ballOn, Game->ballY, NewDir) + FVector(0, 0, 9));
		Ball->SetActorRotation(FRotator(0, NewDir > 0 ? 0 : 180, 0));
	}
	else
	{
		// (a pass is drawn out of the passer's hand: the sim launches it from his body's centre 2 m up; the gap closes
		// over its first 0.15 s)
		FVector At = ToWorld(B.pos.x, B.pos.y, Dir, B.pos.z);
		if (B.mode == tag::BallMode::Air && ReleaseSerial == Sim->serial && B.t < 0.15 && !Sim->cfg.kick)
		{
			if (ReleaseOffT < 0.0 || B.t < ReleaseOffT) ReleaseOff = ReleaseHandW - At; // (the first frame of the flight)
			ReleaseOffT = B.t;
			At += ReleaseOff * static_cast<float>(1.0 - B.t / 0.15);
		}
		else ReleaseOffT = -1.0;
		// Onto the hands. The sim brings a pass to within half a yard of the man its outcome was rolled for; where his
		// hands are in that half yard is the capture's and his body's (which may stand a step off its sim spot). Over the
		// last 0.25 s the drawn ball is brought the rest of the way: toward where his hands will be when it meets him
		// (the hands now, carried on with his run), by 45 cm at most and 3 m/s at most - a ball further off than that is
		// left to miss them, and the catch's ease (AttachOff) shows what was left. Let go at the same rate after a
		// deflection, so a tipped ball leaves from the hand that tipped it. -TagNoBallBend: the sim's ball as it is (A/B).
		{
			static const bool bNoBallBend = FParse::Param(FCommandLine::Get(), TEXT("TagNoBallBend"));
			FVector CcWant = FVector::ZeroVector;
			// (Filmed: at 45 cm / 3 m/s, weighted in from 0.25 s, the bend was 15-22 cm at the contact and the ball met a
			// pass catcher 30-46 cm from his hands. Bent at the hands as they were early on, though, it was pulled down to
			// a man's waist before his capture and the reach had brought them up to it. Now: only the last 0.15 s, when
			// the capture's hands are coming onto the ball, toward where it sits in them - his hold, the hand bone being
			// the wrist, 15 cm off a ball held - up to 35 cm, at 4.5 m/s: 15 cm a 30 fps frame, the most the step check
			// allows.)
			if (!bNoBallBend && CcWho && CcWhoIn > 0.0 && CcWhoIn <= 0.15 && B.mode == tag::BallMode::Air)
				if (AAthlete* CcA = AthOf(CcWho))
				{
					const float CcT = static_cast<float>(CcWhoIn);
					const FVector CcHl = CcA->BoneWorld(TEXT("hand_l")), CcHr = CcA->BoneWorld(TEXT("hand_r"));
					// (both hands when they are together on it; the nearer one for a one-hander or a swat)
					const bool bCcSwat = !Sim->cfg.kick && B.fate == 3;
					const bool bCcOne = bCcSwat || FVector::Dist(CcHl, CcHr) > 40.f;
					FVector CcHand = !bCcOne ? (CcHl + CcHr) * 0.5 : FVector::DistSquared(CcHl, At) < FVector::DistSquared(CcHr, At) ? CcHl : CcHr;
					FVector CcHoldW;
					FRotator CcHoldR;
					// (a one-hander's hold is that hand's; two hands on it take it between them - their hold is one hand's
					// socket, 10 cm to its side, and the ball was bent past the other hand's fingers to it)
					if (!bCcSwat && bCcOne && CcA->GetBallHold(CcHoldW, CcHoldR)) CcHand = CcHoldW;
					const FVector CcBodyV = Dir > 0 ? FVector(CcWho->vel.x, -CcWho->vel.y, 0) * YD : FVector(-CcWho->vel.x, CcWho->vel.y, 0) * YD;
					const FVector CcMeet = Sim->cfg.kick ? ToWorld(Sim->cfg.kick->landX, Sim->cfg.kick->landY, Dir, 1.3)
						: At + BallVelW * CcT - FVector(0, 0, 0.5 * tag::G * YD * CcT * CcT);
					CcWant = (CcHand + CcBodyV * CcT - CcMeet).GetClampedToMaxSize(35.f) * FMath::SmoothStep(0.f, 1.f, (0.15f - CcT) / 0.1f);
				}
			// (eased out the same way once the ball is off its flight - on the turf, dead - so it never jumps back onto the
			// sim's; a new play starts with none)
			BallBend = BallDrawnSerial == Sim->serial ? FMath::VInterpConstantTo(BallBend, CcWant, Dt, 450.f) : FVector::ZeroVector;
			At += BallBend;
		}
		// the rigid ball's own orientation (BallPhysics.h): a loose ball, a tipped pass turning over, a dead one bouncing on
		// (the sim's frame to the world's, as positions go: x and y swap sign with the direction of play)
		auto ToW = [this](const tag::V3& V) { return Dir > 0 ? FVector(V.x, -V.y, V.z) : FVector(-V.x, V.y, V.z); };
		const bool bBody = B.phys || B.tumble || B.mode == tag::BallMode::Loose;
		FRotator BodyRot = FRotator::ZeroRotator;
		if (bBody)
			BodyRot = FRotationMatrix::MakeFromXZ(ToW(tag::qrot(B.body.q, tag::V3{1, 0, 0})), ToW(tag::qrot(B.body.q, tag::V3{0, 0, 1}))).Rotator();
		// a fumble: in the carrier's arm until the strip capture punches it out, then eased onto the sim's loose ball
		bool bInArm = false;
		if (FumbleSerial == Sim->serial && B.mode != tag::BallMode::Air && B.holder == nullptr)
		{
			const float Since = static_cast<float>(GetWorld()->GetTimeSeconds() - FumbleWT);
			FVector Hand;
			FRotator HandRot;
			AAthlete* Fc = FumbleCarrier.Get();
			if (Fc && Since < FumbleOutIn && Fc->GetBallHold(Hand, HandRot))
			{
				FumbleHandW = Hand;
				Ball->SetActorLocationAndRotation(Hand, HandRot);
				bInArm = true;
			}
			else if (Since < FumbleOutIn + 0.25f)
				At = FMath::Lerp(FumbleHandW, At, FMath::SmoothStep(0.f, 1.f, (Since - FumbleOutIn) / 0.25f));
		}
		if (!bInArm)
		{
			Ball->SetActorLocation(At);
			const FVector V = Dir > 0 ? FVector(B.vel.x, -B.vel.y, B.vel.z) : FVector(-B.vel.x, B.vel.y, B.vel.z);
			if (bBody) Ball->SetActorRotation(BodyRot);
			else if (V.SizeSquared() > 0.01)
			{
				FRotator R = V.Rotation();
				R.Roll = FMath::RadiansToDegrees(B.spin);
				Ball->SetActorRotation(R);
			}
		}
	}
	if (Sim->isPlaceKick()) SyncPlaceBall(); // (before the foot: in the snapper's and the holder's hands)
	if (bLivePunt && Sim->t < Sim->cfg.kick->kickT && B.mode == tag::BallMode::Dead)
	{
		// the ball before the punt: under the snapper, back to the punter, in his hands, dropped onto the foot
		const float Tc = static_cast<float>(Sim->t) - PuntT0;
		const float FwdYaw = PuntFwd.Rotation().Yaw;
		// (no snapper found this frame: the ball at the line, not at the world's origin off camera)
		const FVector SnapAt = PuntSnapW.IsZero() ? ToWorld(Sim->cfg.kick->coverX + 0.6, tag::FIELD_W / 2, Dir) : PuntSnapW;
		const FVector Turf = SnapAt + PuntFwd * 45.f + FVector(0, 0, 11);
		const FVector Foot = ToWorld(Sim->cfg.kick->kickX, tag::FIELD_W / 2, Dir, 1.0);
		FVector Hands = PuntPt - PuntFwd * (PuntWalk + 90.f) + FVector(0, 0, 110);
		if (USkeletalMeshComponent* M = PuntMan ? PuntMan->Visible() : nullptr)
			if (M->GetBoneIndex(TEXT("hand_l")) != INDEX_NONE && M->GetBoneIndex(TEXT("hand_r")) != INDEX_NONE)
				Hands = 0.5 * (M->GetBoneLocation(TEXT("hand_l")) + M->GetBoneLocation(TEXT("hand_r")));
		FVector At = Turf;
		FRotator R(0, FwdYaw, 0);
		if (Tc >= PuntSnapAt && Tc < PuntCatchAt)
		{
			const float Al = (Tc - PuntSnapAt) / (PuntCatchAt - PuntSnapAt);
			At = FMath::Lerp(Turf + FVector(0, 0, 30), Hands, Al) + FVector(0, 0, 55.f * FMath::Sin(PI * Al));
			R = FRotator(0, FwdYaw, Tc * 1400.f);
		}
		else if (Tc >= PuntCatchAt)
		{
			const float Al = FMath::SmoothStep(0.f, 1.f, (Tc - (PuntKickAt - 0.2f)) / 0.2f);
			At = FMath::Lerp(Hands, Foot, Al);
			R = FRotator(-12.f, FwdYaw, 0);
		}
		Ball->SetActorLocationAndRotation(At, R);
	}
	// the ball as drawn this frame: where the next frame's catch ease starts from (AttachOff above, off this frame's
	// place and motion), and what -TagCatchCheck measures
	if (bCatchCheckOn) TickCatchCheck(Dt, CcBendWas, CcSnapNow);
	{
		const FVector CcDrawn = Ball->GetActorLocation();
		if (BallDrawnSerial != Sim->serial) BallDrawnVelW = BallVelW;
		else if (Dt > 1e-4f) BallDrawnVelW = (CcDrawn - BallDrawnW) / Dt;
		BallDrawnW = CcDrawn;
		BallDrawnSerial = Sim->serial;
		BallDrawnMode = static_cast<int32>(B.mode);
		BallDrawnHolder = B.mode == tag::BallMode::Held ? B.holder : nullptr;
	}
	DetectContacts();
	ResolveEngagedOverlap();
}

void AMatchDirector::TickCatchCheck(float Dt, float BendWas, float SnapNow)
{
	// -TagCatchCheck: at every contact the sim awards - a catch, an interception, a breakup, a drop, a batted pass, a
	// fielded kick, a loose ball recovered - the drawn ball against the hands of the man credited with it, in the frame
	// it happens: the nearer hand to the ball (now) and to the ball's path over this frame (path; at 30 fps a pass
	// covers 50-70 cm a frame). Then the nearest it comes to those hands over the next 0.15 s while it is not in them
	// (a breakup, a drop), and the largest off-path step of the drawn ball in the 0.3 s around the contact: how far from
	// where its own motion last frame was carrying it the ball is drawn (slowing or stopping on that path is 0; a ball
	// put somewhere else is the jump). One line a contact, with a running summary. Off by default.
	if (!Sim || !Ball || !Game) return;
	const tag::TeamKey OffK = Game->poss; // (as SyncPlay maps the sim's men to bodies)
	auto AthOf = [&](const tag::Ent* X) { return AthleteFor(X->p, X->side == tag::Side::Off ? OffK : tag::other(OffK)); };
	const tag::Ball& Bc = Sim->ball;
	const FVector P = Ball->GetActorLocation();
	const bool bSame = BallDrawnSerial == Sim->serial;
	static TArray<float> GCcHand, GCcStep;
	auto Pct = [](TArray<float> V, float Q) { if (!V.Num()) return 0.f; V.Sort(); return V[FMath::Clamp(static_cast<int32>(Q * (V.Num() - 1)), 0, V.Num() - 1)]; };
	auto Flush = [&](const FCatchCheck& C)
	{
		const float Hand = FMath::Min(C.HandNow, C.HandPath);
		GCcHand.Add(Hand);
		GCcStep.Add(C.MaxStep);
		UE_LOG(LogTemp, Display, TEXT("TAG catchcheck %s #%d: hand %.1f cm (now %.1f, path %.1f, nearest after %.1f) | socket gap %.1f bend %.1f | ball %.0f cm up %.0f cm/s | max step %.1f cm | hands mid %.1f"),
			*C.What, C.Num, Hand, C.HandNow, C.HandPath, C.HandMin < 1e8f ? C.HandMin : -1.f, C.Snap, C.Bend, C.BallZ, C.BallSpeed, C.MaxStep, C.HandMid);
		UE_LOG(LogTemp, Display, TEXT("TAG catchcheck summary: n %d | hand cm p50 %.1f p95 %.1f max %.1f | step cm p95 %.1f max %.1f"), GCcHand.Num(),
			Pct(GCcHand, 0.5f), Pct(GCcHand, 0.95f), Pct(GCcHand, 1.f), Pct(GCcStep, 0.95f), Pct(GCcStep, 1.f));
	};
	if (!bSame)
	{
		for (const FCatchCheck& C : CatchChecks) Flush(C);
		CatchChecks.Reset();
		BallSteps.Reset();
		CatchCheckClock = 0.0;
	}
	CatchCheckClock += Dt;
	float Step = 0.f;
	if (bSame && Dt > 1e-4f)
		Step = static_cast<float>(FVector::Dist(P, FMath::ClosestPointOnSegment(P, BallDrawnW, BallDrawnW + BallDrawnVelW * Dt)));
	// (in a man's hands the ball goes where they go - a catch's capture swings them up and in at 5-10 m/s: its step
	// then is against his hold, not its own last motion)
	static FVector GCcHoldWas = FVector::ZeroVector;
	static const tag::Ent* GCcHolderWas = nullptr;
	{
		FVector CcHoldNow = FVector::ZeroVector;
		FRotator CcHoldRot;
		AAthlete* Ha = Bc.mode == tag::BallMode::Held && Bc.holder ? AthOf(Bc.holder) : nullptr;
		const bool bHeldNow = Ha && Ha->GetBallHold(CcHoldNow, CcHoldRot);
		if (bSame && bHeldNow && GCcHolderWas == Bc.holder)
			Step = FMath::Min(Step, static_cast<float>(FVector::Dist(P - CcHoldNow, BallDrawnW - GCcHoldWas)));
		GCcHolderWas = bHeldNow ? Bc.holder : nullptr;
		if (bHeldNow) GCcHoldWas = CcHoldNow;
	}
	BallSteps.Add(FVector2D(CatchCheckClock, Step));
	while (BallSteps.Num() && BallSteps[0].X < CatchCheckClock - 0.3) BallSteps.RemoveAt(0);
	auto Hands = [&](const tag::Ent* E, FVector& L, FVector& R) -> bool
	{
		AAthlete* A = E ? AthOf(E) : nullptr;
		if (!A) return false;
		L = A->BoneWorld(TEXT("hand_l"));
		R = A->BoneWorld(TEXT("hand_r"));
		return true;
	};
	for (const TPair<const tag::Ent*, FString>& N : CatchCheckNew)
	{
		FCatchCheck C;
		C.Who = N.Key;
		C.What = Sim->cfg.kick && N.Value == TEXT("catch") ? FString(TEXT("kickcatch")) : N.Value;
		C.Num = N.Key && N.Key->p ? N.Key->p->number : -1;
		FVector L, R;
		if (!Hands(N.Key, L, R)) continue;
		C.HandNow = static_cast<float>(FMath::Min(FVector::Dist(L, P), FVector::Dist(R, P)));
		// (between two hands together on it, the middle of them: the hand bones are the wrists, 15 cm off a ball held)
		C.HandMid = FVector::Dist(L, R) < 45.f ? static_cast<float>(FVector::Dist((L + R) * 0.5, P)) : C.HandNow;
		C.HandPath = C.HandNow;
		if (bSame)
			C.HandPath = static_cast<float>(FMath::Min(FVector::Dist(L, FMath::ClosestPointOnSegment(L, BallDrawnW, P)),
				FVector::Dist(R, FMath::ClosestPointOnSegment(R, BallDrawnW, P))));
		C.Bend = BendWas;
		C.Snap = SnapNow;
		C.BallZ = static_cast<float>(P.Z);
		C.BallSpeed = static_cast<float>(BallDrawnVelW.Size());
		for (const FVector2D& S : BallSteps)
			if (S.X >= CatchCheckClock - 0.15) C.MaxStep = FMath::Max(C.MaxStep, static_cast<float>(S.Y));
		CatchChecks.Add(C);
	}
	CatchCheckNew.Reset();
	for (int32 i = CatchChecks.Num() - 1; i >= 0; --i)
	{
		FCatchCheck& C = CatchChecks[i];
		C.MaxStep = FMath::Max(C.MaxStep, Step);
		FVector L, R;
		if (!(Bc.mode == tag::BallMode::Held && Bc.holder == C.Who) && Hands(C.Who, L, R))
			C.HandMin = FMath::Min(C.HandMin, static_cast<float>(FMath::Min(FVector::Dist(L, P), FVector::Dist(R, P))));
		if ((C.Left -= Dt) <= 0.f)
		{
			Flush(C);
			CatchChecks.RemoveAt(i);
		}
	}
}

void AMatchDirector::DetectContacts()
{
	// (-TagNoImpact: the bodies are not pushed; the contacts are still found and reported, Game/TagContact.h)
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("TagNoImpact"));
	if (!GetWorld()) return;
	const double Now = GetWorld()->GetTimeSeconds();
	FTagContacts& Found = FTagContacts::Get();
	Found.BeginFrame(GFrameCounter);
	// each man's sim entity by player id (who a player drives; whether the two are in a tackle)
	TMap<int32, const tag::Ent*, TInlineSetAllocator<32>> EntOf;
	if (Sim)
		for (const tag::Ent* E : Sim->ents)
			if (E && E->p) EntOf.Add(E->p->id, E);
	TArray<AAthlete*, TInlineAllocator<64>> All;
	for (const auto& KV : Athletes)
		if (AAthlete* X = KV.Value.Get(); X && !X->IsHidden()) All.Add(X);
	for (int32 i = 0; i < All.Num(); ++i)
		for (int32 j = i + 1; j < All.Num(); ++j)
		{
			AAthlete* A1 = All[i];
			AAthlete* A2 = All[j];
			FVector N = A2->GetActorLocation() - A1->GetActorLocation();
			N.Z = 0.f;
			const float D = static_cast<float>(N.Size());
			// (torso to torso: two men's shoulders meet about 75 cm apart, centre to centre)
			if (D > 85.f || D < 1.f) continue;
			N /= D;
			const float Closing = static_cast<float>(FVector::DotProduct(A1->GetLastVel() - A2->GetLastVel(), N));
			if (Closing < 180.f) continue;
			const uint64 Key = (static_cast<uint64>(FMath::Min(A1->GetUniqueID(), A2->GetUniqueID())) << 32) | FMath::Max(A1->GetUniqueID(), A2->GetUniqueID());
			if (const double* U = ContactUntil.Find(Key); U && Now < *U) continue;
			ContactUntil.Add(Key, Now + 0.6);
			// the impulse of an inelastic meeting of two masses, restitution 0.15; a man in a capture of the contact (a
			// paired fall, a two-man rep) already moves with it, so he takes a third of the jolt on top
			const float M1 = A1->MassKg(), M2 = A2->MassKg();
			const float Jmp = 1.15f * FMath::Min(Closing, 900.f) / (1.f / M1 + 1.f / M2);
			const float S1 = A1->IsFalling() || A1->IsDown() ? 0.33f : 1.f, S2 = A2->IsFalling() || A2->IsDown() ? 0.33f : 1.f;
			if (!bOff)
			{
				A1->AddImpact(-N * (Jmp / M1) * S1);
				A2->AddImpact(N * (Jmp / M2) * S2);
			}
			++ContactsThisPlay;
			MaxJoltThisPlay = FMath::Max(MaxJoltThisPlay, FMath::Max(Jmp / M1 * S1, Jmp / M2 * S2));
			UE_LOG(LogTemp, Verbose, TEXT("TAG contact #%d / #%d closing %.0f cm/s: %.0f / %.0f cm/s"), A1->Player ? A1->Player->number : -1, A2->Player ? A2->Player->number : -1,
				Closing, Jmp / M1 * S1, Jmp / M2 * S2);
			// the contact, for whoever reads it (Game/TagContact.h: the body reactions, the sound, the hit-stop)
			FTagContact C;
			C.SimT = Sim ? Sim->t : 0.0;
			C.PlaySerial = Sim ? Sim->serial : 0;
			C.IdA = A1->Player ? A1->Player->id : -1;
			C.IdB = A2->Player ? A2->Player->id : -1;
			C.AthA = A1;
			C.AthB = A2;
			C.Pos = 0.5f * (A1->GetActorLocation() + A2->GetActorLocation());
			C.Normal = N;
			C.ClosingSpeed = Closing;
			C.Impulse = Jmp;
			C.DvA = Jmp / M1 * S1;
			C.DvB = Jmp / M2 * S2;
			C.bImpactApplied = !bOff;
			const tag::Ent* const* EA = EntOf.Find(C.IdA);
			const tag::Ent* const* EB = EntOf.Find(C.IdB);
			if (Sim && EA && EB)
			{
				C.SlotA = (*EA)->userControlled ? Sim->slotOf(*EA) : -1;
				C.SlotB = (*EB)->userControlled ? Sim->slotOf(*EB) : -1;
				C.bHuman = C.SlotA >= 0 || C.SlotB >= 0;
				// a tackle: the sim's tackle (wrap, big hit, truck, assist) between these two in the last 0.35 s
				for (int32 e = static_cast<int32>(Sim->events.size()) - 1; e >= 0 && !C.bTackle; --e)
				{
					const tag::PlayEvent& V = Sim->events[e];
					if (V.t < Sim->t - 0.35) break;
					const bool bPair = (V.a == *EA && V.b == *EB) || (V.a == *EB && V.b == *EA);
					C.bTackle = bPair && (V.type == "tackle" || V.type == "wrap" || V.type == "bighit" || V.type == "trucked" || V.type == "assist");
				}
			}
			C.bTackle = C.bTackle || (A1->IsFalling() && A2->IsFalling());
			Found.Add(C);
		}
	if (ContactUntil.Num() > 512)
		for (auto It = ContactUntil.CreateIterator(); It; ++It) if (Now > It->Value) It.RemoveCurrent();
	OnContactsFound(); // (the sound of them, the hit-stop on a player's big hit: Game/MatchFeel.cpp)
}

void AMatchDirector::ResolveEngagedOverlap()
{
	// every locked-up pair (the blocker holds the partner link both ways: each pair once, by address)
	TArray<AAthlete*, TInlineAllocator<64>> All;
	for (const auto& KV : Athletes) if (AAthlete* X = KV.Value.Get()) All.Add(X);
	for (AAthlete* X : Lineup) if (X) All.AddUnique(X); // the look-dev scenes' men
	// any two men standing near each other (a locked-up pair, a rusher ripping past the blocker's shoulder, a crossing
	// route through traffic): upper bodies clear each other; falls and tackles keep their own contact
	TArray<AAthlete*, TInlineAllocator<64>> Every = All;
	All.RemoveAll([](AAthlete* X) { return X->IsHidden() || X->IsFalling() || X->IsDown() || X->GetAnimName() == TEXT("tackle") || X->GetAnimName() == TEXT("dive"); });
	TMap<AAthlete*, FVector> Push;
	for (int32 i = 0; i < All.Num(); ++i)
	for (int32 j = i + 1; j < All.Num(); ++j)
	{
		AAthlete* A = All[i];
		AAthlete* B = All[j];
		if (FVector::DistSquared2D(A->GetActorLocation(), B->GetActorLocation()) > 160.f * 160.f) continue;
		const FVector CA = A->BoneWorld(TEXT("spine_04")), CB = B->BoneWorld(TEXT("spine_04"));
		const FVector HA = A->BoneWorld(TEXT("head")), HB = B->BoneWorld(TEXT("head"));
		FVector Sep = A->GetActorLocation() - B->GetActorLocation();
		Sep.Z = 0.f;
		if (Sep.IsNearlyZero()) continue;
		Sep.Normalize();
		// how far each pair of parts is inside its clearance (chest to chest 52 cm, a helmet to the other chest 38 cm)
		// (and hips 48 cm: two men each carrying his follow lag toward the other stood inside one another with chests
		// that happened to lean apart - 1.4 % of live frames with pelvises under 30 cm in the p24 game)
		const float In = FMath::Max(FMath::Max3(52.f - FVector::Dist2D(CA, CB), 38.f - FVector::Dist2D(HA, CB), 38.f - FVector::Dist2D(HB, CA)),
			48.f - static_cast<float>(FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation())));
		if (In <= 0.f) continue;
		const FVector Off = Sep * FMath::Min(In, 60.f) * 0.5f;
		Push.FindOrAdd(A) += Off;
		Push.FindOrAdd(B) -= Off;
	}
	// the push comes on and goes off at a step's pace (1.5 m/s): applied whole each frame, a man jumped up to 57 cm the
	// frame another body came into or left his space (a rusher at the end of his win, p38 clip 46)
	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	for (AAthlete* X : Every)
	{
		const FVector* Want = Push.Find(X);
		X->SepNow = FMath::VInterpConstantTo(X->SepNow, Want ? Want->GetClampedToMaxSize(60.f) : FVector::ZeroVector, Dt, 150.f);
		if (!X->SepNow.IsNearlyZero(0.1f) && Want) X->AddActorWorldOffset(X->SepNow);
		else if (!Want && !X->SepNow.IsNearlyZero(0.1f) && !X->IsFalling() && !X->IsDown()) X->AddActorWorldOffset(X->SepNow);
	}
}

FVector AMatchDirector::FocusPoint() const
{
	if (!Sim) return IdleFocus;
	const tag::Ball& B = Sim->ball;
	double X = B.holder ? B.holder->pos.x : B.pos.x;
	double Y = B.holder ? B.holder->pos.y : B.pos.y;
	// the run exchange: the framing goes with the ball from the quarterback to the back - along a toss while it is in
	// the air, over a quarter second at a handoff - and not from one man's feet to the other's in a frame (a yard at a
	// handoff, 2-4 at a toss)
	if (Sim->meshOn() && Sim->qb && Sim->isLive())
	{
		const tag::Ent* Rb = Sim->offRole(Sim->cfg.offPlay->run->carrier);
		double S = -1.0;
		if (Rb && !Sim->handedOff && Sim->tossT0 >= 0 && B.holder == Sim->qb) S = FMath::Clamp((Sim->t - Sim->tossT0) / FMath::Max(0.05, Sim->mesh().tossDur), 0.0, 1.0);
		else if (Rb && Sim->handedOff && Sim->meshDoneT >= 0 && Sim->t - Sim->meshDoneT < 0.25 && B.holder == Rb)
			S = FMath::SmoothStep(0.0, 1.0, (Sim->t - Sim->meshDoneT) / 0.25);
		if (S >= 0.0)
		{
			X = FMath::Lerp(Sim->qb->pos.x, Rb->pos.x, S);
			Y = FMath::Lerp(Sim->qb->pos.y, Rb->pos.y, S);
		}
	}
	return ToWorld(X, Y, Dir);
}

void AMatchDirector::UpdateCamera(float Dt, const FVector& InFocus)
{
	if (bTackleTest || bLineTest) return; // the look-dev scenes frame their own shots
	const double Y = YD;
	// a dead ball keeps flying (an overthrow out of the back of the end zone, a kick into the stands): the operators
	// stay on the field, so the framing never follows it more than a few yards past the end lines and sidelines
	const FVector Focus(FMath::Clamp(InFocus.X, -8.0 * Y, 108.0 * Y), FMath::Clamp(InFocus.Y, -(tag::FIELD_W / 2 + 3) * Y, (tag::FIELD_W / 2 + 3) * Y), InFocus.Z);
	FVector T, Look;
	const double D = static_cast<double>(Dir);
	switch (CamMode)
	{
	case ECamMode::Broadcast:
	{
		// play art up: the camera rises and looks further downfield so the whole route tree fits
		const double A = State == EMatchState::Presnap && PlayArt ? static_cast<double>(PlayArt->Alpha()) : 0.0;
		T = FVector(Focus.X - D * (17 + 1.5 * A) * Y, Focus.Y * 0.45, (10.5 + 4 * A) * Y);
		// backed up against an end line the camera cannot get its distance and climbs instead (below): it then looks
		// down steeply, and a look point 7 yd ahead left the ball at the bottom edge of the frame (a returner fielding
		// a kickoff at the goal line stood half out of the picture). Look less far ahead by what the camera lost.
		const double Lost = FMath::Max(0.0, FMath::Max(-15.0 * Y - T.X, T.X - 115.0 * Y)) / Y;
		Look = FVector(Focus.X + D * (FMath::Max(1.0, 7.0 - 0.4 * Lost) + 3.5 * A) * Y, Focus.Y * 0.8, 0);
		break;
	}
	case ECamMode::Sideline:
	{
		// the TV game camera: a fixed position high on the near side, at midfield or level with either 20 (where a
		// network puts its three game cameras). The operator pans and zooms; the camera itself never travels. The
		// position is picked while the ball is dead (nearest to the ball, with some hold so it does not flip between
		// plays near a boundary) and a change of position is a cut.
		// 22 yd behind the sideline and 13 yd up: 13 rows up the lower bowl (its 33 degree rake starts 9 yd out; the
		// sight line to the near sideline clears the heads in front), which looks down on midfield at 15 degrees, as a
		// network's game camera does. (12 yd back and 17 up, the old dolly position, read as an overhead coaches' film.)
		static float Back = FParse::Value(FCommandLine::Get(), TEXT("TagTvBack="), Back) ? Back : 22.f;
		static float High = FParse::Value(FCommandLine::Get(), TEXT("TagTvHigh="), High) ? High : 13.f;
		float Best = CamStationX;
		if (State == EMatchState::Kick || (Sim && Sim->isReturn())) Best = 50.f; // a kick covers the whole field: the midfield camera
		else if (CamStationX < 0.f || (State == EMatchState::Presnap && Sim))
		{
			// (not at the whistle: the camera that showed the play stays on the tackle and the walk back)
			const float FX = static_cast<float>(Focus.X / Y);
			for (float S : {22.f, 50.f, 78.f})
				if (Best < 0.f || FMath::Abs(FX - S) + 5.f < FMath::Abs(FX - Best)) Best = S;
		}
		if (Best != CamStationX)
		{
			CamStationX = Best;
			LastCamMode = ECamMode::Player; // a cut (below)
		}
		T = FVector(CamStationX * Y, -(tag::FIELD_W / 2 + Back) * Y, High * Y);
		Look = FVector(Focus.X + D * 1.5 * Y, Focus.Y * 0.9, 0);
		break;
	}
	case ECamMode::Skycam:
		T = FVector(Focus.X - D * 22 * Y, Focus.Y * 0.3, 30 * Y);
		Look = FVector(Focus.X + D * 10 * Y, Focus.Y * 0.6, 0);
		break;
	case ECamMode::Player:
	{
		FVector F = Focus;
		if (Sim && Sim->user) F = ToWorld(Sim->user->pos.x, Sim->user->pos.y, Dir);
		T = FVector(F.X - D * 7 * Y, F.Y, 3.4 * Y);
		Look = FVector(F.X + D * 8 * Y, F.Y, 1 * Y);
		break;
	}
	}
	// a break in the broadcast: the wide shot (its own slow move, no game framing; cut to on its first frame)
	BreakCamera(T, Look);
	// before the snap, the art's trigger held and the left stick pushed: the camera swings along the line to show the
	// edges of the field (the receivers split wide, the corners and safeties over them), and comes back when let go
	{
		PreLook = FMath::FInterpTo(PreLook, State == EMatchState::Presnap ? PreLookWant : 0.f, Dt, 5.f);
		if (FMath::Abs(PreLook) > 0.004f)
		{
			FVector Fw = Look - T;
			Fw.Z = 0.0;
			Fw.Normalize();
			const FVector Rt(-Fw.Y, Fw.X, 0.0); // the screen's right on the ground (as the left stick reads)
			Look += Rt * (PreLook * 17.0 * Y);
			if (CamMode != ECamMode::Sideline) T += Rt * (PreLook * 6.0 * Y);
		}
	}
	// a user kick being aimed: over the kicker's shoulder (Game/MatchKicking.cpp)
	const bool bKickAim = KickAimCamera(T, Look);
	// keep cameras over the field / in front of the stands (never inside geometry); backed up against an end line
	// the broadcast camera can't get its distance, so it climbs instead to keep the backfield in the frame
	// (the camera behind the offense goes out over the end zone and the apron behind the end line - nothing stands there
	// short of the seats, 9 yd past it - so a team backed up on its own 5 is shown the way it is at midfield. Held at
	// 6 yd deep in the end zone it climbed to make up the distance and looked nearly straight down on the formation, the
	// backfield at the bottom edge: unwatchable with 80+ yd to go.)
	const double WantX = T.X;
	const double Deep = CamMode == ECamMode::Broadcast && !bKickAim ? 15.0 : 6.0;
	T.X = FMath::Clamp(T.X, -Deep * Y, (100.0 + Deep) * Y);
	if (CamMode == ECamMode::Broadcast) T.Z += FMath::Abs(WantX - T.X) * 0.5;
	if (CamMode != ECamMode::Sideline) T.Y = FMath::Clamp(T.Y, -(tag::FIELD_W / 2 + 12) * Y, (tag::FIELD_W / 2 + 12) * Y);
	const bool bTv = CamMode == ECamMode::Sideline;
	if (CamMode != LastCamMode || bCutNext)
	{
		MarkCameraCut(bCutNext ? TEXT("cut") : LastCamMode == ECamMode::Player && bTv ? TEXT("tv station") : TEXT("camera mode"));
		bCutNext = false;
		LastCamMode = CamMode;
		CamPos = T; // cut, don't swoop, when the camera changes
		CamLook = Look;
		CamVel = LookVel = FVector::ZeroVector;
		CamFocalVel = 0.f;
		if (bTv) CamFocal = -1.f; // the lens starts at its framing too (set below)
	}
	// lead the ball the way an operator does: frame where the play is going, not where it was
	if (Sim && CamMode != ECamMode::Player)
	{
		// the play's velocity: the carrier's while the ball is held (a held ball's own velocity is not kept up), else the ball's
		const tag::Ball& B = Sim->ball;
		const double VX = B.mode == tag::BallMode::Held && B.holder ? B.holder->vel.x : B.vel.x;
		const double VY = B.mode == tag::BallMode::Held && B.holder ? B.holder->vel.y : B.vel.y;
		const FVector BV = (Dir > 0 ? FVector(VX, -VY, 0) * Y : FVector(-VX, VY, 0) * Y).GetClampedToMaxSize(12.f * Y);
		Look += (BV * 0.35f).GetClampedToMaxSize(6.f * Y);
		// a critically damped spring trails a target moving at v by ~1.44 x halflife x v (4.5 m at 9 m/s for the dolly):
		// feed that much of the velocity forward while the play is live, so a long run stays in the frame
		if (Sim->isLive() || Sim->isReturn())
		{
			if (!bTv) T += BV * (1.44f * 0.35f); // (the TV camera stays on its platform)
			Look += BV * (1.44f * 0.22f);
		}
	}
	// critically damped moves (no overshoot, no linear-lerp drag): ~0.35 s to settle the dolly, ~0.25 s the pan
	auto Spring = [Dt](FVector& X, FVector& V, const FVector& Goal, float Halflife)
	{
		const float W = 1.3863f / FMath::Max(Halflife, 0.01f); // ln(4) / halflife
		const FVector J0 = X - Goal, J1 = V + J0 * W;
		const float E = FMath::Exp(-W * Dt);
		X = Goal + (J0 + J1 * Dt) * E;
		V = (V - J1 * W * Dt) * E;
	};
	// the TV camera between plays: what it is asked to show is suddenly somewhere else (the teams set for a kick 40 yd
	// from the last whistle) and it cuts there; panned, it swept the turf for half a second
	// (not under a broadcast shot: the shot sets CamLook, and the game's own look far from it read as a reframe every frame)
	if (bTv && !ShotOnScreen() && FVector::Dist2D(Look, CamLook) > 15.f * Y && !(Sim && (Sim->isLive() || Sim->isReturn())))
	{
		MarkCameraCut(TEXT("tv reframe"));
		CamLook = Look;
		LookVel = FVector::ZeroVector;
		CamFocal = -1.f;
		CamFocalVel = 0.f;
	}
	Spring(CamPos, CamVel, T, 0.35f);
	Spring(CamLook, LookVel, Look, 0.22f);
	FVector P = CamPos;
	CamTime += Dt;
	// lens: zoom to the action (players within 18 yd of the focus) so a pile fills the frame and a breakaway does
	// not shrink to a speck; wider when the ball is in the air
	float WantFocal = CamMode == ECamMode::Sideline ? 45.f : CamMode == ECamMode::Player ? 24.f : 30.f;
	if (CamMode == ECamMode::Sideline || CamMode == ECamMode::Broadcast)
	{
		float Spread = 6.f * Y;
		for (const auto& KV : Athletes)
		{
			const AAthlete* O = KV.Value;
			if (!O || O->IsHidden()) continue;
			const float Dd = FVector::Dist2D(O->GetActorLocation(), Look);
			if (Dd < 18.f * Y) Spread = FMath::Max(Spread, Dd);
		}
		if (Sim && Sim->ball.mode == tag::BallMode::Air) Spread = FMath::Max(Spread, 16.f * Y);
		WantFocal = FMath::Clamp(WantFocal * (11.f * Y) / Spread, WantFocal * 0.75f, WantFocal * 1.9f);
		if (bTv)
		{
			// the zoom lens of a fixed camera: frame a set width of field at the look point, whatever the distance.
			// Across the screen is along the field, so the width comes from how far along the field the men near
			// the ball stand: the formation before the snap (the backfield to the linebackers; the deep safeties
			// can fall off the edge, as on TV), the ball carrier and the men around him after it.
			float Half = 9.f * Y;
			for (const auto& KV : Athletes)
			{
				const AAthlete* O = KV.Value;
				if (!O || O->IsHidden()) continue;
				const FVector L = O->GetActorLocation();
				if (FVector::Dist2D(L, Look) < 16.f * Y) Half = FMath::Max(Half, FMath::Abs(static_cast<float>(L.X - Look.X)) + 2.f * Y);
			}
			// before the snap one framing, held (the formation); in the play the lens follows the men around the ball
			float Width = State == EMatchState::Presnap || State == EMatchState::Call ? 25.f * Y : FMath::Clamp(2.f * Half, 22.f * Y, 32.f * Y);
			if (Sim && Sim->ball.mode == tag::BallMode::Air) Width = FMath::Max(Width, 30.f * Y); // a pass or a kick in the air: pull back
			if (State == EMatchState::Kick) Width = 44.f * Y;
			float Sensor = 24.89f;
			if (const UCineCameraComponent* C = Camera->GetCineCameraComponent()) Sensor = C->Filmback.SensorWidth;
			WantFocal = FMath::Clamp(Sensor * static_cast<float>((Look - T).Size()) / Width, 30.f, 220.f);
			if (CamFocal < 0.f) CamFocal = WantFocal;
		}
	}
	if (bKickAim) { WantFocal = 32.f; CamFocal = WantFocal; CamFocalVel = 0.f; } // (a fixed lens behind the kicker)
	if (BreakLeft > 0.f || bSetAfterBreak) WantFocal = 22.f; // (the break's wide shot)
	{
		const float W = 1.3863f / 0.6f, J0 = CamFocal - WantFocal, J1 = CamFocalVel + J0 * W, E = FMath::Exp(-W * Dt);
		CamFocal = WantFocal + (J0 + J1 * Dt) * E;
		CamFocalVel = (CamFocalVel - J1 * W * Dt) * E;
	}
	// handheld drift for the field-level cameras (low frequency, a few cm; the booth and skycam stay rock steady)
	FVector Hand = FVector::ZeroVector;
	if (CamMode == ECamMode::Player) // (the TV game camera is on a tripod head)
		Hand = FVector(FMath::PerlinNoise1D(CamTime * 0.31f), FMath::PerlinNoise1D(CamTime * 0.27f + 17.f), FMath::PerlinNoise1D(CamTime * 0.23f + 41.f)) * 6.f;
	P += Hand;
	// the shake (a big hit, a sack, a touchdown's sway): smooth noise off a decaying trauma (Game/MatchFeel.cpp)
	FRotator ShakeRot = FRotator::ZeroRotator;
	P += CameraShake(Dt, ShakeRot);
	if (bAutoEstablish && GAutoCoach)
	{
		const FVector C(46 * Y, (tag::FIELD_W / 2 + 1.7) * Y, 0);
		P = C + FVector(-5 * Y, -11 * Y, 2.2 * Y);
		CamLook = C + FVector(0, 0, 1.1 * Y);
	}
	else if (bAutoEstablish && GAutoChains && Stadium)
	{
		const FVector B = Stadium->DownBoxLocation();
		P = B + FVector(-4 * Y, -14 * Y, 3.5 * Y);
		CamLook = B + FVector(4 * Y, 0, 1.2 * Y);
	}
	else if (bAutoEstablish && GAutoBoard)
	{
		P = FVector(40 * Y, -12 * Y, 12 * Y);
		CamLook = FVector(-76 * Y, 0, 72 * Y);
	}
	else if (bAutoEstablish)
	{
		P = FVector(50 * Y, -30 * Y, 22 * Y);
		CamLook = FVector(50 * Y, 70 * Y, 14 * Y);
	}
	else if (!CloseupTarget && ShotCamera(Dt, P, T, Look)) {} // (a broadcast shot: the lead, a capper, the wide reset, the open - MatchBroadcast.cpp)
	else if (!CloseupTarget && CutInTarget && CutInLeft > 0.f)
	{
		CutInLeft -= Dt;
		const AAthlete* CT = Cast<AAthlete>(CutInTarget.Get());
		// what the lens holds: between his head and his hips (standing, his chest; lying in the pile, the pile) - the
		// actor's own spot sat 1.2 m over a man on the turf, and his facing spun as he rolled
		const FVector Tgt = CT ? (CT->BoneWorld(TEXT("head")) * 0.55f + CT->BoneWorld(TEXT("pelvis")) * 0.45f) : CutInTarget->GetActorLocation() + FVector(0, 0, 120);
		auto Clear = [&](const FVector& C, const FVector& At)
		{
			if (!(FMath::Abs(C.Y) < (tag::FIELD_W / 2) * Y - 120.f && C.X > -2.0 * Y && C.X < 102.0 * Y)) return false;
			// (nobody at the lens or on the sight line: a long lens turns a man a stride away into a white blur)
			for (const auto& KV : Athletes)
			{
				const AAthlete* O = KV.Value;
				if (!O || O == CutInTarget || O->IsHidden()) continue;
				const FVector Q = O->GetActorLocation() + FVector(0, 0, 100);
				if (FVector::Dist2D(Q, C) < 260.f || (FVector::Dist2D(Q, At) > 150.f && FMath::PointDistToSegment(Q, C, At) < 60.f)) return false;
			}
			return true;
		};
		// placed once (and once more at most, as a cut to a second camera when the first is blocked for half a second):
		// in front of him, on the game camera's side of the field so the cut does not flip the picture, 9.4 m off on
		// an 85 mm lens, at shoulder height. Per frame it only pans; it trails him if he walks off.
		auto Place = [&]()
		{
			FVector Face = CutInTarget->GetActorForwardVector();
			Face.Z = 0.f;
			Face = Face.GetSafeNormal();
			FVector ToCam = CamPos - Tgt;
			ToCam.Z = 0.f;
			ToCam = ToCam.GetSafeNormal();
			float BestS = -1e9f;
			FVector BestOff = Face * 940.f;
			for (int32 k = 0; k < 12; ++k)
			{
				const FVector D = Face.RotateAngleAxis(k * 30.f, FVector::UpVector);
				const FVector C = FVector(Tgt.X, Tgt.Y, 0.f) + D * 940.f + FVector(0, 0, 150.f);
				const float S = static_cast<float>(FVector::DotProduct(D, Face) + 0.7f * FVector::DotProduct(D, ToCam)) + (Clear(C, Tgt) ? 10.f : 0.f);
				if (S > BestS) { BestS = S; BestOff = D * 940.f; }
			}
			CutCamPos = FVector(Tgt.X, Tgt.Y, 0.f) + BestOff + FVector(0, 0, 150.f);
			CutCamVel = CutLookVel = FVector::ZeroVector;
			CutLook = Tgt;
			CutBlockedT = 0.f;
			bCutInPlaced = true;
			MarkCameraCut(TEXT("cut-in"));
		};
		if (!bCutInPlaced) Place();
		const FVector Off = CutCamPos - FVector(CutLook.X, CutLook.Y, 0.f);
		Spring(CutLook, CutLookVel, Tgt, 0.3f);
		Spring(CutCamPos, CutCamVel, FVector(CutLook.X + Off.X, CutLook.Y + Off.Y, CutCamPos.Z), 1.4f);
		CutBlockedT = Clear(CutCamPos, CutLook) ? 0.f : CutBlockedT + Dt;
		if (CutBlockedT > 0.5f && CutRecuts < 1 && CutInLeft > 1.2f) { ++CutRecuts; Place(); }
		P = CutCamPos + FVector(FMath::PerlinNoise1D(CamTime * 0.4f), FMath::PerlinNoise1D(CamTime * 0.35f + 9.f), FMath::PerlinNoise1D(CamTime * 0.3f + 23.f) * 0.5f) * 4.f;
		CamLook = CutLook;
		{
			// steadiness (logged per shot): how fast the view direction swings, and the lens's own travel
			static FVector PrevDir = FVector::ZeroVector, PrevP = FVector::ZeroVector;
			static float MaxPan = 0.f, SumPan = 0.f, MaxMove = 0.f;
			static int32 NPan = 0;
			const FVector Vd = (CamLook - P).GetSafeNormal();
			if (!PrevDir.IsZero() && Dt > 0.f && CutInLeft + Dt < 9.f)
			{
				const float Pan = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Vd, PrevDir)), -1.f, 1.f))) / Dt;
				MaxPan = FMath::Max(MaxPan, Pan);
				SumPan += Pan;
				++NPan;
				MaxMove = FMath::Max(MaxMove, static_cast<float>(FVector::Dist(P, PrevP)) / Dt);
			}
			PrevDir = Vd;
			PrevP = P;
			if (CutInLeft <= 0.f)
			{
				UE_LOG(LogTemp, Display, TEXT("TAG cut-in done: pan max %.1f mean %.1f deg/s, camera max %.0f cm/s, recuts %d"), MaxPan, NPan ? SumPan / NPan : 0.f, MaxMove, CutRecuts);
				PrevDir = FVector::ZeroVector;
				MaxPan = SumPan = MaxMove = 0.f;
				NPan = 0;
			}
		}
		static int32 GCutShots = 0;
		static const bool bShots = FParse::Param(FCommandLine::Get(), TEXT("TagCutIns"));
		if (bShots && CutInLeft < 1.6f && CutInLeft + Dt >= 1.6f && GCutShots < 6)
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_cutin_%d.png"), GCutShots++), false, false);
		if (CutInLeft <= 0.f) { CutInTarget = nullptr; bCutNext = true; } // (a cut back to the game camera, next frame)
	}
	else if (CloseupTarget)
	{
		const FVector Tgt = CloseupTarget->GetActorLocation() + FVector(0, 0, 140);
		const FVector F = CloseupTarget->GetActorForwardVector();
		const FVector R = CloseupTarget->GetActorRightVector();
		if (CloseupView == 1)
		{
			P = Tgt + F * 380.f + R * 120.f - FVector(0, 0, 30);
			CamLook = Tgt - FVector(0, 0, 60);
		}
		else if (CloseupView == 2)
		{
			P = Tgt - F * 260.f + R * 70.f;
			CamLook = Tgt - FVector(0, 0, 25);
		}
		else
		{
			P = Tgt + F * 230.f + R * 90.f + FVector(0, 0, 10);
			CamLook = Tgt - FVector(0, 0, 25);
		}
		// broadcast operators reframe around bodies: orbit the camera about the target until no other visible
		// athlete sits on the camera or the camera-to-target line (catches are usually contested)
		auto Blocked = [&](const FVector& C)
		{
			// (off the playing field counts as blocked: the benches, the crew and the wall are there)
			if (FMath::Abs(C.Y) > (tag::FIELD_W / 2) * Y - 120.f || C.X < -2.0 * Y || C.X > 102.0 * Y) return true;
			for (const AAthlete* O : Crew)
				if (O && !O->IsHidden() && FMath::PointDistToSegment(O->GetActorLocation() + FVector(0, 0, 100), C, Tgt) < 70.f) return true;
			if (Umpire && !Umpire->IsHidden() && FMath::PointDistToSegment(Umpire->GetActorLocation() + FVector(0, 0, 100), C, Tgt) < 70.f) return true;
			for (const auto& KV : Athletes)
			{
				const AAthlete* O = KV.Value;
				if (!O || O == CloseupTarget || O->IsHidden()) continue;
				const FVector Q = O->GetActorLocation() + FVector(0, 0, 100);
				// (on the sight line, or simply standing at the lens: two backs a stride from the camera filled the frame)
				if (FMath::PointDistToSegment(Q, C, Tgt) < 70.f || FVector::Dist2D(Q, C) < 230.f) return true;
			}
			return false;
		};
		if (Blocked(P))
		{
			const FVector Off = P - Tgt;
			bool bClear = false;
			for (float Deg : {35.f, -35.f, 70.f, -70.f, 110.f, -110.f, 150.f, -150.f})
			{
				const FVector Cand = Tgt + Off.RotateAngleAxis(Deg, FVector::UpVector) + FVector(0, 0, 40);
				if (!Blocked(Cand)) { P = Cand; bClear = true; break; }
			}
			// in a crowd of bodies nothing at eye level is clear: the high angle, over their heads, from the field's side
			if (!bClear)
			{
				const FVector In = FVector(0, Tgt.Y >= 0.0 ? -1.0 : 1.0, 0);
				P = Tgt + In * 430.f + FVector(0, 0, 210.f);
			}
		}
	}
	// an operator never loses the ball carrier: the springs above trail him and lead the play, and a returner who fields
	// a kick deep and runs at the climbed camera (c085, to6: 76 frames under the bottom edge as he got up to speed)
	// outran the tilt. Past 72 % of the half frame the pan / tilt turns just enough to hold him there.
	if (Sim && CamMode != ECamMode::Player && !bAutoEstablish && !CloseupTarget && !(CutInTarget && CutInLeft > 0.f) && !ShotOnScreen() && !bKickAim &&
		BreakLeft <= 0.f && !bSetAfterBreak && Game && (Sim->isLive() || Sim->isReturn()) && Sim->ball.mode == tag::BallMode::Held && Sim->ball.holder)
	{
		const tag::Ent* H = Sim->ball.holder;
		const AAthlete* HA = AthleteFor(H->p, H->side == tag::Side::Off ? Game->poss : tag::other(Game->poss));
		if (HA && !HA->IsHidden())
		{
			FVector2D VP(16.0, 9.0);
			if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(VP);
			const double TanH = 18.0 / FMath::Max(static_cast<double>(CamFocal), 1.0); // (half the 36 mm filmback)
			const double TanV = TanH * VP.Y / FMath::Max(VP.X, 1.0);
			FRotator Rot = (CamLook - P).Rotation();
			const double Dist = (CamLook - P).Size();
			const FRotationMatrix RM(Rot);
			const FVector Q = HA->GetActorLocation() + FVector(0, 0, 90) - P;
			const double Fz = FVector::DotProduct(Q, RM.GetUnitAxis(EAxis::X));
			if (Fz > 100.0)
			{
				const double Ax = FMath::Atan(FVector::DotProduct(Q, RM.GetUnitAxis(EAxis::Y)) / Fz);
				const double Ay = FMath::Atan(FVector::DotProduct(Q, RM.GetUnitAxis(EAxis::Z)) / Fz);
				const double LimX = FMath::Atan(0.72 * TanH), LimY = FMath::Atan(0.72 * TanV);
				const double DYaw = Ax > LimX ? Ax - LimX : Ax < -LimX ? Ax + LimX : 0.0;
				const double DPitch = Ay > LimY ? Ay - LimY : Ay < -LimY ? Ay + LimY : 0.0;
				if (DYaw != 0.0 || DPitch != 0.0)
				{
					Rot.Yaw += FMath::RadiansToDegrees(DYaw);
					Rot.Pitch += FMath::RadiansToDegrees(DPitch);
					CamLook = P + Rot.Vector() * Dist;
				}
			}
		}
	}
	// -TagLineCam (look-dev, with -TagAutoWatch): the trenches in profile. A low camera on the sideline at the line of
	// scrimmage, framing the interior line, for the first 4 s of every scrimmage play; a frame every 0.25 s.
	static const bool bLineCam = FParse::Param(FCommandLine::Get(), TEXT("TagLineCam"));
	bool bLineShot = false;
	if (bLineCam && Sim && (State == EMatchState::Live || State == EMatchState::Presnap) && Sim->t < 4.0)
	{
		const FVector Los = ToWorld(Sim->los, Sim->by, Dir);
		P = FVector(Los.X, Los.Y - 15.5 * Y, 1.7 * Y);
		CamLook = FVector(Los.X - Dir * 1.0 * Y, Los.Y, 0.8 * Y);
		bLineShot = State == EMatchState::Live;
	}
	// -TagCatchCam (film only): the pass's last 0.9 s and 0.8 s after it is played, framed on the man it was thrown to
	// (or the defender its outcome was rolled for) from 6 m side on, chest high, so a catch's hands can be judged on screen
	static const bool bCatchCam = FParse::Param(FCommandLine::Get(), TEXT("TagCatchCam"));
	static const tag::Ent* GCcCamWho = nullptr;
	static double GCcCamUntil = -1.0;
	static unsigned long long GCcCamSerial = 0;
	static FVector GCcCamSide = FVector::ZeroVector;
	bool bCcCamShot = false;
	if (bCatchCam && Sim && Game && (State == EMatchState::Live || State == EMatchState::Post))
	{
		const tag::Ball& Bk = Sim->ball;
		if (GCcCamSerial != Sim->serial) { GCcCamWho = nullptr; GCcCamUntil = -1.0; GCcCamSerial = Sim->serial; }
		if (Bk.mode == tag::BallMode::Air && Bk.target && !Sim->cfg.kick && Bk.flightT - Bk.t < 0.9)
		{
			GCcCamWho = Bk.fate >= 3 && Bk.fateBy ? Bk.fateBy : Bk.target;
			GCcCamUntil = Sim->t + 0.8;
		}
		if (GCcCamWho && Sim->t <= GCcCamUntil)
		{
			const tag::TeamKey CamOffK = Game->poss;
			if (AAthlete* Cw = AthleteFor(GCcCamWho->p, GCcCamWho->side == tag::Side::Off ? CamOffK : tag::other(CamOffK)))
			{
				const FVector Fo = Cw->GetActorLocation() + FVector(0, 0, 110.f - Cw->GetActorLocation().Z);
				if (GCcCamSide.IsNearlyZero() || Bk.mode == tag::BallMode::Air)
				{
					const FVector Fl = (Dir > 0 ? FVector(Bk.vel.x, -Bk.vel.y, 0) : FVector(-Bk.vel.x, Bk.vel.y, 0)).GetSafeNormal2D();
					FVector Sd = Fl.IsNearlyZero() ? FVector(0, -1, 0) : FVector(-Fl.Y, Fl.X, 0.f);
					if (Sd.Y > 0.f) Sd = -Sd; // (from the broadcast side)
					GCcCamSide = Sd;
				}
				P = Fo + GCcCamSide * 650.f + FVector(0, 0, 120.f);
				CamLook = Fo;
				bCcCamShot = true;
			}
		}
	}
	// -TagThrowCam (film only): the quarterback's windup to 0.5 s after the release, from 5.5 m side on (the side he
	// throws from, the broadcast side when he faces it), chest high - a throw's arm judged on screen. The catch camera
	// takes over for the ball's last 0.9 s when both are on.
	static const bool bThrowCam = FParse::Param(FCommandLine::Get(), TEXT("TagThrowCam"));
	static TWeakObjectPtr<AAthlete> GThrowCamWho;
	static double GThrowCamUntil = -1.0;
	static unsigned long long GThrowCamSerial = 0;
	if (bThrowCam && Sim && Game && State == EMatchState::Live && !bCcCamShot)
	{
		if (GThrowCamSerial != Sim->serial) { GThrowCamWho = nullptr; GThrowCamUntil = -1.0; GThrowCamSerial = Sim->serial; }
		for (const tag::Ent* E : Sim->ents)
			if (E->side == tag::Side::Off && E->hasBall)
				if (AAthlete* Qa = AthleteFor(E->p, Game->poss); Qa && Qa->IsThrowing())
				{
					GThrowCamWho = Qa;
					GThrowCamUntil = Sim->t + 0.5;
				}
		if (AAthlete* Qa = GThrowCamWho.Get(); Qa && Sim->t <= GThrowCamUntil)
		{
			const FVector Fo = Qa->GetActorLocation() + FVector(0, 0, 120.f - Qa->GetActorLocation().Z);
			FVector Sd = Qa->GetActorRightVector(); // (his throwing side)
			Sd.Z = 0.f;
			P = Fo + Sd.GetSafeNormal() * 550.f + Qa->GetActorForwardVector() * 150.f + FVector(0, 0, 60.f);
			CamLook = Fo;
			bCcCamShot = true;
		}
	}
	bFilmCloseShot = bCcCamShot;
	Camera->SetActorLocation(P);
	Camera->SetActorRotation((CamLook - P).Rotation() + ShakeRot);
	if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
	{
		C->FocusSettings.ManualFocusDistance = (CamLook - P).Size();
		C->CurrentFocalLength = bCcCamShot ? 35.f : bLineShot || (bLineCam && State == EMatchState::Presnap) ? 50.f
			: ShotOnScreen() && !CloseupTarget ? ShotFocal : CutInTarget && CutInLeft > 0.f && !CloseupTarget ? 85.f : CloseupTarget ? 30.f : CamFocal;
		// a close shot is a long lens wide open, as on TV: the man sharp, the stands behind him soft (at f/5.6 the
		// painted crowd behind a touchdown close-up read as coloured blocks); the game camera keeps the field sharp
		const bool bClose = (CutInTarget && CutInLeft > 0.f) || CloseupTarget;
		C->CurrentAperture = ShotOnScreen() && !CloseupTarget && !bCcCamShot && !bLineShot ? ShotAperture : bClose ? 1.2f : 5.6f; // (f/2 left the crowd's heads as readable blobs, p75 c013)
	}
	static int32 GLineShots = 0;
	static double GNextLineShot = 0.0;
	if (bLineShot && GLineShots < 400)
	{
		if (Sim->t < GNextLineShot - 1.0) GNextLineShot = 0.0; // new play
		if (Sim->t >= GNextLineShot)
		{
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_line_%03d.png"), GLineShots++), false, false);
			GNextLineShot = Sim->t + 0.25;
		}
	}
}

void AMatchDirector::ReadUserInput()
{
	if (!Sim) return;
	if (!bMulti)
	{
		CurIn = FTagReader(GetWorld()->GetFirstPlayerController());
		CurIn.bPad = PadUI();
		ReadUserInputSlot();
		return;
	}
	// every player in turn: his man and his controls in slot 0 while his own pad (or the keyboard) is read
	for (FTagHuman& H : Humans)
	{
		if (H.Slot < 0 || !Sim->slotEnt(H.Slot)) continue;
		CurIn = H.Reader();
		SwapHuman(H);
		ReadUserInputSlot();
		SwapHuman(H);
	}
	CurIn = FTagReader();
}

void AMatchDirector::ReadUserInputSlot()
{
	const FTagReader& PC = CurIn;
	if (!PC || !Sim) return;
	tag::PlaySim& P = *Sim;
	// the player's own layout for what he is doing now (Settings > Controls): every button below is read through it
	TagBind::ECtx BindCtx = TagBind::ECtx::Receiving;
	if (P.user && P.user->side == tag::Side::Def) BindCtx = TagBind::ECtx::Defense;
	else if (P.user && P.user == P.qb && P.ball.holder == P.qb && !P.passThrown && !Labels.empty()) BindCtx = TagBind::ECtx::Passing;
	else if (P.user && P.user->hasBall) BindCtx = TagBind::ECtx::Carrier;
	const TagBind::FScope BindScope(BindCtx);
	float MX = 0, MY = 0;
	if (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Up)) MY += 1;
	if (PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Down)) MY -= 1;
	if (PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right)) MX += 1;
	if (PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Left)) MX -= 1;
	// (the stick through its radial dead zone, TagInput::Stick: the way the thumb points, its length the speed)
	const float GX = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	const float GY = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	if (GX * GX + GY * GY > 0.01f) { MX = GX; MY = GY; }
	FVector F = CamLook - CamPos;
	F.Z = 0;
	F.Normalize();
	const FVector R(-F.Y, F.X, 0);
	FVector Wv = R * MX + F * MY;
	if (Wv.Size() > 1) Wv.Normalize();
	P.input.move = Dir > 0 ? tag::V2{Wv.X, -Wv.Y} : tag::V2{-Wv.X, Wv.Y};
	// sprint (Madden: R2 held; the keyboard runs flat out) on a stamina bar: about 6 s of it from full, back in about
	// 4 s off it; run dry, no sprint until a quarter of it is back
	{
		const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 1.f / 60.f;
		const bool bWant = PC->IsInputKeyDown(EKeys::Gamepad_RightTrigger);
		const bool bMoving = P.user && tag::len(P.input.move) > 0.2;
		if (SprintStamina < 0.02f) bSprintSpent = true;
		else if (SprintStamina > 0.25f) bSprintSpent = false;
		bSprinting = PC.IsPad() && bWant && bMoving && !bSprintSpent;
		SprintStamina = FMath::Clamp(SprintStamina + (bSprinting ? -Dt / 6.f : Dt / 4.f), 0.f, 1.f);
		P.input.sprint = !PC.IsPad() || bSprinting;
	}
	const FKey Nums[5] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five};
	const FKey Pads[5] = {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top, EKeys::Gamepad_RightShoulder};
	auto ThrowTo = [&](int32 i, int32 Style)
	{
		for (auto& KV : Labels)
			if (KV.second == FString::FromInt(i + 1)) P.input.throwTo = KV.first->role;
		P.input.lob = Style == 1;
		P.input.passStyle = Style;
	};
	const double Now = FPlatformTime::Seconds();
	// L1 held: a high pass; L2 held: a low one
	const int32 ModStyle = PC->IsInputKeyDown(EKeys::Gamepad_LeftShoulder) ? 2 : PC->IsInputKeyDown(EKeys::Gamepad_LeftTrigger) ? 3 : -1;
	// the pad's pass starts on the press (Game/MatchFeel.cpp: the windup as the button goes down, the pass settled during
	// it); -TagThrowOnRelease: the old timing below, the press only counted to the windup
	const bool bOldThrow = TagFeel::ThrowOnRelease();
	FeelThrow(PC, ModStyle, bOldThrow);
	for (int i = 0; i < 5; ++i)
	{
		// the keyboard throws at once: a bullet (SHIFT lob, CTRL high, ALT low)
		if (P.user != P.qb) break; // (the receivers' buttons are the quarterback's: a player on a receiver has his own)
		if (PC->WasInputKeyJustPressed(Nums[i]))
			ThrowTo(i, PC->IsInputKeyDown(EKeys::LeftShift) ? 1 : PC->IsInputKeyDown(EKeys::LeftControl) ? 2 : PC->IsInputKeyDown(EKeys::LeftAlt) ? 3 : 0);
		// the pad, Madden's way: the receiver's button held is a bullet, tapped a lob / touch, tapped twice a pump fake
		if (bOldThrow && PC->WasInputKeyJustPressed(Pads[i]) && !Labels.empty() && P.ball.holder == P.qb && !P.passThrown)
		{
			if (PadTapIdx == i && Now - PadTapT < 0.22) { P.input.pumpFake = true; PadTapIdx = -1; PadThrowIdx = -1; }
			else { PadThrowIdx = i; PadThrowT0 = Now; }
		}
	}
	if (PadThrowIdx >= 0 && P.user == P.qb && bOldThrow)
	{
		const double Held = Now - PadThrowT0;
		if (!PC->IsInputKeyDown(Pads[PadThrowIdx]) || Held >= 0.3)
		{
			if (Held < 0.15 && ModStyle < 0) { PadTapIdx = PadThrowIdx; PadTapT = Now; } // (a tap: thrown unless a second tap comes)
			else ThrowTo(PadThrowIdx, ModStyle >= 0 ? ModStyle : 0);
			PadThrowIdx = -1;
		}
	}
	if (PadTapIdx >= 0 && Now - PadTapT >= 0.22 && P.user == P.qb && bOldThrow) { ThrowTo(PadTapIdx, 1); PadTapIdx = -1; }
	// the pump fake on the keyboard
	if (PC->WasInputKeyJustPressed(EKeys::P) && P.ball.holder == P.qb && !P.passThrown) P.input.pumpFake = true;
	// throw it away (Madden: R3; keyboard T)
	if (PC->WasInputKeyJustPressed(EKeys::Gamepad_RightThumbstick) || PC->WasInputKeyJustPressed(EKeys::T)) P.input.throwAway = true;
	// SPACE jukes (with the ball) and dives (on defense); on the pad SQUARE dives for the tackle (Madden)
	const bool bDefUser = P.user && P.user->side == tag::Side::Def;
	const bool bKeyAction = PC->WasInputKeyJustPressed(EKeys::SpaceBar);
	P.input.juke = P.input.juke || bKeyAction;
	P.input.dive = bKeyAction || (bDefUser && PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Left));
	// Madden's: R1 held with the ball covers it up (keyboard X); on defense R1 goes for the strip (X) and L2 held
	// strafes (LEFT SHIFT)
	{
		const tag::Ent* U = P.user;
		const bool bRunning = U && U->hasBall && P.ball.mode == tag::BallMode::Held && (U != P.qb || P.scramble || U->state == tag::EntState::Carrier);
		P.input.protectBall = bRunning && (PC->IsInputKeyDown(EKeys::Gamepad_RightShoulder) || PC->IsInputKeyDown(EKeys::X));
		P.input.strafe = bDefUser && (PC->IsInputKeyDown(EKeys::Gamepad_LeftTrigger) || PC->IsInputKeyDown(EKeys::LeftShift));
		if (bDefUser && (PC->WasInputKeyJustPressed(EKeys::Gamepad_RightShoulder) || PC->WasInputKeyJustPressed(EKeys::X))) P.input.strip = true;
	}
	// switch player on defense (Madden: CIRCLE): FeelSwitch below, with TAB / L1
	const bool bSwitchCircle = bDefUser && PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right);
	const bool bSwitchCircleHeld = bDefUser && PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Right);
	// a field goal block: TRIANGLE (keyboard C) leaps at the line, hands up; SQUARE / SPACE lays out across the kick (dive)
	if (bDefUser && P.isPlaceKick() && (PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Top) || PC->WasInputKeyJustPressed(EKeys::C))) P.input.blockJump = true;
	// the highlight stick. With the ball: flick the right stick ahead of him to truck, back to stiff arm, to a side to
	// juke that way; CIRCLE spins, TRIANGLE hurdles (keyboard F truck, G stiff arm, Q / E juke left / right, R spin,
	// V hurdle). On defense: flick it up for the hit stick, down to cut him low (keyboard F / V). A flick counts once
	// the stick has come back to the middle.
	{
		tag::Ent* U = P.user;
		const bool bCarrier = U && U->hasBall && P.ball.mode == tag::BallMode::Held && (U != P.qb || U->state == tag::EntState::Carrier || P.scramble);
		const bool bDefender = U && P.ball.holder && P.ball.mode == tag::BallMode::Held && U->side != P.ball.holder->side;
		const float RX = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX), RY = RightStickY(PC);
		const float RM = FMath::Sqrt(RX * RX + RY * RY);
		tag::Move Want = tag::Move::None;
		double WantSide = 0;
		// the user's man engaged with a blocker: the pass rush moves (Madden's): SQUARE finesse, CROSS power, the left
		// stick picking which (to a side: swim / club, back: spin / long arm, else rip / bull rush); the right stick
		// flicked up bull rushes, to a side swims, down spins (keyboard J finesse, K power)
		const bool bRushing = U && U->side == tag::Side::Def && !U->engagedBy.empty();
		if (bRushing)
		{
			const double Fwd = -P.input.move.x, Lat = FMath::Abs(P.input.move.y);
			const bool bSide = Lat > 0.5 && Lat > FMath::Abs(Fwd), bBack = Fwd < -0.5 && !bSide;
			std::string M;
			// keyboard: J finesse, K power (A / D a side: swim / club; S back: spin / long arm)
			if (PC->WasInputKeyJustPressed(EKeys::J)) M = bSide ? "swim" : bBack ? "spin" : "rip";
			if (PC->WasInputKeyJustPressed(EKeys::K)) M = bSide ? "club" : bBack ? "longarm" : "bull";
			// the pad, Madden 26's engaged controls: R-STICK up rip, down bull rush, to a side club or swim (his build
			// picks: a power man clubs), R2 speed rush
			if (RM > 0.75f && bRStickArmed)
			{
				bRStickArmed = false;
				const bool bPowerMan = U->r().strength > U->r().agility + 4;
				M = RY > 0.6f ? "rip" : RY < -0.6f ? "bull" : (bPowerMan ? "club" : "swim");
			}
			if (PC->WasInputKeyJustPressed(EKeys::Gamepad_RightTrigger)) M = "diprip";
			if (RM < 0.35f) bRStickArmed = true;
			if (!M.empty()) P.input.rushMove = M;
			if (!bRushHint && Overlay)
			{
				bRushHint = true;
				Overlay->SetHint(PC.IsPad() ? PadHint(TEXT("PASS RUSH   R-STICK up rip   down bull rush   left / right swim or club   R2 speed rush"))
					: TagBind::TranslateHint(TEXT("J finesse   K power   (A / D side: swim / club   S back: spin / long arm)"), false));
			}
		}
		else if (bRushHint && Overlay) { bRushHint = false; Overlay->SetHint(TEXT("")); }
		if (bRushing) {}
		else if (RM < 0.35f) bRStickArmed = true;
		else if (RM > 0.75f && bRStickArmed)
		{
			bRStickArmed = false;
			if (bCarrier)
			{
				// the flick in the world (up the screen = away from the camera), then in the runner's own frame
				FVector Wf = (R * RX + F * RY).GetSafeNormal2D();
				const tag::V2 Sv = Dir > 0 ? tag::V2{Wf.X, -Wf.Y} : tag::V2{-Wf.X, Wf.Y};
				const tag::V2 Fd = U->speed() > 0.5 ? tag::norm(U->vel) : tag::V2{U->side == tag::Side::Off ? 1.0 : -1.0, 0.0};
				const double Ahead = tag::dot(Sv, Fd), Lat = tag::dot(Sv, tag::V2{-Fd.y, Fd.x});
				if (Ahead > 0.6) Want = tag::Move::Truck;
				else if (Ahead < -0.6) Want = tag::Move::Juke; // (the back juke: the sim picks the side away from the tackler)
				else
				{
					Want = tag::Move::Juke;
					WantSide = Lat >= 0 ? 1 : -1;
				}
			}
			else if (bDefender && !bRushing) Want = RY > 0.f ? tag::Move::Hit : tag::Move::Cut;
		}
		if (bCarrier && Labels.empty())
		{
			// (the face buttons are the receivers' while the quarterback can still throw)
			if (PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right) || PC->WasInputKeyJustPressed(EKeys::R)) Want = tag::Move::Spin;
			if (PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Top) || PC->WasInputKeyJustPressed(EKeys::V)) Want = tag::Move::Hurdle;
			if (PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)) Want = tag::Move::StiffArm; // (Madden: CROSS)
		}
		if (bCarrier)
		{
			if (PC->WasInputKeyJustPressed(EKeys::F)) Want = tag::Move::Truck;
			if (PC->WasInputKeyJustPressed(EKeys::G)) Want = tag::Move::StiffArm;
			if (PC->WasInputKeyJustPressed(EKeys::Q)) { Want = tag::Move::Juke; WantSide = 1; }
			if (PC->WasInputKeyJustPressed(EKeys::E)) { Want = tag::Move::Juke; WantSide = -1; }
		}
		else if (bDefender)
		{
			if (PC->WasInputKeyJustPressed(EKeys::F)) Want = tag::Move::Hit;
			if (PC->WasInputKeyJustPressed(EKeys::V)) Want = tag::Move::Cut;
		}
		if (bMoveShot && bCarrier)
		{
			// the look-dev user: takes the highlighted move once the man is close
			const tag::Ent* Th = nullptr;
			const tag::Move Sg = P.suggestMove(U, &Th);
			if (Sg != tag::Move::None && Th && tag::dist(Th->pos, U->pos) < 3.0 && !bMoveShotHlThisPlay && MoveShotsHl < 6)
			{
				bMoveShotHlThisPlay = true;
				FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_hl_%02d.png"), MoveShotsHl++), true, false);
			}
			if (Sg != tag::Move::None && Th && tag::dist(Th->pos, U->pos) < 1.6 && U->animT > 0.6) Want = Sg;
		}
		if (Want != tag::Move::None)
		{
			P.input.stickMove = Want;
			P.input.stickSide = WantSide;
		}
		// the ball carrier's hint: his moves
		if (bCarrier && !bCarrierHint && Overlay)
		{
			bCarrierHint = true;
			Overlay->SetHint(PC.IsPad() ? PadHint(TEXT("R2 sprint    R-STICK up truck   left / right / back juke    CROSS stiff arm    CIRCLE spin    TRIANGLE hurdle"))
				: TagBind::TranslateHint(TEXT("F truck   G stiff arm   Q / E juke   R spin   V hurdle   SPACE juke"), false));
		}
	}
	// the aggressive catch: TRIANGLE / Y held while our pass is in the air
	if (P.ball.mode == tag::BallMode::Air && P.user && P.user->side == tag::Side::Off)
	{
		if (PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Top) || PC->IsInputKeyDown(EKeys::Y)) bAggroCatch = true;
	}
	else if (P.ball.mode == tag::BallMode::Held && P.ball.holder == P.qb) bAggroCatch = false;
	// (the quarterback's L1 is the high pass, not a switch: it took him off the quarterback with the ball in his hands)
	// the switch follows the ball, the button held with the stick picks the man that way, and the stick does not steer
	// the new man for a beat (Game/MatchFeel.cpp; -TagSwitchOld: the sim's nearest-the-ball switch, as it was)
	{
		const bool bL1 = BindCtx != TagBind::ECtx::Passing;
		FeelSwitch(bSwitchCircle || PC->WasInputKeyJustPressed(EKeys::Tab) || (bL1 && PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftShoulder)),
			bSwitchCircleHeld || PC->IsInputKeyDown(EKeys::Tab) || (bL1 && PC->IsInputKeyDown(EKeys::Gamepad_LeftShoulder)));
	}
	// auto-switch to our ball carrier after a handoff / catch
	tag::Ent* H = P.ball.holder;
	// (multiplayer: a man another player has is his; on offense only the player on the quarterback follows the ball)
	const bool bMayTake = !bMulti || (H && !H->userControlled && (P.user == P.qb || P.user->side == tag::Side::Def));
	if (H && P.user && H->side == P.user->side && H != P.user && H != P.qb && bMayTake)
	{
		P.user->userControlled = false;
		P.user = H;
		H->userControlled = true;
		if (!bMulti || H->side == tag::Side::Off) Labels.clear();
	}
	if (P.passThrown) Labels.clear();
	// (a handoff: the sim hands the user the back in the tick the ball is his - PlaySim::updateSnap - so the switch above
	// finds him already there; the receivers' buttons go as they did when it switched here)
	else if (P.handedOff && P.user && P.user != P.qb && P.user->side == tag::Side::Off && P.ball.holder == P.user && !Labels.empty()) Labels.clear();
	FeelNotePresses(PC); // (-TagLatencyLog: each press followed to the step that takes it, Game/TagLatency.h)
}

// ------------------------------------------------------------------ tick
void AMatchDirector::TickHelpUp(float Dt)
{
	if (HelpStage == 0) return;
	AAthlete* Dn = HelpDown.Get();
	AAthlete* By = HelpBy.Get();
	const FAthleteAssets& HA = FAthleteAssets::Get();
	// (a new play, a replay or a kick takes the men back: whatever they are doing ends with the next Drive)
	if (!Dn || !By || !HA.HelpUpDown.IsValid() || !HA.HelpUpBy.IsValid() || State == EMatchState::Presnap || (State == EMatchState::Live && Sim && Sim->isLive()) ||
		State == EMatchState::Kick || State == EMatchState::Replay)
	{
		UE_LOG(LogTemp, Display, TEXT("TAG help-up ended: stage %d state %d age %.1f down %d up %d hips %.0f"), HelpStage, static_cast<int32>(State), HelpAge, Dn && Dn->IsDown() ? 1 : 0, Dn && Dn->IsGettingUp() ? 1 : 0, Dn && !Dn->IsHidden() ? Dn->PelvisZ() : 0.f); // (no bones read off a man taken off the field)
		HelpStage = 0;
		return;
	}
	HelpAge += Dt;
	// handing a man back to the post-whistle jog: he goes on from where he stands (left to the sim's spot, three
	// metres off, he jumped most of a yard as the capture ended)
	auto HandBack = [this](AAthlete* A)
	{
		if (!A || !Sim) return;
		A->AbsorbVisOffset();
		for (tag::Ent* E : Sim->ents)
			if (E->p == A->Player) { FVector At = A->GetActorLocation(); At.Z = 0.f; PostPos[E] = At; }
	};
	if (HelpStage == 1)
	{
		// the capture's two men: the one on his knees at its origin, the helper 1.6 m to his left, facing him
		const FVector At = Dn->PelvisAt();
		const float Yaw = Dn->GetActorRotation().Yaw;
		const FVector Fwd = FRotator(0, Yaw, 0).Vector(), Left = FVector::CrossProduct(Fwd, FVector::UpVector);
		const FVector Spot = FVector(At.X, At.Y, 0) + Fwd * 44.4f;
		const FVector Start = Spot + Left * 164.4f + Fwd * 12.5f;
		FVector To = Start - HelpByPos;
		To.Z = 0.f;
		const float Dd = static_cast<float>(To.Size());
		FVector V = FVector::ZeroVector;
		if (Dd > 25.f)
		{
			HelpSpd = FMath::Min(FMath::Min(400.f, 110.f + Dd * 1.3f), HelpSpd + 320.f * Dt);
			V = To / Dd * HelpSpd;
			HelpByPos += V * FMath::Min(Dt, Dd / FMath::Max(HelpSpd, 1.f));
		}
		else HelpSpd = 0.f;
		const bool bMoving = V.Size() > 60.f;
		By->Drive(HelpByPos, bMoving ? V.Rotation().Yaw : (At - HelpByPos).Rotation().Yaw, V, 9.f, bMoving ? TEXT("run") : TEXT("idle"), 0.f, Dt, false);
		// he never went down (out of bounds), is already up, or nobody got there: no help-up this time
		if ((HelpAge > 1.6f && !Dn->IsDown()) || (HelpAge > 3.6f && !Dn->IsGettingUp()) || HelpAge > 5.2f)
		{
			HandBack(By);
			UE_LOG(LogTemp, Display, TEXT("TAG help-up off: age %.1f, down %d, getting up %d, hips %.0f cm, helper %.0f cm short"), HelpAge, Dn->IsDown() ? 1 : 0, Dn->IsGettingUp() ? 1 : 0, Dn->PelvisZ(), Dd);
			HelpStage = 0;
			if (State == EMatchState::Post) Timer = FMath::Min(Timer, 0.6f);
			return;
		}
		// the moment: he is coming up through his knees and the helper is at his shoulder
		if (Dn->IsGettingUp() && Dn->PelvisZ() > 36.f && Dn->PelvisZ() < 70.f && Dd < 45.f)
		{
			HelpStage = 2;
			HelpT = 0.f;
			HelpSpot = Spot;
			HelpYaw = Yaw;
			Dn->BeginScene(HA.HelpUpDown.Get(), Spot - Fwd * 44.4f, Yaw, 0.f, false);
			By->BeginScene(HA.HelpUpBy.Get(), Start, Yaw + 90.f, 0.f, false);
			UE_LOG(LogTemp, Display, TEXT("TAG help-up: #%d pulled up by #%d"), Dn->Player ? Dn->Player->number : -1, By->Player ? By->Player->number : -1);
		}
		return;
	}
	HelpT += Dt;
	Dn->Drive(Dn->GetActorLocation(), HelpYaw, FVector::ZeroVector, 9.f, TEXT("scene"), 0.f, Dt, false);
	By->Drive(By->GetActorLocation(), HelpYaw + 90.f, FVector::ZeroVector, 9.f, TEXT("scene"), 0.f, Dt, false);
	if (HelpT > HA.HelpUpDown->GetPlayLength() - 0.12f)
	{
		HelpWalker = Dn;
		HandBack(Dn);
		HandBack(By);
		HelpStage = 0;
		if (State == EMatchState::Post) Timer = FMath::Min(Timer, 0.4f);
	}
}

void AMatchDirector::UpdateKeepOut()
{
	// measured as well as applied: a hand more than 3 cm inside another man's head / chest / hips, per pair of men down
	// together and frame (logged every 20 s of pile time, and by -TagNoKeepOut for the comparison)
	static int64 PairFrames = 0, Inside = 0, LastLogged = 0, FeetIn = 0, ElbowsIn = 0, KneesIn = 0;
	TArray<AAthlete*, TInlineAllocator<24>> Down;
	for (const TPair<int32, TObjectPtr<AAthlete>>& It : Athletes)
		if (AAthlete* A = It.Value.Get())
		{
			if (!A->IsHidden() && A->IsDown()) Down.Add(A);
			else if (A->KeepOutN > 0) A->SetKeepOut(nullptr, 0);
		}
	// (the counting is -TagAnimStats' alone: without it only the spheres are handed over, no limb is read)
	const bool bStats = AnimStats();
	for (AAthlete* A : Down)
	{
		FVector4f S[12];
		int32 N = 0;
		FVector HL = FVector::ZeroVector, HR = FVector::ZeroVector, FL = FVector::ZeroVector, FR = FVector::ZeroVector;
		const bool bHands = bStats && A->Hands(HL, HR);
		const bool bFeet = bStats && A->Feet(FL, FR);
		FVector EL = FVector::ZeroVector, ER = FVector::ZeroVector, KL = FVector::ZeroVector, KR = FVector::ZeroVector;
		const bool bElbows = bStats && A->BonePair(TEXT("lowerarm_l"), TEXT("lowerarm_r"), EL, ER);
		const bool bKnees = bStats && A->BonePair(TEXT("calf_l"), TEXT("calf_r"), KL, KR);
		for (AAthlete* O : Down)
		{
			if (O == A || FVector::DistSquared2D(O->GetActorLocation(), A->GetActorLocation()) > 220.f * 220.f || N > 9) continue;
			const int32 N0 = N;
			N += O->BodySpheres(S + N);
			if (!bHands) continue;
			++PairFrames;
			bool bIn = false;
			for (int32 k = N0; k < N; ++k)
				for (const FVector& Hd : {HL, HR})
					bIn = bIn || FVector::Dist(Hd, FVector(S[k].X, S[k].Y, S[k].Z)) < S[k].W - 3.f;
			Inside += bIn;
			bool bFootIn = false;
			if (bFeet)
				for (int32 k = N0; k < N; ++k)
					for (const FVector& Fo : {FL, FR})
						bFootIn = bFootIn || FVector::Dist(Fo, FVector(S[k].X, S[k].Y, S[k].Z)) < S[k].W - 3.f;
			FeetIn += bFootIn;
			// (elbows and knees: measured only, the keep-out moves hands and feet)
			auto AnyIn = [&](const FVector& P0, const FVector& P1)
			{
				for (int32 k = N0; k < N; ++k)
					for (const FVector& P : {P0, P1})
						if (FVector::Dist(P, FVector(S[k].X, S[k].Y, S[k].Z)) < S[k].W - 3.f) return true;
				return false;
			};
			ElbowsIn += bElbows && AnyIn(EL, ER);
			KneesIn += bKnees && AnyIn(KL, KR);
		}
		A->SetKeepOut(S, N);
	}
	if (PairFrames - LastLogged >= 600)
	{
		LastLogged = PairFrames;
		const double Pf = 100.0 / FMath::Max<int64>(1, PairFrames);
		UE_LOG(LogTemp, Display, TEXT("TAG limb clip: a hand inside another body in %lld of %lld pile pair-frames (%.2f%%), a foot in %lld (%.2f%%), an elbow %lld (%.2f%%), a knee %lld (%.2f%%)"), Inside, PairFrames,
			Inside * Pf, FeetIn, FeetIn * Pf, ElbowsIn, ElbowsIn * Pf, KneesIn, KneesIn * Pf);
	}
}

void AMatchDirector::Tick(float Dt)
{
	Super::Tick(Dt);
	// every device's "just pressed" lasts this one tick, whichever way it returns
	ON_SCOPE_EXIT { FTagInputHub::Get().EndFrame(); };
	TickHitStop(); // (a hit-stop lets go on real time, and at once for a pause or the end of the play: Game/MatchFeel.cpp)
	if (bFeelTest) TickFeelTest(Dt);
	if (bHitStopTest) TickHitStopTest();
	{
		// a menu one player works (multiplayer: a play call, the try): the other pads and keys go nowhere
		const bool bOwned = bMulti && MenuOwner >= 0 && Overlay && Overlay->HasModal() && !bEnginePaused && State != EMatchState::Paused;
		FTagInputHub::Get().SetMenuOwners(bOwned ? HumanDevices(MenuOwner) : TArray<int32>());
		if (bMulti && Overlay) Overlay->SetPadKind(TagBind::Sony(Humans.IsValidIndex(MenuOwner) ? HumanDevice(MenuOwner).Contains(TEXT("PlayStation")) : bPlayStationPad));
	}
	if (bMultiTest) TickMultiTest(Dt);
	if (bLobby) TickLobby();
	UpdateKeepOut();
	TickHelpUp(Dt);
	TickAutomation(Dt);
	TickHighlights(Dt);
	if (bTackleCam) TcRecord();
	{
		// -TagPairLog: a paired fall's two pelvises (the pose shown last frame) beside the clip time, to check the pair
		// against the capture's own distances (Tools/Art/pair_truth.py)
		static const bool bPairLog = FParse::Param(FCommandLine::Get(), TEXT("TagPairLog"));
		AAthlete* PC = PileCarrier.Get();
		AAthlete* PT = PileTackler.Get();
		if (bPairLog && PC && PT && PC->IsFalling() && PT->IsFalling())
			UE_LOG(LogTemp, Display, TEXT("TAG pairdist %s | %s %.1f"), *PT->TelemetryAnim(), *PC->TelemetryAnim(),
				FVector::Dist2D(PT->BoneWorld(TEXT("pelvis")), PC->BoneWorld(TEXT("pelvis"))));
		// -TagTackleCheck: the tackles started, against the sim's carrier and the ball as drawn last frame
		if (Ball) TackleCheckTick(Sim.get(), Ball->GetActorLocation(), GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	}
	// (the soak's stall check: when the play log last grew)
	static size_t GSoakLogN = 0;
	static double GSoakLogT = 0.0;
	if (SoakSecs > 0.f && Game && Game->log.size() != GSoakLogN) { GSoakLogN = Game->log.size(); GSoakLogT = FPlatformTime::Seconds(); }
	if (SoakSecs > 0.f && Game && FPlatformTime::Seconds() - SoakStart > SoakSecs)
	{
		UE_LOG(LogTemp, Display, TEXT("TAG soak done: %.0f s, Q%d %d-%d, pack batches %d"), SoakSecs, Game->q, Game->score.away, Game->score.home, FAthleteAssets::Get().PackBatch);
		SGridironOverlay::LogChartTest(this); // (-TagChartTest: its own result line)
		{
			// the soak's verdict: the game kept playing (plays in the log, the last one inside 3 minutes, or the game over)
			// and its state is a legal one at the end (Game/TagResult.h)
			const tag::Game& SG = *Game;
			int32 SoakPass = 0, SoakFail = 0;
			auto SoakCheck = [&SoakPass, &SoakFail](bool bOk, const FString& What)
			{
				(bOk ? SoakPass : SoakFail)++;
				UE_LOG(LogTemp, Display, TEXT("TAG soak %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
			};
			SoakCheck(SG.log.size() >= 4, FString::Printf(TEXT("the game was played (%d log lines)"), static_cast<int32>(SG.log.size())));
			SoakCheck(SG.isFinal() || FPlatformTime::Seconds() - GSoakLogT < 180.0, FString::Printf(TEXT("no stall (last play %.0f s before the end)"), FPlatformTime::Seconds() - GSoakLogT));
			SoakCheck(SG.score.home >= 0 && SG.score.away >= 0 && SG.score.home != 1 && SG.score.away != 1, FString::Printf(TEXT("a legal score (%d-%d)"), SG.score.away, SG.score.home));
			SoakCheck(SG.isFinal() || (SG.down >= 1 && SG.down <= 4 && SG.toGo > 0.0 && SG.ballOn >= 0.0 && SG.ballOn <= 100.0),
				FString::Printf(TEXT("a legal down and spot (%d & %.1f at %.1f)"), SG.down, SG.toGo, SG.ballOn));
			SoakCheck(SG.timeouts.home >= 0 && SG.timeouts.home <= 3 && SG.timeouts.away >= 0 && SG.timeouts.away <= 3 && SG.q >= 1 &&
				SG.clock >= 0.0 && SG.clock <= (SG.q > 4 ? FMath::Max(600.0, static_cast<double>(SG.quarterLen)) : static_cast<double>(SG.quarterLen)) + 1e-6, // (overtime is ten minutes whatever the quarters are)
				FString::Printf(TEXT("legal timeouts and clock (%d / %d, Q%d %.0f s)"), SG.timeouts.away, SG.timeouts.home, SG.q, SG.clock));
			TagResult::Finish(TEXT("TagSoak"), SoakPass, SoakFail, false);
		}
		SoakSecs = 0.f;
		FPlatformMisc::RequestExit(false);
	}
	if (bArtTest) TickArtTest(Dt);
	if (bPlayClockOn && bRealClock)
	{
		// a human game: real seconds, from the whistle to the snap (the game clock only while it runs, never under 0:01)
		// (a broadcast shot or the open on screen holds them too, as a replay does: the presentation's pace never takes the
		// game's time - MatchBroadcast.cpp)
		const bool bBetween = (State == EMatchState::Call || State == EMatchState::Presnap || State == EMatchState::Post || State == EMatchState::Wait) &&
			!BroadcastHoldsClock();
		if (bBetween && !UGameplayStatics::IsGamePaused(this)) // (the pause menu holds both clocks)
		{
			PlayClockF = FMath::Max(0.f, PlayClockF - Dt);
			// (multiplayer: the defense's caller taking his time is not the offense's delay of game - the clock holds at 10
			// while his call is up)
			if (bMulti && State == EMatchState::Call && Humans.IsValidIndex(MenuOwner) && Game && KeyOf(Humans[MenuOwner].Team) != Game->poss)
				PlayClockF = FMath::Max(PlayClockF, FMath::Min(10.f, PlayClockF + Dt));
			if (State == EMatchState::Post || State == EMatchState::Wait) { if (bPreRun && Game) HudPreRun = FMath::Min(HudPreRun + Dt, FMath::Max(0.0, Game->clock)); } // (never past 0:00)
			else if (bClockLive && Game) HudRunoffLeft = FMath::Max(HudRunoffLeft - Dt, -FMath::Max(0.0, Game->clock - 1.0));
			// the user's offense let it run out: delay of game (not on a try)
			if (PlayClockF <= 0.f && UserOff() && !bTwoPoint && (State == EMatchState::Call || State == EMatchState::Presnap)
				&& Game && Game->phase == tag::GamePhase::Scrimmage)
				DelayOfGame();
		}
		else if (bBetween) {} // paused
		else if (State == EMatchState::Kick || State == EMatchState::Final || State == EMatchState::Menu)
		{
			bPlayClockOn = false;
			HudRunoffLeft = 0.0;
		}
	}
	else if (bPlayClockOn)
	{
		// the call and the pre-snap take ~8-12 s on screen: the clocks run fast enough to be near the snap value by then
		// (a user offense that waits sees the play clock reach its snap value and hold)
		if (State == EMatchState::Call || State == EMatchState::Presnap)
		{
			const float From = PlayClockF;
			PlayClockF = FMath::Max(PlayClockSnap, PlayClockF - Dt * PlayClockRate);
			// the game clock comes down with the play clock (the same seconds), to the game's value at the snap
			const float Span = FMath::Max(0.01f, From - PlayClockSnap);
			if (From > PlayClockSnap) HudRunoffLeft *= FMath::Clamp((PlayClockF - PlayClockSnap) / Span, 0.f, 1.f);
			else HudRunoffLeft = 0.0;
		}
		else if (State == EMatchState::Post || State == EMatchState::Wait)
		{
			// after the whistle: the play clock starts at 40, and a running game clock runs on (at most 12 s before the call)
			PlayClockF = FMath::Max(26.f, PlayClockF - Dt * 2.f);
			if (bPreRun && Game) HudPreRun = FMath::Min(FMath::Min(12.0, FMath::Max(0.0, Game->clock)), HudPreRun + Dt * 2.0);
		}
		else if (State == EMatchState::Kick || State == EMatchState::Final || State == EMatchState::Menu)
		{
			bPlayClockOn = false; // a kick (punt, field goal) snaps with the clock where the game has it
			HudRunoffLeft = 0.0;
		}
	}
	{
		// -TagMenuShot: screenshot the main menu with its UI (gamepad focus frame), then quit
		static const bool bMenuShot = FParse::Param(FCommandLine::Get(), TEXT("TagMenuShot"));
		static float MenuT = 0.f;
		if (bMenuShot && (MenuT += Dt) > 6.f && MenuT - Dt <= 6.f) FScreenshotRequest::RequestScreenshot(TEXT("tag_menu.png"), true, false);
		if (bMenuShot)
			if (APlayerController* MPC = GetWorld()->GetFirstPlayerController())
				for (const FKey& K : {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Enter, EKeys::SpaceBar,
					EKeys::Gamepad_Special_Right, EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_RightTrigger})
					if (MPC->IsInputKeyDown(K)) UE_LOG(LogTemp, Display, TEXT("TAG menu input held: %s at %.2f s"), *K.ToString(), MenuT);
		if (bMenuShot && MenuT > 8.f) FPlatformMisc::RequestExit(false);
	}
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && InputPrereq.Get() != PC)
	{
		// tick after the controller, so "just pressed" is this frame's input in paused and running ticks alike (the
		// order otherwise differed between the two: the Esc that resumed in a paused tick was read again in the first
		// running tick, before the controller had moved on, and paused the game again)
		if (APlayerController* Old = InputPrereq.Get()) RemoveTickPrerequisiteActor(Old);
		AddTickPrerequisiteActor(PC);
		InputPrereq = PC;
	}
	TrackInputDevice(PC);
	TickRumble(FeelRealDt(Dt)); // (the pad's rumble, Game/MatchFeel.cpp: before the pause's return, so a pause stops it at once; on real time through a hit-stop)
	if (bPlayNowPending && !(IsMoviePlayerEnabled() && GetMoviePlayer()->IsMovieCurrentlyPlaying()))
	{
		bPlayNowPending = false;
		StartMatch(3, 7, true, true, 1, true, 900);
		return;
	}
	if (bPauseTest) TickPauseTest();
	const bool bEsc = PC && (PC->WasInputKeyJustPressed(EKeys::Escape) || PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Right));
	if (bEnginePaused)
	{
		FeedAudio(Dt); // (the menu's own sounds and music)
		// the world is held: only the pause menu's own key does anything (the menu's buttons are Slate's)
		if (bEsc) Unpause();
		return;
	}
	TickSidelines(Dt);
	if (State == EMatchState::Menu)
	{
		MenuOrbit += Dt * 0.05f;
		const FVector C(50 * YD, 0, 0);
		Camera->SetActorLocation(C + FVector(FMath::Cos(MenuOrbit) * 75 * YD, FMath::Sin(MenuOrbit) * 60 * YD, 26 * YD));
		Camera->SetActorRotation((C - Camera->GetActorLocation()).Rotation());
		FeedAudio(Dt); // (the front end's playlist and its bed: nothing else ticks the sound while the menus are up)
		return;
	}
	if (!Game) return;
	if (bLineup)
	{
#if WITH_EDITOR
		if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0) return; // wait for shaders
#endif
		TickLineup(Dt);
		return;
	}
	// (a field athlete hidden or shown by a path that did not say so is put to sleep / woken here: asleep exactly while
	// hidden. ShowOnly and the kicks' own un-hides do it at once; this is the net under them.)
	for (const auto& KV : Athletes)
		if (AAthlete* A = KV.Value.Get()) A->SetDormant(A->IsHidden());
	if (bEsc && State != EMatchState::Final) TogglePause();
	if (bPadPlayTest) TickPadPlayTest(Dt);
	if (bPadPlayTest && (PadPlay.Step == 99 || PadPlay.Clock < -1e8f))
	{
		// (the test has asked to quit, MatchPadPlay.cpp: its result line and the exit status, Game/TagResult.h)
		TagResult::Finish(TEXT("TagPadPlayTest"), PadPlay.Pass, PadPlay.Fail, false);
		bPadPlayTest = false;
	}
	if (BreakLeft > 0.f || bSetAfterBreak)
	{
		BreakT = FMath::Min(BreakT + Dt, 6.f); // (the crane's travel, held at its end through a long play call)
		if (BreakLeft > 0.f && (BreakLeft -= Dt) <= 0.f && !bSetAfterBreak) bCutNext = true; // (a cut back to the game camera)
	}
	{
		// the camera: C on the keyboard, or the pad's VIEW / CREATE button held a beat (a tap changed it - a brush of the
		// button between plays swapped the player's camera mid-drive with nothing on screen to say so); named when it changes
		bool bCycle = PC && PC->WasInputKeyJustPressed(EKeys::C);
		if (PC && PC->IsInputKeyDown(EKeys::Gamepad_Special_Left))
		{
			const float Was = CamHoldT;
			CamHoldT += Dt;
			bCycle = bCycle || (Was < 0.5f && CamHoldT >= 0.5f);
		}
		else CamHoldT = 0.f;
		if (bCycle)
		{
			CamMode = static_cast<ECamMode>((static_cast<uint8>(CamMode) + 1) % 4);
			static const TCHAR* Names[] = {TEXT("CAMERA: BEHIND THE OFFENSE"), TEXT("CAMERA: TV SIDELINE"), TEXT("CAMERA: SKYCAM"), TEXT("CAMERA: PLAYER")};
			if (Overlay) Overlay->Toast(Names[static_cast<uint8>(CamMode) % 4]);
			UE_LOG(LogTemp, Display, TEXT("TAG camera changed by the player: %s"), Names[static_cast<uint8>(CamMode) % 4]);
		}
	}
	if (bWatch && PC)
	{
		if (PC->WasInputKeyJustPressed(EKeys::One)) Speed = 1.f;
		if (PC->WasInputKeyJustPressed(EKeys::Two)) Speed = 2.f;
		if (PC->WasInputKeyJustPressed(EKeys::Four)) Speed = 4.f;
	}
	const float SDt = Dt * (bWatch ? Speed : 1.f);
	// the one skip rule (04 A8): a confirm press (CROSS / A, SPACE, ENTER) skips the shot, the replay or the open on
	// screen, and no press skips anything for 0.3 s after (MatchBroadcast.cpp)
	const bool bSkipPress = BroadcastSkipPress(PC);
	// the snap: the offense's own (multiplayer: the player in slot 0 - the offense's caller - and nobody else)
	// (on the player's own snap button: Settings > Controls, pre-snap offense)
	bool bSnapKey = false;
	{
		const TagBind::FScope SnapScope(TagBind::ECtx::PreOff);
		const FTagReader Pr = bMulti ? PrimaryReader() : FTagReader(PC);
		bSnapKey = Pr && (Pr->WasInputKeyJustPressed(EKeys::SpaceBar) || Pr->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom));
	}
	if (Game && Game->q != PackQ && (State == EMatchState::Wait || State == EMatchState::Call))
	{
		// a new quarter: a fresh draw of the pack library streams in behind the game
		PackQ = Game->q;
		FAthleteAssets& A = FAthleteAssets::Get();
		A.ReseedPack(A.PackSeed + PackQ * 1000);
	}
	switch (State)
	{
	case EMatchState::Wait:
		Timer -= SDt;
		ReadTempoInput(SDt);
		BroadcastSkip(bSkipPress); // (the shot after the replay, or the pregame open, skipped: MatchBroadcast.cpp)
		if (bHurryNext || bSpikeNext) Timer = FMath::Min(Timer, 0.1f);
		UpdateCamera(Dt, Sim ? FocusPoint() : IdleFocus);
		// (the halftime show walks the last play's men off itself: MatchBroadcast.cpp)
		if (Sim && !Sim->isLive() && !BroadcastDrivesMen()) TickPostJog(SDt);
		if (Sim && !BroadcastDrivesMen()) SyncPlay(Dt, 1.f);
		{
			// -TagHuddleSecs=<n>: a watched or filmed game waits this long between plays, as a user picking his play does
			// (the films cut straight to the next snap, and what the men do in the huddle was never on them)
			// a played game: a confirm press (a pad's face button, ENTER, SPACE) ends the booth's hold - the opening kickoff
			// waited up to 45 s for the crew with no way past it. The booth is not told (it talks on over the kick); its
			// hold is passed over here for the 45 s it could last. Watched and automated games wait as before.
			bool bBoothHeld = BoothHold();
			// (the press has to come a moment into the wait: not the one that started the half from its menu)
			static double BoothHeldSince = -1.0;
			if (!bBoothHeld) BoothHeldSince = -1.0;
			else if (BoothHeldSince < 0.0) BoothHeldSince = FPlatformTime::Seconds();
			if (bBoothHeld && FPlatformTime::Seconds() < BoothSkipUntil) bBoothHeld = false;
			else if (bBoothHeld && FPlatformTime::Seconds() - BoothHeldSince > 0.25 && !bWatch && PC && !(bPadTest || bPadPlayTest || bMultiTest || bArtTest || bCallShot || bMoveShot || bPauseTest) &&
				!(Overlay && Overlay->HasModal()))
			{
				bool bHoldPress = false;
				for (const FKey& SkipKey : {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left,
					EKeys::Gamepad_FaceButton_Top, EKeys::Enter, EKeys::SpaceBar})
					bHoldPress = bHoldPress || PC->WasInputKeyJustPressed(SkipKey);
				if (bHoldPress)
				{
					BoothSkipUntil = FPlatformTime::Seconds() + 45.0;
					bBoothHeld = false;
					UE_LOG(LogTemp, Display, TEXT("TAG booth hold skipped by the player"));
				}
			}
			if (Timer <= 0 && !bBoothHeld)
			{
				Next();
				// the next play set up this frame (StartPresnap / StartLiveReturn put the men on their spots, SyncPlay): the
				// camera goes to it in the same frame. It had been updated above for the old scene, so the frame drawn showed the
				// men on their new spots in the last shot and the cut came a frame later (defects.py scene_reset, 17 per 100
				// plays on the 26 Nov overnight). (A staged kick's camera is UpdateKick's.)
				if (State == EMatchState::Presnap || State == EMatchState::Live)
				{
					BroadcastEndShotForPlay(); // (a between-play shot still up: ended here, not after the camera - MatchBroadcast.cpp)
					UpdateCamera(Dt, Sim ? FocusPoint() : IdleFocus);
				}
			}
		}
		break;
	case EMatchState::Presnap:
	{
		TickPresnap(SDt);
		{
			// automation: the formation setting up, men still walking to their spots
			static bool bSetShot = false;
			static int32 SetLogged = 0;
			if (bAuto && SetLogged < 6 && PresnapClock > 0.4f && Timer > 0.f && Timer < 100.f && PresnapClock - SDt <= 0.4f)
			{
				++SetLogged;
				int32 Walking = 0;
				for (const tag::Ent* E : Sim->ents) Walking += E->shiftTo ? 1 : 0;
				UE_LOG(LogTemp, Display, TEXT("TAG presnap at %.2f s: %d men still walking to their spots, timer %.2f"), PresnapClock, Walking, Timer);
			}
			if (bAuto && !bSetShot && PresnapClock > 0.4f && Sim->walking())
			{
				bSetShot = true;
				FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_setup.png"), false, false);
				UE_LOG(LogTemp, Display, TEXT("TAG auto screenshot setup: formation still setting at %.1f s"), PresnapClock);
			}
		}
		const bool bMenu = PreMode != EPre::Base;
		if (!(UserDef() && bMenu)) Timer -= SDt;
		PresnapClock += SDt;
		// (the play's jet motion runs through the set: the clock counts on while he is on his way)
		SetClock = Sim->walking() && !Sim->setButMotion() ? 0.f : SetClock + SDt;
		// the rolled pre-snap foul: once the men are set at the line (and before anyone can snap it - the user's snap
		// press draws it too, as a jump on the cadence does)
		if (PendingPreFlag && !Sim->walking() && (SetClock >= PreFlagAt || (UserOff() && bSnapKey && PreMode == EPre::Base && !bPreConsumed)))
		{
			CallPresnapFlag();
			break;
		}
		// the CPU snaps only once every man has been set for a full second (the rules; no snap teleports). A man who
		// somehow never reaches his spot is placed after 12 s so the game cannot hang (logged).
		const bool bCpuSet = SetClock >= 1.f && !Sim->motionPending(); // (and a jet man at his spot: snapped as he gets there)
		const bool bStuck = PresnapClock > 12.f && Sim->walking();
		if (bStuck) UE_LOG(LogTemp, Warning, TEXT("TAG presnap: men still walking after %.1f s, snapping anyway"), PresnapClock);
		if ((Timer <= 0 && (bCpuSet || bStuck) && !(BoothHold() && !UserOff())) || (UserDef() && !UserOff() && PresnapClock > 25.f && (bCpuSet || bStuck)) ||
			(bRealClock && !UserOff() && PlayClockF <= 1.5f && (bCpuSet || bStuck)) || // a CPU offense beats the play clock
			((bCallShot || bMoveShot) && PresnapClock > 4.f && bCpuSet) ||
			(UserOff() && bSnapKey && !bMenu && !bPreConsumed && !Sim->walking()))
		{
			EndPresnapUI();
			BroadcastAtSnap(); // (a man going off still on the field is put away; 11 a side checked: MatchBroadcast.cpp)
			State = EMatchState::Live;
			SyncRealClock();
			HudRunoffLeft = 0.0; // the snap: the scorebug clock is the game's again
			bPlayClockOn = false;
			Acc = 0;
			Overlay->SetHint(TEXT(""));
			ResetReplayBuf();
			ReplayClock = 0.f;
			ReplayPostRec = 0.f;
			bReplayWorthy = false;
		}
		SyncPlay(Dt, 1.f);
		UpdateCamera(Dt, FocusPoint());
		break;
	}
	case EMatchState::Live:
	{
		if (UserTeam)
		{
			ReadUserInput();
			TakeMovePresses(); // (a move pressed a beat early waits 150 ms for the sim to take it: Game/MatchFeel.cpp)
		}
		if (!HitStop.bOn) Acc += SDt; // (a hit-stop holds the sim's steps from the frame it starts: Game/MatchFeel.cpp)
		int N = 0;
		while (Acc >= STEP && Sim->isLive() && N++ < 12)
		{
			CapturePrev();
			OfferBufferedMoves();
			FeelBeforeStep(); // (Responsive movement, the step count, the hit-stop's check: Game/MatchFeel.cpp)
			Sim->step(STEP);
			FeelAfterStep();
			SettleBufferedMoves();
			Acc -= STEP;
			for (int32 Us = 0; Us < Sim->userSlots(); ++Us)
			{
				Sim->slotInput(Us).juke = false;
				Sim->slotInput(Us).stickMove = tag::Move::None; // a flick is one move, on the step it came in
			}
		}
		SyncPlay(Dt, FMath::Min(1.f, Acc / STEP));
		if (bPlaceKick) TickPlaceKickCalls(); // (IT'S GOOD / NO GOOD as the ball gets there: MatchKicking.cpp)
		UpdateCamera(Dt, FocusPoint());
		RecordReplayFrame(SDt);
		Overlay->SetClockOffset(Sim->t);
		{
			// a kick downed in the end zone: the returner on his knee (logged; filmed under automation)
			static unsigned long long KneelSerial = 0;
			if (bLiveReturn && Sim->kneelT >= 0 && Sim->serial != KneelSerial && Sim->t - Sim->kneelT > 0.45 && Sim->returner)
			{
				KneelSerial = Sim->serial;
				static int32 GKneelShots = 0;
				UE_LOG(LogTemp, Display, TEXT("TAG kick downed: #%d fielded it at %.1f yd (%.1f s) and took a knee in the end zone at %.1f, %.1f yd (%s, %s returner)"),
					Sim->returner->p->number, Sim->catchX, Sim->kickCatchT, Sim->returner->pos.x, Sim->returner->pos.y, Sim->cfg.kick->punt ? TEXT("punt") : TEXT("kickoff"),
					Sim->returner->userControlled ? TEXT("user") : TEXT("CPU"));
				if ((bAuto || bPadTest || FParse::Param(FCommandLine::Get(), TEXT("TagUserKicks"))) && GKneelShots < 4)
					FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_kneel_%d.png"), GKneelShots++), true, false);
			}
		}
		if (bLiveReturn && Sim->catcher && !bKickCatchLogged)
		{
			// the dynamic kickoff: nobody on the coverage may have moved before this moment
			bKickCatchLogged = true;
			double Near = 1e9, UserD = -1;
			for (const tag::Ent* E : Sim->def)
			{
				const double D = tag::dist(E->pos, Sim->catcher->pos);
				if (E != Sim->kickerEnt) Near = FMath::Min(Near, D);
				if (E->userControlled) UserD = D;
			}
			UE_LOG(LogTemp, Display, TEXT("TAG kickoff catch at %.1f s: nearest coverage %.1f yd away, user's man %.1f yd (%s)"), Sim->t, Near, UserD,
				Sim->cfg.kick && Sim->cfg.kick->punt ? TEXT("punt") : TEXT("kickoff"));
		}
		if (!Sim->isLive())
		{
			State = EMatchState::Post;
			Timer = bTackleCam ? 3.4f : bWatch ? 1.4f / Speed : 1.8f; // (the tackle cam films the whole fall)
			OnPlayDead();
			if (HelpStage > 0) Timer = FMath::Max(Timer, 8.8f); // (a help-up is on its way: the shot stays for it)
		}
		break;
	}
	case EMatchState::Post:
		Timer -= SDt;
		ReadTempoInput(SDt);
		if (bHurryNext || bSpikeNext) Timer = FMath::Min(Timer, 0.25f); // (no replay, no lingering: to the line)
		for (tag::Ent* E : Sim->ents) E->animT += Dt;
		Sim->coastBall(SDt); // (a pass out of bounds flies on to the ground)
		// the takeaway's man: on his feet again (up from his dive, his tackle), he holds the ball up
		if (TurnoverCelebMan && TurnoverCelebSerial == Sim->serial && Game)
		{
			AAthlete* TA = AthleteFor(TurnoverCelebMan->p, TurnoverCelebMan->side == tag::Side::Off ? Game->poss : tag::other(Game->poss));
			TurnoverWaitT += SDt;
			if (!TA) TurnoverCelebMan = nullptr;
			else if (!TA->IsDown() && !TA->IsFalling() && !TA->PreShotPlaying())
			{
				TurnoverCelebMan->anim = tag::Anim::Celebrate;
				TurnoverCelebMan->animT = 0;
				TurnoverCelebMan = nullptr;
				Timer = FMath::Max(Timer, 1.8f); // (the shot stays on it)
			}
			// (the shot waits for him to get up - a fall and a get-up run 4-6 s - rather than cutting away mid get-up)
			else if (TurnoverWaitT < 7.f) Timer = FMath::Max(Timer, 0.5f);
		}
		else TurnoverWaitT = 0.f;
		TickPostJog(SDt);
		SyncPlay(Dt, 1.f);
		{
			// automation: the men jogging back toward the ball after the whistle (the first scrimmage play's)
			static bool bJogShot = false;
			if (bAuto && !bJogShot && !Sim->isReturn() && Timer < 0.25f && PostPos.size() >= 6)
			{
				bJogShot = true;
				FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_postjog.png"), false, false);
				UE_LOG(LogTemp, Display, TEXT("TAG auto screenshot postjog: %d men jogging"), static_cast<int32>(PostPos.size()));
			}
		}
		UpdateCamera(Dt, FocusPoint());
		if ((ReplayPostRec += SDt) < 1.3f) RecordReplayFrame(SDt); // the whistle and the moment after it
		BroadcastSkip(bSkipPress); // (the one skip rule: the lead shot, else the last of the beat - MatchBroadcast.cpp)
		if (Timer <= 0)
		{
			if (!bTackleCam && !bHurryNext && !bSpikeNext && StartReplay()) break;
			Overlay->SetClockOffset(0);
			ApplyPlay();
		}
		break;
	case EMatchState::Replay:
		TickReplay(SDt, bSkipPress); // (the package: its angles, its ramp, the wipe - MatchBroadcast.cpp)
		break;
	case EMatchState::Kick:
		UpdateKick(SDt);
		FirePendingKick(); // (a user kick swung this frame: played now, it replaces the staged kick)
		break;
	default:
		UpdateCamera(Dt, Sim ? FocusPoint() : IdleFocus);
		if (State == EMatchState::Call) TickPostJog(SDt);
		if (Sim) SyncPlay(Dt, 1.f);
		break;
	}
	FeedBooth(Dt);
	FeedAudio(Dt);
	TickBroadcast(Dt); // (the field's graphics for the shot, the stands with the crowd: Game/MatchBroadcast.cpp)
	if (State != EMatchState::Menu && State != EMatchState::Final && !bLineup)
	{
		GatherOfficialBodies();
		TickUmpire(Dt);
		TickCrew(Dt);
		if (AnimStats())
		{
			// (-TagAnimStats only: nothing but the log line below reads these)
			// measured: how much of the time before the snap an official is in among the players (within 2 yd, within
			// 1.2 yd of one), logged as each snap goes ("TAG officials presnap")
			static double PreSecs = 0, Near2 = 0, Near1 = 0, PlaySecs = 0, PlayNear2 = 0;
			static int32 Plays = 0;
			static bool bWasPre = false;
			const bool bPreNow = State == EMatchState::Presnap && Sim && !Sim->isReturn();
			if (bPreNow && OffBodies.Num())
			{
				float MinD = 1e9f;
				auto Probe = [&](const AAthlete* O)
				{
					if (!O || O->IsHidden()) return;
					for (const FOffBody& B : OffBodies) MinD = FMath::Min(MinD, static_cast<float>(FVector::Dist2D(B.P, O->GetActorLocation())));
				};
				Probe(Umpire);
				for (const AAthlete* O : Crew) Probe(O);
				PreSecs += Dt;
				PlaySecs += Dt;
				if (MinD < 2.f * YD) { Near2 += Dt; PlayNear2 += Dt; }
				if (MinD < 1.2f * YD) Near1 += Dt;
			}
			if (bWasPre && !bPreNow)
			{
				++Plays;
				UE_LOG(LogTemp, Display, TEXT("TAG officials presnap: play %d had an official within 2 yd of a player for %.2f of %.1f s; so far %.1f%% of %.0f s within 2 yd, %.1f%% within 1.2 yd"),
					Plays, PlayNear2, PlaySecs, PreSecs > 0 ? 100.0 * Near2 / PreSecs : 0.0, PreSecs, PreSecs > 0 ? 100.0 * Near1 / PreSecs : 0.0);
				PlaySecs = PlayNear2 = 0;
			}
			bWasPre = bPreNow;
		}
	}
	bOverlayDue = true; // (UpdateIcons / UpdateHighlight / UpdateLineBars: OnPostActorTick, once camera and bodies are final)
	if (PlayArt && State != EMatchState::Presnap) PlayArt->SetShown(false);
	TickTackleCam(Dt);
}

// ------------------------------------------------------------------ the broadcast booth
bool AMatchDirector::BoothHold() const
{
	return Booth && Game && Booth->WantsHold();
}

void AMatchDirector::FeedBooth(float Dt)
{
	if (!Booth || !Game) return;
	tag::booth::View V;
	using St = tag::booth::View::Stage;
	switch (State)
	{
	case EMatchState::Menu: V.stage = St::Menu; break;
	case EMatchState::Call: V.stage = St::Call; break;
	case EMatchState::Presnap: V.stage = St::Presnap; break;
	case EMatchState::Live: V.stage = St::Live; break;
	case EMatchState::Post: V.stage = St::Post; break;
	case EMatchState::Kick: V.stage = St::Kick; break;
	case EMatchState::Wait: V.stage = St::Wait; break;
	case EMatchState::Final: V.stage = St::Final; break;
	case EMatchState::Paused: V.stage = Game->isFinal() ? St::Final : St::Wait; break; // (halftime box score: the booth goes on)
	case EMatchState::Replay: V.stage = St::Replay; break;
	}
	if (bEnginePaused) V.stage = St::Paused;
	V.sim = Sim.get();
	if (State == EMatchState::Kick && Kick)
	{
		V.kickKind = Kick->Kind;
		V.kickT = Kick->T;
		V.kickLead = Kick->Lead;
		V.kickHang = Kick->Res.flight ? Kick->Res.flight->hang : 2.5;
		V.sim = nullptr;
	}
	V.userGame = UserTeam.has_value() && !bAuto;
	V.night = bBoothNight;
	// a watched game lets the booth finish its breakdown before the next snap (a user's own pace is his)
	V.canHold = !UserTeam.has_value() || bWatch;
	V.cpuOffense = V.userGame && !UserOff(); // (the CPU's snap waits for the booth: the analyst gets a few seconds of it)
	Booth->Feed(Dt, *Game, V);
}

void AMatchDirector::FeedAudio(float Dt)
{
	if (!Audio) return;
	FTagAudioView V;
	using St = FTagAudioView::EStage;
	switch (State)
	{
	case EMatchState::Menu: V.Stage = St::Menu; break;
	case EMatchState::Call: V.Stage = St::Call; break;
	case EMatchState::Presnap: V.Stage = St::Presnap; break;
	case EMatchState::Live: V.Stage = St::Live; break;
	case EMatchState::Post: V.Stage = St::Post; break;
	case EMatchState::Kick: V.Stage = St::Kick; break;
	case EMatchState::Wait: V.Stage = St::Wait; break;
	case EMatchState::Final: V.Stage = St::Final; break;
	case EMatchState::Paused: V.Stage = Game && Game->isFinal() ? St::Final : St::Halftime; break;
	case EMatchState::Replay: V.Stage = St::Replay; break;
	}
	V.bPaused = bEnginePaused;
	V.G = State == EMatchState::Menu ? nullptr : Game.get();
	V.Sim = Sim.get();
	V.Dir = Dir;
	V.Ball = Ball ? Ball->GetActorLocation() : FVector::ZeroVector;
	V.bBoothTalking = Booth && Booth->Talking();
	V.bQuiet = bHighlights;
	if (State == EMatchState::Replay) V.ReplayT = ReplayT;
	if (State == EMatchState::Kick && Kick)
	{
		V.Sim = nullptr;
		V.KickKind = Kick->Kind;
		V.KickT = Kick->T;
		V.KickLead = Kick->Lead;
		V.KickHang = Kick->Res.flight ? static_cast<float>(Kick->Res.flight->hang) : 2.5f;
		V.KickDir = Kick->Dir;
		V.bKickBounce = Kick->bBounce;
		V.KickHops = Kick->Hops;
		V.bKickBlocked = Kick->bBlocked;
		V.KickDesc = Kick->Res.desc;
		if (Kick->Res.flight) V.KickGood = Kick->Res.flight->good;
		if (Kick->SceneT0 >= 0.f && Kick->Kind != 0)
		{
			// the long snap and the holder's (the punter's) catch
			V.KickSnapAt = Kick->SceneT0 + (Kick->Kind == 1 ? PuntSnapAt : FgSnapAt);
			V.KickCatchAt = Kick->SceneT0 + (Kick->Kind == 1 ? PuntCatchAt : FgCatchAt);
		}
	}
	Audio->Feed(Dt, V);
}
