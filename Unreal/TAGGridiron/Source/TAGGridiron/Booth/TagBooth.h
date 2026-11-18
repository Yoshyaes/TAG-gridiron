// The broadcast booth in the game: the commentary engine (Booth.h), its two voices and the captions.
//
// The match director spawns one ATagBooth per match and feeds it every frame (Feed: the Game and a View of what the
// presentation is doing). The booth decides what is said; this actor speaks it:
//   - each line's audio is a wav in Saved/BoothVoice/<key>.wav, made by the local speech service
//     (Tools/Booth/booth_tts.py, started here when it is not already running) and kept: a line said once is never
//     synthesized again. A line with no audio yet is asked for at once and waited on briefly; the live calls the
//     next snap could need are asked for before it (the booth's forecast), the generic ones at the start of a match.
//   - the audio plays through a 2D audio component (a procedural sound wave fed the wav's samples); a line the booth
//     cuts (a live call takes the floor) fades out in 0.15 s.
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
	/** Hide the captions (the reel, a cut-in); the voices go on. */
	void SetCaptionsShown(bool b) { bCaptions = b; }
	/** Stop talking at once (quit to menu). */
	void Silence();

	static FString LineKey(const tag::booth::Line& L);
	/** Start the speech service now (at boot), so the voices are loaded by the first kickoff. */
	static void WarmService();

private:
	struct FClip
	{
		TArray<uint8> Pcm;
		int32 Rate = 24000;
		float Secs = 0.f;
	};
	bool LoadClip(const FString& Key, FClip& Out) const;
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
	FString CurKey;
	tag::booth::Line CurLine;
	TSet<FString> Asked;
	float ForecastT = 0.f;

	TSharedPtr<SWidget> CaptionRoot;
	TSharedPtr<STextBlock> CaptionWho, CaptionText;
	float CaptionAge = 99.f;
	FString CaptionLast;
};
