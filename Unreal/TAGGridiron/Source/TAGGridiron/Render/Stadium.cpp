#include "Render/Stadium.h"

#include "Render/Athlete.h"
#include "Render/Coords.h"
#include "Render/TagFanSpawner.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"
#if WITH_EDITOR
#include "SkinnedAssetCompiler.h"
#endif
#include "EngineUtils.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "TextureResource.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Game/TagContact.h"
#include "Sim/Types.h"
#include "Sim/PlaySim.h"

using namespace TagCoords;

static constexpr double FW = tag::FIELD_W;
// the bowl's front edge: sideline stands 9 yd off the sideline, end stands 14 yd behind the end lines
static const double BowlSideY = (FW / 2 + 9) * YD;
static const double BowlHalfX = 74 * YD;      // midfield to the end stands
static const double BowlCornerR = 14 * YD;    // corner radius where the sideline and end stands meet
static const double BowlRoofEdgeD = 3900;     // roof canopy's front edge, out from the bowl's front edge (cm)
static const double BowlRoofEdgeZ = 5600;

/** The broadcast picture, the night rig, the weather, the wall, the wear and the kits (Research/NextLevel/03 A1-A6, A8).
 *  Each number reads the -Tag flag beside it for A/B tests, and the comment gives the value it replaced. -TagOldLook puts
 *  all of them back, -TagOldGrade only the picture (A1), -TagOldNight only the night lighting (A3), -TagOldWeather the
 *  weather (A2), -TagOldWall the wall and benches (A4), -TagOldWear the fixed wear (A5), -TagOldKit the clean kits (A6);
 *  a flag for one number still applies on top of those. */
struct FStadiumLook
{
	// A1, the picture. A TV feed has no vignette and no grain, and a camera's knee rolls the highlights off so a white
	// jersey keeps its folds: a softer, longer shoulder that approaches white without reaching it (the filmic curve with
	// Shoulder 0.20 / White clip 0 reaches 250 of 255 at about 1.9x the scene level the engine's 0.26 / 0.04 did).
	float Vignette = 0.f;            // -TagVignette=      (was 0.35)
	float Grain = 0.f;               // -TagGrain=         (was 0.04)
	float Shoulder = 0.20f;          // -TagShoulder=      (was the engine's 0.26; smaller is a softer roll-off)
	float WhiteClip = 0.f;           // -TagWhiteClip=     (was the engine's 0.04)
	float HiContrastNight = 0.7f;    // -TagHiContrast=    (was 0.8, the project's r.DefaultFeature.LocalExposure.HighlightContrastScale)
	float HiContrastDay = 0.8f;      // -TagHiContrastDay= (unchanged: the sunlit field keeps its punch)
	float BloomThrNight = 2.f;       // -TagBloomThr=      (was -1, no threshold): only what is brighter than white blooms
	float BloomThrDay = -1.f;        // -TagBloomThrDay=   (unchanged)
	// A3, the night rig. The stands were lit by the banks' own beams (46 degree cones aimed 8 yd past the middle put the
	// far lower bowl 7 degrees off the beam axis, so the seats got as much light as the grass and, being paler, read
	// brighter); the painted crowd's glow barely mattered. Tighter beams aimed at the middle keep the light on the field
	// and leave the front rows in the spill, the upper deck and roof in the dark. The candelas went up so the field
	// between the 20s keeps its illuminance (a point-light model of the 12 banks, not a render: ~82 -> ~90 lux between
	// the 20s, ~57 -> ~55 in the end zones; the far lower bowl's middle rows from ~64 to ~28 lux, upper deck ~27 -> ~1.5).
	float CrowdGlowNight = 0.03f;    // -TagCrowdGlow=     (was 0.07; 0.08 by day, unchanged)
	float LampGlow = 120.f;          // -TagLampGlow=      (was 60): the lamp housings' emissive at night, so they glare
	float BankCd = 70000.f;          // -TagBankCd=        (was 60000) sideline banks, before the throw scaling
	float BankInner = 16.f;          // -TagBankInner=     (was 24) cone half-angles, degrees
	float BankOuter = 28.f;          // -TagBankOuter=     (was 46)
	float BankAimYd = 0.f;           // -TagBankAimYd=     (was 8): how far past the middle the sideline banks aim
	float EndCd = 96000.f;           // -TagEndCd=         (was 50000) end-zone banks
	float EndInner = 20.f;           // -TagEndInner=      (was 24)
	float EndOuter = 36.f;           // -TagEndOuter=      (was 46)
	float MotionBlur = 0.15f;        // -TagMBlur=         (unchanged; 0 for the A8 ghosting tests)

	// A2, weather you can see (-TagOldWeather puts all of it back). Rain and snow days were exposed like a sunny one:
	// the day window (EV100 0.6..2.0) held the camera where the sunny field sits, so with the sun at a third the
	// field came out 1.85 stops darker (measured on the 11-24 matrix: grass median 0.16 -> 0.045 linear) and read as dusk.
	// An overcast camera opens up: the window moves down. A snow field is ~4 stops brighter than grass, so the camera
	// stops down for it (the night snow field sat at 0.73 linear, flat white in the tonemapper's shoulder).
	float OvercastEV = -3.3f;        // -TagOvercastEV=    (was 0): the day window's floor, EV100 (lower = may open up further). It
	                                 //                    moves only the floor (the auto exposure picks within the window): moving
	                                 //                    the whole window filmed 0.25 (through the fog's veil), 0.017 (no shift)
	                                 //                    and white (-3.3) on 11-25
	float SnowEV = 1.3f;             // -TagSnowEV=        (was 0): a snow field, day and night (higher = darker)
	float NightRainEV = -0.3f;       // -TagNightRainEV=   (was 0): a wet field at night is darker
	float RainSun = 1.2f;            // -TagRainSun=       (was 3; 2 first cut left crisp shadows): the sun through the overcast
	float RainSunAngle = 8.f;        // -TagRainSunAngle=  (was the engine's 0.5357 deg): soft, faint shadows
	float RainSky = 3.f;             // -TagRainSky=       (was 1; 2.2 first cut): the sky is the light on an overcast day
	float FogDay = 0.01f;            // -TagFogDay=        (was 0.03: the rain lineup at field level was a white-out) fog density in rain or snow by day
	float FogNight = 0.012f;         // -TagFogNight=      (was 0.03; 0.02 first cut): less grey veil over the floodlit field
	float FogRamp = 0.5f;            // -TagFogRamp=       (was 0): fog x (1 + FogRamp x (strength - 0.6)): thicker as it builds
	float PrecipStart = 0.45f;       // -TagPrecipStart=   (was 1, one fixed column): the weather's strength at kickoff
	float PrecipEnd = 1.f;           // -TagPrecipEnd=     ... by the end of the fourth quarter
	float PrecipFixed = -1.f;        // -TagPrecip=        a fixed strength 0..1 for look tests (-1: the ramp)
	int32 SnowFlakes = 40000;        // -TagSnowFlakes=    (was 9000 cubes, laid twice, over the whole bowl)
	int32 RainDrops = 48000;         // -TagRainDrops=     (was 12000 cubes, laid twice)
	int32 NearCount = 12000;         // -TagPrecipNear=    the sparse layer around the lens (0: none; 8000 first cut)
	float FarBox = 4000.f;           // -TagPrecipBox=     the dense box around the shot's subject, cm a side (25 m tall)
	float SnowFlakeCm = 2.f;         // -TagFlakeCm=       (was a 3 cm cube; 1.2 first cut: specks on the 11-25 film)
	float SnowMinPx = 2.5f;          // -TagFlakePx=       a flake never draws under this many pixels (1.6 first cut)
	float SnowFallCm = 110.f;        // -TagSnowFall=      (was 183 cm/s)
	float RainFallCm = 800.f;        // -TagRainFall=      (was 2743 cm/s)
	float RainStreakCm = 40.f;       // -TagStreakCm=      (was a 1 x 60 cm cube)
	float RainMinPx = 1.f;           // -TagStreakPx=      a streak's least width in pixels (3x that its least length)
	float SnowBrightDay = 1.2f;      // -TagSnowBright=    on-screen brightness of a flake, after exposure (0.55 first cut)
	float SnowBrightNight = 1.6f;    // -TagSnowBrightNight= (0.75 first cut)
	float RainBrightDay = 0.25f;     // -TagRainBright=    (0.12 first cut: barely there on the 11-25 film)
	float RainBrightNight = 0.4f;    // -TagRainBrightNight= (0.3 first cut)
	float SnowCoverStart = 0.7f;     // -TagSnowStart=     (was 1) x -TagSnowCover (0.5) at kickoff: the snow builds
	float SnowCoverEnd = 1.4f;       // -TagSnowEnd=       ... and by the fourth quarter
	float SnowAlbedo = 0.54f;        // -TagSnowAlbedo=    (was 0.62, the material's snow grey): snow that holds detail
	float SnowSweep = 1.f;           // -TagSnowSweep=     (was 0): the crews keep the lines swept: a green band each side
	float SnowBump = 2.f;            // -TagSnowBump=      (was 0): soft lumps in the snow's normal
	float RainWetStart = 0.7f;       // -TagWetStart=      (was 1): the turf's wetness at kickoff, 1 by the time the rain is full
	bool bOldWeather = false;

	// A4, the field-level wall (-TagOldWall: the plain team-colour padding and the cube benches)
	float WallLedDay = 1.2f;         // -TagWallLed=       LED ribbon brightness on the wall (was a plain sweep, HomeC x 0.6; 2 in the first cut: garish)
	float WallLedNight = 14.f;       // -TagWallLedNight=  (the night camera is ~4 stops less sensitive: see BuildBowl; 24 first cut)
	float WallPanelDay = 1.1f;       // -TagWallPanel=     the dark panels behind the wordmarks (the video boards' recipe)
	float WallPanelNight = 1.4f;     // -TagWallPanelNight=
	bool bOldWall = false;

	// A5, the live wear map (-TagOldWear: the season's wear texture, fixed all game)
	int32 WearRes = 1024;            // -TagWearRes=       texels along the field (half across): ~11 cm a texel
	float WearStep = 0.2f;           // -TagWearStep=      seconds between passes over the men
	float WearWalk = 0.003f;         // -TagWearWalk=      wear a man's feet lay on a spot each time he crosses it
	float WearLine = 0.025f;         // -TagWearLine=      a man at the line of scrimmage, per pass (his feet churning) ...
	float WearLineMax = 0.25f;       // -TagWearLineMax=   ... up to this much per man per snap
	float WearTackle = 0.12f;        // -TagWearTackle=    a tackle's spot
	float WearDown = 0.08f;          // -TagWearDown=      where a man goes down (piles, dives)
	float SnowTrample = 0.22f;       // -TagTrample=       trampled snow each time a man crosses a spot
	float SnowRefill = 0.006f;       // -TagSnowRefill=    share of the trampled snow covered again per second at full strength
	float MudRain = 0.5f;            // -TagMud=           mud with each stamp in rain, x its wear
	bool bOldWear = false;

	// A6, the men's kits (-TagOldKit: none of it; -TagKitForce=x every man at grime x, wet x, snow x, sweat x)
	float GrimeTackle = 0.05f;       // -TagGrimeTackle=   the tackler's front and arms, the carrier's arms, per tackle
	float GrimeFall = 0.07f;         // -TagGrimeFall=     a man going down: the side he lands on, his knees or seat, arms
	float GrimeLine = 0.0007f;       // -TagGrimeLine=     a lineman's knees (x 0.6 hands) per pass at the line, as long as
	                                 //                    the line's wear budget lasts (WearLineMax / WearLine passes a snap)
	float WetRate = 0.02f;           // -TagWetRate=       wetness per second in full rain (a minute to soak), x the rain's strength
	float WetGround = 0.25f;         // -TagWetGround=     soaked at once when he goes down on wet turf
	float SnowStick = 0.02f;         // -TagSnowStick=     snow on the shoulders per second standing still in full snow
	float SweatHot = 0.8f;           // -TagSweat=         sweat gloss at full exertion on a warm day (x 0.4 in the cold)
	float KitForce = -1.f;
	bool bOldKit = false;

	// Phase 4, A7: the sun moves (-TagOldSun: the fixed sun, pitch -48 yaw 135, and no late-game dusk)
	bool bOldSun = false;
	FString Kickoff;                 // -TagKickoff=       early (1:00 pm) | late (4:25 pm) | hours (16.5); default by the matchup
	int32 Week = 0;                  // -TagWeek=          1..18 (week 1 = Sept 8); 0: by the matchup
	float Latitude = 40.f;           // -TagLatitude=      degrees north (one for the league)
	float GameHours = 3.1f;          // -TagGameHours=     the broadcast's length, kickoff to the final whistle
	float LateShare = 0.35f;         // -TagLateShare=     share of day games that kick off late
	float SunMaxEVComp = 1.5f;       // -TagSunEVComp=     at most this many stops the day camera may open up as the sun gets low ...
	float SunEVK = 1.f;              // -TagSunEVK=        ... SunEVK x log2(sin 48 / sin elevation) (2.5 tried: too bright once the floor alone moves)
	                                 //                    as the roof's shadow takes more of the field (a shaded late-season
	                                 //                    field still reads a little darker: a 32 deg sun filmed 0.083 vs 0.165)
	float ProgressFixed = -1.f;      // -TagProgress=      look tests: the game's progress 0..1 held there (sun, weather)
	// Phase 4, A10: breath (-TagNoBreath: none)
	bool bNoBreath = false;
	float BreathTempF = 45.f;        // -TagBreathTemp=    colder than this (or snowing) and breath shows
	float BreathDist = 2600.f;       // -TagBreathDist=    only men this near the lens (cm): the close shots
	float BreathOpacity = 0.2f;      // -TagBreathOpacity= (0.35 first cut: a solid white blob at the mouth in the lineup close-up)
	// Phase 4, A13 (shader version): -TagNoCrowdAnim keeps the still painted crowd (M_TagCrowd)
	bool bNoCrowdAnim = false;
	float CrowdAlbedoNight = 0.6f;   // -TagCrowdNight=    the painted crowd's colour under the lights (was 1): the bowl falls
	                                 //                    off behind a night field (M_TagCrowd2 only)
	float StandsNight = 0.6f;        // -TagStandsNight=   the concrete, steps and roof underside at night (was 1)
	// Phase 4, A9: -TagOldHelmet sends the plain shell (no clear coat, no scuffs) to the coat master
	bool bOldHelmet = false;

	// A8, anti-aliasing (presets in ApplyAAPreset; -TagAA=tsr is the engine default as before).
	FString AAPreset = TEXT("tsrsharp"); // -TagAA=        tsr tsrsharp tsrlow tsrhist tsrq taa fxaa none dlaa dlss (was the engine's
	                                 //                    TSR, -TagAA=tsr: tsrsharp keeps less history on moving pixels, same cost;
	                                 //                    11-25 A/B: identical on still frames, crisper legs in motion)
	int32 AAVis = -1;                // -TagAAVis=         r.TSR.Visualize (0 samples kept, 2 rejected, 3 clamped)

	static const FStadiumLook& Get()
	{
		static const FStadiumLook Look = []()
		{
			FStadiumLook L;
			const TCHAR* Cmd = FCommandLine::Get();
			const bool bOldAll = FParse::Param(Cmd, TEXT("TagOldLook"));
			if (bOldAll || FParse::Param(Cmd, TEXT("TagOldGrade")))
			{
				L.Vignette = 0.35f; L.Grain = 0.04f; L.Shoulder = 0.26f; L.WhiteClip = 0.04f;
				L.HiContrastNight = 0.8f; L.HiContrastDay = 0.8f; L.BloomThrNight = -1.f; L.BloomThrDay = -1.f;
			}
			if (bOldAll || FParse::Param(Cmd, TEXT("TagOldNight")))
			{
				L.CrowdGlowNight = 0.07f; L.LampGlow = 60.f;
				L.BankCd = 60000.f; L.BankInner = 24.f; L.BankOuter = 46.f; L.BankAimYd = 8.f;
				L.EndCd = 50000.f; L.EndInner = 24.f; L.EndOuter = 46.f;
			}
			FParse::Value(Cmd, TEXT("TagVignette="), L.Vignette);
			FParse::Value(Cmd, TEXT("TagGrain="), L.Grain);
			FParse::Value(Cmd, TEXT("TagShoulder="), L.Shoulder);
			FParse::Value(Cmd, TEXT("TagWhiteClip="), L.WhiteClip);
			FParse::Value(Cmd, TEXT("TagHiContrast="), L.HiContrastNight);
			FParse::Value(Cmd, TEXT("TagHiContrastDay="), L.HiContrastDay);
			FParse::Value(Cmd, TEXT("TagBloomThr="), L.BloomThrNight);
			FParse::Value(Cmd, TEXT("TagBloomThrDay="), L.BloomThrDay);
			FParse::Value(Cmd, TEXT("TagCrowdGlow="), L.CrowdGlowNight);
			FParse::Value(Cmd, TEXT("TagLampGlow="), L.LampGlow);
			FParse::Value(Cmd, TEXT("TagBankCd="), L.BankCd);
			FParse::Value(Cmd, TEXT("TagBankInner="), L.BankInner);
			FParse::Value(Cmd, TEXT("TagBankOuter="), L.BankOuter);
			FParse::Value(Cmd, TEXT("TagBankAimYd="), L.BankAimYd);
			FParse::Value(Cmd, TEXT("TagEndCd="), L.EndCd);
			FParse::Value(Cmd, TEXT("TagEndInner="), L.EndInner);
			FParse::Value(Cmd, TEXT("TagEndOuter="), L.EndOuter);
			FParse::Value(Cmd, TEXT("TagMBlur="), L.MotionBlur);
			// A2
			if (bOldAll || FParse::Param(Cmd, TEXT("TagOldWeather")))
			{
				L.bOldWeather = true;
				L.OvercastEV = 0.f; L.SnowEV = 0.f; L.NightRainEV = 0.f; L.RainSun = 3.f; L.RainSunAngle = 0.5357f; L.RainSky = 1.f;
				L.FogNight = 0.03f; L.FogRamp = 0.f; L.PrecipStart = 1.f; L.PrecipEnd = 1.f;
				L.SnowCoverStart = 1.f; L.SnowCoverEnd = 1.f; L.SnowAlbedo = 0.62f; L.SnowSweep = 0.f; L.SnowBump = 0.f; L.RainWetStart = 1.f;
			}
			FParse::Value(Cmd, TEXT("TagOvercastEV="), L.OvercastEV);
			FParse::Value(Cmd, TEXT("TagSnowEV="), L.SnowEV);
			FParse::Value(Cmd, TEXT("TagNightRainEV="), L.NightRainEV);
			FParse::Value(Cmd, TEXT("TagRainSun="), L.RainSun);
			FParse::Value(Cmd, TEXT("TagRainSunAngle="), L.RainSunAngle);
			FParse::Value(Cmd, TEXT("TagRainSky="), L.RainSky);
			FParse::Value(Cmd, TEXT("TagFogDay="), L.FogDay);
			FParse::Value(Cmd, TEXT("TagFogNight="), L.FogNight);
			FParse::Value(Cmd, TEXT("TagFogRamp="), L.FogRamp);
			FParse::Value(Cmd, TEXT("TagPrecipStart="), L.PrecipStart);
			FParse::Value(Cmd, TEXT("TagPrecipEnd="), L.PrecipEnd);
			FParse::Value(Cmd, TEXT("TagPrecip="), L.PrecipFixed);
			FParse::Value(Cmd, TEXT("TagSnowFlakes="), L.SnowFlakes);
			FParse::Value(Cmd, TEXT("TagRainDrops="), L.RainDrops);
			FParse::Value(Cmd, TEXT("TagPrecipNear="), L.NearCount);
			FParse::Value(Cmd, TEXT("TagPrecipBox="), L.FarBox);
			FParse::Value(Cmd, TEXT("TagFlakeCm="), L.SnowFlakeCm);
			FParse::Value(Cmd, TEXT("TagFlakePx="), L.SnowMinPx);
			FParse::Value(Cmd, TEXT("TagSnowFall="), L.SnowFallCm);
			FParse::Value(Cmd, TEXT("TagRainFall="), L.RainFallCm);
			FParse::Value(Cmd, TEXT("TagStreakCm="), L.RainStreakCm);
			FParse::Value(Cmd, TEXT("TagStreakPx="), L.RainMinPx);
			FParse::Value(Cmd, TEXT("TagSnowBright="), L.SnowBrightDay);
			FParse::Value(Cmd, TEXT("TagSnowBrightNight="), L.SnowBrightNight);
			FParse::Value(Cmd, TEXT("TagRainBright="), L.RainBrightDay);
			FParse::Value(Cmd, TEXT("TagRainBrightNight="), L.RainBrightNight);
			FParse::Value(Cmd, TEXT("TagSnowStart="), L.SnowCoverStart);
			FParse::Value(Cmd, TEXT("TagSnowEnd="), L.SnowCoverEnd);
			FParse::Value(Cmd, TEXT("TagSnowAlbedo="), L.SnowAlbedo);
			FParse::Value(Cmd, TEXT("TagSnowSweep="), L.SnowSweep);
			FParse::Value(Cmd, TEXT("TagSnowBump="), L.SnowBump);
			FParse::Value(Cmd, TEXT("TagWetStart="), L.RainWetStart);
			L.SnowFlakes = FMath::Clamp(L.SnowFlakes, 0, 200000);
			L.RainDrops = FMath::Clamp(L.RainDrops, 0, 200000);
			L.NearCount = FMath::Clamp(L.NearCount, 0, 50000);
			L.FarBox = FMath::Clamp(L.FarBox, 500.f, 20000.f);
			// A4
			L.bOldWall = bOldAll || FParse::Param(Cmd, TEXT("TagOldWall"));
			FParse::Value(Cmd, TEXT("TagWallLed="), L.WallLedDay);
			FParse::Value(Cmd, TEXT("TagWallLedNight="), L.WallLedNight);
			FParse::Value(Cmd, TEXT("TagWallPanel="), L.WallPanelDay);
			FParse::Value(Cmd, TEXT("TagWallPanelNight="), L.WallPanelNight);
			// A5
			L.bOldWear = bOldAll || FParse::Param(Cmd, TEXT("TagOldWear"));
			FParse::Value(Cmd, TEXT("TagWearRes="), L.WearRes);
			L.WearRes = FMath::Clamp(L.WearRes, 128, 4096);
			FParse::Value(Cmd, TEXT("TagWearStep="), L.WearStep);
			L.WearStep = FMath::Clamp(L.WearStep, 0.05f, 2.f);
			FParse::Value(Cmd, TEXT("TagWearWalk="), L.WearWalk);
			FParse::Value(Cmd, TEXT("TagWearLine="), L.WearLine);
			FParse::Value(Cmd, TEXT("TagWearLineMax="), L.WearLineMax);
			FParse::Value(Cmd, TEXT("TagWearTackle="), L.WearTackle);
			FParse::Value(Cmd, TEXT("TagWearDown="), L.WearDown);
			FParse::Value(Cmd, TEXT("TagTrample="), L.SnowTrample);
			FParse::Value(Cmd, TEXT("TagSnowRefill="), L.SnowRefill);
			FParse::Value(Cmd, TEXT("TagMud="), L.MudRain);
			// A6
			L.bOldKit = bOldAll || FParse::Param(Cmd, TEXT("TagOldKit"));
			FParse::Value(Cmd, TEXT("TagGrimeTackle="), L.GrimeTackle);
			FParse::Value(Cmd, TEXT("TagGrimeFall="), L.GrimeFall);
			FParse::Value(Cmd, TEXT("TagGrimeLine="), L.GrimeLine);
			FParse::Value(Cmd, TEXT("TagWetRate="), L.WetRate);
			FParse::Value(Cmd, TEXT("TagWetGround="), L.WetGround);
			FParse::Value(Cmd, TEXT("TagSnowStick="), L.SnowStick);
			FParse::Value(Cmd, TEXT("TagSweat="), L.SweatHot);
			FParse::Value(Cmd, TEXT("TagKitForce="), L.KitForce);
			// Phase 4
			L.bOldSun = bOldAll || FParse::Param(Cmd, TEXT("TagOldSun"));
			FParse::Value(Cmd, TEXT("TagKickoff="), L.Kickoff);
			FParse::Value(Cmd, TEXT("TagWeek="), L.Week);
			FParse::Value(Cmd, TEXT("TagLatitude="), L.Latitude);
			FParse::Value(Cmd, TEXT("TagGameHours="), L.GameHours);
			FParse::Value(Cmd, TEXT("TagLateShare="), L.LateShare);
			FParse::Value(Cmd, TEXT("TagSunEVComp="), L.SunMaxEVComp);
			FParse::Value(Cmd, TEXT("TagSunEVK="), L.SunEVK);
			FParse::Value(Cmd, TEXT("TagProgress="), L.ProgressFixed);
			L.bNoBreath = bOldAll || FParse::Param(Cmd, TEXT("TagNoBreath"));
			FParse::Value(Cmd, TEXT("TagBreathTemp="), L.BreathTempF);
			FParse::Value(Cmd, TEXT("TagBreathDist="), L.BreathDist);
			FParse::Value(Cmd, TEXT("TagBreathOpacity="), L.BreathOpacity);
			L.bNoCrowdAnim = bOldAll || FParse::Param(Cmd, TEXT("TagNoCrowdAnim"));
			if (bOldAll || FParse::Param(Cmd, TEXT("TagOldNight"))) { L.CrowdAlbedoNight = 1.f; L.StandsNight = 1.f; }
			FParse::Value(Cmd, TEXT("TagCrowdNight="), L.CrowdAlbedoNight);
			FParse::Value(Cmd, TEXT("TagStandsNight="), L.StandsNight);
			L.bOldHelmet = bOldAll || FParse::Param(Cmd, TEXT("TagOldHelmet"));
			// A8
			if (bOldAll) L.AAPreset = TEXT("tsr");
			FParse::Value(Cmd, TEXT("TagAA="), L.AAPreset);
			FParse::Value(Cmd, TEXT("TagAAVis="), L.AAVis);
			UE_LOG(LogTemp, Display, TEXT("TAG look A2: %s, EV overcast %.2f snow %.2f night rain %.2f, sun %.1f (%.1f deg) sky %.1f, fog %.3f/%.3f ramp %.2f, strength %.2f -> %.2f%s, flakes %d drops %d near %d box %.0f | A4 wall %s | A5 wear %s %d | A6 kits %s%s | A8 AA '%s'"),
				L.bOldWeather ? TEXT("old") : TEXT("on"), L.OvercastEV, L.SnowEV, L.NightRainEV, L.RainSun, L.RainSunAngle, L.RainSky, L.FogDay, L.FogNight,
				L.FogRamp, L.PrecipStart, L.PrecipEnd, L.PrecipFixed >= 0.f ? *FString::Printf(TEXT(" (fixed %.2f)"), L.PrecipFixed) : TEXT(""),
				L.SnowFlakes, L.RainDrops, L.NearCount, L.FarBox, L.bOldWall ? TEXT("old") : TEXT("on"), L.bOldWear ? TEXT("old") : TEXT("on"),
				L.WearRes, L.bOldKit ? TEXT("old") : TEXT("on"), L.KitForce >= 0.f ? *FString::Printf(TEXT(" (forced %.2f)"), L.KitForce) : TEXT(""), *L.AAPreset);
			UE_LOG(LogTemp, Display, TEXT("TAG look: vignette %.2f grain %.2f shoulder %.2f white clip %.2f hi contrast %.2f/%.2f bloom thr %.1f/%.1f | night: crowd glow %.2f lamps %.0f banks %.0f cd %.0f/%.0f deg aim %.0f yd, ends %.0f cd %.0f/%.0f deg"),
				L.Vignette, L.Grain, L.Shoulder, L.WhiteClip, L.HiContrastDay, L.HiContrastNight, L.BloomThrDay, L.BloomThrNight,
				L.CrowdGlowNight, L.LampGlow, L.BankCd, L.BankInner, L.BankOuter, L.BankAimYd, L.EndCd, L.EndInner, L.EndOuter);
			return L;
		}();
		return Look;
	}
};

