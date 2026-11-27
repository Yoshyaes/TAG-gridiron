// Procedurally built stadium: turf, markings, end zones, posts, stands, crowd, sky & lighting.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Stadium.generated.h"

class UStaticMeshComponent;

namespace tag { struct Team; struct Weather; }

/** One stamp on the live wear map (AStadium::StampWear): centre and size in the map's pixels, turn, what it adds. */
struct FTagWearStamp
{
	FVector2D At = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;
	float Yaw = 0.f;
	FLinearColor Add = FLinearColor::Black;
};

/** One breath puff (AStadium::UpdateBreath): where it is, how it drifts, its age (< 0: free) and life (s), its size. */
struct FTagPuff
{
	FVector At = FVector::ZeroVector;
	FVector Vel = FVector::ZeroVector;
	float Age = -1.f;
	float Life = 1.f;
	float Size = 1.f;
};

/** One man's kit through the game (Research/NextLevel/03 A6), kept by player id for the whole game. */
struct FTagKitWeather
{
	float Grime[5] = {0.f, 0.f, 0.f, 0.f, 0.f}; // knees and shins, elbows and forearms, chest and front, back and seat, helmet
	float Wet = 0.f, Snow = 0.f, Sweat = 0.f, Mud = 0.f;
	float Exert = 0.f;                           // his exertion from his own running (the face's comes only with a close face)
	bool bWasDown = false;
	FVector LastPush = FVector::ZeroVector;     // the last hit's push on him (a fall forward lands him on his front)
	float PushAge = 99.f;
	FVector LastAt = FVector::ZeroVector;        // where he stood at the last step (the wear's path stamps)
	bool bHasLast = false;
	int32 LosSeen = 0;                           // the line of scrimmage his churn budget is for
	float LosChurn = 0.f;                        // how much of the line's scuffing he has laid down at it
	float Sent[10] = {-9.f, -9.f, -9.f, -9.f, -9.f, -9.f, -9.f, -9.f, -9.f, -9.f}; // the values last handed to his meshes
};

/** The precipitation column: its instances never move within it and it moves every frame, so its bounds come from a
 *  box measured once (the stock bounds pass walks all ~20k instances on every move). */
UCLASS()
class TAGGRIDIRON_API UTagPrecipComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()
public:
	FBox LocalBox = FBox(ForceInit);
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override
	{
		return LocalBox.IsValid ? FBoxSphereBounds(LocalBox.TransformBy(LocalToWorld)) : Super::CalcBounds(LocalToWorld);
	}
};

UCLASS()
class TAGGRIDIRON_API AStadium : public AActor
{
	GENERATED_BODY()

public:
	AStadium();
	void Build(const tag::Team& Home, const tag::Team& Away, const tag::Weather& W, bool bNight);
	virtual void Tick(float DeltaSeconds) override;

	/** Line of scrimmage / first-down overlays (world X in cm, or <0 to hide). */
	void SetLines(double LosX, double FirstDownX);
	/** The two overlay lines (0 the line of scrimmage, 1 the line to gain) for the director's broadcast look
	 *  (Game/MatchBroadcast.cpp: their colour and material, hidden in replays and close shots by SetHiddenInGame,
	 *  so SetLines' own showing and hiding stands). */
	UStaticMeshComponent* OverlayLine(int32 i) const { return i == 0 ? LosLine.Get() : FdLine.Get(); }

	/** Crowd excitement 0..1 (makes the crowd bounce). */
	float Hype = 0.f;

	/** The down on the chain crew's down box (0 hides the number: kicks, tries). */
	void SetDown(int32 Down);

	/** The chain crew's posts (0 rear rod, 1 front rod, 2 down box): the crew stand attached to them. */
	USceneComponent* CrewPost(int32 i) const { return i == 0 ? ChainRear.Get() : i == 1 ? ChainFront.Get() : DownBox.Get(); }
	/** Where the down box stands (automation framing). */
	FVector DownBoxLocation() const;

	/** The end-zone video boards' score lines. */
	void SetScoreboard(const FString& Top, const FString& Bottom);

