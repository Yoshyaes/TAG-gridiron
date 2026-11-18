// The game's sound: the stadium crowd, the field, the stadium's own PA and music, the broadcast music and the menus.
//
// Like the booth, it watches the game rather than being told about it: the director hands it one view a frame (the
// stage, the play's sim, a kick in the air, the ball) and it finds the snap, the hits, the whistle and the result
// itself. The crowd is a home crowd with a mood: it knows the score, the down and who has the ball.
//
// Sounds are USoundWave assets under /Game/TAG/Audio/<group>/<Bank>/ (built by Tools/Audio); a bank is a folder of
// takes of one sound, and a take is never played twice running. A bank that is not there is simply silent.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include <string>
#include <optional>
#include "Sim/Game.h"
#include "Sim/PlaySim.h"
#include "TagAudio.generated.h"

class UAudioComponent;
class USoundWave;
class USoundBase;
class USoundAttenuation;

/** What the director shows the sound each frame. */
struct FTagAudioView
{
	enum class EStage : uint8 { Menu, Call, Presnap, Live, Post, Kick, Wait, Final, Halftime, Replay };
	EStage Stage = EStage::Menu;
	bool bPaused = false;            // the pause menu is up (the world's sounds are held by the engine)
	const tag::Game* G = nullptr;
	const tag::PlaySim* Sim = nullptr;
	int32 Dir = 1;                   // the offense attacks world +X (1) or -X (-1)
	FVector Ball = FVector::ZeroVector;
	bool bBoothTalking = false;
	bool bQuiet = false;             // a capture that wants no music (the highlight reel)
	float ReplayT = -1.f;            // Stage == Replay: the moment of the play on screen, in seconds from the snap
	// a canned kick (Stage == Kick)
	int32 KickKind = -1;             // 0 kickoff, 1 punt, 2 field goal / extra point
	float KickT = 0.f, KickLead = 0.f, KickHang = 0.f;
	float KickSnapAt = -1.f, KickCatchAt = -1.f; // a kick played as its snap-hold-kick scene: when the ball is snapped and caught
	int32 KickDir = 1;
	bool bKickBounce = false;
	int32 KickHops = 0;
	bool bKickBlocked = false;
	std::string KickDesc;
	std::optional<bool> KickGood;
};

UCLASS()
class ATagAudio : public AActor
{
	GENERATED_BODY()

public:
	ATagAudio();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** The one in this world (null with -TagNoAudio). */
	static ATagAudio* Get(const UObject* WorldCtx);
	/** A menu sound (plays through the pause): UiMove, UiSelect, UiBack, UiTab, UiSnap, UiWhoosh, UiToast, UiPause, UiError. */
	static void Ui(const UObject* WorldCtx, FName Bank, float Vol = 1.f);
	/** The same from Slate code, which has no world to hand: the game's one sound actor. */
	static void Ui(const TCHAR* Bank, float Vol = 1.f);

	void Feed(float Dt, const FTagAudioView& V);
	void MatchStart(const tag::Game& G);
	void MatchEnd();
	/** Stop the music now, without a fade (the game thread is about to be busy loading). */
	void Hush();
	/** Things only the director knows: "hardcount", "audible", "flag", "replay", "banner". */
	void Cue(FName What, const FVector& At = FVector::ZeroVector);

	/** 0..1 each (the settings screen). */
	void SetLevels(float Master, float Music, float Stadium);
	/** The settings screen's volumes, in quarters (0..4), kept in GameUserSettings [TAG.Settings]. Which: 0 everything, 1 music, 2 stadium and field. */
	static int32 Level(int32 Which);
	static void StepLevel(int32 Which);
	float LevelMaster = 1.f, LevelMusic = 1.f, LevelStadium = 1.f;

	/** How loud the crowd is right now, 0..1 (the stands' animation can follow it). */
	float CrowdEnergy() const { return FMath::Clamp(0.25f + 0.75f * FMath::Max(Excite, 0.6f * DNoise), 0.f, 1.f); }

private:
	// ---- banks
	struct FBank
	{
		TArray<FSoftObjectPath> Paths;
		TArray<USoundWave*> Waves; // (kept alive by Loaded)
		int32 Last = -1;
		bool bLoaded = false;
	};
	TMap<FName, FBank> Banks;
	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> Loaded;
	void FindBanks();
	USoundWave* Pick(FName Bank);
	bool Has(FName Bank) const { return Banks.Contains(Bank); }