/**
 * A8, the anti-aliasing A/B (Research/NextLevel/03 A8): console variable sets behind -TagAA=<preset>, set by code (so
 * they win over the project settings and the scalability groups). Nothing changes without the flag. The DLSS presets
 * need NVIDIA's DLSS plugin for UE 5.8, which is not installed (checked 11-24: Engine/Plugins/Runtime/Nvidia has only
 * Reflex and GeForceNOW, the project has no Plugins folder); without it they log that and stay on TSR.
 *   tsr        TSR as shipped (r.AntiAliasingMethod 4): the baseline, said explicitly
 *   tsrsharp   TSR, less history kept after a rejection and on moving pixels (r.TSR.ShadingRejection.SampleCount 2 -> 1,
 *              r.TSR.Velocity.WeightClampingSampleCount 4 -> 2): shorter trails behind legs and the ball, a little more
 *              shimmer on the yard lines
 *   tsrlow     tsrsharp and a shorter history (r.TSR.History.SampleCount 16 -> 8): the least ghosting TSR does
 *   tsrhist    TSR with a 200 % history (r.TSR.History.ScreenPercentage): sharper still image, costs GPU
 *   tsrq       TSR upscaling from 66.7 % (r.ScreenPercentage): the like-for-like cost of DLSS Quality
 *   taa        the old temporal AA (2)      fxaa   no history at all (1): if a trail is there it is not TSR's
 *   none       no anti-aliasing (0)
 *   dlaa       DLAA at 100 % (r.NGX.DLSS.Enable 1, r.NGX.DLAA.Enable 1)
 *   dlss       DLSS Quality (r.NGX.DLSS.Enable 1, r.ScreenPercentage 66.7)
 * -TagAAVis=N sets r.TSR.Visualize (0 samples in the history, 2 where it was rejected, 3 where it was clamped).
 */
static void ApplyAAPreset(const FStadiumLook& L)
{
	auto SetCVar = [](const TCHAR* Name, const TCHAR* Value) -> bool
	{
		IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
		if (!V)
		{
			UE_LOG(LogTemp, Warning, TEXT("TAG AA: no console variable %s"), Name);
			return false;
		}
		V->Set(Value, ECVF_SetByCode);
		return true;
	};
	const FString P = L.AAPreset.ToLower();
	if (!P.IsEmpty())
	{
		bool bOk = true;
		if (P == TEXT("tsr")) bOk = SetCVar(TEXT("r.AntiAliasingMethod"), TEXT("4"));
		else if (P == TEXT("tsrsharp") || P == TEXT("tsrlow"))
		{
			bOk = SetCVar(TEXT("r.AntiAliasingMethod"), TEXT("4"));
			bOk &= SetCVar(TEXT("r.TSR.ShadingRejection.SampleCount"), TEXT("1"));
			bOk &= SetCVar(TEXT("r.TSR.Velocity.WeightClampingSampleCount"), TEXT("2"));
			if (P == TEXT("tsrlow")) bOk &= SetCVar(TEXT("r.TSR.History.SampleCount"), TEXT("8"));
		}
		else if (P == TEXT("tsrhist"))
		{
			bOk = SetCVar(TEXT("r.AntiAliasingMethod"), TEXT("4"));
			bOk &= SetCVar(TEXT("r.TSR.History.ScreenPercentage"), TEXT("200"));
		}
		else if (P == TEXT("tsrq"))
		{
			bOk = SetCVar(TEXT("r.AntiAliasingMethod"), TEXT("4"));
			bOk &= SetCVar(TEXT("r.ScreenPercentage"), TEXT("66.67"));
		}
		else if (P == TEXT("taa")) bOk = SetCVar(TEXT("r.AntiAliasingMethod"), TEXT("2"));
		else if (P == TEXT("fxaa")) bOk = SetCVar(TEXT("r.AntiAliasingMethod"), TEXT("1"));
		else if (P == TEXT("none")) bOk = SetCVar(TEXT("r.AntiAliasingMethod"), TEXT("0"));
		else if (P == TEXT("dlaa") || P == TEXT("dlss"))
		{
			// (the plugin's own variables: look them up first, so a missing plugin changes nothing)
			if (IConsoleManager::Get().FindConsoleVariable(TEXT("r.NGX.DLSS.Enable")))
			{
				bOk = SetCVar(TEXT("r.NGX.DLSS.Enable"), TEXT("1"));
				if (P == TEXT("dlaa"))
				{
					bOk &= SetCVar(TEXT("r.NGX.DLAA.Enable"), TEXT("1"));
					bOk &= SetCVar(TEXT("r.ScreenPercentage"), TEXT("100"));
				}
				else
				{
					if (IConsoleManager::Get().FindConsoleVariable(TEXT("r.NGX.DLAA.Enable"))) SetCVar(TEXT("r.NGX.DLAA.Enable"), TEXT("0"));
					bOk &= SetCVar(TEXT("r.ScreenPercentage"), TEXT("66.67"));
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("TAG AA: -TagAA=%s asks for DLSS, but the DLSS plugin is not installed (no r.NGX.DLSS.Enable): staying on TSR"), *P);
				bOk = false;
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("TAG AA: unknown preset '%s' (tsr tsrsharp tsrlow tsrhist tsrq taa fxaa none dlaa dlss)"), *P);
			bOk = false;
		}
		UE_LOG(LogTemp, Display, TEXT("TAG AA preset %s%s"), *P, bOk ? TEXT("") : TEXT(" (not fully applied)"));
	}
	if (L.AAVis >= 0) SetCVar(TEXT("r.TSR.Visualize"), *FString::FromInt(L.AAVis));
}

namespace
{
	// rounded-rectangle path along the bowl's front edge. Each sample carries its outward normal angle Phi; a point at
	// offset d outward is P + N(Phi) * d, and its distance around the bowl at that offset is L + d * Phi, so the seat
	// spacing holds through the corners.
	struct FBowlSample { FVector2D P; double Phi = 0; double L = 0; };
	struct FBowlBuf { TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FVector2D> UV; };

	TArray<FBowlSample> BowlPath(double X0, double X1, double HalfY, double Rc, int32 NCorner)
	{
		const FVector2D C[4] = {{X1 - Rc, HalfY - Rc}, {X0 + Rc, HalfY - Rc}, {X0 + Rc, -HalfY + Rc}, {X1 - Rc, -HalfY + Rc}};
		TArray<FBowlSample> S;
		for (int32 q = 0; q < 4; ++q)
			for (int32 k = 0; k <= NCorner; ++k)
			{
				const double Phi = (q + double(k) / NCorner) * UE_HALF_PI;
				S.Add({C[q] + Rc * FVector2D(FMath::Cos(Phi), FMath::Sin(Phi)), Phi, 0.0});
			}
		S.Add({S[0].P, 2 * UE_PI, 0.0}); // closes the loop along the end stand
		for (int32 i = 1; i < S.Num(); ++i) S[i].L = S[i - 1].L + FVector2D::Distance(S[i].P, S[i - 1].P);
		return S;
	}

	FBowlSample PathAt(const TArray<FBowlSample>& S, double L)
	{
		for (int32 i = 1; i < S.Num(); ++i)
			if (S[i].L >= L)
			{
				const double A = (L - S[i - 1].L) / FMath::Max(S[i].L - S[i - 1].L, 1e-6);
				return {FMath::Lerp(S[i - 1].P, S[i].P, A), FMath::Lerp(S[i - 1].Phi, S[i].Phi, A), L};
			}
		return S.Last();
	}

	/** A short stretch of path centred on L, HalfW either side (aisles, tunnels, boards). */
	TArray<FBowlSample> PathPatch(const TArray<FBowlSample>& S, double L, double HalfW)
	{
		const FBowlSample C = PathAt(S, L);
		const FVector2D T(-FMath::Sin(C.Phi), FMath::Cos(C.Phi));
		return {{C.P - T * HalfW, C.Phi, L - HalfW}, {C.P + T * HalfW, C.Phi, L + HalfW}};
	}

	// Sweeps a cross-section (d outward from the path, z up; one flat face per profile segment) along the path. Hint
	// (d, z) points to the front side; both sides are emitted so nothing vanishes from behind. UVs: U is distance
	// around the bowl / USc, V is distance along the profile / VSc.
	void Sweep(FBowlBuf& B, const TArray<FBowlSample>& Path, const TArray<FVector2D>& Prof, const FVector2D& Hint, double USc, double VSc)
	{
		double V0 = 0;
		for (int32 k = 0; k + 1 < Prof.Num(); ++k)
		{
			const FVector2D Pa = Prof[k], Pb = Prof[k + 1];
			const double Seg = FVector2D::Distance(Pa, Pb);
			for (int32 Back = 0; Back < 2; ++Back)
			{
				const int32 Base = B.V.Num();
				for (const FBowlSample& S : Path)
				{
					const FVector N3(FMath::Cos(S.Phi), FMath::Sin(S.Phi), 0), T3(-N3.Y, N3.X, 0);
					FVector Nrm = FVector::CrossProduct(T3, N3 * (Pb.X - Pa.X) + FVector::UpVector * (Pb.Y - Pa.Y)).GetSafeNormal();
					if (FVector::DotProduct(Nrm, N3 * Hint.X + FVector::UpVector * Hint.Y) < 0) Nrm = -Nrm;
					if (Back) Nrm = -Nrm;
					for (int32 j = 0; j < 2; ++j)
					{
						const FVector2D E = j ? Pb : Pa;
						B.V.Add(FVector(S.P.X + N3.X * E.X, S.P.Y + N3.Y * E.X, E.Y));
						B.N.Add(Nrm);
						B.UV.Add(FVector2D((S.L + E.X * S.Phi) / USc, (V0 + (j ? Seg : 0.0)) / VSc));
					}
				}
				for (int32 i = 0; i + 1 < Path.Num(); ++i)
				{
					const int32 A0 = Base + 2 * i, B0 = A0 + 1, A1 = A0 + 2, B1 = A0 + 3;
					// UE front faces: (v1 - v0) x (v2 - v0) points away from the face normal (left-handed, clockwise)
					const bool bFlip = FVector::DotProduct(FVector::CrossProduct(B.V[A1] - B.V[A0], B.V[B1] - B.V[A0]), B.N[A0]) > 0;
					B.T.Append(bFlip ? TArray<int32>{A0, B1, A1, A0, B0, B1} : TArray<int32>{A0, A1, B1, A0, B1, B0});
				}
			}
			V0 += Seg;
		}
	}

	/** Point on a pitched deck's top surface: deck box centred at (Dc, Zc), Depth along the slope, 50 cm thick;
	 *  T 0..1 from the front row to the back row, Lift off the surface. */
	FVector2D DeckPt(double Dc, double Zc, double Depth, double PitchDeg, double T, double Lift = 0)
	{
		const double P = FMath::DegreesToRadians(PitchDeg);
		const FVector2D Along(FMath::Cos(P), FMath::Sin(P)), Up(-FMath::Sin(P), FMath::Cos(P));
		return FVector2D(Dc, Zc) + Along * (Depth * (T - 0.5)) + Up * (25 + Lift);
	}
}

AStadium::AStadium()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

UStaticMeshComponent* AStadium::AddMesh(UStaticMesh* Mesh, const FVector& Loc, const FVector& Scale, const FLinearColor& Color, float Rough, const FRotator& Rot, bool bShadow)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(RootComponent);
	C->SetStaticMesh(Mesh);
	C->SetRelativeLocation(Loc);
	C->SetRelativeRotation(Rot);
	C->SetRelativeScale3D(Scale);
	C->SetMaterial(0, TagColorMat(this, Color, Rough));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadow);
	C->RegisterComponent();
	return C;
}

UInstancedStaticMeshComponent* AStadium::AddISM(UStaticMesh* Mesh, const FLinearColor& Color, float Rough, bool bShadow)
{
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
	C->SetupAttachment(RootComponent);
	C->SetStaticMesh(Mesh);
	C->SetMaterial(0, TagColorMat(this, Color, Rough));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadow);
	C->RegisterComponent();
	return C;
}

void AStadium::AddText(const FString& S, const FVector& Loc, const FRotator& Rot, float Size, const FColor& Color)
{
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
	T->SetupAttachment(RootComponent);
	T->SetText(FText::FromString(S));
	T->SetWorldSize(Size);
	T->SetTextRenderColor(Color);
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetVerticalAlignment(EVRTA_TextCenter);
	T->SetRelativeLocation(Loc);
	T->SetRelativeRotation(Rot);
	T->RegisterComponent();
}

void AStadium::BuildBowl(const tag::Team& Home, const tag::Team& Away, bool bNight)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	const TArray<FBowlSample> Path = BowlPath(50 * YD - BowlHalfX, 50 * YD + BowlHalfX, BowlSideY, BowlCornerR, 16);
	const double Around = Path.Last().L;

	// the two seating decks: the same boxes the stands were (lower 36 m deep at 33 deg, upper 28 m at 38 deg), so the
	// Mass fans in the front sideline rows still sit on the seats
	auto Lower = [](double T, double Lift = 0) { return DeckPt(1600, 1050, 3600, 33, T, Lift); };
	auto Upper = [](double T, double Lift = 0) { return DeckPt(4200, 3300, 2800, 38, T, Lift); };
	const FVector2D Wall(-1, 0), Up(0, 1), Down(0, -1);

	FBowlBuf Crowd, Concrete, Steps, Tunnel, Padding, Glass, Ribbon, Roof, Edge, WallLed;
	const FStadiumLook& Look = FStadiumLook::Get();
	// seats: crowd texture is 16 rows of fans (~1450 cm of slope) by ~55 seats (~3000 cm) per tile. The decks are real
	// seating steps, one row of fans per riser + tread, so the stands get depth and a shadow line along every row; the
	// front rows (where the 3D fans sit) stay on the plain slope. A landing two thirds up the lower bowl is the cross
	// aisle, and the wall behind it carries the tunnel mouths (vomitories).
	const double RowLen = 1450.0 / 16.0;
	const FVector2D StepFace(-1, 1); // risers face the field, treads face up
	auto StepRun = [&](FVector2D C, int32 Rows, double PitchDeg, const FVector2D& Shift = FVector2D::ZeroVector)
	{
		const double R = FMath::DegreesToRadians(PitchDeg), Tread = RowLen * FMath::Cos(R), Rise = RowLen * FMath::Sin(R);
		TArray<FVector2D> P{C + Shift};
		for (int32 i = 0; i < Rows; ++i)
		{
			C.Y += Rise;
			P.Add(C + Shift);
			C.X += Tread;
			P.Add(C + Shift);
		}
		return P;
	};
	const double StepV = 16.0 * RowLen * (FMath::Cos(FMath::DegreesToRadians(33.0)) + FMath::Sin(FMath::DegreesToRadians(33.0)));
	const double StepVU = 16.0 * RowLen * (FMath::Cos(FMath::DegreesToRadians(38.0)) + FMath::Sin(FMath::DegreesToRadians(38.0)));
	const double T0 = 0.1, TLand = 0.58, LandDepth = 300;
	const int32 Rows1 = FMath::RoundToInt((TLand - T0) * 3600 / RowLen);
	const TArray<FVector2D> Lower1 = StepRun(Lower(T0), Rows1, 33);
	const FVector2D LandA = Lower1.Last(), LandB = LandA + FVector2D(LandDepth, 0);
	const FVector2D WallTop(LandB.X, LandA.Y + LandDepth * FMath::Tan(FMath::DegreesToRadians(33.0)));
	const int32 Rows2 = FMath::FloorToInt((FVector2D::Distance(WallTop, Lower(1)) - 1.0) / RowLen);
	const TArray<FVector2D> Lower2 = StepRun(WallTop, Rows2, 33);
	const int32 RowsU = FMath::FloorToInt(2800.0 / RowLen);
	const TArray<FVector2D> UpperS = StepRun(Upper(0), RowsU, 38);
	FBowlBuf Flat;
	Sweep(Flat, Path, {Lower(0), Lower(T0)}, Up, 3000, 1450);
	Sweep(Crowd, Path, Lower1, StepFace, 3000, StepV);
	Sweep(Crowd, Path, Lower2, StepFace, 3000, StepV);
	Sweep(Crowd, Path, UpperS, StepFace, 3000, StepVU);
	Crowd.T.Reserve(Crowd.T.Num() + Flat.T.Num());
	for (int32 t : Flat.T) Crowd.T.Add(t + Crowd.V.Num());
	Crowd.V.Append(Flat.V);
	Crowd.N.Append(Flat.N);
	Crowd.UV.Append(Flat.UV);
	for (FVector2D& UV : Crowd.UV) UV = FVector2D(UV.Y, UV.X); // T_Crowd is stored turned: rows along U, seats along V
	// the landing (cross aisle) and the wall behind it
	Sweep(Steps, Path, {LandA + FVector2D(0, 1), LandB + FVector2D(0, 1)}, Up, 100, 100);
	Sweep(Concrete, Path, {LandB, WallTop}, Wall, 100, 100);
	// field-level wall and its cap. A4: it filled the top of every TV frame as a flat team-colour slab; now a dark
	// padded band at the bottom, an LED ribbon in the team colours scrolling slowly around the bowl, a dark trim on top,
	// and wordmark and sponsor panels and tunnel mouths on it (BuildWallDressing). -TagOldWall: the plain 2.2 m padding.
	if (Look.bOldWall) Sweep(Padding, Path, {{-60, 0}, {-60, 220}}, Wall, 100, 100);
	else
	{
		Sweep(Padding, Path, {{-60, 0}, {-60, 75}}, Wall, 100, 100);
		Sweep(WallLed, Path, {{-60, 75}, {-60, 195}}, Wall, 100, 100);
		Sweep(Padding, Path, {{-60, 195}, {-60, 220}}, Wall, 100, 100);
	}
	Sweep(Concrete, Path, {{-60, 220}, {80, 220}}, Up, 100, 100);
	// stair aisles every ~17 m around both decks (offset on the upper deck), stepped with the seats; tunnel mouths in the
	// landing wall between every other pair of aisles
	const double Step = 1700;
	const FVector2D AisleLift(-3, 3);
	const TArray<FVector2D> AisleL1 = StepRun(Lower(T0), Rows1, 33, AisleLift), AisleL2 = StepRun(WallTop, Rows2, 33, AisleLift);
	const TArray<FVector2D> AisleU = StepRun(Upper(0), RowsU, 38, AisleLift);
	for (double L = Step * 0.5; L < Around; L += Step)
	{
		const TArray<FBowlSample> At = PathPatch(Path, L, 60);
		Sweep(Steps, At, {Lower(0.0, 3), Lower(T0, 3)}, Up, 100, 100);
		Sweep(Steps, At, AisleL1, StepFace, 100, 100);
		Sweep(Steps, At, AisleL2, StepFace, 100, 100);
		Sweep(Steps, PathPatch(Path, L + Step * 0.5, 60), AisleU, StepFace, 100, 100);
		if (FMath::RoundToInt(L / Step) % 2 == 0)
			Sweep(Tunnel, PathPatch(Path, L + Step * 0.5, 160), {LandB + FVector2D(-2, 1), FVector2D(LandB.X - 2, FMath::Min(WallTop.Y, LandA.Y + 230))}, Wall, 100, 100);
	}
	// club level between the decks: concrete lip, suite glass, LED ribbon board, lip, and the upper deck's front cap
	const double F = 2990;
	Sweep(Concrete, Path, {{F, 1975}, {F, 2010}}, Wall, 100, 100);
	Sweep(Glass, Path, {{F, 2010}, {F, 2230}}, Wall, 100, 100);
	Sweep(Concrete, Path, {{F, 2230}, {F, 2260}}, Wall, 100, 100);
	Sweep(Ribbon, Path, {{F, 2260}, {F, 2440}}, Wall, 100, 180);
	Sweep(Concrete, Path, {{F, 2440}, {F, 2480}}, Wall, 100, 100);
	Sweep(Concrete, Path, {{F, 2480}, {Upper(0).X + 10, 2480}}, Up, 100, 100);
	// back of the upper deck: press / suite level glass under the roof
	const double Bk = Upper(1).X + 10;
	Sweep(Concrete, Path, {{Bk, Upper(1).Y - 15}, {Bk, 4250}}, Wall, 100, 100);
	Sweep(Glass, Path, {{Bk, 4250}, {Bk, 4650}}, Wall, 100, 100);
	Sweep(Concrete, Path, {{Bk, 4650}, {Bk, 5900}}, Wall, 100, 100);
	// roof canopy: team-colour front edge and the steel underside, rising gently outward
	Sweep(Edge, Path, {{BowlRoofEdgeD, BowlRoofEdgeZ - 180}, {BowlRoofEdgeD, BowlRoofEdgeZ}}, Wall, 100, 100);
	Sweep(Roof, Path, {{BowlRoofEdgeD, BowlRoofEdgeZ - 180}, {BowlRoofEdgeD + 3100, BowlRoofEdgeZ + 270}}, Down, 400, 400);

	UProceduralMeshComponent* M = NewObject<UProceduralMeshComponent>(this);
	M->SetupAttachment(RootComponent);
	M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	M->SetCastShadow(true);
	M->RegisterComponent();
	int32 Sec = 0;
	auto Add = [&](const FBowlBuf& B, UMaterialInterface* Mat)
	{
		if (B.V.Num() == 0) return;
		M->CreateMeshSection(Sec, B.V, B.T, B.N, B.UV, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
		M->SetMaterial(Sec++, Mat);
	};
	auto LED = [&](const FLinearColor& C, const FLinearColor& C2, float Intensity, float Count, float Speed, float Diffuse, float Rough) -> UMaterialInterface*
	{
		if (!A.LEDMat) return TagColorMat(this, C * 0.5f, Rough);
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(A.LEDMat, this);
		D->SetVectorParameterValue(TEXT("Color"), C);
		D->SetVectorParameterValue(TEXT("ColorB"), C2);
		D->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		D->SetScalarParameterValue(TEXT("Count"), Count);
		D->SetScalarParameterValue(TEXT("Speed"), Speed);
		D->SetScalarParameterValue(TEXT("Diffuse"), Diffuse);
		D->SetScalarParameterValue(TEXT("Roughness"), Rough);
		return D;
	};
	// (a late game builds the day's values and cross-fades each of these to the night's as the sun sets: UpdateSun)
	auto Dusk = [&](UMaterialInterface* Mi, const TCHAR* Param, float DayV, float NightV)
	{
		if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Mi))
			if (bDuskGame)
			{
				DuskMIDs.Add(Mid);
				DuskMIDParam.Add(FName(Param));
				DuskMIDRange.Add(FVector2f(DayV, NightV));
			}
		return Mi;
	};
	UMaterialInterface* CrowdM = TagColorMat(this, FLinearColor(0.12f, 0.12f, 0.13f), 0.9f);
	// Phase 4 (A13, the shader version): the painted crowd past the 3D rows jumps on its own beat, fan by fan, with the
	// crowd's excitement (M_TagCrowd2, Tools/Art/ue_build_stadium_mats.py; -TagNoCrowdAnim: the still M_TagCrowd)
	UMaterialInterface* CrowdMatBase = Look.bNoCrowdAnim ? nullptr
		: LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/TAG/Materials/M_TagCrowd2.M_TagCrowd2"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!CrowdMatBase) CrowdMatBase = A.CrowdMat;
	if (CrowdMatBase)
	{
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(CrowdMatBase, this);
		D->SetVectorParameterValue(TEXT("TeamColor"), Hex(Home.colors.primary));
		D->SetVectorParameterValue(TEXT("Tiling"), FLinearColor(1, 1, 0, 0)); // the sweep's UVs are already in tiles
		// self-glow: by day it lifts the stands under the roof's shade; at night it is kept faint, so the seats away from
		// the light banks fall off into the dark and the floodlit field outshines them (FStadiumLook, -TagCrowdGlow=)
		D->SetScalarParameterValue(TEXT("Glow"), bNight ? Look.CrowdGlowNight : 0.08f);
		// ... and under the lights the painted fans are darker still (M_TagCrowd2's Albedo; -TagCrowdNight=)
		D->SetScalarParameterValue(TEXT("Albedo"), bNight ? Look.CrowdAlbedoNight : 1.f);
		Dusk(D, TEXT("Glow"), 0.08f, Look.CrowdGlowNight);
		Dusk(D, TEXT("Albedo"), 1.f, Look.CrowdAlbedoNight);
		if (CrowdMatBase != A.CrowdMat) CrowdAnimMID = D;
		CrowdM = D;
	}
	const FLinearColor HomeC = Hex(Home.colors.primary), HomeC2 = Hex(Home.colors.secondary), AwayC = Hex(Away.colors.primary);
	// the bowl's concrete, steps and roof underside: under the lights they fall off behind the field (-TagStandsNight=;
	// a late game keeps the day's shade: the fade is on the emissives and the lights)
	const float Stand = bNight ? Look.StandsNight : 1.f;
	auto StandMat = [&](const FLinearColor& Col, float Rough) { return TagColorMat(this, Col * Stand, Rough); };
	Add(Crowd, CrowdM);
	Add(Concrete, StandMat(FLinearColor(0.16f, 0.16f, 0.17f), 0.9f));
	Add(Steps, StandMat(FLinearColor(0.32f, 0.32f, 0.33f), 0.85f));
	Add(Tunnel, TagColorMat(this, FLinearColor(0.008f, 0.008f, 0.01f), 0.9f));
	Add(Padding, Look.bOldWall ? TagColorMat(this, HomeC * 0.6f, 0.5f) : TagColorMat(this, HomeC * 0.08f + FLinearColor(0.012f, 0.012f, 0.014f), 0.7f));
	// the wall's ribbon: 6 m panels alternating the home colours (U runs in metres round the bowl), drifting along. Its
	// brightness by night is 12x the day's: the night camera is pinned at EV100 4.5, the day one sits near its 0.6 floor
	// less a 0.4 bias, so the same glow reads ~20x dimmer under the lights, and a board should glow more against a
	// dark bowl, not less (FStadiumLook WallLed / WallLedNight)
	Add(WallLed, Dusk(LED(HomeC, HomeC2 * 0.7f, bNight ? Look.WallLedNight : Look.WallLedDay, 1.f / 12.f, 0.05f, 0.1f, 0.35f), TEXT("Intensity"), Look.WallLedDay, Look.WallLedNight));
	// suites: dark reflective glass, warm interiors that read at night
	Add(Glass, Dusk(LED(FLinearColor(1.f, 0.82f, 0.6f), FLinearColor(1.f, 0.82f, 0.6f), bNight ? 0.35f : 0.03f, 0, 0, 0.02f, 0.08f), TEXT("Intensity"), 0.03f, 0.35f));
	// ribbon board: 12.5 m panels alternating the home colours, scrolling slowly around the bowl
	Add(Ribbon, LED(HomeC, HomeC2, bNight ? 3.f : 5.f, 0.08f, 0.12f, 0.1f, 0.3f));
	Add(Roof, StandMat(FLinearColor(0.34f, 0.35f, 0.37f), 0.6f));
	Add(Edge, TagColorMat(this, HomeC * 0.5f, 0.4f));

	// video boards on the end-zone roofs, facing the field, with the score
	for (int32 End : {-1, 1})
	{
		const double D = BowlRoofEdgeD + 900, X = 50 * YD + End * (BowlHalfX + D);
		const double Z = BowlRoofEdgeZ + 270.0 * 900 / 3100 + 1050; // board centre: sits on the canopy
		const FRotator R(0, End < 0 ? 0.f : 180.f, 0);
		const FVector In = -End * FVector::XAxisVector;
		AddMesh(A.Cube, FVector(X, 0, Z), FVector(2.4, 50, 19), FLinearColor(0.03f, 0.03f, 0.035f), 0.5f, R);
		for (double LegY : {-1600.0, 1600.0}) // legs down to the canopy
			AddMesh(A.Cube, FVector(X, LegY, Z - 1100), FVector(1.2, 1.2, 4), FLinearColor(0.2f, 0.2f, 0.22f), 0.6f, R);
		UStaticMeshComponent* Screen = AddMesh(A.Cube, FVector(X, 0, Z) + In * 125, FVector(0.05, 47, 16.4), FLinearColor::Black, 0.5f, R, false);
		Screen->SetMaterial(0, LED(FLinearColor(0.02f, 0.04f, 0.12f), FLinearColor(0.02f, 0.04f, 0.12f), bNight ? 1.5f : 1.1f, 0, 0, 0.02f, 0.2f));
		UStaticMeshComponent* Band = AddMesh(A.Cube, FVector(X, 0, Z + 640) + In * 128, FVector(0.05, 47, 2.2), FLinearColor::Black, 0.5f, R, false);
		Band->SetMaterial(0, LED(HomeC, AwayC, bNight ? 3.f : 6.f, 0.5f / 47, 0, 0.05f, 0.3f));
		for (int32 Line = 0; Line < 2; ++Line)
		{
			UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
			T->SetupAttachment(RootComponent);
			T->SetWorldSize(Line ? 480.f : 620.f);
			T->SetTextRenderColor(Line ? FColor(255, 214, 90) : FColor::White);
			T->SetHorizontalAlignment(EHTA_Center);
			T->SetVerticalAlignment(EVRTA_TextCenter);
			T->SetRelativeLocation(FVector(X, 0, Z + (Line ? -480 : 120)) + In * 140);
			T->SetRelativeRotation(R);
			T->SetCastShadow(false);
			T->RegisterComponent();
			ScoreText.Add(T);
		}
	}
	SetScoreboard(FString::Printf(TEXT("%s 0   %s 0"), UTF8_TO_TCHAR(Away.abbr.c_str()), UTF8_TO_TCHAR(Home.abbr.c_str())), TEXT("1ST  15:00"));
}

