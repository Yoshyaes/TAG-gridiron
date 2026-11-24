# 05 - Audio and Commentary: making TAG Gridiron sound like a live stadium and a live broadcast

Researcher 5 of 7. Date: 2026-11-23. Read-only on the project: no code, assets or config were changed and nothing was built or run.

**Tools note.** Web search and page fetches were available. The shared search budget ran out near the end, so two
checks were never made: the MetaSound Builder C++ API, and recent player complaints about Madden's commentary. Several
pages could not be fetched (403, 404 or timeout), including the Operation Sports MLB 22 deep dive, the GameSpot 2K17
article, the NPR 2K piece, the HyperX FIFA 21 piece and the ACM sports-commentary paper. Where a claim rests only on a
search-engine summary of a page, it is marked **(snippet)**. Anything from my own background knowledge is marked
**(memory)**.

---

## 1. What the best studios do

Each technique lists its source, then a line on confidence. "Stated" means the source says it. "Inferred" means it is my reading.

### 1.1 Commentary content and assembly

**T1. Situations, not scripts, with about ten takes per situation, and every take played before any repeats.**
EA's NHL 15 producer said they give commentators a situation, ask for ten samples, and aim for all ten to play
before any of them repeats. That year produced about 35,000 lines over about 13 sessions per commentator
([SI, 2014](https://www.si.com/extra-mustard/2014/08/21/how-are-commentators-programmed-sports-video-games)).
EA FC's Guy Mowbray gave the same figure: "we need 10 different ways" of saying even "crossing the halfway line"
([VGC](https://www.videogameschronicle.com/news/ea-sports-fc-commentator-lets-ea-use-an-ai-version-of-his-voice-to-record-some-lines-its-revealed)).
Confidence: high (stated by two EA sources).

**T2. Several intensity levels per line, with neighbouring fragments recorded at the same intensity.**
NHL recorded lines at several intensity levels ("four to six" for Doc Emrick). Statements that play next to each
other have to be recorded at matching intensity so they flow together ([SI](https://www.si.com/extra-mustard/2014/08/21/how-are-commentators-programmed-sports-video-games)).
MLB The Show 22 says it widened the dynamic range of play-by-play calls to match each moment, alongside "audio
stitching enhancements" ([Push Square](https://pushsquare.com/news/2022/03/mlb-the-show-22s-new-ps5-ps4-commentary-team-recorded-for-350-hours), snippet).
Confidence: high for NHL, medium for MLB.

**T3. Names are stitched in, recorded in several intonations, and sentences are built to start with the name.**
NHL built its sentences to start with a player's name, covering about 5,000 players ([SI](https://www.si.com/extra-mustard/2014/08/21/how-are-commentators-programmed-sports-video-games)).
EA FC has more than 20,000 names, each recorded in several intonations (pass, shot, goal). EA now uses an AI clone
of Mowbray's voice, with his permission, for some of the names. Longer lines are still recorded and written by him
([VGC](https://www.videogameschronicle.com/news/ea-sports-fc-commentator-lets-ea-use-an-ai-version-of-his-voice-to-record-some-lines-its-revealed),
[PCGamesN](https://www.pcgamesn.com/ea-sports-fc-26/ai-commentary-lines-guy-mowbray)). Chris Fowler read about 700
names for College Football 25 across roughly 115 hours of recording
([SI](https://www.si.com/college/2024/05/01/chris-fowler-details-recording-calls-ea-college-football-25), snippet).
Confidence: high.
Inferred: the industry's answer to "names plus numbers" is slot stitching with intonation-matched fragments. Even EA
reserves synthesis for the short, slot-like pieces (names) and not for whole sentences.

**T4. Chemistry comes from hearing, or being with, the other commentator.**
For 2K, Clark Kellogg records "with headphones" playing his colleagues' prerecorded lines. He spends 40-50 hours a
season on it ([NPR, 2013](https://www.npr.org/sections/alltechconsidered/2013/11/18/245974844/calling-the-shots-realistic-commentary-heightens-video-games), snippet; fetch timed out).
Harlan says the topics are thrown out and riffed on, "not pre-destined"
([Kotaku](https://kotaku.com/the-weird-stuff-that-happens-when-youre-an-nba-2k-comme-1798481486)). Madden 17's
Gaudin and Davis worked in EA Tiburon's office several days a week, which the team credits for their chemistry, and
their ad-libs were kept ([Game Informer](https://gameinformer.com/games/madden_nfl_17/b/playstation4/archive/2016/06/08/madden-gets-new-commentators-but-more-importantly-a-new-approach)).
Older MLB The Show crews recorded separately, and one analysis says banter was "nearly non-existent" as a result
(Operation Sports via snippet). Confidence: medium-high.
Inferred: **this is the main reason 2K is the benchmark.** The lines are recorded as exchanges, so the reply fits
the question in content, timing and energy. A rotating third analyst, a studio show and player interviews (2K17:
200+ clips, 150+ interviews; [GameSpot](https://www.gamespot.com/articles/nba-2k17-rotates-its-commentary-teams-to-keep-thin/1100-6443430/), snippet)
add variety on top. Volume is not what sets 2K apart: every major series records tens of thousands of lines.

**T5. Volume is enormous and keeps growing.**
- Madden 17: about 40 hours shipped, plus 60 hours recorded in two weeks that had not yet been used (search summary of the Game Informer and Uproxx coverage).
- NBA 2K14: "50+ hours of fresh commentary every year" ([GamesRadar](https://www.gamesradar.com/nba-2k14-50-hours-fresh-commentary-every-year)).
- MLB The Show 22: 128 sessions, 350 hours, about 45,000 lines ([Push Square](https://pushsquare.com/news/2022/03/mlb-the-show-22s-new-ps5-ps4-commentary-team-recorded-for-350-hours)).
- MLB The Show 25 PA announcers: 3 voices, 50 hours combined ([theshow.com](https://theshow.com/news/presentation-commentary-updates-25/)).

Confidence: high (these are promotional figures).

**T6. Triggers use rich context.**
Madden 17 lines key on down, distance, the players involved, the game situation, time of day, weather, the player's
handedness and the user's moves, and the logic can be patched after release
([Game Informer](https://gameinformer.com/games/madden_nfl_17/b/playstation4/archive/2016/06/08/madden-gets-new-commentators-but-more-importantly-a-new-approach)).
NHL narrows the candidate pool by conditions such as "final minute", star rating and the score ([SI](https://www.si.com/extra-mustard/2014/08/21/how-are-commentators-programmed-sports-video-games)).
College Football 25 picks its broadcast crew by the "magnitude" of the game
([SI](https://www.si.com/college/2024/05/02/announcers-ea-sports-college-football-25-depend-on-magnitude-game), snippet).
Confidence: high.

**T7. Cross-game and season context.**
MLB The Show 25 adds 75 mid-game presentations: standings, races, award candidates and recent trends
([theshow.com](https://theshow.com/news/presentation-commentary-updates-25/)). Madden and 2K patch in topical lines
during the season. Confidence: high. TAG has no season mode yet (see 2.3), so this matters later.

**T8. Interruption, lead-in and trail-off.**
No source I could reach describes how Madden, 2K or FC cut a line when a play interrupts it.
Background (memory, medium confidence): shipped games commonly keep short "interrupt" lines ("Oh, and..."), cut at
phrase boundaries, and prefer dropping a pending line to cutting a running one. Treat this as unverified.

### 1.2 Synthesized commentary

**T9. Shipped synthetic sports commentary exists, and its known weaknesses are monotone delivery and mispronounced names.**
IBM's watsonx commentary has run on Wimbledon, the Masters (20,000+ clips) and US Open highlight reels since 2023.
Listeners criticised it as robotic and for mispronouncing names
([summary of ESPN, IBM and AOL coverage](https://africa.espn.com/tennis/story/_/id/37890817/wimbledon-ibm-team-ai-generated-highlight-commentary), snippet).
IBM's follow-up research uses a three-step pipeline: an excitement score, then a script whose wording follows that
score, then ProsodyLM, which emits word-level prosody tokens (F0, duration, energy) to avoid "a monotone level of
excitement" ([IBM Think](https://www.ibm.com/think/news/future-tennis-broadcasting-ai-sports-commentary)).
Confidence: high.

**T10. The acoustics of excitement.**
Horse-race commentary shows higher mean pitch, a wider pitch range, higher intensity and a faster rate as
excitement rises. Arousal climbs through the race and falls afterwards
([Trouvain & Barry 2000](https://www.coli.uni-saarland.de/~trouvain/trouvain_barry_2000.pdf); the PDF would not
decode, so this comes from a search summary). Iura et al. (IEEE Access 2025) condition commentary TTS on an
excitement label ([PDF](https://takamichi-lab.github.io/assets/pdf/paper/2025/iura25ieee-access_commentary-tts.pdf), snippet).
Confidence: medium. Inferred: excited lines should be louder, not just faster and higher.

**T11. Dead air in live generation is solved by buffering candidates ahead of playback.**
A June 2026 IJCAI demo generates text in parallel with playback and buffers several candidate utterances. That cut
mean inter-utterance silence from 9.6 s to 0.3 s and improved perceived rhythm in a 120-player study
([arXiv 2606.13322](https://arxiv.org/abs/2606.13322)). Confidence: high (abstract). TAG's forecast and prewarm
already apply this idea.

**T12. Open TTS landscape and licences, as of mid/late 2026.**

| Model | Licence | Notes relevant to TAG | Source |
|---|---|---|---|
| Chatterbox / Turbo / **Nano** | MIT | **The repo is now "retired research and no longer maintained".** Turbo is 350M with native `[laugh]` `[chuckle]` `[cough]` tags. Nano is 110M, "3x faster than realtime on 8 CPU cores", same architecture and tags as Turbo. All output carries Resemble's Perth watermark. | [GitHub](https://github.com/resemble-ai/chatterbox) |
| Kokoro-82M | Apache 2.0 | Fast on CPU, no cloning, flat delivery | [HF](https://huggingface.co/hexgrad/Kokoro-82M) |
| Dia (1.6B) / Dia2 (1B, 2B) | Apache 2.0 | Two speakers in one generation (`[S1]`/`[S2]`) with nonverbals. Voice drifts unless prefix-prompted. English only. Dia: ~4.4 GB VRAM, 1.3-2.2x real time on a 4090. | [Dia](https://github.com/nari-labs/dia), [Dia2](https://github.com/nari-labs/dia2) |
| Qwen3-TTS (0.6B/1.7B) | Apache 2.0 | Natural-language instructions control emotion and prosody | [GitHub](https://github.com/QwenLM/Qwen3-TTS) (snippet) |
| CosyVoice 3, F5-TTS | Apache 2.0 / MIT (per blog) | Not checked at source | [ocdevel](https://ocdevel.com/blog/20250720-tts) |
| IndexTTS2 | bilibili custom licence; commercial use needs written permission | Best emotion and duration control on paper | [GitHub](https://github.com/index-tts/index-tts) (snippet) |
| Fish Audio S2, VibeVoice, Higgs Audio v3 | Research or non-commercial | Excluded | [ocdevel](https://ocdevel.com/blog/20250720-tts) |

Confidence: medium-high for the repos fetched directly (Chatterbox, Kokoro, Dia, Dia2); medium for the rest.

### 1.3 Crowd

**T13. Large amounts of crowd material, chosen by context, panned with the camera.**
Madden 12 had about 50 hours of interactive crowd files, recorded in stadium quadrants at 15-20 minutes each. Logic
picks files by context (rivalry or Super Bowl gets huge, a blowout thins out), and the crowd pans with the camera
([Designing Sound, 2011](https://designingsound.org/2011/09/21/madden-nfl-12/)). Confidence: high (but old).

**T14. Real captures plus staff chants made huge in processing.**
EA puts recorders in ESPN broadcast trucks every week. Fight songs and chants are sung by groups of volunteer EA
staff, then run "through a third-party audio plugin ... so it sounds like 70,000 people". College Football 26 used
"hundreds of thousands of audio files". Foley actors in pads simulate collisions
([EA](https://www.ea.com/ea-studios/ea-sports/news/engineering-game-day-sounds-behind-ea-sports)). Confidence: high.

**T15. Crowd emitters placed around the bowl.**
Madden 21 positions crowd sources from crowd placement data (search summary of
[EA](https://www.ea.com/en-us/news/gridiron-notes-madden-nfl-21-next-level-gameday-presentation-deep-dive), snippet).
MLB The Show 25 uses emitter-based crowd "from specific sections of the stadium" and separate crowd sets per level
([theshow.com](https://theshow.com/news/presentation-commentary-updates-25/)). Confidence: medium-high.

**T16. The broadcast virtual-crowd model: a base bed plus samples a human fires on events.**
In the 2020 empty-stadium broadcasts, a base "carpet" was built from the previous meeting of the two teams, and
producers fired samples (fouls, VAR, reactions) by hand while anticipating the play. Their view: "You can expect,
but never predict", and "there is no technological solution" ([ESPN](https://global.espn.com/football/story/_/id/37583797/where-crowd-noise-feed-comes-how-made-it)).
EA's archive of 1,300 chants and crowd sounds fed Sky's version (search summary). Confidence: high.
Inferred: this human-operator model is exactly what a code-driven crowd imitates. Its quality depends on anticipation
(a swell starting before the outcome) and on the depth of the sample library.

**T17. Crowd noise as gameplay.**
Madden 22 tied home-field advantage to crowd noise affecting the away offense (snippet,
[TechRadar](https://techradar.com/how-to/best-madden-22-home-field-advantages-the-teams-with-game-changing-perks)).
Confidence: medium. That is a gameplay question for another researcher.

### 1.4 On-field sound and the broadcast mix

**T18. The broadcast A1's rules for balancing the booth against the field.**
NBC's Sunday Night Football A1 keeps the announcers in the center channel about 6 dB above the rest. On big plays
the announcers drop 6-10 dB against the crowd. He rides the crowd faders "like a music mixer" and uses light
compression, because "a viewer can get fatigued listening if the level is too even". Crowd mics stay in the same
spots about 85% of the time. Parabolic dishes naturally filter out lows and highs
([SVG, 2018](https://www.sportsvideo.org/2018/09/21/inside-nbc-sports-all-encompassing-audio-production-for-sunday-night-football/)).
Confidence: high.

**T19. What the snap and the hit sound like on TV.**
Player mics, usually on the center or a guard, carry the QB cadence "like you're standing next to him". They open
at the break of the huddle and close a beat after the snap, because the first hit sounds too big for the picture.
Effects mics then take over, and plastic pads sound unnatural up close
([SVG, 2013](https://www.sportsvideo.org/2013/02/26/nfl-season-on-field-audio-rises-to-a-new-level/)). There are
usually six parabolic mics, a dozen at a Super Bowl (search summary of SVG). Confidence: high.
Inferred: the TV football sound is a close cadence, a crunch at the line, and then mostly crowd. Individual hits sit
*under* the crowd and the voice.

**T20. NFL Films "sweetens" in post.**
Mixers do a finishing pass on edited sequences ([Storybench](https://www.storybench.org/?p=1429), snippet).
Confidence: medium. TAG's replays are its "NFL Films moment".

**T21. Loudness targets.**
Console games: -24 ±2 LUFS integrated. Portable: -18 ±2. True peak -1 dBTP
([ITU-R BS.2434](https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BS.2434-2018-PDF-E.pdf);
[Audiokinetic](https://audiokinetic.com/en/mastering-a-game-with-wwise-part1), snippet). The ASWG guidance is that
loudness range, not the average, is what changes between speaker systems
([Designing Sound](https://designingsound.org/2013/02/different-loudness-ranges-for-console-and-mobile-games/), snippet).
Confidence: high for -24/-1.

**T22. DICE HDR audio.**
Mixing is driven by loudness. The listener's window slides with the loudest sound, and priority and culling follow
loudness ([Frostbite](https://ea.com/frostbite/news/how-hdr-audio-makes-battlefield-bad-company-go-boom),
[slides](https://www.slideshare.net/slideshow/adaptive-mixing-in-frostbite/3128152)). Confidence: medium (slides).
It was built for the very wide dynamic range of a shooter; see section 4.

**T23. User mix presets.**
Madden 23 offers Broadcast, Field and Custom audio mixes (search summary of
[EA accessibility](https://www.ea.com/able/resources/madden/madden-nfl-23/ps4/volume)). Confidence: medium.

### 1.5 UE5 tooling (what exists, from Epic docs)

- **Submix Dynamics Processor with a sidechain key**: the KeySource can be an external submix or an audio bus
  ([API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AudioMixer/FSubmixEffectDynamicsProcessorSe-),
  [5.1 Python](https://docs.unrealengine.com/5.1/en-US/PythonAPI/class/SubmixEffectDynamicsProcessorSettings.html)).
  A known bug stops the audio-bus sidechain when the sound retriggers ([UE-125147](https://issues.unrealengine.com/issue/UE-125147)).
  Confidence: high.
- **Audio Modulation** (control buses, bus mixes, parameter patches): mix states such as "Broadcast" and "Field"
  ([overview](https://dev.epicgames.com/documentation/unreal-engine/audio-modulation-overview-in-unreal-engine)).
- **Convolution Reverb** as a submix effect with an IR asset ([docs](https://dev.epicgames.com/documentation/unreal-engine/convolution-reverb-in-unreal-engine)).
- **Soundscape** plugin: procedural ambient palettes streamed as the *player moves around the world*
  ([quick start](https://dev.epicgames.com/documentation/unreal-engine/soundscape-quick-start)).
- **Voice limit**: platform Max Channels (the effective cap is the smaller of the platform and global values); the
  default is reported as 32 ([Epic](https://dev.epicgames.com/documentation/unreal-engine/platform-audio-settings-in-unreal-engine);
  32 from forum and UDN snippets, medium confidence).
- MetaSounds and Quartz were not checked further: the search budget was exhausted.

---

## 2. Where TAG Gridiron stands

### 2.1 What it already does well (verified in code)

**Crowd with a home point of view and a mood model.** Code: `Source/TAGGridiron/Audio/TagAudio.cpp`.
- Four woven loop layers (Calm, Buzz, Roar, Noise) are driven by Excite, DNoise, Swell and Deflate (lines 1149-1197).
- Home-ness comes from roster pointers (`HomeMen`, line 497).
- `Stakes()` raises the level late and close, in the red zone and on third down (lines 571-584).
- Pre-snap noise against the visiting offense is graded by down (lines 1152-1161), and the home crowd hushes for its own offense (line 1185).
- Swell follows yards gained past the line of scrimmage and deep balls in the air (lines 1162-1179).
- There is a stunned quiet after visitor scores (Deflate) and a building that "gives up" in blowouts (line 1188).

These match T13 and T16 conceptually.

**Field sound.**
- Tackle hits are graded by closing speed (lines 748-761).
- Line crash at 0.36 s (line 1084).
- Footsteps for the carrier, the user's man and up to six chasers (lines 1093-1123).
- A per-QB cadence voice with color, set and hut timing, hard count and audible cues (lines 624-644, 973-985).
- Linebacker calls, kick scenes with the long snap, and weather beds.
- **Replays are heard**: the play's sounds are taped and replayed at 0.8 pitch (lines 1137-1145). That is a real NFL-Films-style touch.

**Stadium.**
- Per-team PA "first down" and "touchdown", third-down "make noise" calls, house music between plays, the kickoff rise, horn and fireworks.
- Per-penalty referee calls through a synthesized bowl IR with slap-back (`Tools/Audio/build_voices.py` lines 36-57 and 139-152; `audiolib.stadium_ir`).

**Booth.** Code: `Source/TAGGridiron/Booth/Booth.cpp`, `Booth.h`.
- A priority and TTL queue with live calls that expire in about 1 s (`live()`, lines 1206-1227).
- A big call clears smaller pending ones (lines 1216-1222). Analysts give up the floor at the snap (`snapFlush`, lines 189-198). A question is never asked without time for its answer (lines 220-229).
- A 0.45-0.7 s gap when the voice changes (line 232).
- Results are held for the officials, and BoothScript fact-checks them (0 contradictions on 6 seeds per `memory/broadcast-booth.md`).
- Energy runs 0..3 (`Booth.h` line 46).
- Pre-snap live-call planning and prewarm (`forecast()`, lines 277-313; `TagBooth.cpp` `Prewarm`, lines 275-322).
- Within-game no-repeat: `spokenSet_`, `recentPick_`, and `rot()` with 3 stable variants per man.
- About 232 `pick()` banks.

This covers T6 and T11 well and is ahead of what most indie games attempt.

**Testing.** `-TagAudioLog`, `-TagAudioRec`, `-TagAudioSilent`, `run_test.sh`, BoothScript transcripts with metrics.
This is the right base for measurement.

### 2.2 Where it falls short (each verified in code unless marked)

1. **The commentary ignores the Master volume, and there is no commentary volume.**
   - `TagBooth.cpp:399` sets `Voice->SetVolumeMultiplier(1.f)`, and no sound class or submix applies `LevelMaster`.
   - The Settings screen offers only Master, Music and Stadium (`TagAudio.cpp:66-67`; `UI/GridironOverlay.cpp:2139-2149`).
   - With Master at 0, the booth should still be heard. Inferred from the code; not run.
2. **The stands' animation disagrees with the crowd's sound.**
   - The director sets `Stadium->Hype = 1.f` on *every* touchdown, whichever team scores (`Game/MatchDirector.cpp:3946-3947`), and `Hype = 0.6` on any 20+ yard gain (`:3841`).
   - Meanwhile the audio plays GroanBig and Deflate for a visiting touchdown (`TagAudio.cpp:808-814`).
   - `ATagAudio::CrowdEnergy()` exists "so the stands' animation can follow it" (`TagAudio.h:92-93`), but nothing calls it.
3. **The duck runs the opposite way to TV on big plays, and it only touches the beds.**
   - While the booth talks, the crowd *loops* are multiplied by 0.74 and the music by 0.55, whatever the line (`TagAudio.cpp:1026-1027` and `1197-1198`).
   - Crowd and PA one-shots (`PlaySound2D`, line 335) and field sounds are never ducked.
   - On TV, the crowd rises over the voice on big plays (T18). Here a touchdown call pushes the roar *down*.
4. **Every commentary line is levelled to the same loudness, whatever its energy.**
   `booth_tts.py finish()` sets every clip to -19 dBFS RMS (lines 118-128). An energy-3 "Touchdown!" is no louder than
   an energy-0 aside, which goes against T10 and T18.
5. **The mix is never measured against a loudness target.**
   - There are no submixes, no master limiter or bus compressor, and no audio settings in `Config/DefaultEngine.ini` (grep found none).
   - The voice limit is therefore the engine default (reported as 32).
   - The assets are levelled (crowd loops -20 LUFS, PA -18 LUFS, voice -19 dBFS RMS), but the summed game mix has no known LUFS or true peak. `memory/game-audio.md` says "nobody has LISTENED yet".
6. **Live calls with no cached audio play as silent captions.**
   - A live call waits 0.3 s for its wav, then the caption carries it (`TagBooth.cpp:381, 422-431`).
   - `live()`'s `fallback` is a text fallback, not an audio one (`Booth.cpp:1206-1210`).
   - Memory: 15-60% of lines voiced on a cold cache, 66-83% after fixes, 93% only after a bake of one matchup (teams 3 v 7).
7. **Repetition is controlled within a game only.**
   - `spokenSet_` and `recentPick_` are per-match members (`Booth.h:395-397`). Nothing is kept between matches, so the same 2-3 wordings per bank return game after game.
   - `rot()` deliberately keeps 3 stable variants per man, for the cache.
   - NHL's rule is "all ten before any repeat" (T1).
8. **Template writing is at its ceiling.** Reviewer scores are 4.3-5/10 and chemistry 2-3/10 (memory). The paused LLM
   writer cost $0.35-1.80 per game on Opus, or $0.008 per game on Haiku with more factual slips (memory). Analyst word
   share is about 24%, against 35-45% on TV.
9. **One speech channel, with hard cuts.**
   - A cut is a 0.15 s fade wherever the line happens to be (`TagBooth.cpp:355-358`).
   - The two voices never overlap, and there are no interjections, laughs or breaths (the Turbo tags are unused: grep found no `[laugh]` or `[chuckle]` in `Tools/Booth`).
   - 2K's chemistry comes from recorded exchanges (T4).
10. **Thin take counts.** These come from counting `Content/TAG/Audio`.
    - Crowd: CheerBig 4, CheerSmall 4, Boo 4, CheerMed 5, Gasp 5, Groan 6, GroanBig 3, AwayCheer 3.
    - Field: HitHeavy 6, HitHuge 4.
    - Each crowd bed layer is **one** 44-64 s loop (`Tools/Audio/build_crowd.py:207-210`).
    - Over a full game, each cheer take plays many times. There is no DE-FENSE chant (memory: two synthesized attempts were rejected).
11. **Physical contacts are silent.** `AMatchDirector::DetectContacts()` (`MatchDirector.cpp:6955-6993`) already works
    out a mass-and-closing-speed impulse for every pair of bodies that meet on screen, but it only adds visual impact. The
    sound comes from sim events alone (`TagAudio.cpp:668-784`), so collisions in traffic and in piles that don't produce
    a sim event make no sound.
12. **Referee and PA content is generic.**
    - Referee lines have no player number, and no "declined", "offsetting" or "repeat third down" (`build_voices.py:36-57`).
    - The PA has two generic calls (`:58-61`) plus per-team first down and touchdown. It never names players.
    - There is one PA voice and one ref voice, used for every stadium.
13. **TTS competes with the game for the GPU.** Memory: the game takes about 14 GB of the 16 GB card; Turbo with throttling costs about 15 fps (111.7 → 96.6); Kokoro costs about 8 fps.
14. **No spatial crowd.** The beds and reactions are 2D stereo (`TagAudio.cpp:198-211, 335`). There is no surround or emitter placement (T15).

### 2.3 Not applicable today

- No season or franchise mode: `Sim/League.h` has only TeamSeed and TeamOvr, and a grep finds nothing for "Season" or "Franchise". Season storylines (T7) therefore have no data to draw on.
- No domes or roofs in the Sim: grep finds nothing for dome, roof or indoor. Every stadium is an open bowl.

---

## 3. Actions

Ranked by audible (and visible) impact per unit of effort. S = under a day, M = days, L = weeks. Everything uses
code, CC0, MIT or Apache assets and tools, and the original synthesized voices already in the project: no recording budget and no real broadcaster voices.

### A1. Make the booth obey Master volume and add a Commentary volume (S)
- **Build:**
  - Multiply the booth voice by `LevelMaster × LevelCommentary`.
  - Add a fourth level, `VolumeCommentary`, to `[TAG.Settings]` and the Settings screen.
  - The booth reads the level from `ATagAudio` on each line start and on change.
- **Audible:** the commentary volume follows the player's choice; today it cannot be lowered or muted.
- **Where:** `Booth/TagBooth.cpp:399` (and the FadeOut path); `Audio/TagAudio.cpp:66-90` (GLevels becomes 4);
  `UI/GridironOverlay.cpp:2143-2149`.
- **Depends on:** nothing.
- **Verify:** `run_test.sh` with `-TagAudioRec` at Master 0. The recording should be digital silence (peak below -90 dBFS), where today the voice is expected to remain. At Commentary 2/4, the voice RMS drops by 6 dB ±0.5.

### A2. Drive the stands' animation from the home crowd's mood (S)
- **Build:** replace the director's `Stadium->Hype = 1.f` writes (`MatchDirector.cpp:3813, 3841, 3946-3947`, `MatchKicking.cpp:456`) with a value fed from a home-centric energy in `ATagAudio`.
  - Use `Excite` and `Swell`, not `DNoise`: DNoise is noise, not cheering.
  - Visiting scores leave the stands seated.
  - Keep the intro's scripted `Hype = 1` (line 608).
- **Audible/visible:** fans jump when the building roars and stay seated when the building groans. Today they celebrate visitor touchdowns.
- **Where:** `Audio/TagAudio.h:92-93`; `Render/Stadium.cpp:1221, 1274-1284`; the director's Hype writes.
- **Depends on:** nothing.
- **Verify:** add `Hype` to the `-TagAudioLog` line. In a CPU game:
  - On every away touchdown, Hype stays below 0.15 while GroanBig plays.
  - On every home touchdown, Hype is 1 within 0.3 s of CheerBig.
  - Count mismatches per game: the target is 0.

### A3. Broadcast-style dynamic mix: louder excited calls, a duck that depends on the line, and the crowd over the voice on big plays (S)
- **Build:**
  - (a) Apply a per-energy playback gain in `TagBooth`: about -2 dB for e0, 0 for e1, +2 for e2, +4 for e3, under a -1 dBFS ceiling. No cache rebuild is needed, because the energy is in the line.
  - (b) Replace the fixed 0.74/0.55 duck with one keyed on the current line:
    - analysis, story or e0-e1: crowd beds about -5 dB and music about -8 dB;
    - e2 live calls: crowd beds about -2 dB;
    - e3 calls, and whenever Excite > 0.8: no duck, so the crowd blooms around the call (T18's "announcers drop 6-10 dB against the crowd").
  - (c) Duck the crowd and PA *one-shots* too: apply the duck at `PlayNow` for `ECat::Crowd` and `ECat::Stadium`, and track the active ones so the duck can ride them.
- **Audible:**
  - Calm analysis is clear over a softer bed.
  - "Touchdown!" lands inside a roar instead of on top of a hole.
  - The game feels dynamic rather than "too even" (T18).
- **Where:** `Booth/TagBooth.h` (expose the current line's energy and kind), `TagBooth.cpp:396-400`; `Audio/TagAudio.h:35` (pass energy and kind in the view, not a bool); `TagAudio.cpp:320-343, 1026-1027, 1197-1198`; `MatchDirector.cpp:8451`.
- **Depends on:** A4 to confirm the levels.
- **Verify:** log the applied voice gain and duck per line. Record a 10-minute CPU game and measure the short-term loudness (3 s) of the master in two windows: during e0-e1 lines and during e3 lines. Expect:
  - voice-dominant windows to sit about 4-6 LU above the bed-only windows nearby;
  - e3 windows to sit within 2 LU of the roar-only windows (stems from A10 make this exact);
  - a listening check on two touchdowns.

### A4. Loudness harness and targets for the whole mix (S)
- **Build:** a `Tools/Audio/loudness.py` that uses **pyloudnorm (MIT)**, or the existing `audiolib.lufs` after checking it against pyloudnorm, on `Saved/AudioRec/tag_audio.wav`. It reports:
  - integrated LUFS;
  - LRA (EBU 3342);
  - 4x-oversampled true peak (scipy `resample_poly`);
  - the share of time voice is active (from the `-TagBoothLog` timestamps).

  Targets: **-24 ±2 LUFS integrated, ≤ -1 dBTP** (T21). Record LRA as a baseline; don't target it yet. If the summed mix runs hot, add a master limiter (UE Submix Effect Dynamics Processor in limiter mode, created in C++ on the main submix) with a -1 dBTP ceiling.
- **Audible:** TAG plays at the same loudness as other console games: no sudden peaks, no reaching for the remote.
- **Where:** `Tools/Audio/`; master limiter in `ATagAudio::BeginPlay` (`TagAudio.cpp:119-181`).
- **Depends on:** nothing. Use `-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0` as in `memory/game-audio.md`.
- **Verify:** three 10-minute recordings (seeded CPU games) all fall within -24 ±2 LUFS with true peak ≤ -1 dBTP. Commit the numbers as a baseline.

### A5. Voice every live call: bake all 32 rosters offline, and fall back to a cached generic call on a miss (S-M)
- **Build:**
  - (a) A `BoothScript --bake-all` mode that writes the `rosterCalls()` lines for every team (generics are already baked) and feeds them to `booth_tts.py --once` offline, Whisper-checked.
    - Rebake one player when Create-a-Player saves.
    - The cache key already makes this incremental (`TagBooth.cpp:126-138`).
    - Scale (estimate): about 250 lines per team × 32 teams ≈ 8,000 short clips, so hours of idle GPU, run once.
  - (b) Give each live `Line` an `alt` generic text with the same tag: "Caught!" for a named catch, "Pressure!" for a named sack.
    - `TagBooth`, on a live line whose wav is not `Present`, swaps to `alt` if that is present, instead of waiting 0.3 s and showing a caption.
- **Audible:** every catch, sack and pick is *heard*, from the first game of any matchup. Today a cold matchup can lose 15-40% of lines to silent captions.
- **Where:** `Tools/BoothScript.cpp`, `Booth/Booth.h:40-54` (`alt`), `Booth.cpp:1206-1227`, `TagBooth.cpp:445-470`.
- **Depends on:** nothing. Run the bake when the GPU is idle.
- **Verify:** three cold-cache soaks on matchups never played before (clear only the per-match prewarm, not the bake). The share of live lines tagged `[voice]` in `-TagBoothLog` should be at least 98%. Time from event to voice start, logged as `Core->now()` at push minus at `Voice->Play`: p95 under 150 ms.

### A6. Remember across games what the booth has said (S)
- **Build:**
  - Persist per-family use counts and last-heard match index for `pick()` banks and story and analysis families in `Saved/BoothVoice/heard.json`.
  - `pick()` then prefers the least recently heard option across the last N matches: NHL's "all variants before repeat" (T1), across games.
  - Live-call `rot()` stays as it is, for the cache.
- **Audible:** the second and third games of a session stop replaying the first game's lines.
- **Where:** `Booth/Booth.cpp` `pick()`, `storyLine()` and the `once()`/`cooled()` users; load and save in `TagBooth.cpp` BeginPlay/EndPlay (BoothScript keeps it in memory across `--games`).
- **Depends on:** nothing. It pays off more after A7.
- **Verify:** BoothScript runs 5 games back to back with the same teams and different seeds. Measure the share of non-live sentences in game k already heard in games 1..k-1. Take a baseline first; the target is at least a 50% reduction.

### A7. Offline LLM authoring of the template banks and exchange pairs (M)
- **Build:**
  - A one-time authoring job (Claude, with Fred's existing key) writes 8-12 variants per `pick()` bank and 30-60 PBP→analyst exchange pairs per topic family. The output is slot-based text: `{qb}`, `{yds}` and so on.
  - Every variant passes the BoothScript FACT CHECK and a reviewer agent before it lands in a data file the Booth loads.
  - This moves the LLM from *per game* (paused over cost) to *per content drop*, and removes runtime cost, latency, GPU use and live factual risk. Rough cost estimate: a few dollars per pass. Not measured.
  - Exchanges are written as pairs whose answer's energy and wording fit the question (T4's "recorded together" effect).
- **Audible:** more variety and real back-and-forth, the gap the reviewers scored 2-3/10.
- **Where:** `Booth/Booth.cpp` (`pick()` banks and `exchange()` read from a data table); a new `Tools/Booth/author_banks.py`.
- **Depends on:** A6, so the larger banks actually rotate.
- **Verify:**
  - BoothScript on 6 seeds: 0 FACT CHECK contradictions.
  - Distinct-sentence ratio per game up (take a baseline first).
  - Reviewer brief (memory: 7 categories): chemistry score at least +2 against the current transcript.
  - Talk repeats still under 5%.

### A8. Sound for on-screen contacts (S)
- **Build:**
  - In `DetectContacts()`, for contacts within about 12 yd of the ball or camera focus, call `Audio->Cue("contact", Pos, Impulse)`.
  - Map the impulse to a quiet pad clack or body thud (existing HitLight and HitMed banks plus BodyFall). Gain should follow the impulse but sit 6+ dB under tackle hits (T19: hits sit under crowd and voice).
  - Skip contacts in the 0.3 s after a sim tackle or bighit event (`P.BigHitT`) and cap at about 4 per 0.5 s.
- **Audible:** blocks, collisions in traffic and pile-ups sound in sync with the picture. Today the pocket and piles are mute except for random trench thuds (`TagAudio.cpp:712-716`).
- **Where:** `Game/MatchDirector.cpp:6955-6993`; `Audio/TagAudio.cpp:973-985` (new cue).
- **Depends on:** nothing.
- **Verify:**
  - Log contacts against contact sounds per play: no more than the cap, and every sound within 1 frame of its contact.
  - Record a goal-line run and a sack and listen.
  - Check that the voice limit is not hit: `au.Debug` / `stat audio` voice count stays under the platform max during a pile.

### A9. Deeper reaction banks and rotating bed takes (S-M)
- **Build:**
  - Extend the procedural recipes in `build_crowd.py` and `build_field.py` with new seeds to 12-16 takes for CheerBig, CheerMed, CheerSmall, Groan, GroanBig, Boo, Gasp, AwayCheer, HitHeavy and HitHuge.
  - Build 3 takes per crowd bed layer.
  - In `TickLoops`, crossfade to another take every 2-4 minutes, at a quiet moment, with a second component per layer.
- **Audible:** no recognisable cheer every few minutes and no audible 60 s bed cycle (T13: Madden used about 50 hours).
- **Where:** `Tools/Audio/build_crowd.py:205-260`, `build_field.py`; `TagAudio.cpp:372-395`.
- **Depends on:** nothing. All sources are already CC0 (`Tools/Audio/CREDITS.tsv`).
- **Verify:**
  - From `TAG audio summary`, uses per take per full game should be 3 or fewer for every crowd reaction bank. Measure the baseline first: about 4-6 takes today.
  - Spectral self-similarity of a 10-minute bed-only recording: no autocorrelation peak at the loop length.

### A10. Submix graph, stems and a Broadcast/Field mix choice (M)
- **Build:**
  - Create submixes in C++ (Voice, Crowd, Field, Stadium/PA, Music, UI) under a master with the A4 limiter, and assign them on components and `PlaySound*` calls.
  - Optionally move the A3 duck onto a Submix Dynamics Processor keyed by the Voice submix. Keep the code duck if UE-125147 bites.
  - Add a Settings option, Broadcast / Field / Custom (T23). Field lifts the field submix about 4 dB and turns the booth down; Broadcast is the A3 mix.
  - `StartRecordingOutput` accepts a submix, so each stem can be recorded.
- **Audible:** a player can choose "on the field" or "on TV". The stems make every later tuning measurable.
- **Where:** `Audio/TagAudio.cpp:119-211, 320-343`; `Booth/TagBooth.cpp:45-49`; settings UI.
- **Depends on:** A1, A3, A4.
- **Verify:** record stems for 10 minutes, then:
  - per-stem LUFS;
  - voice-to-crowd ratio during e0-e1 lines of about +6 LU (T18);
  - changing the preset shifts the field stem by the set amount (±0.5 dB).

### A11. Referee announcements with numbers and the down; stitched through the PA (S)
- **Build:**
  - Bake fragments with the ref voice: "number" plus 0-99, "offense", "defense", "the penalty is declined", "offsetting fouls", "replay the down", "repeat first/second/third/fourth down", "automatic first down".
  - Assemble them from the penalty log ("Holding, number seventy-two, offense. Ten yard penalty. Repeat second down.").
  - The PA reverb and slap-back hide the stitch seams; T3 uses stitching for exactly this kind of slot.
- **Audible:** sounds like an NFL white hat at the mic (memory, high confidence on NFL practice), instead of a generic line.
- **Where:** `Tools/Audio/build_voices.py:36-57`; `TagAudio.cpp:864-883` (the parse already extracts the name, so add the number and the enforcement).
- **Depends on:** the penalty log carrying the number and the down result; check `Sim/Game.cpp`'s penalty text.
- **Verify:** an offscreen run with 10+ penalties. Whisper-transcribe each assembled announcement and compare it with the log: 0 word errors. The gap between fragments should be 0.15-0.35 s.

### A12. Word-boundary cuts, interjections and overlap between the two voices (M)
- **Build:**
  - (a) `booth_tts.py` already runs faster-whisper (MIT) for QC. Have it also write word end-times (`word_timestamps=True`) beside each wav.
    - When `cutCurrent()` fires, `TagBooth` fades at the next word end within 250 ms, or at once for prio 7 swing calls.
  - (b) Bake a small bank of short reactions per man: "Oh!", "Wow.", "Yeah.", "Mm-hm.", laughs and chuckles, using Turbo's native `[laugh]`/`[chuckle]` tags (Chatterbox repo).
    - Play them on a **second** `UAudioComponent` overlapping the last 0.3-0.5 s of the other man's line after big results and story answers.
    - Use them as lead-ins when a live call cuts the analyst.
- **Audible:** fewer chopped words, and the booth sounds like two people in a room rather than turn-taking reads (T4).
- **Where:** `Tools/Booth/booth_tts.py:227-258`; `Booth/TagBooth.cpp:355-373`; `Booth.cpp:265-275` (expose a cut deadline); new `Who`-tagged reaction lines.
- **Depends on:** A1 (voice gain path), A3.
- **Verify:**
  - Share of cuts landing mid-word (cut time against the word list): under 10%. Measure the baseline first.
  - Overlaps per game: 10-25.
  - Reviewer chemistry score.
  - No overlap ever covers a live call: assert in BoothScript.

### A13. Take TTS off the game's GPU for in-match lines (M, evaluation first)
- **Build:**
  - Try Chatterbox-**Nano** (MIT, 110M, "3x faster than realtime on 8 CPU cores", same tags as Turbo) as `--engine nano` for prio 1-2 dynamic lines (setups, results with yards and score).
  - Keep Turbo for offline bakes (A5, A11, A12) and for the energy reference takes.
  - Note: the Chatterbox repo is now *retired and unmaintained*. Pin the wheel and vendor it; the MIT licence allows that.
- **Audible:** fewer caption-only dynamic lines while the game holds its frame rate.
- **Where:** `Tools/Booth/booth_tts.py:144-258`; `TagBooth.cpp:176-179` engine flag.
- **Depends on:** A5 (most live calls cached, so dynamic lines are the only in-match synthesis).
- **Verify:**
  - `bench.ps1` fps with the booth on: within 3 fps of no-booth (memory baseline 111.7).
  - Whisper WER on 200 lines: 2% or less.
  - Energy-3 lines still judged excited in an A/B listen against Turbo.
  - Share of Setup and Result lines voiced (not caption-only) in a 10-minute soak: 90% or more.

---

## 4. Not worth doing (now)

- **A live LLM writer per game.** It was measured at $0.35-1.80 per game on Opus, or slips facts on Haiku; it adds
  seconds of latency and competes for GPU or CPU (memory). A7 gets the variety offline with fact checks at authoring time.
- **Full-sentence neural TTS for everything, live, on the GPU.** It costs 8-15 fps today (memory). Industry practice
  synthesizes only the slots (T3), and buffering ahead (T11) is already in place.
- **IndexTTS2, Fish S2, VibeVoice, Higgs v3.** Non-commercial or permission-only licences (T12). **Dia/Dia2**
  (Apache) is appealing for one-pass two-speaker banter, but voices drift unless prefix-prompted and the clips must
  match the existing Chatterbox voices. Revisit only for pre-rendered exchanges after A7, as a listening test.
- **Cloning any real broadcaster, PA voice or referee,** or "sound-alike" prompts. This is out of scope by the brief
  and a likeness risk. The Kokoro-seeded original voices are the right base.
- **Per-stadium convolution reverb and dome/open variants.** The Sim has no domes or roofs (grep), and the bowl IR is
  already baked into the crowd and PA. Do this only if stadium types arrive.
- **DICE-style HDR audio.** It was built for the 130 dB swings of a shooter (T22). A sports broadcast is deliberately
  narrow and ridden by an A1 (T18). A3 plus a limiter covers the need.
- **The Soundscape plugin.** Its palettes are for ambience spawned as the player moves through a world (1.5). A
  stadium bed driven by game state is what `ATagAudio` already does.
- **Quartz.** Nothing in TAG needs beat-synced triggers. House music and stings are free-running.
- **Rebuilding the crowd in MetaSounds now.** The C++ layer model works, and MetaSound graphs are editor assets; the
  gain over A9 is small. (The C++ MetaSound Builder API was not checked: the search budget ran out.)
- **College band and fight songs.** TAG is a pro league. EA had a pro band record 180 songs (T14), which is not
  feasible here anyway.
- **Season or franchise story threads (T7).** There is no season mode to read from (2.3). Design the Booth's story
  memory so it can take season facts later; A6's persistence file is the natural place.
- **Synthesized DE-FENSE chant, attempt three.** Two attempts already tagged as robotic (memory). It needs a real
  group recording; see Open questions.
- **Surround and emitter crowd placement (T15).** Real, but audible only on 5.1 or spatial headphones, at M-L
  effort. Revisit after A10.

---

## 5. Open questions

1. **Voice limit.** Is the default (reported as 32) actually hit during piles, with 6 loops, 2 music decks, house,
   booth, footsteps and hits? Not run. Check `stat audio` in a goal-line recording before A8.
2. **The current mix's real loudness, LRA and true peak** are unknown; A4 establishes them. Fred has apparently not
   listened critically yet (memory), and his ears are the final judge.
3. **`audiolib.lufs`** was not checked against a reference BS.1770 meter.
4. **How Madden, 2K and FC handle interruption and trail-off.** No technical source found (T8).
5. **2K's actual pipeline.** No GDC talk or technical write-up was found. The "recorded together" explanation rests on
   interviews and reviews (T4), and is the best-supported inference, not a documented fact.
6. **Chatterbox-Nano's excited delivery** (energy 3) is unverified. The repo documents speed, not expressiveness.
   A13 is an evaluation, not a commitment.
7. **A DE-FENSE chant and other group chants** need real voices. A free option: Fred and a few friends recording
   on a phone, layered and processed the way EA does with staff volunteers (T14). Whether Fred wants that is his call.
8. **The penalty log** may not carry the fouling player's number or the down result in a parseable form (A11
   dependency); not checked in `Sim/Game.cpp`.
9. **The NFL PA practice** of naming ball carriers and tacklers varies by stadium (memory, medium confidence). A
   stitched home-PA play call could follow A11, but its value is uncertain.
10. **Unchecked for lack of search budget:** recent Madden commentary complaints (to rank which repetition players
    notice most) and the MetaSound Builder C++ API.