	/** How far the game has got, for the weather and the wear that build over a game (Research/NextLevel/03 A2, A5):
	 *  the quarter (1..4, 5 and on overtime), the seconds left in it and a quarter's length. The director calls it before
	 *  each snap. Visual only: nothing here reaches the sim. */
	void SetGameClock(int32 Quarter, double ClockLeft, double QuarterLen);

	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	/** The seating bowl: one swept surface around a rounded-rectangle path (tiers, aisles, suites, ribbon boards,
	 *  roof canopy), so the stands wrap the corners instead of meeting as separate slabs. */
	void BuildBowl(const tag::Team& Home, const tag::Team& Away, bool bNight);
	/** Chain crew equipment: rods 10 yd apart and the down box on the far sideline (the crew are AMatchDirector's). */
	void BuildOfficials();
	bool LoadFanMeshes();
	UPROPERTY()
	TObjectPtr<USceneComponent> ChainRear;
	UPROPERTY()
	TObjectPtr<USceneComponent> ChainFront;
	UPROPERTY()
	TObjectPtr<USceneComponent> DownBox;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ChainLink;
	UPROPERTY()
	TArray<TObjectPtr<class UTextRenderComponent>> DownText;
	UPROPERTY()
	TArray<TObjectPtr<class UTextRenderComponent>> ScoreText;
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh, const FVector& Loc, const FVector& Scale, const FLinearColor& Color, float Rough = 0.8f, const FRotator& Rot = FRotator::ZeroRotator, bool bShadow = true);
	UInstancedStaticMeshComponent* AddISM(UStaticMesh* Mesh, const FLinearColor& Color, float Rough = 0.8f, bool bShadow = true);
	void AddText(const FString& S, const FVector& Loc, const FRotator& Rot, float Size, const FColor& Color);
	void BuildCrowd(const tag::Team& Home);
	void BuildRain(const tag::Weather& W);
	/** Close-up grass: one patch of real blades (M_TagGrassBlade wraps it around the shot's subject in the vertex
	 *  shader). Hidden from the game camera; shown when a blade is tall enough on screen or the lens is wide open
	 *  (the f/1.2 close shots). -TagNoGrass3D leaves it out. */
	void BuildGrass3D();
	void UpdateGrass3D(float Dt);
	UPROPERTY()
	TObjectPtr<class UProceduralMeshComponent> Grass3D;
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> GrassMID;
	float GrassGrow = 0.f;          // 0 hidden .. 1 full height (eased, snapped on a camera cut)
	float GrassPx = 7.f;            // blades show once one is this many pixels tall in a 1080p frame (-TagGrassPx=)
	float GrassMaxDist = 3000.f;    // never further than this from the lens, cm (-TagGrassDist=)
	float GrassPatch = 1500.f;      // side of the blade patch, cm
	bool bGrassAlways = false;      // -TagGrassAlways: full height in every shot (look tests)
	FVector GrassCamPrev = FVector::ZeroVector;
	/** 3D spectators (MetaHuman Crowd Sample merged meshes) in the lowest rows of the sideline stands, idling and
	 *  cheering with Hype. Superseded by the Mass fan spawner (TagFanSpawner); kept as a fallback. */
	void BuildHeroCrowd(const FVector& StandC, float Yaw);
	/** Model pack spectators (crowd rig) in the given seats; false when the pack characters are not in. */
	bool BuildPackFans(const TArray<FTransform>& Seats, const tag::Team& Home, const tag::Team& Away);
	/** Model pack sideline people that stand and idle (media crews, photographers, coordinators, sideline players). */
	void BuildPackSideline(const tag::Team& Home, const tag::Team& Away);
	class USkeletalMeshComponent* AddStaticPerson(int32 Item, int32 HeadItem, const FVector& Rel, float FaceYaw, const FLinearColor& Team,
		const FLinearColor& Bottom, bool bKneel, int32 Seed);
	/** Set while BuildPackSideline runs its collect pass: AddStaticPerson records the catalog items it is asked for
	 *  here and builds nothing, so the meshes can be loaded as one batch before the build pass. */
	TArray<int32>* PackPreload = nullptr;
	UPROPERTY()
	TArray<TObjectPtr<class USkeletalMeshComponent>> SidelinePeople;
	UPROPERTY()
	TArray<TObjectPtr<class UAnimationAsset>> FanIdles;   // per fan (the clip for its skeleton family)
	UPROPERTY()
	TArray<TObjectPtr<class UAnimationAsset>> FanCheers;
	void UpdateHeroCrowd(float Dt);

	UPROPERTY()
	TArray<TObjectPtr<class USkeletalMeshComponent>> Fans;
	TArray<float> FanDelay;          // per-fan reaction delay (s) so the stands ripple instead of switching at once
	TArray<uint8> FanCheering;
	float HypeHeld = 0.f;            // time Hype has been above / below the switch thresholds
	UPROPERTY()
	TObjectPtr<class UAnimationAsset> FanIdle;
	UPROPERTY()
	TObjectPtr<class UAnimationAsset> FanCheer;
	UPROPERTY()
	TObjectPtr<class UAnimationAsset> FanClap;
	UPROPERTY()
	TArray<TObjectPtr<class USkeletalMesh>> FanMeshes;
	/** Mass crowd (TagFanSpawner): cheer on Hype by swapping the close-up crowd actors' body to the keyed cheer. */
	void UpdateMassFans(float Dt);
	UPROPERTY()
	TObjectPtr<class UAnimationAsset> MassCheer;
	TMap<TWeakObjectPtr<class USkeletalMeshComponent>, TWeakObjectPtr<UClass>> MassFanAnimClass; // fans currently cheering
	bool bMassCheer = false;
	UPROPERTY()
	TObjectPtr<class ATagFanSpawner> FanSpawnerActor;
	float MassScanT = 0.f;

	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> CrowdSections;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> LosLine;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> FdLine;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Rain;

	TArray<FTransform> CrowdBase;
	TArray<int32> CrowdSectionOf;
	TArray<int32> CrowdLocal;
	int32 CrowdCursor = 0;
	FVector RainStep = FVector::ZeroVector; // one fall period's travel of the precipitation column (BuildRain)
	float RainPeriod = 1.f, RainPhase = 0.f;
	FVector Wind = FVector::ZeroVector;
	bool bSnow = false;
	bool bRainy = false;
	bool bNightGame = false;
	float TempF = 60.f;
	float Time = 0.f;
	float WeatherT = 0.f;

	// ---- A2: weather you can see
	/** The camera-local precipitation (M_TagPrecip, Tools/Art/ue_build_stadium_mats.py), built like the close-up grass:
	 *  one mesh of quads made once and never moved, each quad wrapped by the material's vertex shader into a box that
	 *  follows the shot, falling with time, drifting with the wind, turned to the camera. Layer 0 the dense box around
	 *  the shot's subject, layer 1 a sparse box of near flakes or drops around the lens. False (no material, or
	 *  -TagOldWeather): the old cube column (BuildRain). */
	bool BuildPrecip(const tag::Weather& W);
	void UpdatePrecip(float Dt);
	UPROPERTY()
	TArray<TObjectPtr<class UProceduralMeshComponent>> PrecipLayers;
	UPROPERTY()
	TArray<TObjectPtr<class UMaterialInstanceDynamic>> PrecipMIDs;
	TArray<FVector> PrecipBox;     // each layer's box (cm)
	/** The weather's strength now (0..1): kickoff's to the fourth quarter's (FStadiumLook PrecipStart / PrecipEnd). */
	float PrecipIntensity = 1.f;
	float WeatherApplied = -1.f;    // the strength the fog, turf and players were last set for
	/** 0 at kickoff .. 1 at the end of the fourth quarter (SetGameClock; -1 until the director has said). */
	float GameProgress = -1.f;
	int32 GameQuarter = 0;
	void ApplyWeather();
	UPROPERTY()
	TObjectPtr<class UExponentialHeightFogComponent> FogC;
	float FogBase = 0.f;
	FLinearColor FogInscatterBase = FLinearColor::White; // the fog's own glow as built (dimmed by the stops the day camera opens up)
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> TurfMID;   // the ground (M_TagTurf2), for the Wet / Snow ramps and the wear target
	float SnowCoverBase = 0.f;

	// ---- A4: the field-level wall and the sidelines
	/** Wordmark and sponsor panels and tunnel mouths on the field-level wall (BuildBowl lays the LED ribbon itself). */
	void BuildWallDressing(const tag::Team& Home, const tag::Team& Away, bool bNight);
	/** Benches, coolers, kicking nets, heaters (cold games), the medical tent: engine shapes, a few instanced draws. */
	void BuildSidelineProps(const tag::Team& Home, const tag::Team& Away, const tag::Weather& W);

	// ---- A5: turf that changes over the game
	/** The live wear map over the field (M_TagTurf2's WearRT): R wear, G trampled snow, B mud. Seeded from the season's
	 *  wear (T_TurfWear) and stamped by the game: the linemen's feet at the line, tackle and pile spots, the men's paths
	 *  (trampled lanes in snow). */
	void BuildWear();
	void UpdateWear(float Dt);
	void StampWear(const FVector& World, float RadiusCm, const FLinearColor& Add, float Stretch = 1.f, float YawDeg = 0.f);
	void FlushWear();
	void DumpWear(const TCHAR* Tag);
	UPROPERTY()
	TObjectPtr<class UTextureRenderTarget2D> WearRT;
	UPROPERTY()
	TObjectPtr<class UTexture2D> WearBrush;    // a soft round stamp (made here, not an asset)
	UPROPERTY()
	TObjectPtr<class UTexture2D> WearWhite;    // 4 x 4 white: the snow's refill pass
	UPROPERTY()
	TObjectPtr<class UTexture> WearSeed;       // T_TurfWear
	bool bWearSeeded = false;
	bool bWearLive = false;
	float WearSeedAmount = 0.6f;               // the season's wear laid into the map at kickoff (-TagWear=)
	float WearRefillT = 0.f;
	int32 WearDumpedQ = 0;                     // -TagWearPng: the last quarter whose map was written
	double LosNowX = -1.0;                     // the line of scrimmage (SetLines), world X cm; < 0 none
	int32 LosSerial = 0;                       // a new line of scrimmage: the linemen's scuffing budget starts again
	float LosSetTime = -100.f;
	TArray<FTagWearStamp> WearStamps;

	// ---- A6: uniforms that get dirty, wet and snowy (the hero masters read these as custom primitive data)
	TMap<int32, FTagKitWeather> Kits;
	/** Every StepDt: the hits since the last step (tackle spots on the turf, grime on the men), then every man on the
	 *  field (his path, the line's scuffing, a fall, the rain and snow on him, his sweat), sent to his meshes. */
	void StepPlayers(float StepDt);
	float PlayerStepT = 0.f;
	uint64 ContactCursor = 0;

	// ---- Phase 4, A7: a sun that moves through the game, and late games that run into the dusk
	/** The sun for the time of day the game has reached (kickoff + progress x the broadcast's length), at the game's
	 *  date and latitude; stepped (a VSM sun re-renders its shadow cache on any turn), never moved per frame. In a late
	 *  game the light banks, exposure, sky and the bowl's glow cross-fade to the night rig as the sun sets. */
	void UpdateSun(bool bForce);
	UPROPERTY()
	TObjectPtr<class UDirectionalLightComponent> SunC;
	UPROPERTY()
	TObjectPtr<class USkyLightComponent> SkyC;
	UPROPERTY()
	TObjectPtr<class UPostProcessComponent> PPC;
	UPROPERTY()
	TArray<TObjectPtr<class USpotLightComponent>> BankLights;
	TArray<float> BankCandela;
	UPROPERTY()
	TArray<TObjectPtr<class UMaterialInstanceDynamic>> DuskMIDs;   // lamp housings, wall ribbon, painted crowd (day .. night values)
	TArray<FVector2f> DuskMIDRange;                                  // each one's emissive param value by day and by night
	TArray<FName> DuskMIDParam;
	bool bMovingSun = false;
	bool bOvercastDay = false;       // no sun to follow: the camera does not open up as it sinks
	bool bDuskGame = false;          // a late kickoff: the banks are built and fade up as the sun goes down
	float KickoffHour = 13.f;
	float SunDecl = -10.f;           // the date's solar declination, degrees
	float FieldNorthYaw = 0.f;       // how far the field's +X turns from north (degrees, toward east)
	float SunBaseIntensity = 9.f;
	float SkyDayIntensity = 1.f;
	FVector2f DayEV = FVector2f(0.6f, 2.f), NightEV2 = FVector2f(4.5f, 4.5f);
	float DayBias = 0.4f;
	float SunElevSet = -99.f, SunAzSet = -999.f, DuskSet = -1.f;
	float SunStepT = 0.f;
	float LightsUp = 0.f;            // 0 the day rig .. 1 the night rig (late games through the dusk)

	// ---- Phase 4, A10: breath in cold games (a pool of soft puffs at the mouths of the men near the lens)
	void UpdateBreath(float Dt);
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Puffs;
	UPROPERTY()
	TArray<TObjectPtr<class UMaterialInstanceDynamic>> PuffMIDs;
	TArray<FTagPuff> PuffState;
	TMap<TWeakObjectPtr<AActor>, float> BreathClock;
	bool bBreath = false;

	// ---- Phase 4, A13 (shader version): the painted crowd jumps with the crowd's excitement
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> CrowdAnimMID;
	float CrowdHypeSent = -1.f;
};