void AStadium::SetScoreboard(const FString& Top, const FString& Bottom)
{
	for (int32 i = 0; i < ScoreText.Num(); ++i)
		if (ScoreText[i]) ScoreText[i]->SetText(FText::FromString(i % 2 ? Bottom : Top));
}

void AStadium::Build(const tag::Team& Home, const tag::Team& Away, const tag::Weather& W, bool bNight)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	A.Load();
	UStaticMesh* Cube = A.Cube;
	const bool bSnowy = W.precip == tag::Precip::Snow;
	const bool bWet = W.precip == tag::Precip::Rain;
	bSnow = bSnowy;
	bRainy = bWet;
	bNightGame = bNight;
	TempF = static_cast<float>(W.tempF);
	// Phase 4, A7: a day game's kickoff and date. The sun moves through the game from them (UpdateSun); a late kickoff
	// runs into the dusk, so its light banks are built and fade up as the sun sets. -TagOldSun: the fixed sun.
	{
		const FStadiumLook& SunLook = FStadiumLook::Get();
		bMovingSun = !bNight && !SunLook.bOldSun;
		const uint32 MatchHash = HashCombine(GetTypeHash(Home.id), GetTypeHash(Away.id * 7 + 3));
		const FString Slot = SunLook.Kickoff.ToLower();
		if (Slot == TEXT("late")) KickoffHour = 16.42f;
		else if (Slot == TEXT("early")) KickoffHour = 13.f;
		else if (!Slot.IsEmpty() && FCString::Atof(*Slot) > 0.f) KickoffHour = FMath::Clamp(FCString::Atof(*Slot), 6.f, 22.f);
		else KickoffHour = static_cast<float>(MatchHash % 100) < SunLook.LateShare * 100.f ? 16.42f : 13.f;
		// (by the matchup: an early game in weeks 1-9, when the 1 pm sun stands 35-55 degrees up and the roof's shadow
		// keeps to the stands until the second half; a late game in weeks 9-17, so it runs into the dusk. A late-November
		// early game, the sun 27 degrees up, had the whole field in the roof's shadow from kickoff: 11-25 film)
		const bool bLateSlot = KickoffHour >= 15.5f;
		const int32 Week = SunLook.Week > 0 ? FMath::Clamp(SunLook.Week, 1, 18) : (bLateSlot ? 9 : 1) + static_cast<int32>((MatchHash / 100) % 9);
		const float DayOfYear = 251.f + 7.f * (Week - 1); // week 1 = Sept 8
		SunDecl = -23.44f * FMath::Cos(2.f * UE_PI / 365.f * (DayOfYear + 10.f));
		// most stadiums run north-south, the press box (the TV side, -Y) to the west: the afternoon sun is behind the
		// main camera and the west stand's shadow crosses the field as the day goes. Each home field turns a little.
		FieldNorthYaw = static_cast<float>((Home.id * 37) % 41) - 20.f;
		bDuskGame = bMovingSun && KickoffHour >= 15.5f;
		if (bMovingSun)
			UE_LOG(LogTemp, Display, TEXT("TAG sun: kickoff %.2f h, week %d (declination %.1f deg), latitude %.0f, field %+.0f deg off north%s"),
				KickoffHour, Week, SunDecl, SunLook.Latitude, FieldNorthYaw, bDuskGame ? TEXT(", a late game: the lights come up at dusk") : TEXT(""));
	}
	// invisible ground for physics (ragdoll tackles land on the turf)
	{
		UStaticMeshComponent* Ground = NewObject<UStaticMeshComponent>(this);
		Ground->SetupAttachment(RootComponent);
		Ground->SetStaticMesh(Cube);
		Ground->SetRelativeLocation(FVector(50 * YD, 0, -50));
		Ground->SetRelativeScale3D(FVector(200 * YD / 100, 120 * YD / 100, 1));
		Ground->SetCollisionProfileName(TEXT("BlockAll"));
		Ground->SetHiddenInGame(true);
		Ground->SetCastShadow(false);
		Ground->RegisterComponent();
	}
	// The realistic turf (Tools/Art/ue_build_turf.py): M_TagTurf2 is the whole playing surface in one material mapped
	// from the world position (grass, view-dependent mowing stripes, wear, lines, end zones and their wordmarks, the
	// midfield logo, the white border, the apron, rain and snow), so the paint lies in the grass and the close-up
	// blades (M_TagGrassBlade) take the colour of the ground under them. -TagOldTurf falls back to M_TagTurf.
	UMaterialInterface* Turf2Mat = (A.Plane && !FParse::Param(FCommandLine::Get(), TEXT("TagOldTurf")))
		? LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/TAG/Materials/M_TagTurf2.M_TagTurf2"), nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
	const bool bTurf2 = Turf2Mat != nullptr;
	const bool bPro = bTurf2 || (A.TurfMat && A.Plane);
	auto MakeTurfMID = [&](float LinesAmt, const FLinearColor& Tint, float PaintAmt, const FLinearColor& Paint, const FLinearColor& Tiling, float Stripes)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(A.TurfMat, this);
		M->SetScalarParameterValue(TEXT("LinesAmount"), LinesAmt);
		M->SetVectorParameterValue(TEXT("Tint"), Tint);
		M->SetScalarParameterValue(TEXT("PaintAmount"), PaintAmt);
		M->SetVectorParameterValue(TEXT("PaintColor"), Paint);
		M->SetVectorParameterValue(TEXT("GrassTiling"), Tiling);
		M->SetScalarParameterValue(TEXT("StripeAmount"), Stripes);
		M->SetScalarParameterValue(TEXT("Roughness"), bWet ? 0.45f : 0.88f);
		return M;
	};
	auto PlaneAt = [&](const FVector& C, double LenYd, double WidYd, UMaterialInterface* M)
	{
		UStaticMeshComponent* P = NewObject<UStaticMeshComponent>(this);
		P->SetupAttachment(RootComponent);
		P->SetStaticMesh(A.Plane);
		P->SetRelativeLocation(C);
		P->SetRelativeScale3D(FVector(LenYd * YD / 100, WidYd * YD / 100, 1));
		P->SetMaterial(0, M);
		P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		P->RegisterComponent();
		return P;
	};
	if (bTurf2)
	{
		// -TagTurfLum= scales the grass albedo (1 = linear 0.0135 / 0.0306 / 0.0063, before the material's Tint: the
		// deep green of NFL broadcast turf in the sun, sRGB ~85 / 118 / 52; twice that read lime under Lumen), -TagStripes= the mowing stripes,
		// -TagWear= the season wear between the hashes (0 = a fresh field), -TagSnowCover= how much snow lies in a snow game
		static float Turf2Lum = 1.f, Turf2Stripes = 1.f, Turf2Wear = 0.6f, Turf2Snow = 0.5f;
		FParse::Value(FCommandLine::Get(), TEXT("TagTurfLum="), Turf2Lum);
		FParse::Value(FCommandLine::Get(), TEXT("TagStripes="), Turf2Stripes);
		FParse::Value(FCommandLine::Get(), TEXT("TagWear="), Turf2Wear);
		FParse::Value(FCommandLine::Get(), TEXT("TagSnowCover="), Turf2Snow);
		const FLinearColor PaintWhite(0.72f, 0.72f, 0.70f); // brighter clips flat white under the sun
		auto PaintLum = [](const FLinearColor& Col) { return 0.2126f * Col.R + 0.7152f * Col.G + 0.0722f * Col.B; };
		// an end zone is the team's first colour laid on the grass; the wordmark (and the midfield mark's letter) its
		// second colour when that stands off the first, white otherwise
		auto FieldColor = [&](const tag::Team& Tm)
		{
			const FLinearColor First = Hex(Tm.colors.primary) * 0.6f;
			return First * FMath::Min(1.f, 0.45f / FMath::Max(PaintLum(First), 0.01f));
		};
		auto WordColor = [&](const tag::Team& Tm)
		{
			const FLinearColor First = Hex(Tm.colors.primary), Second = Hex(Tm.colors.secondary);
			return FMath::Abs(PaintLum(Second) - PaintLum(First)) > 0.18f ? Second * 0.8f : PaintWhite;
		};
		auto SetTurf2 = [&](UMaterialInstanceDynamic* M)
		{
			M->SetScalarParameterValue(TEXT("HomeIdx"), static_cast<float>(FMath::Clamp(Home.id, 0, 31)));
			M->SetScalarParameterValue(TEXT("AwayIdx"), static_cast<float>(FMath::Clamp(Away.id, 0, 31)));
			M->SetVectorParameterValue(TEXT("HomeA"), FieldColor(Home));
			M->SetVectorParameterValue(TEXT("HomeB"), WordColor(Home));
			M->SetVectorParameterValue(TEXT("AwayA"), FieldColor(Away));
			M->SetVectorParameterValue(TEXT("AwayB"), WordColor(Away));
			M->SetVectorParameterValue(TEXT("LogoLine"), FieldColor(Home));
			M->SetVectorParameterValue(TEXT("LogoFill"), WordColor(Home));
			M->SetVectorParameterValue(TEXT("LineColor"), PaintWhite);
			M->SetVectorParameterValue(TEXT("GrassColor"), FLinearColor(0.0135f, 0.0306f, 0.0063f) * Turf2Lum);
			M->SetScalarParameterValue(TEXT("StripeAmount"), Turf2Stripes);
			M->SetScalarParameterValue(TEXT("WearAmount"), Turf2Wear);
			M->SetScalarParameterValue(TEXT("Wet"), bWet ? 1.f : 0.f);
			M->SetScalarParameterValue(TEXT("Snow"), bSnowy ? Turf2Snow : 0.f);
			M->SetScalarParameterValue(TEXT("Specular"), bWet ? 0.45f : 0.3f);
			// A2 (Tools/Art/ue_build_turf.py; each parameter's default in the material is the old look, so an older
			// M_TagTurf2 ignores these): snow that holds detail, swept lines, soft lumps. Wet and Snow ramp with the
			// weather's strength (ApplyWeather).
			const FStadiumLook& WLook = FStadiumLook::Get();
			M->SetVectorParameterValue(TEXT("SnowColor"), FLinearColor(0.62f, 0.64f, 0.68f) * (WLook.SnowAlbedo / 0.62f));
			M->SetScalarParameterValue(TEXT("SweepAmount"), WLook.SnowSweep);
			M->SetScalarParameterValue(TEXT("SnowBump"), WLook.SnowBump);
			M->SetScalarParameterValue(TEXT("SnowDetail"), WLook.bOldWeather ? 0.f : 1.f);
		};
		// one plane for field, border and apron, at the height the old field plane had
		UMaterialInstanceDynamic* GroundM = UMaterialInstanceDynamic::Create(Turf2Mat, this);
		SetTurf2(GroundM);
		PlaneAt(FVector(50 * YD, 0, 0.5), 170, 100, GroundM);
		TurfMID = GroundM;
		SnowCoverBase = bSnowy ? Turf2Snow : 0.f;
		// blades for the close shots
		UMaterialInterface* BladeMat = FParse::Param(FCommandLine::Get(), TEXT("TagNoGrass3D")) ? nullptr
			: LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/TAG/Materials/M_TagGrassBlade.M_TagGrassBlade"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (BladeMat)
		{
			GrassMID = UMaterialInstanceDynamic::Create(BladeMat, this);
			SetTurf2(GrassMID);
			// the tips stir with the wind: 0.15 cm in still air, up to 0.7 cm in a gale
			const FVector2D WindDir = FVector2D(W.windX, -W.windY).GetSafeNormal();
			const float Sway = FMath::Clamp(0.15f + 0.05f * static_cast<float>(FMath::Sqrt(W.windX * W.windX + W.windY * W.windY)), 0.15f, 0.7f);
			GrassMID->SetVectorParameterValue(TEXT("WindVec"), WindDir.IsNearlyZero() ? FLinearColor(Sway, 0.f, 0.f, 0.f)
				: FLinearColor(static_cast<float>(WindDir.X) * Sway, static_cast<float>(WindDir.Y) * Sway, 0.f, 0.f));
			BuildGrass3D();
		}
		UE_LOG(LogTemp, Display, TEXT("TAG turf: M_TagTurf2, stripes %.2f wear %.2f wet %d snow %.2f, 3D grass %s"), Turf2Stripes, Turf2Wear,
			bWet ? 1 : 0, bSnowy ? Turf2Snow : 0.f, Grass3D ? TEXT("on") : TEXT("off"));
	}
	else if (bPro)
	{
		// broadcast turf: a deep, natural green (the grass texture alone renders neon under the sun), and 5-yard mowing
		// stripes strong enough to read from the high camera (StripeAmount above 1 pushes the material's stripe contrast)
		static FLinearColor TurfTint(1.05f, 0.30f, 1.3f);
		static float StripeAmt = 2.4f;
		FParse::Value(FCommandLine::Get(), TEXT("TagTurfG="), TurfTint.G);
		FParse::Value(FCommandLine::Get(), TEXT("TagStripes="), StripeAmt);
		const FLinearColor SnowTint = bSnowy ? FLinearColor(0.85f, 0.9f, 0.9f) : TurfTint;
		// one surface for the whole 120-yard field: grass + mowing stripes + painted markings
		UMaterialInstanceDynamic* FieldM = MakeTurfMID(1.f, SnowTint, 0.f, FLinearColor::Black, FLinearColor(60, 27, 0, 0), StripeAmt);
		// season wear between the hashes (T_TurfWear); -TagWear=0 for a fresh field
		static float WearAmt = 0.7f;
		FParse::Value(FCommandLine::Get(), TEXT("TagWear="), WearAmt);
		FieldM->SetScalarParameterValue(TEXT("WearAmount"), bSnowy ? WearAmt * 0.4f : WearAmt);
		PlaneAt(FVector(50 * YD, 0, 0.5), 120, FW, FieldM);
		// team-painted end zones, inset so the boundary paint stays visible
		auto EZ = [&](double X0, const tag::Team& T, float Yaw)
		{
			PlaneAt(FVector((X0 + 5) * YD, 0, 1.0), 9.6, FW - 0.8, MakeTurfMID(0.f, SnowTint, 0.92f, Hex(T.colors.primary), FLinearColor(5, 27, 0, 0), 0.f));
			AddText(FString(T.name.c_str()).ToUpper(), FVector((X0 + 5) * YD, 0, 3), FRotator(90, Yaw, 0), 520.f, FColor(240, 240, 240));
		};
		EZ(-10, Away, 0); // lettering runs across the width, tops toward the end line
		EZ(100, Home, 180);
		// apron around the field (darker, unpainted turf)
		PlaneAt(FVector(50 * YD, 0, -0.5), 170, 100, MakeTurfMID(0.f, FLinearColor(0.7f, 0.75f, 0.7f) * SnowTint, 0.f, FLinearColor::Black, FLinearColor(85, 50, 0, 0), 0.f));
		// the 6 ft white border painted around the field (outside the sidelines and end lines), and the team areas'
		// yellow coaching-box line 6 ft further out between the 32s
		UMaterialInstanceDynamic* Border = MakeTurfMID(0.f, SnowTint, 0.97f, FLinearColor(0.75f, 0.75f, 0.73f), FLinearColor(40, 1, 0, 0), 0.f);
		for (double S : {-1.0, 1.0})
		{
			PlaneAt(FVector(50 * YD, S * (FW / 2 + 1) * YD, 0.4), 124, 2, Border);
			PlaneAt(FVector((50 + S * 61) * YD, 0, 0.4), 2, FW, Border);
			PlaneAt(FVector(50 * YD, S * (FW / 2 + 2.05) * YD, 0.4), 36, 0.1, MakeTurfMID(0.f, SnowTint, 0.95f, FLinearColor(0.45f, 0.38f, 0.02f), FLinearColor(10, 1, 0, 0), 0.f));
		}
	}
	// ---------------- turf: 5-yard mowing stripes (cube is 100 cm)
	const FLinearColor G1 = bSnowy ? FLinearColor(0.11f, 0.2f, 0.1f) : FLinearColor(0.035f, 0.13f, 0.03f);
	const FLinearColor G2 = bSnowy ? FLinearColor(0.1f, 0.18f, 0.09f) : FLinearColor(0.028f, 0.105f, 0.025f);
	const float TurfRough = bWet ? 0.35f : 0.9f;
	for (int i = 0; i < (bPro ? 0 : 24); ++i)
	{
		const double X0 = (-10 + i * 5) * YD;
		AddMesh(Cube, FVector(X0 + 2.5 * YD, 0, -50), FVector(5 * YD / 100, FW * YD / 100, 1), i % 2 ? G1 : G2, TurfRough, FRotator::ZeroRotator, false);
	}
	// surround
	if (!bPro) AddMesh(Cube, FVector(50 * YD, 0, -52), FVector(170 * YD / 100, 100 * YD / 100, 1), bSnowy ? FLinearColor(0.6f, 0.62f, 0.65f) : FLinearColor(0.02f, 0.08f, 0.02f), 0.95f, FRotator::ZeroRotator, false);
	// end zones
	auto EndZone = [&](double X0, const tag::Team& T, float Yaw)
	{
		AddMesh(Cube, FVector((X0 + 5) * YD, 0, -49.6), FVector(10 * YD / 100, FW * YD / 100, 1), Hex(T.colors.primary) * 0.55f, 0.85f, FRotator::ZeroRotator, false);
		AddText(FString(T.name.c_str()).ToUpper(), FVector((X0 + 5) * YD, 0, 2), FRotator(90, Yaw, 0), 520.f, FColor::White);
	};
	if (!bPro)
	{
		EndZone(-10, Away, 0);
		EndZone(100, Home, 180);
	}
	// ---------------- markings (white paint, slightly emissive-looking via low roughness off)
	const FLinearColor Paint(0.85f, 0.85f, 0.82f);
	UInstancedStaticMeshComponent* Lines = AddISM(Cube, Paint, 0.7f, false);
	auto Line = [&](double X, double Y, double Len, double Wid, bool bAlongY)
	{
		const FVector S = bAlongY ? FVector(Wid * YD / 100, Len * YD / 100, 0.005) : FVector(Len * YD / 100, Wid * YD / 100, 0.005);
		Lines->AddInstance(FTransform(FRotator::ZeroRotator, FVector(X * YD, Y * YD, 1.0), S));
	};
	for (int yd = 0; yd <= (bPro ? -1 : 100); ++yd)
	{
		if (yd % 5 == 0)
			Line(yd, 0, FW - 0.6, (yd == 0 || yd == 100) ? 0.22 : 0.12, true);
		else
		{
			for (double y : {-FW / 2 + 0.6, FW / 2 - 0.6, -3.083, 3.083})
				Line(yd, y, 0.67, 0.1, true);
		}
	}
	if (!bPro)
	{
		Line(50, -FW / 2, 120, 0.33, false);
		Line(50, FW / 2, 120, 0.33, false);
		Line(-10, 0, FW, 0.33, true);
		Line(110, 0, FW, 0.33, true);
		Line(2, 0, 1, 0.12, true);
		Line(98, 0, 1, 0.12, true);
	}
	// yard numbers (6 ft tall, tops 9 yd from the sideline) - baked into the turf texture when available
	for (int yd = 10; yd <= (bPro ? 0 : 90); yd += 10)
	{
		const int N = yd <= 50 ? yd : 100 - yd;
		const FString S = FString::Printf(TEXT("%d"), N);
		AddText(S, FVector(yd * YD, (FW / 2 - 11) * YD, 2), FRotator(90, 180, 0), 180.f, FColor(225, 225, 220));
		AddText(S, FVector(yd * YD, -(FW / 2 - 11) * YD, 2), FRotator(90, 0, 0), 180.f, FColor(225, 225, 220));
	}
	// midfield emblem (painted in the grass by M_TagTurf2)
	if (!bTurf2)
	{
		AddMesh(A.Cylinder, FVector(50 * YD, 0, 0.6), FVector(9, 9, 0.004), Hex(Home.colors.primary) * 0.45f, 0.85f, FRotator::ZeroRotator, false);
		AddText(FString(Home.abbr.c_str()), FVector(50 * YD, 0, 3), FRotator(90, 0, 0), 300.f, FColor::White);
	}
	// ---------------- line overlays
	LosLine = AddMesh(Cube, FVector(0, 0, 2), FVector(0.2, (FW - 0.6) * YD / 100, 0.005), FLinearColor(0.1f, 0.35f, 1.f), 0.3f, FRotator::ZeroRotator, false);
	FdLine = AddMesh(Cube, FVector(0, 0, 2), FVector(0.2, (FW - 0.6) * YD / 100, 0.005), FLinearColor(1.f, 0.82f, 0.f), 0.3f, FRotator::ZeroRotator, false);
	SetLines(-1, -1);
	// ---------------- goal posts (18'6" wide, 10' crossbar, 35' uprights)
	const FLinearColor Gold(0.95f, 0.75f, 0.05f);
	for (double GX : {-10.0, 110.0})
	{
		const double Off = GX < 0 ? -2 : 2; // posts sit on the end line, offset base
		AddMesh(A.Cylinder, FVector((GX + Off * 0.0) * YD, 0, 150), FVector(0.25, 0.25, 3.0), Gold, 0.35f);
		AddMesh(A.Cylinder, FVector(GX * YD, 0, 305), FVector(0.16, 0.16, 5.64), Gold, 0.35f, FRotator(0, 0, 90));
		for (double Z : {-1.0, 1.0})
			AddMesh(A.Cylinder, FVector(GX * YD, Z * 282, 305 + 533), FVector(0.12, 0.12, 10.67), Gold, 0.35f);
	}
	// pylons
	for (double X : {0.0, 100.0, -10.0, 110.0})
		for (double Y : {-FW / 2, FW / 2})
			AddMesh(Cube, FVector(X * YD, Y * YD, 23), FVector(0.1, 0.1, 0.46), FLinearColor(1.f, 0.3f, 0.f), 0.5f);
	// ---------------- stands: one bowl (lower bowl, suites, upper deck, roof) wrapping all four sides
	const double SideY = BowlSideY;
	BuildBowl(Home, Away, bNight);
	if (!FStadiumLook::Get().bOldWall) BuildWallDressing(Home, Away, bNight);
	// 3D fans in the front sideline rows (Mass, dressed by the MetaHuman Crowd Sample). ~485 fans cost about 45 percent
	// of frame rate (175 -> 96 fps in a full game at 1600x900); -TagNoHeroCrowd turns them off on slower machines.
	if (!FParse::Param(FCommandLine::Get(), TEXT("TagNoHeroCrowd")))
	{
		// seats on the lowest sideline rows; fans are Mass entities dressed by the crowd sample's appearance pipeline
		TArray<FTransform> Seats;
		// the front rows behind both end zones too, eight deep (-TagEndRows=N, 0 for none): the painted crowd there sits
		// right behind every touchdown close-up and read as rows of orange discs. The straight run of the end stands,
		// corners left out. Measured (bench, 1080p, seed 7): 3 sideline rows 485 fans 111 fps; + 3 end rows 780 fans 114;
		// 8 rows all round 2059 fans 106 (1% low 62); 12 rows 3073 fans 92
		static int32 EndRows = FParse::Value(FCommandLine::Get(), TEXT("TagEndRows="), EndRows) ? FMath::Clamp(EndRows, 0, 16) : 8;
		TArray<float> Yaws{90.f, -90.f};
		if (EndRows > 0) { Yaws.Add(0.f); Yaws.Add(180.f); }
		for (const float Yaw : Yaws)
		{
			const bool bEnd = Yaw == 0.f || Yaw == 180.f;
			// yd either side of the stand's middle: the straight run of each stand (the sidelines stopped at the 16s, and the
			// stands level with the end zones were the painted crowd)
			const double Span = (bEnd ? BowlSideY - BowlCornerR : BowlHalfX - BowlCornerR) / YD - 1.0;
			const FVector C = bEnd ? FVector(50 * YD + (Yaw == 0.f ? BowlHalfX : -BowlHalfX), 0, 0) : FVector(50 * YD, Yaw > 0 ? SideY : -SideY, 0);
			const FVector Fwd = FRotator(0, Yaw, 0).Vector();
			const FVector Side = FRotator(0, Yaw + 90, 0).Vector();
			const float P = FMath::DegreesToRadians(33.f);
			const FVector SlopeF = Fwd * FMath::Cos(P) + FVector::UpVector * FMath::Sin(P);
			const FVector SlopeU = -Fwd * FMath::Sin(P) + FVector::UpVector * FMath::Cos(P);
			const FVector DeckC = C + Fwd * 1600 + FVector(0, 0, 1050);
			const float FaceYaw = (-Fwd).Rotation().Yaw;
			FRandomStream Rand(static_cast<int32>(Yaw) + 11);
			// (-TagFanRows=N along the sidelines: past the three on the plain slope the rows stand on the seating steps, one
			// a tread - a close-up sees ten rows or more, and above the third the painted crowd shows)
			static int32 SideRows = FParse::Value(FCommandLine::Get(), TEXT("TagFanRows="), SideRows) ? FMath::Clamp(SideRows, 1, 16) : 3;
			const int32 FanRows = bEnd ? EndRows : SideRows;
			const double StepTread = (1450.0 / 16.0) * FMath::Cos(P), StepRise = (1450.0 / 16.0) * FMath::Sin(P);
			const FVector StepBase = C + Fwd * (1600.0 + FMath::Cos(P) * 3600.0 * (0.1 - 0.5) - FMath::Sin(P) * 25.0) + FVector(0, 0, 1050.0 + FMath::Sin(P) * 3600.0 * (0.1 - 0.5) + FMath::Cos(P) * 25.0);
			for (int32 Row = 0; Row < FanRows; ++Row)
				for (double T = -Span * YD; T <= Span * YD; T += 72.0)
				{
					if (Rand.FRand() < 0.1f) continue;
					const FVector OnRow = Row < 3 ? DeckC + SlopeF * (120.0 + Row * 92.0 - 1800.0) + SlopeU * 25.0
						: StepBase + Fwd * ((Row - 3 + 0.5) * StepTread) + FVector(0, 0, (Row - 3 + 1) * StepRise);
					const FVector Feet = OnRow + Side * (T + Rand.FRandRange(-10, 10));
					Seats.Add(FTransform(FRotator(0, FaceYaw + Rand.FRandRange(-14, 14), 0), GetActorTransform().TransformPosition(Feet)));
				}
		}
		// the four corners, round the bowl's arcs (-TagCornerRows=N, default 3): the same rows, out from the curved front
		// edge, facing in to the arc's centre
		{
			static int32 CornerRows = FParse::Value(FCommandLine::Get(), TEXT("TagCornerRows="), CornerRows) ? FMath::Clamp(CornerRows, 0, 16) : 3;
			const double P = FMath::DegreesToRadians(33.0);
			const double StepTread = (1450.0 / 16.0) * FMath::Cos(P), StepRise = (1450.0 / 16.0) * FMath::Sin(P);
			auto RowXZ = [&](int32 Row) -> FVector2D
			{
				// (out from the front edge, up): as the straight stands place them
				if (Row < 3)
				{
					const double S = 120.0 + Row * 92.0 - 1800.0;
					return FVector2D(1600.0 + FMath::Cos(P) * S - FMath::Sin(P) * 25.0, 1050.0 + FMath::Sin(P) * S + FMath::Cos(P) * 25.0);
				}
				const FVector2D Base(1600.0 + FMath::Cos(P) * 3600.0 * (0.1 - 0.5) - FMath::Sin(P) * 25.0, 1050.0 + FMath::Sin(P) * 3600.0 * (0.1 - 0.5) + FMath::Cos(P) * 25.0);
				return Base + FVector2D((Row - 3 + 0.5) * StepTread, (Row - 3 + 1) * StepRise);
			};
			const double X0 = 50 * YD - BowlHalfX, X1 = 50 * YD + BowlHalfX, Rc = BowlCornerR;
			const FVector2D Cc[4] = {{X1 - Rc, SideY - Rc}, {X0 + Rc, SideY - Rc}, {X0 + Rc, -SideY + Rc}, {X1 - Rc, -SideY + Rc}};
			FRandomStream Rand(29);
			for (int32 q = 0; q < 4; ++q)
				for (int32 Row = 0; Row < CornerRows; ++Row)
				{
					const FVector2D Xz = RowXZ(Row);
					const double R = Rc + Xz.X, Arc = R * UE_HALF_PI;
					const int32 N = FMath::Max(1, FMath::FloorToInt(Arc / 72.0));
					for (int32 k = 0; k < N; ++k)
					{
						if (Rand.FRand() < 0.1f) continue;
						const double Phi = (q + (k + 0.5 + Rand.FRandRange(-0.12f, 0.12f)) / N) * UE_HALF_PI;
						const FVector Feet(Cc[q].X + R * FMath::Cos(Phi), Cc[q].Y + R * FMath::Sin(Phi), Xz.Y);
						const float Face = static_cast<float>(FMath::RadiansToDegrees(Phi)) + 180.f;
						Seats.Add(FTransform(FRotator(0, Face + Rand.FRandRange(-14, 14), 0), GetActorTransform().TransformPosition(Feet)));
					}
				}
		}
		// model pack spectators when the pack characters are in (one skeletal mesh per fan on the 29 bone crowd rig,
		// dressed in the home or visiting colours); the Mass MetaHuman crowd otherwise
		if (BuildPackFans(Seats, Home, Away))
		{
			UE_LOG(LogTemp, Display, TEXT("TAG: model pack fans %d in %d seats"), Fans.Num(), Seats.Num());
		}
		else if (ATagFanSpawner* FanSpawner = [this]() { FActorSpawnParameters SP; SP.Owner = this;
			return GetWorld()->SpawnActor<ATagFanSpawner>(ATagFanSpawner::StaticClass(), FTransform::Identity, SP); }())
		{
			const bool bOk = FanSpawner->Setup(Seats);
			if (bOk) FanSpawner->SpawnFans();
			FanSpawnerActor = FanSpawner;
			UE_LOG(LogTemp, Display, TEXT("TAG: Mass fan spawner %d seats, config %s, spawn count %d"), Seats.Num(),
				bOk ? TEXT("ok") : TEXT("MISSING"), FanSpawner->SeatCount());
		}
	}
	if (!A.CrowdMat) BuildCrowd(Home);
	BuildOfficials();
	BuildPackSideline(Home, Away);
	// sideline benches & personnel area tint
	// team benches (home on the far sideline, visitors near): seat, backrest and a team-colour skirt; the dressed
	// sideline (A4) unless -TagOldWall
	const FStadiumLook& Look = FStadiumLook::Get();
	if (Look.bOldWall)
	{
		for (double S : {-1.0, 1.0})
		{
			const FLinearColor TeamC = Hex((S > 0 ? Home : Away).colors.primary);
			const double Y = S * (FW / 2 + 4.6) * YD;
			AddMesh(Cube, FVector(50 * YD, Y, 45), FVector(30 * YD / 100, 0.55, 0.08), FLinearColor(0.06f, 0.06f, 0.07f), 0.5f);
			AddMesh(Cube, FVector(50 * YD, Y + S * 25, 70), FVector(30 * YD / 100, 0.06, 0.5), TeamC * 0.5f, 0.5f);
			AddMesh(Cube, FVector(50 * YD, Y - S * 22, 22), FVector(30 * YD / 100, 0.04, 0.44), TeamC * 0.35f, 0.6f);
		}
	}
	else
	{
		BuildSidelineProps(Home, Away, W);
	}

	// ---------------- sky, sun / floodlights, atmosphere
	// A2: a rain or snow day is overcast, not a weak sun: the sun dimmer and soft-edged, the sky the main light
	const bool bPrecip = W.precip != tag::Precip::None;
	// -TagOvercast: a dry day under cloud (the rain day's light without the rain): for the look matrix and late games
	const bool bOvercast = !bNight && (bPrecip || FParse::Param(FCommandLine::Get(), TEXT("TagOvercast")));
	UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(this);
	Sun->SetupAttachment(RootComponent);
	Sun->SetRelativeRotation(bNight ? FRotator(-62, 30, 0) : FRotator(-48, 135, 0));
	Sun->SetIntensity(bNight ? 0.05f : (!bOvercast ? 9.f : Look.RainSun));
	Sun->SetLightColor(bNight ? FLinearColor(0.6f, 0.7f, 1.f) : !bOvercast || Look.bOldWeather ? FLinearColor(1.f, 0.95f, 0.88f) : FLinearColor(0.93f, 0.95f, 1.f));
	if (bOvercast) Sun->SetLightSourceAngle(Look.RainSunAngle);
	Sun->bAtmosphereSunLight = true;
	Sun->SetDynamicShadowCascades(4);
	Sun->SetCastShadows(!bNight); // at night it is 0.05 lux under the floodlights: a shadow pass nobody can see
	Sun->RegisterComponent();
	SunC = Sun;
	SunBaseIntensity = Sun->Intensity;
	bOvercastDay = bOvercast;

	USkyAtmosphereComponent* Atmo = NewObject<USkyAtmosphereComponent>(this);
	Atmo->SetupAttachment(RootComponent);
	Atmo->RegisterComponent();

	USkyLightComponent* Sky = NewObject<USkyLightComponent>(this);
	Sky->SetupAttachment(RootComponent);
	Sky->bRealTimeCapture = true;
	Sky->SetIntensity(bNight ? 0.04f : (bOvercast ? Look.RainSky : 1.0f)); // night: the sky above the bowl goes near black
	Sky->RegisterComponent();
	SkyC = Sky;
	SkyDayIntensity = Sky->Intensity;

	if (!bNight)
	{
		UVolumetricCloudComponent* Clouds = NewObject<UVolumetricCloudComponent>(this);
		Clouds->SetupAttachment(RootComponent);
		Clouds->RegisterComponent();
	}

	UExponentialHeightFogComponent* Fog = NewObject<UExponentialHeightFogComponent>(this);
	Fog->SetupAttachment(RootComponent);
	// (rain and snow: thicker as the weather builds, ApplyWeather; FStadiumLook FogDay / FogNight / FogRamp)
	FogBase = !bPrecip ? 0.004f : (bNight ? Look.FogNight : Look.FogDay);
	Fog->SetFogDensity(FogBase);
	// volumetric fog for the floodlight beams at night and the thick air of rain and snow; a dry day's thin haze
	// (density 0.004) is left to the height fog alone
	Fog->SetVolumetricFog(bNight || W.precip != tag::Precip::None);
	Fog->RegisterComponent();
	FogC = Fog;
	FogInscatterBase = Fog->FogInscatteringLuminance;

	// LED light banks hung from the roof's front edge along both sidelines and over the end zones, the way a modern
	// stadium is lit (housings by day; lit at night). Many sources from both sides give every player the broadcast
	// look: a fan of crisp shadows around the feet, specular glints on the helmets from several directions, and no dark
	// side to anyone. Six banks cast shadows (cost). Each bank's intensity is set by its throw to the field, so moving
	// the banks out to the roof edge kept the field's illuminance where it was tuned. The beams are tight and aimed at
	// the field (FStadiumLook, A3): the field is bright, the front rows catch the spill and the bowl above falls off
	// into the dark, as at a night game on TV; the lit housings glare.
	{
		UMaterialInstanceDynamic* LampMat = nullptr;
		if (A.LEDMat)
		{
			LampMat = UMaterialInstanceDynamic::Create(A.LEDMat, this);
			LampMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.97f, 0.92f));
			LampMat->SetVectorParameterValue(TEXT("ColorB"), FLinearColor(1.f, 0.97f, 0.92f));
			LampMat->SetScalarParameterValue(TEXT("Intensity"), bNight ? Look.LampGlow : 0.4f);
			LampMat->SetScalarParameterValue(TEXT("Diffuse"), 0.6f);
			if (bDuskGame)
			{
				DuskMIDs.Add(LampMat);
				DuskMIDParam.Add(TEXT("Intensity"));
				DuskMIDRange.Add(FVector2f(0.4f, Look.LampGlow));
			}
		}
		auto Bank = [&](const FVector& At, const FVector& Aim, const FVector& WasAt, bool bShadow, float Candela, float ConeIn, float ConeOut)
		{
			UStaticMeshComponent* H = AddMesh(Cube, At, FVector(9, 1.2f, 3.5f), FLinearColor(0.75f, 0.75f, 0.72f), 0.3f, (Aim - At).Rotation(), false);
			if (LampMat) H->SetMaterial(0, LampMat);
			if (!bNight && !bDuskGame) return; // (a late game builds them dark: they come up at dusk, UpdateSun)
			Candela *= FMath::Square(FVector::Dist(At, Aim) / FVector::Dist(WasAt, Aim));
			USpotLightComponent* L = NewObject<USpotLightComponent>(this);
			L->SetupAttachment(RootComponent);
			L->SetRelativeLocation(At);
			L->SetRelativeRotation((Aim - At).Rotation());
			L->SetIntensityUnits(ELightUnits::Candelas);
			L->SetIntensity(Candela);
			L->SetAttenuationRadius(32000.f);
			L->SetOuterConeAngle(ConeOut);
			L->SetInnerConeAngle(ConeIn);
			L->bUseTemperature = true;
			L->Temperature = 5700.f;                 // LED sports lighting
			L->SourceRadius = 80.f;                  // a bank of lamps, not a point: slightly soft shadow edges
			L->SetCastShadows(bShadow);
			L->SetVolumetricScatteringIntensity(0.12f); // faint beams in the night air (more washes the bowl out in rain)
			L->RegisterComponent();
			BankLights.Add(L);
			BankCandela.Add(Candela);
		};
		const double EdgeD = BowlRoofEdgeD + 150, EdgeZ = BowlRoofEdgeZ - 260; // just under the canopy's front edge
		for (int32 Side : {-1, 1})
			for (int32 i = 0; i < 5; ++i)
			{
				const double X = (10 + 20 * i) * YD;
				const FVector At(X, Side * (BowlSideY + EdgeD), EdgeZ);
				const FVector Aim(X + (50 * YD - X) * 0.25, -Side * Look.BankAimYd * YD, 0);
				Bank(At, Aim, FVector(X, Side * (FW / 2 + 24) * YD, 4600), i % 2 == 1 || i == 2, Look.BankCd, Look.BankInner, Look.BankOuter);
			}
		for (int32 End : {-1, 1})
		{
			const FVector At(50 * YD + End * (BowlHalfX + EdgeD), 0, EdgeZ);
			const FVector Aim((50 + End * 28) * YD, 0, 0);
			Bank(At, Aim, FVector((50 + End * 66) * YD, 0, 4200), false, Look.EndCd, Look.EndInner, Look.EndOuter);
		}
	}

	UPostProcessComponent* PP = NewObject<UPostProcessComponent>(this);
	PP->SetupAttachment(RootComponent);
	PP->bUnbound = true;
	// keep exposure in a narrow, broadcast-like range so the turf doesn't wash out
	// a broadcast camera holds its exposure: at night pin it (the lit bowl stays bright, the sky stays black, no
	// pumping when the shot swings across the stands); by day a narrow range follows sun and cloud
	static float NightEV = FParse::Value(FCommandLine::Get(), TEXT("TagNightEV="), NightEV) ? NightEV : 4.5f;
	// A2: the weather moves the window (EV100: higher is darker). An overcast day opens the camera up; a snow field
	// closes it down, by day and under the lights; a wet field at night is darker (FStadiumLook OvercastEV / SnowEV /
	// NightRainEV, all 0 under -TagOldWeather)
	const float WxEV = bNight ? (bSnowy ? Look.SnowEV : bWet ? Look.NightRainEV : 0.f)
		: (bOvercast ? Look.OvercastEV : 0.f) + (bSnowy ? Look.SnowEV : 0.f);
	PP->Settings.bOverride_AutoExposureMinBrightness = true;
	PP->Settings.AutoExposureMinBrightness = (bNight ? NightEV : 0.6f) + WxEV;
	PP->Settings.bOverride_AutoExposureMaxBrightness = true;
	// (by day only the floor moves down: the camera may open up for an overcast or low-sun field, never be forced to;
	// forcing the whole window down white-washed the 11-25 rain and late-game films)
	PP->Settings.AutoExposureMaxBrightness = (bNight ? NightEV : 2.0f) + (bNight ? WxEV : FMath::Max(0.f, WxEV));
	PP->Settings.bOverride_AutoExposureBias = true;
	PP->Settings.AutoExposureBias = bNight ? 0.f : 0.4f;
	// (A7: the day window rides down as the sun gets low, and a late game's crosses to the night one: UpdateSun)
	DayEV = FVector2f(0.6f + WxEV, 2.0f + FMath::Max(0.f, WxEV));
	NightEV2 = FVector2f(NightEV + (bSnowy ? Look.SnowEV : bWet ? Look.NightRainEV : 0.f), NightEV + (bSnowy ? Look.SnowEV : bWet ? Look.NightRainEV : 0.f));
	DayBias = 0.4f;
	UE_LOG(LogTemp, Display, TEXT("TAG exposure: EV100 %.2f..%.2f bias %.1f (weather %+.2f), sun %.1f sky %.1f"),
		PP->Settings.AutoExposureMinBrightness, PP->Settings.AutoExposureMaxBrightness, PP->Settings.AutoExposureBias, WxEV,
		Sun->Intensity, Sky->Intensity);
	PP->Settings.bOverride_MotionBlurAmount = true;
	PP->Settings.MotionBlurAmount = Look.MotionBlur;  // broadcast shutter: a hint of blur on fast pans, never a smear (A8: -TagMBlur=)
	PP->Settings.bOverride_MotionBlurMax = true;
	PP->Settings.MotionBlurMax = 0.6f;
	PP->Settings.bOverride_ColorSaturation = true;
	PP->Settings.ColorSaturation = FVector4(1.08f, 1.08f, 1.08f, 1.f);
	PP->Settings.bOverride_ColorContrast = true;
	PP->Settings.ColorContrast = FVector4(1.1f, 1.1f, 1.1f, 1.f);
	PP->Settings.bOverride_BloomIntensity = true;
	PP->Settings.BloomIntensity = bNight ? 0.9f : 0.5f;
	// at night only what is brighter than white blooms (threshold in exposed luminance): the light banks glare and the
	// white kits and lines stay crisp instead of hazing. No extra pass: the bloom setup pass already runs while local
	// exposure is on (r.Bloom.ApplyLocalExposure), the threshold is one more term in it
	PP->Settings.bOverride_BloomThreshold = true;
	PP->Settings.BloomThreshold = bNight ? Look.BloomThrNight : Look.BloomThrDay;
	// a TV picture, not a film (FStadiumLook, A1): no vignette or grain, and a camera's knee on the highlights so white
	// jerseys keep their folds and pads instead of clipping flat (the filmic curve's shoulder and white clip, and local
	// exposure holding the big bright areas of a close shot down a little more at night)
	PP->Settings.bOverride_VignetteIntensity = true;
	PP->Settings.VignetteIntensity = Look.Vignette;
	PP->Settings.bOverride_FilmGrainIntensity = true;
	PP->Settings.FilmGrainIntensity = Look.Grain;
	PP->Settings.bOverride_FilmShoulder = true;
	PP->Settings.FilmShoulder = Look.Shoulder;
	PP->Settings.bOverride_FilmWhiteClip = true;
	PP->Settings.FilmWhiteClip = Look.WhiteClip;
	PP->Settings.bOverride_LocalExposureHighlightContrastScale = true;
	PP->Settings.LocalExposureHighlightContrastScale = bNight ? Look.HiContrastNight : Look.HiContrastDay;
	PP->RegisterComponent();
	PPC = PP;

	// weather
	Wind = FVector(W.windX, -W.windY, 0) * YD;
	if (W.precip != tag::Precip::None && !BuildPrecip(W)) BuildRain(W);
	// A5: the live wear map (needs the turf's M_TagTurf2 with WearRT, Tools/Art/ue_build_turf.py)
	if (!Look.bOldWear && TurfMID) BuildWear();
	if (Look.ProgressFixed >= 0.f) GameProgress = FMath::Min(Look.ProgressFixed, 1.f);
	ApplyWeather();
	ApplyAAPreset(Look);
	UpdateSun(true);
	// A10: breath in cold games (M_TagBreath; -TagNoBreath)
	bBreath = !Look.bNoBreath && (W.tempF < Look.BreathTempF || bSnowy);
	if (bBreath)
	{
		UMaterialInterface* BreathMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/TAG/Materials/M_TagBreath.M_TagBreath"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!BreathMat || !A.Sphere) bBreath = false;
		for (int32 i = 0; bBreath && i < 16; ++i)
		{
			UStaticMeshComponent* Pc = AddMesh(A.Sphere, FVector::ZeroVector, FVector(0.1), FLinearColor::White, 0.9f, FRotator::ZeroRotator, false);
			UMaterialInstanceDynamic* Pm = UMaterialInstanceDynamic::Create(BreathMat, this);
			Pc->SetMaterial(0, Pm);
			Pc->bVisibleInRayTracing = false;
			Pc->bAffectDistanceFieldLighting = false;
			Pc->SetVisibility(false);
			Puffs.Add(Pc);
			PuffMIDs.Add(Pm);
			PuffState.AddDefaulted();
		}
		UE_LOG(LogTemp, Display, TEXT("TAG breath: %s (%.0f F%s)"), bBreath ? TEXT("16 puffs") : TEXT("no M_TagBreath (Tools/Art/ue_build_stadium_mats.py)"), W.tempF, bSnowy ? TEXT(", snow") : TEXT(""));
	}
}

