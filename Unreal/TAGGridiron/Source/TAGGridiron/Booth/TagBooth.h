// The broadcast booth in the game: the commentary engine (Booth.h), its two voices and the captions.
//
// The match director spawns one ATagBooth per match and feeds it every frame (Feed: the Game and a View of what the
// presentation is doing). The booth decides what is said; this actor speaks it:
//   - each line's audio is a wav in Saved/BoothVoice/<key>.wav, made by the local speech service
//     (Tools/Booth/booth_tts.py, started here when it is not already running) and kept: a line said once is never
//     synthesized again. A line with no audio yet is asked for at once and waited on briefly; the live calls the
//     next snap could need are asked for before it (the booth's forecast), the generic ones at the start of a match.
//   - the audio plays through a 2D audio component (a procedural sound wave fed the wav's samples) at the settings'
//     Master x Commentary volume, lifted by the line's energy (the broadcast mix, ATagAudio::VoiceEnergyGain), and the
//     sound's mix is told what is on the air (ATagAudio::BoothLine) so the crowd makes room for it; a line the booth
//     cuts (a live call takes the floor) fades out in 0.15 s.
//   - a live call whose own audio is not made yet (a matchup never played) is voiced by its generic stand-in
//     (Line::alt: "Caught!" for a named catch) when that is cached, rather than left to a silent caption.
//   - the caption shows who is talking and what he says, broadcast closed-caption style. Without the speech service
//     (no venv, -TagBoothMute) the booth still runs, captions only, at the estimated pace.
// -TagBoothLog writes every line to the log ("TAG booth ..."), -TagNoBooth turns the booth off.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Booth/Booth.h"
#include "TagBooth.generated.h"

class UAudioComponent;
class USoundWaveProcedural;
class STextBlock;
class SWidget;

UCLASS()
class ATagBooth : public AActor
{
	GENERATED_BODY()

public:
	ATagBooth();
	virtual void BeginPlay() override;
	virtual void Tick(float Dt) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** One presentation frame. The director builds the View (stage, the play's sim, a kick in the air). */
	void Feed(float Dt, const tag::Game& G, const tag::booth::View& V);
	/** The booth would like the snap (or the next play) held a moment: a break or a breakdown is still going. */
	bool WantsHold() const { return Core && Core->wantsHold(); }
	/** A voice is on the air right now (the sound mix makes room for it). */
	bool Talking() const { return bPlaying; }
	/** The line on the air now - its words as heard (a live call's generic stand-in when that is what plays), who says
	 *  it, its kind and Line::topic - or null when nobody is talking (the broadcast graphics follow it: UI/BroadcastGfx). */
	const tag::booth::Line* OnAir() const { return bPlaying && CurId ? &CurLine : nullptr; }
	/** -TagBoothRef: the referee announces the flags himself (Booth::enableReferee). */
	bool RefereeOn() const { return Core && Core->refereeOn(); }
	/** Hide the captions (the reel, a cut-in); the voices go on. */
	void SetCaptionsShown(bool b) { bCaptions = b; }
	/** Stop talking at once (quit to menu). */
	void Silence();

	static FString LineKey(const tag::booth::Line& L);
	/** Start the speech service now (at boot), so the voices are loaded by the first kickoff. */
	static void WarmService();
	/** The settings screen's commentary volume, in quarters (0..4) like the sound's (ATagAudio::Level), kept in
	 *  GameUserSettings [TAG.Settings] VolumeCommentary. The voices play at Master x Commentary. */
	static int32 CommentaryLevel();
	static void StepCommentaryLevel();
	/** The voices' gain now: the settings' Master (ATagAudio::Level(0)) times Commentary, each in quarters. */
	float VoiceGain() const;

private:
	struct FClip
	{
		TArray<uint8> Pcm;
		int32 Rate = 24000;
		float Secs = 0.f;
		float Peak = 1.f;   // its loudest sample, 0..1 (the energy's lift stops under the ceiling: ATagAudio::VoiceEnergyGain)
	};
	/** The line's wav is in the cache (looked for on the disk once, then remembered). */
	bool HasClip(const FString& Key);
	/** Tell the sound's mix what is on the air now (ATagAudio::BoothLine): energy -1 when nobody is. */
	void TellMix(int32 Energy, bool bTalk) const;
	static bool IsTalk(tag::booth::Kind K);
	/** Read and parse one wav (no actor state: it runs on a background thread). */
	static bool LoadClip(const FString& Path, FClip& Out);
	/** The current line's wav is read off the game thread; the answer comes back through ClipLoaded. */
	void StartClipLoad();
	void ClipLoaded(uint32 Serial, bool bOk, FClip&& C);
	/** Forget any read that is out or waiting (the line it was for is gone). */
	void DropClipWait();
	void Request(const tag::booth::Line& L, int32 Prio);
	void StartService();
	bool ServiceAlive() const;
	void Prewarm(const tag::Game& G);
	void ShowCaption(const tag::booth::Line* L);

	TUniquePtr<tag::booth::Booth> Core;
	UPROPERTY()
	TObjectPtr<UAudioComponent> Voice;
	UPROPERTY()
	TObjectPtr<USoundWaveProcedural> Wave;

	FString CacheDir, ReqDir;
	FProcHandle Service;
	bool bService = false;   // the speech service is (or should be) there
	bool bMute = false;
	bool bLog = false;
	bool bCaptions = false; // (a setting: MatchDirector::SetCaptionsOn)
	bool bGenericsAsked = false;
	bool bLiveFlag = false;  // the _live file is up (the service waits on it)
	bool bNoWriter = false;  // -TagNoWriter: templates only
	float WriterCheckT = 0.f;
	TMap<int32, double> WriterAsked;  // writer job id -> when it was asked
	double ServiceCheckT = -10;

	// the line being said
	int32 CurId = 0;
	bool bWaitingAudio = false;
	bool bPlaying = false;
	float WaitT = 0.f, PlayT = 0.f, PlaySecs = 0.f, FadeT = -1.f;
	float GainSet = -1.f;           // the voice's volume as last set (a settings change reaches a line already playing)
	float EnergyGain = 1.f;         // the line's own lift by its energy (the broadcast mix), on top of GainSet
	FString CurKey;
	tag::booth::Line CurLine;
	bool bAltLine = false;          // CurLine is a live call's generic stand-in (its own audio was not made yet)
	int32 AltLines = 0, LiveLines = 0, LiveVoiced = 0; // this match's live calls, voiced ones, and of those the stand-ins
	TSet<FString> Asked;
	TSet<FString> Present;          // line keys whose wav is known to be in the cache (never looked for again)
	float ForecastT = 0.f;
	uint64 ForecastStamp = 0;       // the forecast last asked for (Booth::forecastStamp): the same again is not walked again
	bool bForecastSeen = false;
	// the match's regulars and the generic calls with no audio yet (found on a background thread): asked a few a frame
	std::vector<tag::booth::Line> WarmMissing; // (a std container: a Line holds std::strings, which a TArray may not move)
	size_t WarmAt = 0;
	// the current line's wav, read on a background thread
	uint32 ClipSerial = 0;          // one per wait: an answer with an old serial is for a line since dropped
	bool bClipLoading = false;      // a read is out
	bool bClipReady = false;        // ReadyClip holds the current line's audio
	bool bClipLast = false;         // the last look (as the wait ran out) has been taken
	float ClipPollT = 0.f;          // until the next look (10 a second)
	FClip ReadyClip;

	TSharedPtr<SWidget> CaptionRoot;
	TSharedPtr<STextBlock> CaptionWho, CaptionText;
	float CaptionAge = 99.f;
	FString CaptionLast;
};