	// ---- voices
	enum class ECat : uint8 { Field, Crowd, Stadium, Ui };
	struct FLater { float T; FName Bank; FVector Pos; float Vol; float Pitch; ECat Cat; };
	TArray<FLater> Queue;
	/** The play's field sounds as they happened (T = seconds from the snap), so the replay can be heard as well as seen. */
	TArray<FLater> Tape;
	float TapeT = 0.f, TapeSeen = -1.f;
	bool bTaping = false;
	TMap<const tag::Ent*, float> Feet; // the men around the ball: each one's next footfall
	void Field(FName Bank, const FVector& Pos, float Vol = 1.f, float Pitch = 1.f, float Delay = 0.f);
	void Crowd(FName Bank, float Vol = 1.f, float Delay = 0.f);
	void StadiumSay(FName Bank, float Vol = 1.f, float Delay = 0.f);
	void PlayNow(const FLater& L);
	float Jit(float Lo, float Hi) { return Rand.FRandRange(Lo, Hi); }

	struct FLoop
	{
		FName Bank;
		UAudioComponent* C = nullptr; // (kept alive by Comps)
		float Vol = 0.f, Target = 0.f;
		float Up = 1.5f, Down = 0.6f;  // volume per second
		bool bStarted = false;
	};
	enum { LCalm, LBuzz, LRoar, LNoise, LRain, LWind, LNum };
	FLoop Loops[LNum];
	UPROPERTY()
	TArray<TObjectPtr<UAudioComponent>> Comps;
	UPROPERTY()
	TObjectPtr<USoundAttenuation> FieldAtt;
	UAudioComponent* MakeComp(bool bUi);
	void TickLoops(float Dt, float CrowdGain);
	void StopLoops();

	// ---- music (two decks; a new piece fades in over the old one) and the stadium's own channel (PA music, chants)
	struct FDeck
	{
		UAudioComponent* C = nullptr;
		FName Bank;
		float Vol = 0.f, Target = 0.f, Rate = 1.f, Left = 0.f;
		bool bOn = false;
	};
	FDeck Decks[2];
	int32 Deck = 0;
	FDeck House;      // the stadium's music between plays, a chant: heard in the bowl, part of the crowd level
	FName MusicNow;
	void Music(FName Bank, float Vol = 1.f, float FadeIn = 0.4f, float FadeOutOld = 1.2f);
	void MusicStop(float Fade = 1.5f);
	void HouseStart(FName Bank, float Vol, float LeadIn = -1.f);
	void HouseStop(float Fade);
	void TickMusic(float Dt, float Duck);
	bool MusicPlaying() const { return Decks[Deck].bOn && Decks[Deck].Target > 0.f; }

	// ---- what it has seen
	const tag::Game* Game = nullptr;
	TSet<const tag::Player*> HomeMen;
	bool IsHome(const tag::Player* Man) const { return HomeMen.Contains(Man); }
	std::string HomeAbbr;
	size_t LogSeen = 0;
	FTagAudioView::EStage LastStage = FTagAudioView::EStage::Menu;
	bool bWasPaused = false, bPauseMusic = false, bHushed = false;
	float StageT = 0.f;

	struct FPlay
	{
		const tag::PlaySim* Sim = nullptr;
		size_t EvSeen = 0;
		double SimT = 0;
		bool bSnapped = false, bWhistled = false, bLineCrash = false, bReacted = false, bDeepCalled = false;
		bool bOffHome = false, bReturn = false;
		int32 Down = 1;
		double ToGo = 10, Los = 25;
		double BigHitT = -9;
		double TrenchT = -9;
		float PreT = 0.f;
		int32 Cadence = 0;
		float StepT = 0.f, StepT2 = 0.f;
		bool bChant = false;
	} P;
	struct FKickSeen
	{
		bool bOn = false, bStruck = false, bDone = false, bBounce = false, bHomeKicks = false, bRise = false, bSnap = false, bHeld = false;
		int32 Hops = 0;
		float LastT = 0.f;
	} K;

	// ---- the crowd's mood
	float Excite = 0.f, ExciteHold = 0.f; // the roar: set by what happens, held, then it dies away
	float DNoise = 0.f;                   // the home crowd making noise against the other offense before the snap
	float Swell = 0.f;                    // a play breaking open, a deep ball in the air
	float Deflate = 0.f;                  // seconds of stunned quiet after the visitors score
	float Duck = 1.f;
	void Roar(float Level, float Hold);
	float Stakes(const tag::Game& G) const;
	void NewPlay(const FTagAudioView& V);
	void OnEvent(const FTagAudioView& V, const tag::PlayEvent& Ev);
	void OnWhistle(const FTagAudioView& V, const tag::PlayResult& R);
	void OnLog(const tag::Game& G, const tag::PlayLog& L);
	void TickKick(const FTagAudioView& V);
	void TickPresnap(float Dt, const FTagAudioView& V);
	void BetweenPlays(const FTagAudioView& V);
	FVector At(const tag::Ent* E, int32 Dir) const;

	FRandomStream Rand;
	bool bLog = false;
	double T0 = 0;
	float RecLeft = 0.f;
	bool bRec = false;
	TMap<FName, int32> Counts;
	void Note(const TCHAR* Kind, FName Bank, float Vol);
};