bool AStadium::LoadFanMeshes()
{
	if (FanMeshes.Num() > 0) return true;
	// merged crowd meshes from Epic's MetaHuman Crowd Sample (one skeletal mesh per fan, no grooms) and our
	// spectator clips retargeted onto their skeleton (Tools/Art/crowd_retarget.py)
	FanIdle = LoadObject<UAnimationAsset>(nullptr, TEXT("/Game/TAG/Crowd/Anims/A_Crowd_Idle.A_Crowd_Idle"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	FanCheer = LoadObject<UAnimationAsset>(nullptr, TEXT("/Game/TAG/Crowd/Anims/A_Crowd_Cheer.A_Crowd_Cheer"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!FanIdle || !FanCheer) return false;
	// merged crowd meshes (hard paths: the asset registry is not populated for this plugin in -game)
	static const TCHAR* Names[] = {
		TEXT("AverageFemale_Head01/MHC_AverageFemale_Head01_CombinedSkelMesh"), TEXT("AverageFemale_Head02/MHC_AverageFemale_Head02_CombinedSkelMesh"),
		TEXT("AverageMale_Head01/MHC_AverageMale_Head01_CombinedSkelMesh"), TEXT("AverageMale_Head02/MHC_AverageMale_Head02_CombinedSkelMesh"),
		TEXT("HeavysetFemale_Head01/MHC_HeavysetFemale_Head01_FullBody"), TEXT("HeavysetFemale_Head02/MHC_HeavysetFemale_Head02_FullBody"),
		TEXT("HeavysetMale_Head01/MHC_HeavysetMale_Head01_FullBody"), TEXT("ShortFemale_Head01/MHC_ShortFemale_Head01_FullBody"),
		TEXT("ShortMale_Head01/MHC_ShortMale_Head01_FullBody"), TEXT("TallFemale_Head01/MHC_TallFemale_head01_FullBody"),
		TEXT("TallMale_Head01/MHC_TallMale_Head01_FullBody")};
	int32 Tried = 0;
	FString OtherSkel;
	for (const TCHAR* N : Names)
	{
		const FString Leaf = FPaths::GetCleanFilename(N);
		const FString Path = FString::Printf(TEXT("/MetaHumanCrowdContent/Characters/Assembled/%s.%s"), N, *Leaf);
		USkeletalMesh* M = LoadObject<USkeletalMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		++Tried;
		if (!M) continue;
		if (M->GetSkeleton() == FanIdle->GetSkeleton()) FanMeshes.Add(M); // anims must share the skeleton
		else if (M->GetSkeleton()) OtherSkel = M->GetSkeleton()->GetPathName();
	}
	UE_LOG(LogTemp, Display, TEXT("TAG: hero crowd meshes %d of %d (anim skeleton %s, other %s)"), FanMeshes.Num(), Tried,
		FanIdle->GetSkeleton() ? *FanIdle->GetSkeleton()->GetPathName() : TEXT("-"), *OtherSkel);
	return FanMeshes.Num() > 0;
}

/**
 * Loads the catalog meshes a build is about to use as one batch. One at a time (HeroMesh, then SetSkeletalMesh) every
 * newly seen mesh stalls the game thread alone on its skinned asset build while the other build workers idle; here
 * every package is requested first, so the builds all start, and they are waited for together. HeroMesh then finds
 * each mesh in memory (and roots and caches it as before).
 */
static void PreloadHeroMeshes(FAthleteAssets& A, const TArray<int32>& Items, const TCHAR* What)
{
	const double T0 = FPlatformTime::Seconds();
	TArray<int32> Requests;
	for (int32 Item : Items)
	{
		if (!A.HeroItems.IsValidIndex(Item) || A.HeroItems[Item].Mesh) continue;
		const FString Package = FPackageName::ObjectPathToPackageName(A.HeroItems[Item].Path);
		if (!Package.IsEmpty() && FPackageName::DoesPackageExist(Package)) Requests.Add(LoadPackageAsync(Package));
	}
	if (Requests.Num()) FlushAsyncLoading(Requests);
	TArray<USkinnedAsset*> Meshes;
	for (int32 Item : Items)
		if (USkeletalMesh* M = A.HeroMesh(Item)) Meshes.AddUnique(M);
#if WITH_EDITOR
	if (Meshes.Num()) FSkinnedAssetCompilingManager::Get().FinishCompilation(Meshes);
#endif
	UE_LOG(LogTemp, Display, TEXT("TAG: model pack %s preload %d meshes (%d packages requested) in %.2f s"), What, Meshes.Num(), Requests.Num(),
		FPlatformTime::Seconds() - T0);
}

USkeletalMeshComponent* AStadium::AddStaticPerson(int32 Item, int32 HeadItem, const FVector& Rel, float FaceYaw, const FLinearColor& Team,
	const FLinearColor& Bottom, bool bKneel, int32 Seed)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	if (PackPreload) // BuildPackSideline's collect pass: note the meshes, build nothing
	{
		if (A.HeroItems.IsValidIndex(Item)) PackPreload->AddUnique(Item);
		if (A.HeroItems.IsValidIndex(HeadItem)) PackPreload->AddUnique(HeadItem);
		return nullptr;
	}
	USkeletalMesh* M = A.HeroMesh(Item);
	if (!M) return nullptr;
	const FString Rig = A.HeroItems[Item].Rig;
	UAnimationAsset* Clip = A.HeroClip(Rig, bKneel ? TEXT("Kneel") : TEXT("Idle"));
	USkeletalMeshComponent* P = NewObject<USkeletalMeshComponent>(this);
	P->SetSkeletalMesh(M);
	P->SetupAttachment(RootComponent);
	P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	P->bCastDynamicShadow = true;
	P->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	P->bEnableUpdateRateOptimizations = true;
	P->RegisterComponent();
	P->SetRelativeLocationAndRotation(Rel, FRotator(0, FaceYaw - 90.f, 0)); // the pack characters face +Y
	// team colour on placeholder clothing (recolour slots) and on the untextured sideline players
	auto Paint = [&](USkeletalMeshComponent* C)
	{
		for (int32 Mi = 0; Mi < C->GetNumMaterials(); ++Mi)
		{
			float Mode = 0.f;
			UMaterialInterface* Cur = C->GetMaterial(Mi);
			const FString Slot = Mi < C->GetMaterialSlotNames().Num() ? C->GetMaterialSlotNames()[Mi].ToString() : FString();
			const bool bRecolour = Cur && Cur->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Mode")), Mode) && Mode > 1.5f && Mode < 2.5f;
			const bool bFlatTeam = A.HeroItems[Item].Kind == TEXT("SidePlayer"); // region-painted uniform (Mode 4)
			if (!bRecolour && !bFlatTeam) continue;
			if (UMaterialInstanceDynamic* Mid = C->CreateDynamicMaterialInstance(Mi))
			{
				Mid->SetVectorParameterValue(TEXT("Tint"), Slot.StartsWith(TEXT("Bottom")) ? Bottom : Team);
				Mid->SetVectorParameterValue(TEXT("Trim"), bFlatTeam ? Bottom : Team); // neon placeholder pixels take the team colour
				Mid->SetVectorParameterValue(TEXT("Trim2"), Team);                     // a sideline player's helmet
			}
		}
	};
	Paint(P);
	if (USkeletalMesh* HM = A.HeroMesh(HeadItem))
	{
		USkeletalMeshComponent* Hc = NewObject<USkeletalMeshComponent>(this);
		Hc->SetSkeletalMesh(HM);
		Hc->SetupAttachment(P);
		Hc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Hc->bUseBoundsFromLeaderPoseComponent = true;
		Hc->RegisterComponent();
		Hc->SetLeaderPoseComponent(P);
		Paint(Hc);
	}
	if (Clip)
	{
		P->PlayAnimation(Clip, !bKneel); // the kneel holds its last frame
		if (UAnimSingleNodeInstance* I = P->GetSingleNodeInstance())
		{
			if (!bKneel) I->SetPosition(FMath::Frac(Seed * 0.618f) * Clip->GetPlayLength(), false);
			I->SetPlayRate(0.9f + FMath::Frac(Seed * 0.37f) * 0.2f);
		}
	}
	SidelinePeople.Add(P);
	return P;
}

void AStadium::BuildPackSideline(const tag::Team& Home, const tag::Team& Away)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	if (!A.bHero || FParse::Param(FCommandLine::Get(), TEXT("TagNoPackSideline"))) return;
	// two passes over the same code: the first only collects the meshes it would use (AddStaticPerson records them
	// while PackPreload is set), they load as one batch, then the second pass builds
	if (!PackPreload)
	{
		TArray<int32> Want;
		PackPreload = &Want;
		BuildPackSideline(Home, Away);
		PackPreload = nullptr;
		PreloadHeroMeshes(A, Want, TEXT("sideline"));
	}
	const TCHAR* Wx = A.HeroWeather == 2 ? TEXT("rain") : A.HeroWeather == 1 ? TEXT("cold") : TEXT("warm");
	const FString Top = A.HeroWeather == 2 ? TEXT("rainjacket") : A.HeroWeather == 1 ? TEXT("windbreaker") : TEXT("poloshirt");
	auto Of = [&](const TCHAR* Token) -> TArray<int32>
	{
		TArray<int32> L = A.HeroOf(TEXT("Crew"), [&](const FHeroItem& I) { return I.Name.Contains(Token) && I.Name.Contains(Wx); });
		if (!L.Num()) L = A.HeroOf(TEXT("Crew"), [&](const FHeroItem& I) { return I.Name.Contains(Token); });
		return L;
	};
	const TArray<int32> Heads = A.HeroOf(TEXT("CoachHead"), [](const FHeroItem& I) { return !I.bNoHead; });
	const FLinearColor Grey(0.2f, 0.2f, 0.22f), Khaki(0.55f, 0.47f, 0.33f), Black(0.03f, 0.03f, 0.035f);
	const double Edge = FW / 2;
	int32 Seed = Home.id * 13 + Away.id;
	auto Put = [&](const TArray<int32>& L, int32 k, double Xyd, double Yyd, float Face, bool bKneel = false)
	{
		if (L.Num()) AddStaticPerson(L[k % L.Num()], INDEX_NONE, FVector(Xyd * YD, Yyd * YD, 0), Face, Grey, Black, bKneel, ++Seed);
	};
	// photographers behind both end lines (standing, on monopods, kneeling), facing up the field
	const TArray<int32> Slr = Of(TEXT("handheldslr")), Mono = Of(TEXT("monopodslr_")), Kneel = Of(TEXT("monopodslrkneeling"));
	for (int32 End = 0; End < 2; ++End)
	{
		const double X = End ? 112.5 : -12.5;
		const float Face = End ? 180.f : 0.f;
		for (int32 k = 0; k < 4; ++k) Put(Slr, k + End * 4, X + (End ? 0.6 : -0.6) * (k % 2), -14.0 + k * 9.0, Face);
		Put(Mono, End, X, 10.0 * (End ? -1 : 1), Face);
		Put(Kneel, End, X + (End ? -0.8 : 0.8), 3.0 * (End ? 1 : -1), Face, true);
	}
	// broadcast crews on the near sideline outside the team area: steadicam, handheld video, parabolic audio dish
	const TArray<int32> Steadi = Of(TEXT("steadicam")), Video = Of(TEXT("broadcastvideo")), Dish = Of(TEXT("audiodish"));
	const double NearY = -(Edge + 3.0);
	Put(Steadi, 0, 22.0, NearY, 90.f);
	Put(Video, 0, 26.0, NearY - 0.4, 90.f);
	Put(Dish, 0, 74.0, NearY, 90.f);
	Put(Steadi, 1, 78.0, NearY - 0.3, 90.f);
	Put(Video, 1, 12.0, Edge + 3.2, -90.f);
	Put(Dish, 1, 88.0, Edge + 3.2, -90.f);
	// TV camera operators (media vests) by the far-side 20s
	const TArray<int32> Tv = A.HeroOf(TEXT("Staff"), [](const FHeroItem& I) { return I.Name.Contains(TEXT("tvcameraman")); });
	for (int32 k = 0; k < Tv.Num(); ++k)
		AddStaticPerson(Tv[k], Heads.Num() ? Heads[(Seed + k * 5) % Heads.Num()] : INDEX_NONE, FVector((20.0 + k * 60.0) * YD, (Edge + 3.6) * YD, 0), -90.f, Grey, Black, false, ++Seed);
	// coordinators with headsets beside each head coach (the pack's coordinator outfits in the team colour)
	const TArray<int32> Coord = A.HeroOf(TEXT("Staff"), [&](const FHeroItem& I) { return I.Name.Contains(TEXT("coordinator")) && I.Top == Top; });
	for (int32 T = 0; T < 2 && Coord.Num(); ++T)
	{
		const tag::Team& Tm = T == 0 ? Home : Away;
		const double Side = T == 0 ? 1.0 : -1.0;
		const double CoachX = T == 0 ? 46.0 : 54.0;
		for (int32 k = 0; k < 2; ++k)
		{
			const int32 HeadI = Heads.Num() ? Heads[(Tm.id * 7 + k * 11 + 3) % Heads.Num()] : INDEX_NONE;
			AddStaticPerson(Coord[(Tm.id + k) % Coord.Num()], HeadI, FVector((CoachX + Side * (k ? 2.2 : -2.0) + 0.6) * YD, Side * (Edge + 2.0 + k * 0.6) * YD, 0),
				Side > 0 ? -90.f : 90.f, Hex(Tm.colors.primary) * 0.8f, Khaki, false, ++Seed);
		}
		// the pack's sideline players (helmets on, team colours) at the back of the team area
		const TArray<int32> Sp = A.HeroOf(TEXT("SidePlayer"));
		for (int32 k = 0; k < Sp.Num(); ++k)
			AddStaticPerson(Sp[k], INDEX_NONE, FVector((36.0 + k * 13.0 + T * 4.0) * YD, Side * (Edge + 5.2) * YD, 0), Side > 0 ? -90.f : 90.f,
				Hex(Tm.colors.primary), Hex(Tm.colors.secondary), false, ++Seed);
	}
	if (PackPreload) return; // the collect pass
	UE_LOG(LogTemp, Display, TEXT("TAG: model pack sideline people %d (media, coordinators, sideline players)"), SidelinePeople.Num());
}

/**
 * A seated fan: out of the ray tracing scene (hundreds of skinned meshes rebuilding their ray tracing geometry every
 * frame cost more than the rest of the render thread; Lumen still lights them from the screen) and on the mesh's fixed
 * bounds (a fan sways in place, so no bounds pass over the pose each frame).
 */
static void CrowdCheap(USkeletalMeshComponent* F)
{
	F->bVisibleInRayTracing = false;
	F->bComponentUseFixedSkelBounds = true;
}

bool AStadium::BuildPackFans(const TArray<FTransform>& Seats, const tag::Team& Home, const tag::Team& Away)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	if (!A.bHero || FParse::Param(FCommandLine::Get(), TEXT("TagMassFans"))) return false;
	const TArray<int32> Kinds = A.HeroOf(TEXT("Fan"));
	if (!Kinds.Num()) return false;
	FanIdle = A.HeroClip(TEXT("crowd"), TEXT("Idle"));
	FanCheer = A.HeroClip(TEXT("crowd"), TEXT("Cheer"));
	FanClap = A.HeroClip(TEXT("crowd"), TEXT("Clap"));
	if (!FanIdle || !FanCheer) return false;
	// the season's fans: warm weather in shirts, cold in jackets, rain in ponchos where the pack has them
	const TCHAR* Wx = A.HeroWeather == 2 ? TEXT("_rain_") : A.HeroWeather == 1 ? TEXT("_cold_") : TEXT("_warm_");
	// fans whose skin weights the pack has wrong (they tear apart once animated) stay out of the seats
	TArray<int32> Pool = A.HeroOf(TEXT("Fan"), [Wx](const FHeroItem& I) { return I.Name.Contains(Wx) && I.Far < 0.15f; });
	if (Pool.Num() < 6) Pool = A.HeroOf(TEXT("Fan"), [](const FHeroItem& I) { return I.Far < 0.15f; });
	const FLinearColor HomeC[2] = {Hex(Home.colors.primary), Hex(Home.colors.secondary)};
	const FLinearColor AwayC[2] = {Hex(Away.colors.primary), Hex(Away.colors.secondary)};
	FRandomStream Rand(Home.id * 977 + 13);
	// the pack's story characters take every 24th seat, a different run of them each game (all 166 over a few games)
	const TArray<int32> Story = A.HeroOf(TEXT("Story"), [](const FHeroItem& I) { return (I.Height <= 0.f || I.Height > 140.f) && I.Far < 0.15f; }); // whole people, intact skins only
	const TArray<int32> Props = A.HeroOf(TEXT("FanProp"));
	int32 StoryNext = Story.Num() ? (Home.id * 61 + Away.id * 7) % Story.Num() : 0;
	// every mesh the seats below can ask for, loaded as one batch before the first fan is built: the weather's fan
	// pool (1,900 draws over a few dozen fans use them all), this game's run of story characters (every 24th seat,
	// in order, so the run is known up front) and the hand props. The seat loop itself is untouched and draws from
	// Rand exactly as before; it now finds each mesh already loaded.
	{
		TArray<int32> Want;
		auto Wanted = [&](int32 WantItem)
		{
			const FString WantRig = A.HeroItems[WantItem].Rig;
			if (A.HeroClip(WantRig, TEXT("Idle")) && A.HeroClip(WantRig, TEXT("Cheer"))) Want.AddUnique(WantItem); // seats skip the others
		};
		for (int32 PoolItem : Pool) Wanted(PoolItem);
		int32 StoryAt = StoryNext;
		for (int32 SeatI = 0; SeatI < Seats.Num() && Story.Num(); ++SeatI)
			if (SeatI % 24 == 3) Wanted(Story[StoryAt++ % Story.Num()]);
		for (int32 PropItem : Props) Want.AddUnique(PropItem);
		PreloadHeroMeshes(A, Want, TEXT("fans"));
	}
	for (int32 SeatI = 0; SeatI < Seats.Num(); ++SeatI)
	{
		const FTransform& Seat = Seats[SeatI];
		const bool bStory = Story.Num() && SeatI % 24 == 3;
		const int32 Item = bStory ? Story[StoryNext++ % Story.Num()] : Pool[Rand.RandRange(0, Pool.Num() - 1)];
		const FString Rig = A.HeroItems[Item].Rig;
		UAnimationAsset* Idle = A.HeroClip(Rig, TEXT("Idle"));
		UAnimationAsset* Cheer = A.HeroClip(Rig, TEXT("Cheer"));
		if (!Idle || !Cheer) continue;
		USkeletalMesh* M = A.HeroMesh(Item);
		if (!M) continue;
		USkeletalMeshComponent* F = NewObject<USkeletalMeshComponent>(this);
		F->SetSkeletalMesh(M);
		F->SetupAttachment(RootComponent);
		F->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		F->SetCastShadow(false);
		F->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		// A fan animates at about 30 Hz on a tick of his own (-TagFanHz=N, 0 = every frame). The engine's update rate
		// optimisation keeps one tracker per owning actor, and every fan belongs to the stadium: one near fan held all
		// 1,951 at full rate (measured: the crowd was 8 ms of a 15 ms game thread from the play-call camera, plus the
		// skin cache dispatches of 2,000 bodies a frame). Intervals differ by a few percent so the fans spread over
		// the frames instead of all posing on the same one.
		static float FanHz = FParse::Value(FCommandLine::Get(), TEXT("TagFanHz="), FanHz) ? FMath::Clamp(FanHz, 0.f, 240.f) : 30.f;
		const float FanTick = FanHz > 0.f ? (0.85f + 0.3f * static_cast<float>((SeatI * 37) % 101) / 100.f) / FanHz : 0.f;
		F->bEnableUpdateRateOptimizations = FanHz <= 0.f;
		F->PrimaryComponentTick.TickInterval = FanTick;
		CrowdCheap(F);
		F->RegisterComponent();
		// the pack characters face +Y: yaw -90 turns them to the field
		F->SetWorldLocationAndRotation(Seat.GetLocation(), FRotator(0, Seat.Rotator().Yaw - 90.f, 0));
		// one of the N x N people in the fan's texture, in the home colours (three in four) or the visitors'
		const int32 Cells = FMath::Max(1, A.HeroItems[Item].Cells);
		const bool bAway = Rand.FRand() < 0.25f;
		const FLinearColor C = (bAway ? AwayC : HomeC)[Rand.FRand() < 0.7f ? 0 : 1];
		const FLinearColor Tint = Rand.FRand() < 0.12f ? FLinearColor(0.85f, 0.85f, 0.85f) : C;
		for (int32 Mi = 0; Mi < F->GetNumMaterials(); ++Mi)
		{
			float Mode = 0.f;
			UMaterialInterface* Cur = F->GetMaterial(Mi);
			if (!Cur || !Cur->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Mode")), Mode) || Mode < 0.5f || Mode > 2.5f) continue;
			// crowd atlases: one of the N x N people; team colour on the shirt (fans) or the placeholder clothes (story)
			UMaterialInstanceDynamic* Mid = F->CreateDynamicMaterialInstance(Mi);
			Mid->SetScalarParameterValue(TEXT("CellU"), static_cast<float>(Rand.RandRange(0, Cells - 1)));
			Mid->SetScalarParameterValue(TEXT("CellV"), static_cast<float>(Rand.RandRange(0, Cells - 1)));
			Mid->SetVectorParameterValue(TEXT("Tint"), Tint);
		}
		// a sign, towel or foam finger in a few hands (crowd rig props follow the fan's pose)
		if (!bStory && Props.Num() && Rig == TEXT("crowd") && Rand.FRand() < 0.15f)
			if (USkeletalMesh* PM = A.HeroMesh(Props[Rand.RandRange(0, Props.Num() - 1)]))
			{
				USkeletalMeshComponent* Pc = NewObject<USkeletalMeshComponent>(this);
				Pc->SetSkeletalMesh(PM);
				Pc->SetupAttachment(F);
				Pc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Pc->SetCastShadow(false);
				CrowdCheap(Pc);
				Pc->PrimaryComponentTick.TickInterval = FanTick; // (it follows the fan's pose: no need to tick more often than he does)
				Pc->bUseBoundsFromLeaderPoseComponent = true;
				Pc->RegisterComponent();
				Pc->SetLeaderPoseComponent(F);
				if (UMaterialInstanceDynamic* Mid = Pc->CreateDynamicMaterialInstance(0)) Mid->SetVectorParameterValue(TEXT("Tint"), C);
			}
		F->PlayAnimation(Idle, true);
		if (UAnimSingleNodeInstance* I = F->GetSingleNodeInstance())
		{
			I->SetPosition(Rand.FRandRange(0.f, Idle->GetPlayLength()), false);
			I->SetPlayRate(Rand.FRandRange(0.85f, 1.15f));
		}
		Fans.Add(F);
		FanIdles.Add(Idle);
		FanCheers.Add((A.HeroClip(Rig, TEXT("Clap")) && Fans.Num() % 3 == 2) ? A.HeroClip(Rig, TEXT("Clap")) : Cheer);
		FanDelay.Add(Rand.FRandRange(0.f, 0.9f));
		FanCheering.Add(0);
	}
	return Fans.Num() > 0;
}

void AStadium::BuildHeroCrowd(const FVector& C, float Yaw)
{
	if (!LoadFanMeshes()) return;
	// lower deck surface (matches Stand(): box 3600 x Len x 50 cm, centre Fwd*1600 + Z 1050, pitched 33 deg)
	const FRotator R(0, Yaw, 0);
	const FVector Fwd = R.Vector();
	const FVector Side = FRotator(0, Yaw + 90, 0).Vector();
	const float P = FMath::DegreesToRadians(33.f);
	const FVector SlopeF = Fwd * FMath::Cos(P) + FVector::UpVector * FMath::Sin(P);
	const FVector SlopeU = -Fwd * FMath::Sin(P) + FVector::UpVector * FMath::Cos(P);
	const FVector DeckC = C + Fwd * 1600 + FVector(0, 0, 1050);
	const float FaceYaw = (-Fwd).Rotation().Yaw;
	FRandomStream Rand(static_cast<int32>(Yaw) + 11);
	constexpr int32 Rows = 3;
	constexpr double SeatW = 72.0;
	const double Half = 34.0 * YD; // between the 16s
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const double S = 120.0 + Row * 92.0;
		for (double T = -Half; T <= Half; T += SeatW)
		{
			if (Rand.FRand() < 0.1f) continue; // a few empty seats
			const FVector Feet = DeckC + SlopeF * (S - 1800.0) + SlopeU * 25.0 + Side * (T + Rand.FRandRange(-10, 10));
			USkeletalMeshComponent* F = NewObject<USkeletalMeshComponent>(this);
			F->SetSkeletalMesh(FanMeshes[Rand.RandRange(0, FanMeshes.Num() - 1)]);
			F->SetupAttachment(RootComponent);
			F->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			F->SetCastShadow(false);
			F->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
			F->bEnableUpdateRateOptimizations = true;
			CrowdCheap(F);
			F->RegisterComponent();
			// MetaHumans face +Y in component space: yaw -90 turns them to the field; a little head turn variety
			F->SetWorldLocationAndRotation(Feet, FRotator(0, FaceYaw - 90.f + Rand.FRandRange(-14, 14), 0));
			F->PlayAnimation(FanIdle, true);
			if (UAnimSingleNodeInstance* I = F->GetSingleNodeInstance())
			{
				I->SetPosition(Rand.FRandRange(0.f, FanIdle->GetPlayLength()), false);
				I->SetPlayRate(Rand.FRandRange(0.85f, 1.15f));
			}
			Fans.Add(F);
			FanDelay.Add(Rand.FRandRange(0.f, 0.9f));
			FanCheering.Add(0);
		}
	}
}

