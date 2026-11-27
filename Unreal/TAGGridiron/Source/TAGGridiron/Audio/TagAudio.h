// The game's sound: the stadium crowd, the field, the stadium's own PA and music, the broadcast music and the menus.
//
// Like the booth, it watches the game rather than being told about it: the director hands it one view a frame (the
// stage, the play's sim, a kick in the air, the ball) and it finds the snap, the hits, the whistle and the result
// itself. The crowd is a home crowd with a mood: it knows the score, the down and who has the ball.
//
// Sounds are USoundWave assets under /Game/TAG/Audio/<group>/<Bank>/ (built by Tools/Audio); a bank is a folder of
// takes of one sound, and a take is never played twice running. A bank that is not there is simply silent.
//
// The mix is a broadcast's (Research/NextLevel/05 A3, A4): the master is trimmed to the console loudness target and
// limited under -1 dBTP (-TagNoLimiter: neither; -TagMixTrimDb=<dB> another trim), and the crowd, the stadium and the
// music give way to the booth by what it is saying - well down under calm talk, a little under a live call, not at all
// under the big call over a roaring building (-TagMixLegacy: the old fixed duck under any voice, the beds only).
// -TagMixTest plays a calm line, an excited call and a touchdown over a CPU game and checks the gains it applied.
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
class USubmixEffectDynamicsProcessorPreset;
struct FStreamableHandle;

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
	/** A menu sound (plays through the pause). The kit is Tools/Audio/build_ui.py's: UiMove, UiValue, UiTab, UiSelect, UiBack,
	 *  UiError, UiTeam, UiSwapSides, UiRandom, UiPick, UiDrop, UiSwap, UiEquip, UiUnequip, UiType, UiSave, UiDelete, UiJoin,
	 *  UiLeave, UiCaller (and UiOpen, UiClose, UiTitle, UiStart, which Screen() and the kickoff play themselves). */
	static void Ui(const UObject* WorldCtx, FName Bank, float Vol = 1.f);
	/** The same from Slate code, which has no world to hand: the game's one sound actor. */
	static void Ui(const TCHAR* Bank, float Vol = 1.f, float Pitch = 1.f);
	/** The menu screen now up: "Main", "Lobby", "TeamSelect", "Settings", "Abilities", "DepthChart", "CreatePlayer",
	 *  "Pause". A screen that comes forward or goes back is heard (the first main menu with the title's hit), and the
	 *  front end's bed follows it: the building fills up on the way to the kickoff. The same screen again is nothing. */
	static void Screen(const TCHAR* Id);

	void Feed(float Dt, const FTagAudioView& V);
	void MatchStart(const tag::Game& G);
	void MatchEnd();
	/** Start loading the match's sounds in the background (everything but the two PA calls of one team, which MatchStart
	 *  asks for once it knows the home side). Safe to call at any time and more than once; MatchStart calls it itself,
	 *  so calling it earlier (before the loading screen's wait) only gives the loads more time to land. */
	void PreloadMatchBanks();
	/** Stop the music now, without a fade (the game thread is about to be busy loading). */
	void Hush();
	/** Things only the director knows: "hardcount", "audible", "flag", "replay", "banner". */
	void Cue(FName What, const FVector& At = FVector::ZeroVector);
	/** Two bodies meeting on screen that the sim's events do not sound - a block, a collision in traffic, a pile-up - from
	 *  the director's contacts (Game/TagContact.h; it picks which, Game/MatchFeel.cpp): a quiet pad clack (HitLight) or a
	 *  body thud (HitMed) by Strength 0..1, its gain 6 dB and more under a tackle's hit. Heard in the replay too. */
	void ContactHit(const FVector& At, float Strength);

	/** The booth's line now on the air (ATagBooth, as each voiced line starts and ends): its energy 0..3 (-1: nobody is
	 *  talking) and whether it is talk - analysis, a story, a read, the setup - rather than a call of the action. The
	 *  crowd, the stadium's one-shots and the music ride it (the broadcast mix). */
	void BoothLine(int32 Energy, bool bTalk);
	/** The playback gain for a booth line of this energy: an excited call is louder than calm talk (about -2, 0, +2, +4 dB
	 *  for energy 0..3; the clips are all levelled alike), and the lift stops where the clip's peak (ClipPeak, 0..1) played
	 *  at Level (the settings' Master x Commentary) would pass -1 dBFS at the output. 1 with -TagMixLegacy. */
	static float VoiceEnergyGain(int32 Energy, float ClipPeak, float Level);
	/** -TagMixLegacy: the mix as it was before the broadcast mix (a fixed duck under any voice, the beds only, every
	 *  line at one level). */
	static bool LegacyMix();
	/** The master's trim, linear (the console loudness target; 1 with -TagNoLimiter). */
	static float MasterTrim();

	/** 0..1 each (the settings screen). */
	void SetLevels(float Master, float Music, float Stadium);
	/** The settings screen's volumes, in quarters (0..4), kept in GameUserSettings [TAG.Settings]. Which: 0 everything, 1 music, 2 stadium and field. */
	static int32 Level(int32 Which);
	static void StepLevel(int32 Which);
	float LevelMaster = 1.f, LevelMusic = 1.f, LevelStadium = 1.f;

	/** How hard the home crowd is cheering right now, 0..1: the stands' animation follows it (AMatchDirector::TickStands).
	 *  Its roar (Excite) and its own offense breaking a play open (Swell past what an away gain gives); not the noise it
	 *  makes against the visitors' offense (DNoise, which is not cheering), and nothing while it sits stunned after a
	 *  visitors' score (Deflate). */
	float CrowdEnergy() const
	{
		if (Deflate > 0.f) return 0.f;
		const float Rise = P.bOffHome && !P.bReturn ? FMath::Max(0.f, Swell - 0.3f) / 0.7f : 0.f;
		return FMath::Clamp(FMath::Max(Excite, 0.8f * Rise), 0.f, 1.f);
	}