void AStadium::UpdateMassFans(float Dt)
{
	// big plays: the dressed close-up crowd actors (Mass high-res representation) cheer with our keyed loop;
	// far LODs keep the sample's baked idle. Back to the sample's crowd anim blueprint when the hype fades.
	if (!MassCheer)
	{
		MassCheer = LoadObject<UAnimationAsset>(nullptr, TEXT("/Game/TAG/Crowd/Anims/A_CrowdActor_Cheer.A_CrowdActor_Cheer"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!MassCheer) return;
	}
	const bool bWant = bMassCheer ? Hype > 0.15f : Hype > 0.5f;
	MassScanT -= Dt;
	if (bWant == bMassCheer && (!bWant || MassScanT > 0.f)) return;
	MassScanT = 0.5f; // pick up crowd actors the Mass LOD spawns while the crowd is cheering
	bMassCheer = bWant;
	if (!bWant)
	{
		for (auto& KV : MassFanAnimClass)
		{
			if (USkeletalMeshComponent* C = KV.Key.Get())
			{
				C->SetAnimationMode(EAnimationMode::AnimationBlueprint);
				if (UClass* Cls = KV.Value.Get()) C->SetAnimInstanceClass(Cls);
			}
		}
		MassFanAnimClass.Reset();
		return;
	}
	int32 N = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!It->GetClass()->GetName().Contains(TEXT("MassHighRes"))) continue;
		TInlineComponentArray<USkeletalMeshComponent*> Comps(*It);
		for (USkeletalMeshComponent* C : Comps)
		{
			if (!C->GetSkeletalMeshAsset() || C->GetSkeletalMeshAsset()->GetSkeleton() != MassCheer->GetSkeleton()) continue;
			if (MassFanAnimClass.Contains(C)) continue;
			MassFanAnimClass.Add(C, C->GetAnimClass());
			C->PlayAnimation(MassCheer, true);
			if (UAnimSingleNodeInstance* I = C->GetSingleNodeInstance())
			{
				const uint32 H = GetTypeHash(C->GetUniqueID());
				I->SetPosition((H % 997) / 997.f * MassCheer->GetPlayLength(), false);
				I->SetPlayRate(0.88f + (H % 31) / 31.f * 0.25f);
			}
			++N;
		}
	}
	if (N) UE_LOG(LogTemp, Verbose, TEXT("TAG crowd cheer +%d (total %d)"), N, MassFanAnimClass.Num());
	static bool bListed = false;
	if (!bListed)
	{
		bListed = true;
		TMap<FString, int32> Classes;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It) Classes.FindOrAdd(It->GetClass()->GetName())++;
		for (auto& KV : Classes) if (KV.Value > 3 || KV.Key.Contains(TEXT("Mass")) || KV.Key.Contains(TEXT("Crowd")))
			UE_LOG(LogTemp, Display, TEXT("TAG crowd scan class %s x%d"), *KV.Key, KV.Value);
	}
}

void AStadium::UpdateHeroCrowd(float Dt)
{
	UpdateMassFans(Dt);
	if (FanSpawnerActor) FanSpawnerActor->Animate(Time, Hype);
	if (Fans.Num() == 0) return;
	// cheer while the crowd is hyped (big plays, scores), settle back to idle as Hype decays
	const bool bWantCheer = Hype > 0.5f;
	HypeHeld = (bWantCheer == (Hype > 0.15f)) ? HypeHeld + Dt : 0.f;
	for (int32 i = 0; i < Fans.Num(); ++i)
	{
		const bool bCheer = FanCheering[i] != 0;
		const bool bTarget = bCheer ? Hype > 0.15f : bWantCheer;
		if (bTarget == bCheer || HypeHeld < FanDelay[i]) continue;
		FanCheering[i] = bTarget ? 1 : 0;
		USkeletalMeshComponent* F = Fans[i];
		// each fan's own clips (its skeleton family); some clap instead of throwing their arms up
		UAnimationAsset* Clip = FanIdles.IsValidIndex(i) ? (bTarget ? FanCheers[i].Get() : FanIdles[i].Get())
			: (bTarget ? ((FanClap && i % 3 == 1) ? FanClap.Get() : FanCheer.Get()) : FanIdle.Get());
		F->PlayAnimation(Clip, true);
		if (UAnimSingleNodeInstance* I = F->GetSingleNodeInstance())
		{
			I->SetPosition(FMath::Frac(i * 0.137f) * Clip->GetPlayLength(), false);
			I->SetPlayRate(0.9f + FMath::Frac(i * 0.29f) * 0.25f);
		}
	}
}

void AStadium::BuildCrowd(const tag::Team& Home)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	const FLinearColor Cols[6] = {Hex(Home.colors.primary), Hex(Home.colors.secondary), FLinearColor(0.75f, 0.75f, 0.75f), FLinearColor(0.08f, 0.08f, 0.1f), FLinearColor(0.45f, 0.3f, 0.2f), Hex(Home.colors.primary) * 0.6f};
	for (const FLinearColor& C : Cols) CrowdSections.Add(AddISM(A.Cube, C, 0.9f, false));
	FRandomStream R(7);
	auto Bowl = [&](const FVector& Edge, float Yaw, double Len, double Depth, double Rise, double Base, double Back)
	{
		const FRotator Rot(0, Yaw, 0);
		const FVector Fwd = Rot.Vector();
		const FVector Side = FRotator(0, Yaw + 90, 0).Vector();
		const int Rows = static_cast<int>(Depth / 90.0);
		const int Seats = static_cast<int>(Len / 65.0);
		for (int r = 0; r < Rows; ++r)
			for (int s = 0; s < Seats; ++s)
			{
				if (R.FRand() < 0.12f) continue; // empty seats
				const FVector P = Edge + Fwd * (Back + r * 90.0 * FMath::Cos(FMath::DegreesToRadians(33.f))) + Side * (-Len / 2 + s * 65.0 + R.FRandRange(-8, 8)) + FVector(0, 0, Base + r * Rise);
				const FTransform T(Rot, P + FVector(0, 0, 55), FVector(0.42f, 0.45f, R.FRandRange(0.95f, 1.3f)));
				const int32 Sec = R.RandRange(0, 5);
				const int32 Local = CrowdSections[Sec]->AddInstance(T);
				CrowdBase.Add(T);
				CrowdSectionOf.Add(Sec);
				CrowdLocal.Add(Local);
			}
	};
	const double SideY = (FW / 2 + 9) * YD;
	Bowl(FVector(50 * YD, SideY, 0), 90, 130 * YD, 2600, 58, 260, 300);
	Bowl(FVector(50 * YD, -SideY, 0), -90, 130 * YD, 2600, 58, 260, 300);
	Bowl(FVector(50 * YD, SideY, 0), 90, 130 * YD, 2200, 70, 2700, 3200);
	Bowl(FVector(50 * YD, -SideY, 0), -90, 130 * YD, 2200, 70, 2700, 3200);
	Bowl(FVector(-24 * YD, 0, 0), 180, 70 * YD, 2600, 58, 260, 300);
	Bowl(FVector(124 * YD, 0, 0), 0, 70 * YD, 2600, 58, 260, 300);
}

void AStadium::BuildRain(const tag::Weather& W)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	UTagPrecipComponent* Col = NewObject<UTagPrecipComponent>(this);
	Col->SetupAttachment(RootComponent);
	Col->SetStaticMesh(A.Cube);
	Col->SetMaterial(0, TagColorMat(this, bSnow ? FLinearColor(1, 1, 1) : FLinearColor(0.6f, 0.65f, 0.75f), 0.2f));
	Col->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Col->SetCastShadow(false);
	Rain = Col;
	// The streaks (flakes) stand still in the component and the component falls: one period carries it a column height
	// down and the wind's drift along, then it wraps. The column is laid twice, the copy one period back (up and
	// upwind), so at the wrap the copy sits exactly where the first one started. Moving every instance on the CPU and
	// rebuilding the render state each frame cost the game thread ~3 ms in snow. The tiny cubes stay out of the ray
	// tracing scene and the distance field lighting (a moving field of 20k instances would invalidate both every frame).
	Rain->bVisibleInRayTracing = false;
	Rain->bAffectDistanceFieldLighting = false;
	const double H = 60 * YD;
	const double Fall = (bSnow ? 2.0 : 30.0) * YD; // cm/s
	FVector Drift = Wind * (bSnow ? 1.0 : 0.6) * (H / Fall);
	Drift.Z = 0;
	const double MaxDrift = 15 * YD; // a gale would carry the column off the field in one period
	if (Drift.Size() > MaxDrift) Drift *= MaxDrift / Drift.Size();
	RainStep = FVector(Drift.X, Drift.Y, -H);
	RainPeriod = static_cast<float>(H / Fall);
	RainPhase = 0.f;
	const FRotator Slant = bSnow ? FRotator::ZeroRotator : FRotationMatrix::MakeFromZ(RainStep.GetSafeNormal()).Rotator();
	const FVector S = bSnow ? FVector(0.03f) : FVector(0.01f, 0.01f, 0.6f);
	FRandomStream R(11);
	const int N = bSnow ? 9000 : 12000;
	TArray<FTransform> Ts;
	Ts.Reserve(N * 2);
	for (int i = 0; i < N; ++i)
	{
		const FVector P(R.FRandRange(-30, 130) * YD, R.FRandRange(-60, 60) * YD, R.FRandRange(0, 60) * YD);
		Ts.Add(FTransform(Slant, P, S));
		Ts.Add(FTransform(Slant, P - RainStep, S));
	}
	// the column's box (instance centres padded by the streak length), then the instances
	for (const FTransform& T : Ts) Col->LocalBox += T.GetLocation();
	Col->LocalBox = Col->LocalBox.ExpandBy(100.0);
	Col->RegisterComponent();
	Rain->AddInstances(Ts, false);
	(void)W;
}

void AStadium::BuildGrass3D()
{
	// One square of blades, built once at the origin and never moved: the blade material wraps every blade's foot into
	// the square around the shot's subject (GrassCenter), so the patch follows the camera while each blade stays where
	// it grows, and shrinks the blades to nothing at the patch's rim and on worn turf. A blade is five points (foot,
	// a bend at 55 % of its height, tip), both windings, its normal near straight up so it is lit like the ground it
	// stands on. TexCoord 1 = the point's offset from the foot (x, y), TexCoord 2 = (offset z, random); vertex colour
	// R = the blade's random number, G = height along the blade. No shadows, not in the ray tracing / Lumen scenes.
	// Cost: nothing while hidden (the game camera); ~90k blades, 270k triangles a view when shown.
	int32 Count = 90000;
	FParse::Value(FCommandLine::Get(), TEXT("TagGrassN="), Count);
	Count = FMath::Clamp(Count, 1000, 400000);
	FParse::Value(FCommandLine::Get(), TEXT("TagGrassPx="), GrassPx);
	FParse::Value(FCommandLine::Get(), TEXT("TagGrassDist="), GrassMaxDist);
	bGrassAlways = FParse::Param(FCommandLine::Get(), TEXT("TagGrassAlways"));
	GrassPx = FMath::Max(GrassPx, 0.5f);
	TArray<FVector> Verts, Norms;
	TArray<int32> Tris;
	TArray<FVector2D> Uv0, Uv1, Uv2;
	TArray<FLinearColor> Cols;
	Verts.Reserve(Count * 5);
	Norms.Reserve(Count * 5);
	Uv0.Reserve(Count * 5);
	Uv1.Reserve(Count * 5);
	Uv2.Reserve(Count * 5);
	Cols.Reserve(Count * 5);
	Tris.Reserve(Count * 18);
	FRandomStream Rand(4107);
	static const int32 Idx[9] = {0, 1, 2, 1, 3, 2, 2, 3, 4};
	static const float Along[5] = {0.f, 0.f, 0.55f, 0.55f, 1.f};
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector Foot(Rand.FRand() * GrassPatch, Rand.FRand() * GrassPatch, 0.0);
		const float Az = Rand.FRandRange(0.f, 2.f * UE_PI);
		const FVector Side(-FMath::Sin(Az), FMath::Cos(Az), 0.0);
		const float Tall = Rand.FRandRange(2.4f, 4.0f), Wide = Rand.FRandRange(0.6f, 1.0f); // cm: mown to about 3 cm
		const float LeanAz = Rand.FRandRange(0.f, 2.f * UE_PI);
		const FVector Lean = FVector(FMath::Cos(LeanAz), FMath::Sin(LeanAz), 0.0) * (Tall * Rand.FRandRange(0.1f, 0.5f));
		const float Rnd = Rand.FRand(), Rnd2 = Rand.FRand();
		const FVector Nrm = FVector(Lean.X * 0.08, Lean.Y * 0.08, 1.0).GetSafeNormal();
		const FVector Off[5] = {
			-Side * (Wide * 0.5), Side * (Wide * 0.5),
			Lean * 0.35 - Side * (Wide * 0.36) + FVector(0, 0, Tall * 0.55), Lean * 0.35 + Side * (Wide * 0.36) + FVector(0, 0, Tall * 0.55),
			Lean + FVector(0, 0, Tall * 0.97)};
		const int32 Base = Verts.Num();
		for (int32 k = 0; k < 5; ++k)
		{
			Verts.Add(Foot + Off[k]);
			Norms.Add(Nrm);
			Uv0.Add(FVector2D(k == 4 ? 0.5 : double(k % 2), Along[k]));
			Uv1.Add(FVector2D(Off[k].X, Off[k].Y));
			Uv2.Add(FVector2D(Off[k].Z, Rnd2));
			Cols.Add(FLinearColor(Rnd, Along[k], Rnd2, 1.f));
		}
		for (int32 k = 0; k < 9; k += 3)
		{
			Tris.Append({Base + Idx[k], Base + Idx[k + 1], Base + Idx[k + 2]});
			Tris.Append({Base + Idx[k], Base + Idx[k + 2], Base + Idx[k + 1]});
		}
	}
	UProceduralMeshComponent* G = NewObject<UProceduralMeshComponent>(this);
	G->SetupAttachment(RootComponent);
	G->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	G->SetCastShadow(false);
	G->bVisibleInRayTracing = false;
	G->bAffectDistanceFieldLighting = false;
	G->bAffectDynamicIndirectLighting = false;
	G->bReceivesDecals = false;
	G->SetBoundsScale(40.f); // the vertex shader carries the blades anywhere on the field: never cull by the built square
	G->RegisterComponent();
	G->CreateMeshSection_LinearColor(0, Verts, Tris, Norms, Uv0, Uv1, Uv2, TArray<FVector2D>(), Cols, TArray<FProcMeshTangent>(), false);
	if (GrassMID)
	{
		GrassMID->SetScalarParameterValue(TEXT("PatchSize"), GrassPatch);
		GrassMID->SetScalarParameterValue(TEXT("Grow"), 0.f);
		G->SetMaterial(0, GrassMID);
	}
	G->SetVisibility(false);
	Grass3D = G;
	GrassGrow = 0.f;
}

void AStadium::UpdateGrass3D(float Dt)
{
	if (!Grass3D || !GrassMID) return;
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APlayerCameraManager* CamMgr = PC ? PC->PlayerCameraManager.Get() : nullptr;
	if (!CamMgr) return;
	const FVector Loc = CamMgr->GetCameraLocation();
	const FVector Fwd = CamMgr->GetCameraRotation().Vector();
	const float HalfFov = FMath::DegreesToRadians(FMath::Clamp(CamMgr->GetFOVAngle(), 1.f, 170.f)) * 0.5f;
	// the director's cine camera says what the shot is of (focus distance) and whether it is a close shot (f/1.2)
	double Focus = 0.0;
	float Aperture = 5.6f;
	if (const ACineCameraActor* CineCam = Cast<ACineCameraActor>(PC->GetViewTarget()))
		if (const UCineCameraComponent* Lens = CineCam->GetCineCameraComponent())
		{
			Focus = Lens->FocusSettings.ManualFocusDistance;
			Aperture = Lens->CurrentAperture;
		}
	// the subject: the focus distance down the view, no further than where the view meets the turf
	const double ToTurf = Fwd.Z < -0.02 ? (Loc.Z - 0.5) / -Fwd.Z : 1.0e6;
	const double Dist = FMath::Max(Focus > 50.0 ? FMath::Min(Focus, ToTurf) : ToTurf, 50.0);
	// a 3.2 cm blade's height on a 1080p frame
	const double BladePx = 3.2 * 960.0 / (Dist * FMath::Tan(HalfFov));
	float Want = Aperture <= 2.f ? 1.f : FMath::Clamp(static_cast<float>((BladePx - GrassPx) / (GrassPx * 0.5)), 0.f, 1.f);
	Want *= FMath::Clamp(static_cast<float>((GrassMaxDist - Dist) / 400.0), 0.f, 1.f);
	if (bGrassAlways) Want = 1.f;
	const bool bCut = FVector::DistSquared(Loc, GrassCamPrev) > FMath::Square(500.0);
	GrassCamPrev = Loc;
	GrassGrow = bCut ? Want : FMath::FInterpTo(GrassGrow, Want, Dt, 6.f);
	const bool bShow = GrassGrow > 0.02f;
	if (Grass3D->IsVisible() != bShow)
	{
		Grass3D->SetVisibility(bShow);
		UE_LOG(LogTemp, Log, TEXT("TAG grass3D %s: subject %.0f cm, f/%.1f, blade %.1f px"), bShow ? TEXT("on") : TEXT("off"), Dist, Aperture, BladePx);
	}
	if (!bShow) return;
	const FVector Subject = Loc + Fwd * FMath::Min(Dist, static_cast<double>(GrassMaxDist));
	GrassMID->SetVectorParameterValue(TEXT("GrassCenter"), FLinearColor(static_cast<float>(Subject.X), static_cast<float>(Subject.Y), 0.f, 0.f));
	GrassMID->SetScalarParameterValue(TEXT("Grow"), GrassGrow);
}

void AStadium::BuildOfficials()
{
	FAthleteAssets& A = FAthleteAssets::Get();
	const double CrewY = (FW / 2 + 1.3) * YD; // just outside the far sideline, facing the broadcast cameras
	auto Rig = [&](const TCHAR* Name)
	{
		USceneComponent* R = NewObject<USceneComponent>(this, Name);
		R->SetupAttachment(RootComponent);
		R->RegisterComponent();
		R->SetRelativeLocation(FVector(50 * YD, CrewY, 0));
		return R;
	};
	auto Part = [&](USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Rel, const FVector& Scale, const FLinearColor& C, float Rough)
	{
		UStaticMeshComponent* M = AddMesh(Mesh, FVector::ZeroVector, Scale, C, Rough, FRotator::ZeroRotator, true);
		M->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
		M->SetRelativeLocation(Rel);
		return M;
	};
	const FLinearColor Black(0.02f, 0.02f, 0.02f), Orange(1.f, 0.32f, 0.02f);
	// each rod: an 8 ft pole with an orange marker on top, held upright at the crew member's side (field side)
	for (int32 k = 0; k < 2; ++k)
	{
		USceneComponent* R = Rig(k ? TEXT("ChainFront") : TEXT("ChainRear"));
		(k ? ChainFront : ChainRear) = R;
		// (the model pack's rod men come with a rod skinned to one finger and modelled lying across the body: it swung
		// with the hand and rendered as a slanted 2.4 m bar. hero_build strips it (strip_prop) and these stand instead.)
		Part(R, A.Cylinder, FVector(0, -30, 122), FVector(0.04, 0.04, 2.44), Black, 0.5f);
		Part(R, A.Cylinder, FVector(0, -30, 236), FVector(0.16, 0.16, 0.3), Orange, 0.6f);
	}
	{
		USceneComponent* R = Rig(TEXT("DownBox"));
		DownBox = R;
		Part(R, A.Cylinder, FVector(0, -30, 115), FVector(0.045, 0.045, 2.3), Black, 0.5f);
		Part(R, A.Cube, FVector(0, -30, 258), FVector(0.42, 0.42, 0.42), Orange, 0.5f);
		for (int32 Face = 0; Face < 2; ++Face)
		{
			UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
			T->SetupAttachment(R);
			T->SetWorldSize(36.f);
			T->SetTextRenderColor(FColor(15, 15, 15));
			T->SetHorizontalAlignment(EHTA_Center);
			T->SetVerticalAlignment(EVRTA_TextCenter);
			T->SetCastShadow(false);
			T->RegisterComponent();
			T->SetRelativeLocationAndRotation(FVector(0, -30 + (Face ? -22.f : 22.f), 258), FRotator(0, Face ? -90.f : 90.f, 0));
			DownText.Add(T);
		}
	}
	// the chain lies on the ground between the rods
	ChainLink = AddMesh(A.Cube, FVector(50 * YD, CrewY - 30, 1.5), FVector(10 * YD / 100, 0.012, 0.012), FLinearColor(0.35f, 0.35f, 0.37f), 0.3f, FRotator::ZeroRotator, false);
	SetDown(1);
}

FVector AStadium::DownBoxLocation() const
{
	return DownBox ? DownBox->GetComponentLocation() : FVector::ZeroVector;
}

void AStadium::SetDown(int32 Down)
{
	for (UTextRenderComponent* T : DownText)
		if (T) T->SetText(Down >= 1 && Down <= 4 ? FText::AsNumber(Down) : FText::GetEmpty());
}

// show / hide a chain post: the stadium's own parts, and the crew member standing at it as a whole actor (propagating
// visibility into his components would turn his hidden gear and markers back on)
static void ShowPost(AActor* Self, USceneComponent* Post, bool bShow)
{
	if (!Post) return;
	Post->SetVisibility(bShow, false);
	for (USceneComponent* C : Post->GetAttachChildren())
	{
		if (!C) continue;
		if (C->GetOwner() == Self) C->SetVisibility(bShow, true);
		else if (AActor* O = C->GetOwner()) O->SetActorHiddenInGame(!bShow);
	}
}

void AStadium::SetLines(double LosX, double FirstDownX)
{
	// A5: the line the linemen scuff (a new spot, or the next snap from the same one, starts each man's budget at it
	// again; the director sets the lines twice for one snap, a few seconds apart)
	if (LosX >= 0 && (LosNowX < 0 || FMath::Abs(LosX - LosNowX) > 30.0 || Time - LosSetTime > 15.f))
	{
		++LosSerial;
		LosSetTime = Time;
	}
	LosNowX = LosX;
	// the chain crew: down box at the line of scrimmage, the rods at the start of the series and the line to gain
	if (DownBox)
	{
		ShowPost(this, DownBox, LosX >= 0);
		if (LosX >= 0) DownBox->SetRelativeLocation(FVector(LosX, DownBox->GetRelativeLocation().Y, 0));
	}
	if (ChainRear && ChainFront && ChainLink)
	{
		const bool bChains = FirstDownX >= 0 && LosX >= 0;
		ShowPost(this, ChainRear, bChains);
		ShowPost(this, ChainFront, bChains);
		ChainLink->SetVisibility(bChains);
		if (bChains)
		{
			const double Dir = FirstDownX >= LosX ? 1.0 : -1.0;
			const double RearX = FirstDownX - Dir * 10 * YD;
			const double Y = ChainRear->GetRelativeLocation().Y;
			ChainRear->SetRelativeLocation(FVector(RearX, Y, 0));
			ChainFront->SetRelativeLocation(FVector(FirstDownX, Y, 0));
			ChainLink->SetRelativeLocation(FVector((RearX + FirstDownX) * 0.5, Y - 30, 1.5));
		}
	}
	if (LosLine)
	{
		LosLine->SetVisibility(LosX >= 0);
		LosLine->SetRelativeLocation(FVector(LosX, 0, 2));
	}
	if (FdLine)
	{
		FdLine->SetVisibility(FirstDownX >= 0);
		FdLine->SetRelativeLocation(FVector(FirstDownX, 0, 2));
	}
}

void AStadium::Tick(float Dt)
{
	Super::Tick(Dt);
	Time += Dt;
	Hype = FMath::Max(0.f, Hype - Dt * 0.25f);
	UpdateHeroCrowd(Dt);
	UpdateGrass3D(Dt);
	// crowd: idle sway, jumping on big plays (update a rotating slice each frame to stay cheap)
	if (CrowdBase.Num() > 0)
	{
		const int32 Slice = FMath::Max(1, CrowdBase.Num() / 4);
		for (int32 k = 0; k < Slice; ++k)
		{
			const int32 i = (CrowdCursor + k) % CrowdBase.Num();
			FTransform T = CrowdBase[i];
			const float Bounce = FMath::Max(0.f, FMath::Sin(Time * (5.f + (i % 7)) + i)) * (3.f + Hype * 45.f);
			T.AddToTranslation(FVector(0, 0, Bounce));
			CrowdSections[CrowdSectionOf[i]]->UpdateInstanceTransform(CrowdLocal[i], T, false, false, true);
		}
		CrowdCursor = (CrowdCursor + Slice) % CrowdBase.Num();
		for (UInstancedStaticMeshComponent* C : CrowdSections) C->MarkRenderStateDirty();
	}
	// precipitation
	if (Rain)
	{
		RainPhase = FMath::Fmod(RainPhase + Dt / FMath::Max(RainPeriod, 0.01f), 1.f);
		Rain->SetRelativeLocation(RainStep * RainPhase);
	}
	UpdatePrecip(Dt);
	// the weather's strength (A2): fog, turf and the men's kits follow it; once a second is plenty
	WeatherT += Dt;
	if (WeatherT >= 1.f)
	{
		WeatherT = 0.f;
		ApplyWeather();
	}
	// the men on the field (A5 wear stamps, A6 kits), every WearStep seconds
	const FStadiumLook& Look = FStadiumLook::Get();
	if (bWearLive || !Look.bOldKit)
	{
		PlayerStepT += Dt;
		if (PlayerStepT >= Look.WearStep)
		{
			StepPlayers(PlayerStepT);
			PlayerStepT = 0.f;
		}
	}
	UpdateWear(Dt);
	UpdateBreath(Dt);
	// the painted crowd jumps with the excitement (Phase 4, M_TagCrowd2): the parameter only when it has moved
	if (CrowdAnimMID && FMath::Abs(Hype - CrowdHypeSent) > 0.03f)
	{
		CrowdHypeSent = Hype;
		CrowdAnimMID->SetScalarParameterValue(TEXT("Hype"), Hype);
	}
}