private:
	// ---- banks
	struct FBank
	{
		TArray<FSoftObjectPath> Paths;
		TArray<USoundWave*> Waves; // (kept alive by Loaded)
		int32 Last = -1;
		bool bLoaded = false;
		bool bAsync = false; // a background load has been asked for (PreloadBank)
	};
	TMap<FName, FBank> Banks;
	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> Loaded;
	/** Background loads still held (each keeps its waves alive until BankLanded has put them in Loaded). */
	TArray<TSharedPtr<FStreamableHandle>> Preloads;
	/** Load a bank in the background; Pick still loads it on the spot if it is needed before it lands. */
	void PreloadBank(FName Bank);
	void BankLanded(FName Bank);
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
	float TapeRate = 0.5f;            // the replay's speed (seconds of the play a second), smoothed: the replayed hits' pitch
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
		// a bed with more than one take moves to another every few minutes, crossfaded at a quiet moment, so its loop
		// never comes round to be heard (C2 is the take coming in)
		UAudioComponent* C2 = nullptr;
		float Xf = -1.f;               // the crossfade, 0..1 (-1: none going)
		float NextSwap = 0.f;          // seconds until the next take may come in
	};
	enum { LCalm, LBuzz, LRoar, LNoise, LRain, LWind, LNum };
	FLoop Loops[LNum];
	UPROPERTY()
	TArray<TObjectPtr<UAudioComponent>> Comps;
	UPROPERTY()
	TObjectPtr<USoundAttenuation> FieldAtt;
	/** The master's trim and limiter (an effect on the main submix, so the -TagAudioRec recording hears it too). */
	UPROPERTY()
	TObjectPtr<USubmixEffectDynamicsProcessorPreset> Limiter;
	void StartLimiter();
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
	FName ScreenNow;          // the menu screen last heard (Screen)
	bool bTitled = false;     // the title's hit has been played (once a run)
	float MenuHold = 0.f;     // the playlist waits this long (for the title's hit to land)
	void ScreenChanged();
	int32 Fed = 0;            // frames fed so far (nothing of the menus is heard before the game is on screen)
	float StageT = 0.f;
	bool bOpenPending = false;    // the match has started but is not on screen yet (its load): the open's sound waits
	float OpenT = -1.f;           // seconds into the open (-1: not in it)

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

	// ---- the broadcast mix: the faders the booth's line rides (1 = as built; under 1 the crowd gives way, over 1 it blooms)
	float Duck = 1.f;                     // the crowd's beds, the stadium's music and (but in the legacy mix) its one-shots
	float MusicDuck = 1.f;
	int32 VoiceEnergy = -1;               // the booth's line on the air (BoothLine): its energy, -1 none
	bool bVoiceTalk = false;              // ... and it is talk, not a call
	bool bLegacyMix = false;
	bool bBoothRef = false;               // -TagBoothRef: the booth's referee announces the flags (the PA's stock sample stays quiet)
	/** The crowd's and the PA's one-shots still sounding, so the duck can ride them as well as the beds. */
	struct FShot
	{
		TWeakObjectPtr<UAudioComponent> C;
		float Base = 0.f;                 // its volume with no duck
		float Set = 1.f;                  // the duck it was last set at
	};
	TArray<FShot> Shots;
	TWeakObjectPtr<UAudioComponent> LastShot;     // the last one-shot started (the mix test reads it)
	float LastShotBase = 0.f, LastShotGain = 0.f;
	/** The last one-shot's gain now over its gain with no duck (its gain as it started, once it has played out). */
	float LastShotDuck() const;
	void TickMix(float Dt, const FTagAudioView& V);
	// -TagMixTest: a scripted calm line, excited call and touchdown over the match, the gains checked as they settle
	bool bMixTest = false, bTestOnAir = false;
	float MixTestT = -1.f;
	int32 MixTestStep = 0, MixPass = 0, MixFail = 0;
	void TickMixTest(float Dt);
	void MixCheck(const TCHAR* What, float Got, float Want, float Tol);
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
	TMap<FName, TMap<FName, int32>> TakeUses; // bank -> take -> times played this match (the summary's "take uses")
	void Note(const TCHAR* Kind, FName Bank, float Vol, const USoundWave* Take = nullptr);
};