void AStadium::SetGameClock(int32 Quarter, double ClockLeft, double QuarterLen)
{
	const double Len = FMath::Max(QuarterLen, 1.0);
	const double Done = (FMath::Clamp(Quarter, 1, 4) - 1) * Len + (Len - FMath::Clamp(ClockLeft, 0.0, Len));
	GameProgress = Quarter > 4 ? 1.f : static_cast<float>(FMath::Clamp(Done / (4.0 * Len), 0.0, 1.0));
	if (FStadiumLook::Get().ProgressFixed >= 0.f) GameProgress = FMath::Min(FStadiumLook::Get().ProgressFixed, 1.f);
	GameQuarter = Quarter;
	ApplyWeather();
	UpdateSun(false);
	// -TagWearPng: the wear map at the first snap of each quarter after the first (Saved/TagWear)
	if (Quarter >= 2 && Quarter != WearDumpedQ)
	{
		WearDumpedQ = Quarter;
		DumpWear(*FString::Printf(TEXT("q%d"), Quarter));
	}
}

void AStadium::EndPlay(const EEndPlayReason::Type Reason)
{
	DumpWear(TEXT("end"));
	Super::EndPlay(Reason);
}

// ---------------------------------------------------------------------------------------------------- A2 weather

void AStadium::ApplyWeather()
{
	const FStadiumLook& Look = FStadiumLook::Get();
	if (!bRainy && !bSnow) return;
	// how far into its build-up the weather is: the game's progress (SetGameClock), or a 45 minute guess until the
	// director has said; a fixed strength (-TagPrecip=) stands for the whole build-up
	const float Progress = GameProgress >= 0.f ? GameProgress : FMath::Clamp(Time / 2700.f, 0.f, 1.f);
	const float Ramp = Look.PrecipFixed >= 0.f ? FMath::Clamp(Look.PrecipFixed, 0.f, 1.f) : FMath::SmoothStep(0.f, 1.f, Progress);
	PrecipIntensity = Look.PrecipFixed >= 0.f ? Ramp : FMath::Lerp(Look.PrecipStart, Look.PrecipEnd, Ramp);
	if (WeatherApplied >= 0.f && FMath::Abs(PrecipIntensity - WeatherApplied) < 0.02f) return;
	WeatherApplied = PrecipIntensity;
	// the far veil thickens with it (FogRamp 0: the fixed density as before)
	if (FogC) FogC->SetFogDensity(FogBase * FMath::Max(0.2f, 1.f + Look.FogRamp * (PrecipIntensity - 0.6f)));
	// the turf soaks through; the snow piles up (the trampled lanes are the wear map's, A5)
	for (UMaterialInstanceDynamic* M : {TurfMID.Get(), GrassMID.Get()})
	{
		if (!M) continue;
		if (bRainy) M->SetScalarParameterValue(TEXT("Wet"), FMath::Lerp(Look.RainWetStart, 1.f, Ramp));
		if (bSnow) M->SetScalarParameterValue(TEXT("Snow"), SnowCoverBase * FMath::Lerp(Look.SnowCoverStart, Look.SnowCoverEnd, Ramp));
	}
	UE_LOG(LogTemp, Log, TEXT("TAG weather: strength %.2f (progress %.2f), fog %.4f"), PrecipIntensity, Progress, FogC ? FogC->FogDensity : 0.f);
}

bool AStadium::BuildPrecip(const tag::Weather& W)
{
	const FStadiumLook& Look = FStadiumLook::Get();
	if (Look.bOldWeather) return false;
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/TAG/Materials/M_TagPrecip.M_TagPrecip"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Mat)
	{
		UE_LOG(LogTemp, Display, TEXT("TAG precip: no M_TagPrecip (Tools/Art/ue_build_stadium_mats.py): the old cube column"));
		return false;
	}
	// One quad per flake or drop: its four corners share the point it was made at (TexCoord 0 the corner, TexCoord 1
	// two random numbers), so the mesh is nothing until the material spreads each quad around its point, turned to the
	// camera. Points fill the layer's box; the material wraps them into the box around the shot as they fall. No
	// shadows, not in the ray tracing, distance field or Lumen scenes.
	const int32 Counts[2] = {bSnow ? Look.SnowFlakes : Look.RainDrops, Look.NearCount};
	const FVector Boxes[2] = {FVector(Look.FarBox, Look.FarBox, 2500.0), FVector(1400.0, 1400.0, 900.0)};
	FRandomStream Rand(907);
	for (int32 Layer = 0; Layer < 2; ++Layer)
	{
		const int32 N = Counts[Layer];
		if (N <= 0) continue;
		const FVector Box = Boxes[Layer];
		TArray<FVector> Verts, Norms;
		TArray<int32> Tris;
		TArray<FVector2D> Uv0, Uv1;
		Verts.Reserve(N * 4);
		Norms.Reserve(N * 4);
		Uv0.Reserve(N * 4);
		Uv1.Reserve(N * 4);
		Tris.Reserve(N * 6);
		for (int32 i = 0; i < N; ++i)
		{
			const FVector At(Rand.FRand() * Box.X, Rand.FRand() * Box.Y, Rand.FRand() * Box.Z);
			const FVector2D Rnd(Rand.FRand(), Rand.FRand());
			const int32 Base = Verts.Num();
			for (int32 k = 0; k < 4; ++k)
			{
				Verts.Add(At);
				Norms.Add(FVector::UpVector);
				Uv0.Add(FVector2D(static_cast<double>(k & 1), static_cast<double>(k >> 1)));
				Uv1.Add(Rnd);
			}
			Tris.Append({Base, Base + 2, Base + 1, Base + 1, Base + 2, Base + 3});
		}
		UProceduralMeshComponent* P = NewObject<UProceduralMeshComponent>(this);
		P->SetupAttachment(RootComponent);
		P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		P->SetCastShadow(false);
		P->bVisibleInRayTracing = false;
		P->bAffectDistanceFieldLighting = false;
		P->bAffectDynamicIndirectLighting = false;
		P->bReceivesDecals = false;
		P->SetBoundsScale(40.f); // the vertex shader carries the quads wherever the shot is: never cull by the built box
		P->RegisterComponent();
		P->CreateMeshSection_LinearColor(0, Verts, Tris, Norms, Uv0, Uv1, TArray<FVector2D>(), TArray<FVector2D>(), TArray<FLinearColor>(),
			TArray<FProcMeshTangent>(), false);
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Mat, this);
		const float Fall = bSnow ? Look.SnowFallCm : Look.RainFallCm;
		const FVector Drift = Wind * (bSnow ? 1.0 : 0.6);
		M->SetVectorParameterValue(TEXT("Box"), FLinearColor(static_cast<float>(Box.X), static_cast<float>(Box.Y), static_cast<float>(Box.Z), 0.f));
		M->SetScalarParameterValue(TEXT("Fall"), Fall);
		M->SetVectorParameterValue(TEXT("Wind"), FLinearColor(static_cast<float>(Drift.X), static_cast<float>(Drift.Y), 0.f, 0.f));
		M->SetScalarParameterValue(TEXT("Streak"), bSnow ? 0.f : 1.f);
		M->SetVectorParameterValue(TEXT("Size"), bSnow ? FLinearColor(Look.SnowFlakeCm, Look.SnowFlakeCm, 0.f, 0.f) : FLinearColor(0.15f, Look.RainStreakCm, 0.f, 0.f));
		M->SetScalarParameterValue(TEXT("MinPx"), bSnow ? Look.SnowMinPx : Look.RainMinPx);
		M->SetScalarParameterValue(TEXT("Swirl"), bSnow ? 25.f : 0.f);
		M->SetScalarParameterValue(TEXT("Near"), Layer == 0 ? 150.f : 40.f);
		M->SetVectorParameterValue(TEXT("Color"), bSnow ? FLinearColor(0.95f, 0.97f, 1.f) : FLinearColor(0.8f, 0.85f, 0.92f));
		M->SetScalarParameterValue(TEXT("Bright"), bSnow ? (bNightGame ? Look.SnowBrightNight : Look.SnowBrightDay)
			: (bNightGame ? Look.RainBrightNight : Look.RainBrightDay));
		M->SetScalarParameterValue(TEXT("Intensity"), PrecipIntensity);
		P->SetMaterial(0, M);
		PrecipLayers.Add(P);
		PrecipMIDs.Add(M);
		PrecipBox.Add(Box);
	}
	UE_LOG(LogTemp, Display, TEXT("TAG precip: camera-local %s, %d + %d quads, box %.0f cm"), bSnow ? TEXT("snow") : TEXT("rain"), Counts[0], Counts[1], Look.FarBox);
	(void)W;
	return PrecipLayers.Num() > 0;
}

void AStadium::UpdatePrecip(float Dt)
{
	if (PrecipLayers.Num() == 0) return;
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APlayerCameraManager* CamMgr = PC ? PC->PlayerCameraManager.Get() : nullptr;
	if (!CamMgr) return;
	const FVector Loc = CamMgr->GetCameraLocation();
	const FVector Fwd = CamMgr->GetCameraRotation().Vector();
	const float HalfFov = FMath::DegreesToRadians(FMath::Clamp(CamMgr->GetFOVAngle(), 1.f, 170.f)) * 0.5f;
	// the shot's subject as the close-up grass finds it: the focus distance down the view, no further than the turf
	double Focus = 0.0;
	if (const ACineCameraActor* CineCam = Cast<ACineCameraActor>(PC->GetViewTarget()))
		if (const UCineCameraComponent* Lens = CineCam->GetCineCameraComponent()) Focus = Lens->FocusSettings.ManualFocusDistance;
	const double ToTurf = Fwd.Z < -0.02 ? (Loc.Z - 0.5) / -Fwd.Z : 1.0e5;
	const double Dist = FMath::Clamp(Focus > 50.0 ? FMath::Min(Focus, ToTurf) : ToTurf, 300.0, 30000.0);
	// world cm one pixel covers per cm of distance (the material keeps flakes and streaks at least MinPx wide)
	FVector2D ViewSize(1920.0, 1080.0);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(ViewSize);
	const float PxCm = 2.f * FMath::Tan(HalfFov) / static_cast<float>(FMath::Max(ViewSize.X, 64.0));
	for (int32 Layer = 0; Layer < PrecipLayers.Num(); ++Layer)
	{
		UMaterialInstanceDynamic* M = PrecipMIDs[Layer];
		if (!M) continue;
		const FVector Box = PrecipBox[Layer];
		FVector C;
		if (Layer == 0)
		{
			// the dense box: from just short of the subject back toward the lens, standing on the turf
			C = Loc + Fwd * FMath::Max(Dist - Box.X * 0.2, Box.X * 0.5);
			C.Z = FMath::Max(C.Z, Box.Z * 0.5 - 100.0);
		}
		else C = Loc + Fwd * (Box.X * 0.45); // the near flakes: the lens's own few metres
		M->SetVectorParameterValue(TEXT("Center"), FLinearColor(static_cast<float>(C.X), static_cast<float>(C.Y), static_cast<float>(C.Z), 0.f));
		M->SetScalarParameterValue(TEXT("PxCm"), PxCm);
		M->SetScalarParameterValue(TEXT("Intensity"), PrecipIntensity);
	}
	(void)Dt;
}

// ---------------------------------------------------------------------------------------------------- A4 wall and sidelines

/** The sponsors on the wall: invented for TAG (no real brand), and the game's own name. */
static const TCHAR* const GWallSponsors[] = {TEXT("PYLON PIZZA"), TEXT("SIDELINE SODA"), TEXT("GOAL LINE GARAGE"), TEXT("TWO MINUTE TACOS"),
	TEXT("HASHMARK HARDWARE"), TEXT("KICKOFF COFFEE"), TEXT("FOURTH DOWN FITNESS"), TEXT("COIN TOSS CREDIT UNION")};

void AStadium::BuildWallDressing(const tag::Team& Home, const tag::Team& Away, bool bNight)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	const FStadiumLook& Look = FStadiumLook::Get();
	const FLinearColor HomeC = Hex(Home.colors.primary), HomeC2 = Hex(Home.colors.secondary);
	auto Lum = [](const FLinearColor& Col) { return 0.2126f * Col.R + 0.7152f * Col.G + 0.0722f * Col.B; };
	// the wordmark: the second colour on the first where they stand apart, white otherwise
	const FColor WordInk = FMath::Abs(Lum(HomeC2) - Lum(HomeC)) > 0.18f ? HomeC2.ToFColor(true) : FColor(240, 240, 240);
	const FString Word = FString(Home.name.c_str()).ToUpper();
	// The panels sit 3 cm proud of the ribbon (75..195 cm up the wall), one instanced draw per backing colour, on the
	// LED material at the video boards' low glow (dark faces) so the lit lettering in front reads, as on the end-zone
	// boards. Text is one UTextRenderComponent per panel.
	TMap<uint32, UInstancedStaticMeshComponent*> Backs;
	auto Backing = [&](const FLinearColor& Col) -> UInstancedStaticMeshComponent*
	{
		const uint32 Key = Col.ToFColor(false).ToPackedARGB();
		if (UInstancedStaticMeshComponent** Found = Backs.Find(Key)) return *Found;
		UInstancedStaticMeshComponent* Ism = AddISM(A.Cube, Col * 0.5f, 0.5f, false);
		if (A.LEDMat)
		{
			UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(A.LEDMat, this);
			D->SetVectorParameterValue(TEXT("Color"), Col);
			D->SetVectorParameterValue(TEXT("ColorB"), Col);
			D->SetScalarParameterValue(TEXT("Intensity"), bNight ? Look.WallPanelNight : Look.WallPanelDay);
			D->SetScalarParameterValue(TEXT("Count"), 0.f);
			D->SetScalarParameterValue(TEXT("Diffuse"), 0.05f);
			D->SetScalarParameterValue(TEXT("Roughness"), 0.3f);
			Ism->SetMaterial(0, D);
		}
		Backs.Add(Key, Ism);
		return Ism;
	};
	int32 Panels = 0;
	auto Panel = [&](const FVector& WallAt, float FaceYaw, double Width, const FString& Text, const FLinearColor& Back, const FColor& Ink)
	{
		const FVector Out = FRotator(0, FaceYaw, 0).Vector(); // into the bowl, away from the wall
		const FVector C = WallAt + Out * 1.5 + FVector(0, 0, 135);
		Backing(Back)->AddInstance(FTransform(FRotator(0, FaceYaw, 0), C, FVector(0.03, Width / 100.0, 1.1)));
		UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
		T->SetupAttachment(RootComponent);
		T->SetText(FText::FromString(Text));
		const float Size = 76.f; // (62 in the first cut: small on the panel from the TV camera)
		T->SetWorldSize(Size);
		T->SetTextRenderColor(Ink);
		T->SetHorizontalAlignment(EHTA_Center);
		T->SetVerticalAlignment(EVRTA_TextCenter);
		T->SetCastShadow(false);
		// a long name squeezed to the panel (a capital is ~0.62 of the size wide in the default font)
		const double TextW = Text.Len() * 0.62 * Size;
		if (TextW > Width * 0.88) T->SetXScale(static_cast<float>(Width * 0.88 / TextW));
		T->SetRelativeLocation(C + Out * 2.0);
		T->SetRelativeRotation(FRotator(0, FaceYaw, 0));
		T->RegisterComponent();
		++Panels;
	};
	const int32 NSponsor = UE_ARRAY_COUNT(GWallSponsors);
	int32 Next = (Home.id * 3 + Away.id) % NSponsor;
	auto Sponsor = [&]() { return FString(GWallSponsors[(Next++) % NSponsor]); };
	const FLinearColor Dark(0.015f, 0.02f, 0.04f), Black(0.01f, 0.01f, 0.012f);
	const double WallSide = BowlSideY - 60.0, WallEnd = BowlHalfX - 60.0;
	// the sidelines: wordmark, the game, a sponsor, in turn; the near side leaves the 50 to the tunnel
	for (int32 S : {1, -1})
	{
		const float Face = S > 0 ? -90.f : 90.f;
		int32 k = S > 0 ? 0 : 1;
		for (double Xyd : {-2.0, 18.0, 38.0, 62.0, 82.0, 102.0})
		{
			const FVector At(Xyd * YD, S * WallSide, 0);
			switch ((k++) % 3)
			{
			case 0: Panel(At, Face, 900, Word, HomeC * 0.6f, WordInk); break;
			case 1: Panel(At, Face, 900, TEXT("TAG GRIDIRON"), Black, FColor(240, 240, 240)); break;
			default: Panel(At, Face, 900, Sponsor(), Dark, FColor(255, 214, 90)); break;
			}
		}
	}
	// the end walls: the wordmark in the middle, a sponsor either side
	for (int32 E : {1, -1})
	{
		const float Face = E > 0 ? 180.f : 0.f;
		const double X = 50 * YD + E * WallEnd;
		Panel(FVector(X, 0, 0), Face, 1000, Word, HomeC * 0.6f, WordInk);
		Panel(FVector(X, 1000, 0), Face, 600, Sponsor(), Dark, FColor(240, 240, 240));
		Panel(FVector(X, -1000, 0), Face, 600, Sponsor(), Black, FColor(255, 214, 90));
	}
	// tunnel mouths: the four corners (on the end walls by the arcs) and the near sideline's 50, dark openings the full
	// height of the wall, in one instanced draw
	UInstancedStaticMeshComponent* Tunnels = AddISM(A.Cube, FLinearColor(0.008f, 0.008f, 0.01f), 0.9f, false);
	for (int32 E : {1, -1})
		for (int32 S : {1, -1})
			Tunnels->AddInstance(FTransform(FRotator(0, E > 0 ? 180.f : 0.f, 0), FVector(50 * YD + E * (WallEnd - 2.0), S * 1700.0, 107.5), FVector(0.04, 4.0, 2.15)));
	Tunnels->AddInstance(FTransform(FRotator(0, 90.f, 0), FVector(50 * YD, -(WallSide - 2.0), 107.5), FVector(0.04, 4.0, 2.15)));
	UE_LOG(LogTemp, Display, TEXT("TAG wall: LED ribbon, %d panels (%d backings), 5 tunnel mouths"), Panels, Backs.Num());
}

void AStadium::BuildSidelineProps(const tag::Team& Home, const tag::Team& Away, const tag::Weather& W)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	// one instanced draw per shape and colour (the shared greys once for both sides)
	TMap<FString, UInstancedStaticMeshComponent*> Isms;
	auto PutRot = [&](UStaticMesh* Mesh, const FLinearColor& Col, float Rough, bool bShadow, const FVector& At, const FVector& Scale, const FRotator& Rot)
	{
		const FString Key = FString::Printf(TEXT("%s %.3f %.3f %.3f %.2f %d"), *GetNameSafe(Mesh), Col.R, Col.G, Col.B, Rough, bShadow ? 1 : 0);
		UInstancedStaticMeshComponent*& Ism = Isms.FindOrAdd(Key);
		if (!Ism) Ism = AddISM(Mesh, Col, Rough, bShadow);
		Ism->AddInstance(FTransform(Rot, At, Scale));
	};
	auto Put = [&](UStaticMesh* Mesh, const FLinearColor& Col, float Rough, bool bShadow, const FVector& At, const FVector& Scale)
	{
		PutRot(Mesh, Col, Rough, bShadow, At, Scale, FRotator::ZeroRotator);
	};
	const FLinearColor Alu(0.30f, 0.31f, 0.33f), Frame(0.06f, 0.06f, 0.065f), Lid(0.8f, 0.8f, 0.78f), Case(0.025f, 0.025f, 0.03f), Net(0.03f, 0.03f, 0.035f);
	const bool bCold = W.tempF < 45.0 || W.precip == tag::Precip::Snow;
	UMaterialInstanceDynamic* Glow = nullptr;
	if (bCold && A.LEDMat)
	{
		Glow = UMaterialInstanceDynamic::Create(A.LEDMat, this);
		Glow->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.35f, 0.06f));
		Glow->SetVectorParameterValue(TEXT("ColorB"), FLinearColor(1.f, 0.35f, 0.06f));
		Glow->SetScalarParameterValue(TEXT("Intensity"), bNightGame ? 30.f : 3.f);
		Glow->SetScalarParameterValue(TEXT("Count"), 0.f);
	}
	UInstancedStaticMeshComponent* Heaters = nullptr;
	int32 Count = 0;
	for (int32 S : {1, -1})
	{
		const tag::Team& Tm = S > 0 ? Home : Away;
		const FLinearColor TeamC = Hex(Tm.colors.primary);
		const double Y0 = S * (FW / 2 + 4.6) * YD; // the benches' line, as before
		// three 8 m aluminium benches, a team-colour back and skirt, legs
		for (int32 k = -1; k <= 1; ++k)
		{
			const double Xc = 50 * YD + k * 9.6 * YD;
			Put(A.Cube, Alu, 0.35f, true, FVector(Xc, Y0, 46), FVector(8.0, 0.42, 0.05));
			Put(A.Cube, TeamC * 0.7f, 0.5f, true, FVector(Xc, Y0 + S * 24, 78), FVector(8.0, 0.04, 0.42));
			Put(A.Cube, TeamC * 0.35f, 0.6f, false, FVector(Xc, Y0 - S * 18, 24), FVector(8.0, 0.02, 0.40));
			for (int32 j = -1; j <= 1; ++j)
				Put(A.Cylinder, Frame, 0.4f, false, FVector(Xc + j * 370, Y0 + S * 10, 23), FVector(0.05, 0.05, 0.46));
			Count += 6;
		}
		// a drinks table at each end of the benches: two team-colour jugs with white lids
		for (int32 End : {-1, 1})
		{
			const double Xb = 50 * YD + End * 15.4 * YD;
			Put(A.Cube, Lid, 0.5f, true, FVector(Xb, Y0, 76), FVector(1.8, 0.75, 0.04));
			for (int32 Lx : {-1, 1})
				for (int32 Ly : {-1, 1})
					Put(A.Cylinder, Frame, 0.4f, false, FVector(Xb + Lx * 80, Y0 + Ly * 30, 37), FVector(0.04, 0.04, 0.74));
			for (int32 J : {-1, 1})
			{
				Put(A.Cylinder, TeamC, 0.45f, true, FVector(Xb + J * 45, Y0, 103), FVector(0.34, 0.34, 0.5));
				Put(A.Cylinder, Lid, 0.4f, false, FVector(Xb + J * 45, Y0, 130), FVector(0.35, 0.35, 0.05));
			}
			Count += 9;
		}
		// equipment trunks behind the benches
		for (int32 k = -2; k <= 2; k += 2)
		{
			Put(A.Cube, Case, 0.6f, true, FVector(50 * YD + k * 6.3 * YD + 90, Y0 + S * 130, 25), FVector(1.0, 0.55, 0.5));
			++Count;
		}
		// the kicking net at one end of the team area, the medical tent at the other (each side the other way round)
		{
			const double Xn = 50 * YD - S * 22 * YD, Yn = S * (FW / 2 + 6.4) * YD;
			for (int32 P : {-1, 1}) Put(A.Cylinder, Net, 0.6f, false, FVector(Xn + P * 120, Yn, 125), FVector(0.05, 0.05, 2.5));
			PutRot(A.Cylinder, Net, 0.6f, false, FVector(Xn, Yn, 250), FVector(0.05, 0.05, 2.4), FRotator(90, 0, 0));
			Put(A.Cube, Net, 0.95f, false, FVector(Xn, Yn, 125), FVector(2.3, 0.01, 2.4));
			const double Xt = 50 * YD + S * 22 * YD, Yt = S * (FW / 2 + 6.7) * YD;
			Put(A.Cube, TeamC * 0.45f, 0.85f, true, FVector(Xt, Yt, 105), FVector(2.4, 2.2, 2.1));
			Put(A.Cube, Case, 0.9f, false, FVector(Xt, Yt - S * 111, 95), FVector(1.4, 0.02, 1.9)); // its open front
			Count += 6;
		}
		// sideline heaters in cold games: dark cabinets behind the benches, the grille glowing toward the field
		if (bCold)
		{
			for (int32 k = -1; k <= 1; k += 2)
			{
				const double Xh = 50 * YD + k * 5.0 * YD;
				Put(A.Cube, Frame, 0.6f, true, FVector(Xh, Y0 + S * 75, 65), FVector(0.55, 0.55, 1.3));
				if (Glow)
				{
					if (!Heaters)
					{
						Heaters = AddISM(A.Cube, FLinearColor(1.f, 0.35f, 0.06f), 0.5f, false);
						Heaters->SetMaterial(0, Glow);
					}
					Heaters->AddInstance(FTransform(FRotator::ZeroRotator, FVector(Xh, Y0 + S * 75 - S * 28.5, 80), FVector(0.4, 0.02, 0.6)));
				}
				Count += 2;
			}
		}
	}
	UE_LOG(LogTemp, Display, TEXT("TAG sideline props: %d pieces in %d instanced draws%s"), Count, Isms.Num() + (Heaters ? 1 : 0), bCold ? TEXT(" (heaters)") : TEXT(""));
}

// ---------------------------------------------------------------------------------------------------- A5 wear

void AStadium::BuildWear()
{
	const FStadiumLook& Look = FStadiumLook::Get();
	// a turf material from before A5 has no live map: it keeps the season's wear (Tools/Art/ue_build_turf.py adds it)
	float Has = 0.f;
	if (!TurfMID || !TurfMID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("WearLive")), Has))
	{
		UE_LOG(LogTemp, Display, TEXT("TAG wear: M_TagTurf2 has no WearLive (rebuild it with Tools/Art/ue_build_turf.py): the fixed wear"));
		return;
	}
	// the map: R wear, G trampled snow, B mud, over the field's 120 x 53.3 yd (the turf's field UV), half as many
	// texels across as along. Float, so stamps add up past 1 (the material saturates) and no gamma is applied.
	WearRT = UKismetRenderingLibrary::CreateRenderTarget2D(this, Look.WearRes, Look.WearRes / 2, RTF_RGBA16f, FLinearColor::Black, false, false);
	if (!WearRT)
	{
		UE_LOG(LogTemp, Warning, TEXT("TAG wear: could not make the render target: the fixed wear"));
		return;
	}
	// the brush: a soft round stamp, made here (no asset)
	{
		constexpr int32 B = 64;
		TArray64<uint8> Px;
		Px.SetNumUninitialized(B * B * 4);
		for (int32 y = 0; y < B; ++y)
			for (int32 x = 0; x < B; ++x)
			{
				const float D = FVector2f::Distance(FVector2f(x + 0.5f, y + 0.5f), FVector2f(B * 0.5f, B * 0.5f)) / (B * 0.5f);
				const float F = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(1.f - D, 0.f, 1.f));
				const uint8 V = static_cast<uint8>(FMath::RoundToInt(F * 255.f));
				uint8* P = &Px[(static_cast<int64>(y) * B + x) * 4];
				P[0] = V; P[1] = V; P[2] = V; P[3] = 255;
			}
		WearBrush = UTexture2D::CreateTransient(B, B, PF_B8G8R8A8, NAME_None, Px);
		TArray64<uint8> White;
		White.Init(255, 4 * 4 * 4);
		WearWhite = UTexture2D::CreateTransient(4, 4, PF_B8G8R8A8, NAME_None, White);
		for (UTexture2D* T : {WearBrush.Get(), WearWhite.Get()})
		{
			if (!T) continue;
			T->SRGB = false;
			T->Filter = TF_Bilinear;
			T->AddressX = TA_Clamp;
			T->AddressY = TA_Clamp;
			T->UpdateResource();
		}
	}
	if (!WearBrush || !WearWhite)
	{
		UE_LOG(LogTemp, Warning, TEXT("TAG wear: could not make the brush: the fixed wear"));
		WearRT = nullptr;
		return;
	}
	// the season's wear to start from (the static map, x -TagWear= as the material had it), whole before it is drawn
	WearSeed = LoadObject<UTexture>(nullptr, TEXT("/Game/TAG/Textures/T_TurfWear.T_TurfWear"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (UTexture2D* Seed2D = Cast<UTexture2D>(WearSeed.Get()))
	{
		Seed2D->SetForceMipLevelsToBeResident(30.f);
		Seed2D->WaitForStreaming();
	}
	WearSeedAmount = 0.6f; // (Build's -TagWear= default)
	FParse::Value(FCommandLine::Get(), TEXT("TagWear="), WearSeedAmount);
	for (UMaterialInstanceDynamic* M : {TurfMID.Get(), GrassMID.Get()})
	{
		if (!M) continue;
		M->SetTextureParameterValue(TEXT("WearRT"), WearRT);
		M->SetScalarParameterValue(TEXT("WearLive"), 1.f);
	}
	bWearLive = true;
	bWearSeeded = false;
	UE_LOG(LogTemp, Display, TEXT("TAG wear: live map %d x %d (%.1f cm a texel), seeded from %s x %.2f"), WearRT->SizeX, WearRT->SizeY,
		120.0 * YD / WearRT->SizeX, WearSeed ? *WearSeed->GetName() : TEXT("nothing"), WearSeedAmount);
}

void AStadium::StampWear(const FVector& World, float RadiusCm, const FLinearColor& Add, float Stretch, float YawDeg)
{
	if (!bWearLive || !WearRT) return;
	// the turf's field UV (ue_build_turf.py FIELD_UV): x from -10 to 110 yd, y across 53.3 yd
	const double U = (World.X / YD + 10.0) / 120.0, V = (World.Y / YD) / (160.0 / 3.0) + 0.5;
	if (U < -0.02 || U > 1.02 || V < -0.02 || V > 1.02) return;
	const double TexX = 120.0 * YD / WearRT->SizeX, TexY = (160.0 / 3.0) * YD / WearRT->SizeY; // cm a texel
	FTagWearStamp St;
	St.At = FVector2D(U * WearRT->SizeX, V * WearRT->SizeY);
	St.Size = FVector2D(2.0 * RadiusCm * FMath::Max(Stretch, 1.f) / TexX, 2.0 * RadiusCm / TexY);
	St.Yaw = YawDeg;
	St.Add = FLinearColor(Add.R, Add.G, Add.B, 1.f);
	WearStamps.Add(St);
}

void AStadium::FlushWear()
{
	if (!bWearLive || !WearRT || !WearBrush) return;
	const FStadiumLook& Look = FStadiumLook::Get();
	// snow covers the trampled lanes again, a share a second at the weather's strength (G only: Modulate multiplies)
	const bool bRefill = bSnow && WearRefillT >= 1.f;
	if (WearStamps.Num() == 0 && !bRefill) return;
	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Ctx;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, WearRT, Canvas, Size, Ctx);
	if (Canvas)
	{
		if (bRefill && WearWhite)
		{
			const float Keep = FMath::Clamp(1.f - Look.SnowRefill * PrecipIntensity * WearRefillT, 0.f, 1.f);
			Canvas->K2_DrawTexture(WearWhite, FVector2D::ZeroVector, Size, FVector2D::ZeroVector, FVector2D::UnitVector, FLinearColor(1.f, Keep, 1.f, 1.f), BLEND_Modulate);
		}
		for (const FTagWearStamp& St : WearStamps)
			Canvas->K2_DrawTexture(WearBrush, St.At - St.Size * 0.5, St.Size, FVector2D::ZeroVector, FVector2D::UnitVector, St.Add, BLEND_Additive, St.Yaw, FVector2D(0.5, 0.5));
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Ctx);
	if (bRefill) WearRefillT = 0.f;
	WearStamps.Reset();
}

void AStadium::UpdateWear(float Dt)
{
	if (!bWearLive || !WearRT) return;
	if (!bWearSeeded)
	{
		bWearSeeded = true;
		UCanvas* Canvas = nullptr;
		FVector2D Size;
		FDrawToRenderTargetContext Ctx;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, WearRT, Canvas, Size, Ctx);
		if (Canvas && WearSeed)
			Canvas->K2_DrawTexture(WearSeed, FVector2D::ZeroVector, Size, FVector2D::ZeroVector, FVector2D::UnitVector,
				FLinearColor(WearSeedAmount, 0.f, 0.f, 1.f), BLEND_Opaque);
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Ctx);
	}
	if (bSnow) WearRefillT += Dt;
}

void AStadium::DumpWear(const TCHAR* Tag)
{
	static const bool bDump = FParse::Param(FCommandLine::Get(), TEXT("TagWearPng"));
	if (!bDump || !bWearLive || !WearRT) return;
	FTextureRenderTargetResource* Res = WearRT->GameThread_GetRenderTargetResource();
	TArray<FLinearColor> Px;
	if (!Res || !Res->ReadLinearColorPixels(Px) || Px.Num() != WearRT->SizeX * WearRT->SizeY) return;
	// R wear, G trampled snow, B mud, each clamped to 0..1 (a file to look at, not data)
	TArray64<FColor> Out;
	Out.SetNumUninitialized(Px.Num());
	for (int32 i = 0; i < Px.Num(); ++i) Out[i] = FLinearColor(FMath::Clamp(Px[i].R, 0.f, 1.f), FMath::Clamp(Px[i].G, 0.f, 1.f), FMath::Clamp(Px[i].B, 0.f, 1.f), 1.f).ToFColor(false);
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(WearRT->SizeX, WearRT->SizeY, TArrayView64<const FColor>(Out.GetData(), Out.Num()), Png);
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TagWear"), FString::Printf(TEXT("wear_%s.png"), Tag));
	if (FFileHelper::SaveArrayToFile(Png, *Path)) UE_LOG(LogTemp, Display, TEXT("TAG wear: wrote %s"), *Path);
}

// ---------------------------------------------------------------------------------------------------- A5 + A6 the men

/**
 * The hero masters (Tools/Art/hero_import.py) read a man's kit weather as custom primitive data on each of his meshes:
 *   0 knees and shins, 1 elbows and forearms, 2 chest and front, 3 back and seat, 4 helmet (grime 0..1),
 *   5 wet, 6 snow on top, 7 sweat, 8 mud (the stain's colour: 0 grass, 1 mud), 9 the plain helmet shell (Phase 4 A9:
 *   1 under -TagOldHelmet turns the coat master's clear coat and scuffs off).
 * Nothing set reads as 0: fans, crew and a master from before A6 are unchanged.
 */
static constexpr int32 KitDataCount = 10;

void AStadium::StepPlayers(float StepDt)
{
	const FStadiumLook& Look = FStadiumLook::Get();
	UWorld* World = GetWorld();
	if (!World) return;
	const bool bKits = !Look.bOldKit;
	const float Strength = (bRainy || bSnow) ? PrecipIntensity : 0.f;
	auto Kit = [&](int32 Id) -> FTagKitWeather& { return Kits.FindOrAdd(Id); };
	// the hits since the last step: a tackle's spot is torn up; both men take some of it
	FTagContacts::Get().ForEachSince(ContactCursor, [&](const FTagContact& Hit)
	{
		for (int32 Id : {Hit.IdA, Hit.IdB})
		{
			if (Id < 0) continue;
			FTagKitWeather& K = Kit(Id);
			K.LastPush = Hit.DirFor(Id);
			K.PushAge = 0.f;
			if (Hit.bTackle && bKits)
			{
				K.Grime[1] += Look.GrimeTackle * (0.5f + 0.5f * Hit.Strength());
				K.Grime[2] += Look.GrimeTackle * 0.5f * Hit.Strength();
			}
		}
		if (Hit.bTackle)
			StampWear(Hit.Pos, 70.f, FLinearColor(Look.WearTackle, bSnow ? 0.6f : 0.f, bRainy ? Look.WearTackle * Look.MudRain : 0.f), 1.6f,
				static_cast<float>(Hit.Normal.Rotation().Yaw));
	});
	const double FieldHalfX = 60.0 * YD, FieldHalfY = (FW / 2 + 1.0) * YD;
	const bool bHot = TempF >= 70.f;
	for (TActorIterator<AAthlete> It(World); It; ++It)
	{
		AAthlete* Ath = *It;
		if (!Ath || Ath->IsDormant() || Ath->IsHidden()) continue;
		// a player by his id; an official or other civilian by the actor (paths and the weather only)
		const int32 Key = Ath->Player ? Ath->Player->id : -1000 - static_cast<int32>(Ath->GetUniqueID() & 0xFFFFF);
		FTagKitWeather& K = Kit(Key);
		const FVector At = Ath->GetActorLocation();
		const bool bOnField = FMath::Abs(At.X - 50.0 * YD) < FieldHalfX && FMath::Abs(At.Y) < FieldHalfY;
		const double Moved = K.bHasLast ? FVector::Dist2D(At, K.LastAt) : 0.0;
		const FVector From = K.LastAt;
		K.LastAt = At;
		K.bHasLast = true;
		const bool bDown = Ath->IsDown();
		const float Speed = static_cast<float>(Moved / FMath::Max(StepDt, 0.01f));
		// A5: his path, one stretched stamp from where he was (a jump of more than 30 m/s is a cut between plays,
		// not a path); the trampled snow is the lanes
		if (bWearLive && bOnField && Moved > 5.0 && Speed < 3000.f)
		{
			const FVector Mid = (At + From) * 0.5;
			StampWear(Mid, 35.f, FLinearColor(Look.WearWalk, bSnow ? Look.SnowTrample : 0.f, bRainy ? Look.WearWalk * Look.MudRain : 0.f),
				static_cast<float>((Moved + 70.0) / 70.0), static_cast<float>((At - From).Rotation().Yaw));
		}
		// the line of scrimmage: men set at it or locked up on it (feet churning) scuff it, up to a budget a snap
		const bool bAtLine = LosNowX >= 0 && bOnField && FMath::Abs(At.X - LosNowX) < 1.6 * YD && Speed < 300.f && !bDown;
		if (bAtLine)
		{
			if (K.LosSeen != LosSerial)
			{
				K.LosSeen = LosSerial;
				K.LosChurn = 0.f;
			}
			if (K.LosChurn < Look.WearLineMax)
			{
				K.LosChurn += Look.WearLine;
				// a few feet either side of where he stands (a fixed scatter per man and snap: the films repeat)
				const uint32 H = HashCombine(GetTypeHash(Key), GetTypeHash(LosSerial * 31 + FMath::RoundToInt(K.LosChurn * 100.f)));
				const FVector Jit(((H & 0xFF) / 255.0 - 0.5) * 60.0, (((H >> 8) & 0xFF) / 255.0 - 0.5) * 60.0, 0.0);
				StampWear(At + Jit, 45.f, FLinearColor(Look.WearLine, bSnow ? 0.3f : 0.f, bRainy ? Look.WearLine * Look.MudRain : 0.f));
				// A6: a lineman's knees and hands in the dirt at the line
				if (bKits && Ath->Player)
				{
					const tag::Pos Ps = Ath->Player->pos;
					if (Ps == tag::Pos::OT || Ps == tag::Pos::OG || Ps == tag::Pos::C || Ps == tag::Pos::DE || Ps == tag::Pos::DT)
					{
						K.Grime[0] += Look.GrimeLine;
						K.Grime[1] += Look.GrimeLine * 0.6f;
					}
				}
			}
		}
		// a fall: the spot he goes down on, and the side of him that lands
		if (bDown && !K.bWasDown)
		{
			const FVector Pel = Ath->PelvisAt();
			const FVector Push = K.PushAge < 0.8f && !K.LastPush.IsNearlyZero() ? K.LastPush : Ath->GetLastVel().GetSafeNormal2D();
			if (bOnField)
				StampWear(Pel, 60.f, FLinearColor(Look.WearDown, bSnow ? 0.8f : 0.f, bRainy ? Look.WearDown * Look.MudRain * 1.5f : 0.f), 1.5f,
					static_cast<float>(Push.Rotation().Yaw));
			if (bKits && Ath->Player)
			{
				const bool bOnFront = FVector::DotProduct(Push, Ath->GetActorForwardVector()) >= 0.f; // pushed the way he faces: onto his front
				const float G = Look.GrimeFall;
				if (bOnFront)
				{
					K.Grime[2] += G;
					K.Grime[0] += G * 0.8f;
				}
				else K.Grime[3] += G;
				K.Grime[1] += G * 0.7f;
				K.Grime[4] += G * 0.35f;
				// what he lands in: mud in the rain, slush in the snow, the dirt between the hashes, grass elsewhere
				const float Here = bRainy ? 0.9f : bSnow ? 0.35f : FMath::Abs(Pel.Y) < 4.0 * YD ? 0.6f : 0.2f;
				K.Mud = K.Mud <= 0.f ? Here : FMath::Lerp(K.Mud, Here, 0.35f);
				if (bRainy) K.Wet = FMath::Min(1.f, K.Wet + Look.WetGround * (0.5f + 0.5f * Strength));
				if (bSnow)
				{
					K.Snow *= 0.3f;
					K.Wet = FMath::Min(1.f, K.Wet + Look.WetGround * 0.6f * Strength);
				}
			}
		}
		K.bWasDown = bDown;
		K.PushAge += StepDt;
		if (!bKits) continue;
		for (float& G : K.Grime) G = FMath::Min(G, 1.f);
		// the weather on him: rain soaks in over about a minute; snow settles on a man standing still and is shaken off
		// at a run; snow melting on him dampens the kit a little
		if (bRainy) K.Wet = FMath::Min(1.f, K.Wet + Look.WetRate * Strength * StepDt);
		if (bSnow)
		{
			if (Speed < 100.f && !bDown) K.Snow = FMath::Min(1.f, K.Snow + Look.SnowStick * Strength * StepDt);
			else if (Speed > 450.f) K.Snow = FMath::Max(0.f, K.Snow - 0.15f * StepDt);
			K.Wet = FMath::Min(0.45f, K.Wet + 0.004f * Strength * StepDt);
		}
		// sweat: his exertion (the face's breathing), quick to come, slow to dry; less in the cold
		const float SweatTo = FMath::Clamp(Ath->GetExertion(), 0.f, 1.f) * Look.SweatHot * (bHot ? 1.f : TempF < 45.f ? 0.4f : 0.7f);
		K.Sweat = SweatTo > K.Sweat ? FMath::FInterpTo(K.Sweat, SweatTo, StepDt, 0.5f) : FMath::Max(SweatTo, K.Sweat - 0.004f * StepDt);
		// to his meshes, when a value has moved by a 64th (custom primitive data: no material instance is touched)
		float V[KitDataCount] = {K.Grime[0], K.Grime[1], K.Grime[2], K.Grime[3], K.Grime[4], K.Wet, K.Snow, K.Sweat, K.Mud, Look.bOldHelmet ? 1.f : 0.f};
		if (Look.KitForce >= 0.f)
		{
			for (int32 i = 0; i < 8; ++i) V[i] = FMath::Clamp(Look.KitForce, 0.f, 1.f);
			V[8] = 0.5f;
		}
		bool bSend = false;
		for (int32 i = 0; i < KitDataCount; ++i)
			if (FMath::Abs(V[i] - K.Sent[i]) > 1.f / 64.f) bSend = true;
		if (!bSend) continue;
		for (int32 i = 0; i < KitDataCount; ++i) K.Sent[i] = V[i];
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Ath);
		for (USkeletalMeshComponent* Mc : Meshes)
			if (Mc) Mc->SetCustomPrimitiveDataFloatArray(0, TConstArrayView<float>(V, KitDataCount));
	}
	FlushWear();
}

// ---------------------------------------------------------------------------------------------------- Phase 4 A7 sun

void AStadium::UpdateSun(bool bForce)
{
	if (!bMovingSun || !SunC) return;
	const FStadiumLook& Look = FStadiumLook::Get();
	// the time of day the game has reached: kickoff, then the broadcast's hours spread over the game clock
	const float Hour = KickoffHour + FMath::Max(GameProgress, 0.f) * Look.GameHours;
	// the sun's place (the simple NOAA formulae; the clock is taken as solar time, no equation of time)
	const double Lat = FMath::DegreesToRadians(static_cast<double>(Look.Latitude));
	const double Dec = FMath::DegreesToRadians(static_cast<double>(SunDecl));
	const double Ha = FMath::DegreesToRadians(15.0 * (Hour - 12.0));
	const double SinE = FMath::Sin(Lat) * FMath::Sin(Dec) + FMath::Cos(Lat) * FMath::Cos(Dec) * FMath::Cos(Ha);
	const float ElevDeg = static_cast<float>(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(SinE, -1.0, 1.0))));
	// azimuth from south, westward positive; + 180 from north, eastward
	const float AzDeg = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(FMath::Sin(Ha), FMath::Cos(Ha) * FMath::Sin(Lat) - FMath::Tan(Dec) * FMath::Cos(Lat)))) + 180.f;
	// a late game's lights come up through the dusk: from the sun 6 degrees up to 4 under the horizon
	const float Up = bDuskGame ? 1.f - FMath::SmoothStep(-4.f, 6.f, ElevDeg) : 0.f;
	// steps only (a VSM sun re-renders its whole shadow cache when it turns): a few tenths of a degree, or the dusk moving
	if (!bForce && FMath::Abs(ElevDeg - SunElevSet) < 0.4f && FMath::Abs(AzDeg - SunAzSet) < 0.6f && FMath::Abs(Up - DuskSet) < 0.02f) return;
	SunElevSet = ElevDeg;
	SunAzSet = AzDeg;
	DuskSet = Up;
	LightsUp = Up;
	// the light points away from the sun; in the stadium's frame (+X turned FieldNorthYaw east of north, +Y east of it).
	// Under the horizon it stays at 1 degree and fades out (the atmosphere has reddened and dimmed it on the way down).
	SunC->SetRelativeRotation(FRotator(-FMath::Max(ElevDeg, 1.f), AzDeg - FieldNorthYaw + 180.f, 0.f));
	SunC->SetIntensity(SunBaseIntensity * FMath::SmoothStep(-2.f, 3.f, ElevDeg));
	// the day camera opens up as the sun gets low (the window was set for a sun 48 degrees up), then a late game's
	// crosses to the night rig: the pinned night exposure, the banks, the near-black sky, the bowl's glow
	const float Comp = bOvercastDay ? 0.f : FMath::Clamp(Look.SunEVK * FMath::Log2(0.743f / FMath::Max(FMath::Sin(FMath::DegreesToRadians(FMath::Max(ElevDeg, 0.5f))), 0.03f)), 0.f, Look.SunMaxEVComp);
	if (PPC)
	{
		PPC->Settings.AutoExposureMinBrightness = FMath::Lerp(DayEV.X - Comp, NightEV2.X, Up);
		PPC->Settings.AutoExposureMaxBrightness = FMath::Lerp(DayEV.Y, NightEV2.Y, Up);
		PPC->Settings.AutoExposureBias = FMath::Lerp(DayBias, 0.f, Up);
		PPC->Settings.BloomIntensity = FMath::Lerp(0.5f, 0.9f, Up);
		PPC->Settings.BloomThreshold = Up > 0.5f ? Look.BloomThrNight : Look.BloomThrDay;
	}
	if (SkyC) SkyC->SetIntensity(FMath::Lerp(SkyDayIntensity, 0.04f, Up));
	for (int32 i = 0; i < BankLights.Num(); ++i)
		if (USpotLightComponent* Bank = BankLights[i])
		{
			Bank->SetVisibility(Up > 0.01f); // (dark banks cost nothing: off, not at 0)
			Bank->SetIntensity(BankCandela[i] * Up);
		}
	for (int32 i = 0; i < DuskMIDs.Num(); ++i)
		if (UMaterialInstanceDynamic* Mid = DuskMIDs[i])
			Mid->SetScalarParameterValue(DuskMIDParam[i], FMath::Lerp(DuskMIDRange[i].X, DuskMIDRange[i].Y, Up));
	UE_LOG(LogTemp, Display, TEXT("TAG sun: %.2f h, elevation %.1f azimuth %.0f deg, camera +%.1f stops, lights %.2f"), Hour, ElevDeg, AzDeg, Comp, Up);
}

// ---------------------------------------------------------------------------------------------------- Phase 4 A10 breath

void AStadium::UpdateBreath(float Dt)
{
	if (!bBreath || Puffs.Num() == 0) return;
	const FStadiumLook& Look = FStadiumLook::Get();
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APlayerCameraManager* CamMgr = PC ? PC->PlayerCameraManager.Get() : nullptr;
	// the puffs alive: drift, swell, fade (soft in, long out)
	for (int32 i = 0; i < PuffState.Num(); ++i)
	{
		FTagPuff& P = PuffState[i];
		if (P.Age < 0.f) continue;
		P.Age += Dt;
		const float T = P.Age / P.Life;
		if (T >= 1.f)
		{
			P.Age = -1.f;
			Puffs[i]->SetVisibility(false);
			continue;
		}
		P.At += P.Vel * Dt;
		P.Vel *= FMath::Max(0.f, 1.f - 1.5f * Dt);
		Puffs[i]->SetWorldLocation(P.At);
		Puffs[i]->SetWorldScale3D(FVector(P.Size * (0.08f + 0.3f * FMath::Sqrt(T))));
		PuffMIDs[i]->SetScalarParameterValue(TEXT("Puff"), Look.BreathOpacity * FMath::Min(1.f, T * 6.f) * FMath::Pow(1.f - T, 1.5f));
	}
	if (!CamMgr) return;
	const FVector Cam = CamMgr->GetCameraLocation();
	const float Now = World->GetTimeSeconds();
	// the men near the lens breathe out on their own rhythm (the face's: 0.25 Hz at rest, 0.7 Hz spent)
	for (TActorIterator<AAthlete> It(World); It; ++It)
	{
		AAthlete* Ath = *It;
		if (!Ath || Ath->IsDormant() || Ath->IsHidden()) continue;
		if (FVector::DistSquared(Ath->GetActorLocation(), Cam) > FMath::Square(static_cast<double>(Look.BreathDist))) continue;
		const float Ex = FMath::Clamp(Ath->GetExertion(), 0.f, 1.f);
		const float Hz = 0.25f + Ex * 0.45f;
		float& Next = BreathClock.FindOrAdd(TWeakObjectPtr<AActor>(Ath), Now + FMath::Frac(static_cast<float>(Ath->GetUniqueID()) * 0.618f) / Hz);
		if (Now < Next) continue;
		Next = Now + 1.f / Hz;
		int32 Free = INDEX_NONE;
		for (int32 i = 0; i < PuffState.Num() && Free == INDEX_NONE; ++i)
			if (PuffState[i].Age < 0.f) Free = i;
		if (Free == INDEX_NONE) break;
		FVector Head = Ath->BoneWorld(TEXT("head"));
		if (Head.IsNearlyZero()) Head = Ath->GetActorLocation() + FVector(0, 0, 170);
		const FVector Fwd = Ath->GetActorForwardVector();
		FTagPuff& P = PuffState[Free];
		P.At = Head + Fwd * 14.0 - FVector(0, 0, 6);
		P.Vel = Fwd * (50.0 + 70.0 * Ex) + FVector(0, 0, 12) + Wind * 0.3;
		P.Age = 0.f;
		P.Life = 0.9f + 0.6f * Ex;
		P.Size = 1.f + 0.8f * Ex;
		Puffs[Free]->SetWorldLocation(P.At);
		Puffs[Free]->SetWorldScale3D(FVector(0.08f * P.Size));
		PuffMIDs[Free]->SetScalarParameterValue(TEXT("Puff"), 0.f);
		Puffs[Free]->SetVisibility(true);
	}
}
