# 03 — Rendering, characters and environment

Researcher 3 of 7. Scope: everything on screen that is not motion (players, uniforms, field, stadium, crowd, lighting, weather, image pipeline). Written 2026-11-23.

**Notes on sources.** Web search and page fetches worked for most of this report. The search budget ran out near the end, so one claim is from memory and is marked **[memory, unsourced]**. "Source states" means the linked page says it. "Inference" means it is my reading. I did not build, launch or edit anything in the project. The current-state evidence comes from the code, the config and frames already on disk:

- `D:\TAGFullGame\to18\frames\` was filmed 2026-11-21 at 1280x720, fixed step 30 fps, `-TagWeatherSeed=7`. The command line in `to18\run.log` has no `-TagDay`, so it is a **night** game, and the log says **snow, 23 F**.
- `D:\TAGFullGame\brk\frames\` holds the break crane shot. Same seed, same night and snow.

Every recent film and the standard bench (`Tools/Perf/bench.ps1` pins seed 7) use this same night-snow setting. Nobody has been looking at a dry day game in the films.

---

## 1. What the best studios do

### 1.1 Characters and uniforms

| Technique | What the source says | Link | Confidence |
|---|---|---|---|
| Body-type variation | EA's SAPIEN rebuilt the Madden 24 skeleton "for increased definition and variation of physiques". | [EA 2024 tech innovations](https://www.ea.com/news/2024-ea-sports-latest-tech-innovations) | High that it exists. No detail on method. |
| Jersey cloth simulation | FC 24 shipped Frostbite's compute-shader cloth for jerseys and shorts, with "drastically increased cloth mesh density" and body collisions. | same page | High |
| Animated jersey wrinkles and fit | Madden 26: "much more realistic, animating wrinkles and jersey twisting" and "a much tighter, and more realistic fit" for flak jackets and back plates. | [Madden 26 presentation deep dive](https://www.ea.com/ar/games/madden-nfl/madden-nfl-26/news/madden-26-gridiron-notes-presentation-deep-dive) | High that it exists. Method not stated. |
| Strand hair | Frostbite strand hair was used in earlier FC titles and is still being upgraded. | [Frostbite: strand hair in Veilguard](https://www.ea.com/technology/news/strand-hair-dragon-age-the-veilguard), [Frostbite hair rendering](https://www.ea.com/news/frostbite-hair-rendering-and-simulation) | High |
| Uniforms degrade during a game | CFB 27: "Enhanced player visual uniform degradation". Madden 13 notes an improved "degradation system for both natural grass and snow". Bleacher Report's Madden 16 review: uniform degradation "is really cool in games with rain or snow". MLB The Show 25 patch: "dirt will display on pants more consistently". | [CFB 27 presentation](https://www.ea.com/games/ea-sports-college-football/news/college-football-27-presentation), [Madden 13 presentation](https://www.ea.com/news/madden-nfl-13-presentation), [Bleacher Report Madden 16](https://bleacherreport.com/articles/2548777-madden-16-stadiums-uniforms-and-player-models-review), [MLB The Show 25 update 3](https://theshow.com/news/mlb-the-show-25-game-update-3/) | High that every major sports series does this. The accumulation logic is not published. |
| Rain and snow on gear | A Madden 21 presentation deep dive described "rain and snow impacts specifically on helmets and jerseys where it affects the fabrics and sideline floors". | [EA Madden 21 deep dive](https://www.ea.com/en-us/news/gridiron-notes-madden-nfl-21-next-level-gameday-presentation-deep-dive) (the page now returns 404; quote taken from the search snippet) | Medium |
| Sweat that builds with effort | NBA 2K26's "Dynamic Sweat": each player's sweat level is "proportional to the amount of physical movement". | [ESPN on NBA 2K26](https://www.espn.co.uk/gaming/story/_/id/45925459/nba-2k26-graphics-myplayer) | Medium (press, not a dev talk) |
| Wet surfaces | Lagarde's recipe: albedo × lerp(1, 0.2, porosity) × wetness, gloss raised by the same factor at half weight, no darkening on metals or smooth surfaces, puddles flatten the normal, specular dries faster than diffuse (~1 min). | [Lagarde, Water drop 3b](https://seblagarde.wordpress.com/2013/04/14/water-drop-3b-physically-based-wet-surfaces/) | High. Widely used, cheap and shader-only. |
| Layered helmet paint | UE's Clear Coat model is for "multilayer Materials that have a thin translucent layer of film", with an optional second normal for the layer under the coat (car paint). | [UE shading models](https://dev.epicgames.com/documentation/en-us/unreal-engine/shading-models-in-unreal-engine) | High |
| Eyes | Epic: "Building an eye Material on your own is not recommended without extensive experience". Extract it from the Digital Humans sample instead. | same page | High |

### 1.2 Field

- **Field degradation and weather that build over a game.** CFB 27 has "Rain and snow intensifying over time", "Snow can pile up" and "New field degradation". The stated goal: "We wanted late game conditions to feel earned." ([CFB 27](https://www.ea.com/games/ea-sports-college-football/news/college-football-27-presentation)). Madden 26 says it "completely overhauled not just the visuals, but also the effects of weather" and added heavy snow and rain with fog ([Madden 26 welcome](https://www.ea.com/games/madden-nfl/madden-nfl-26/news/gridiron-notes-welcome-to-madden-26)). Confidence high that this is the current bar. Neither source explains how it is done.
- **Deformable snow.** Batman: Arkham Origins renders feet, hands and bodies from below with an ankle-high orthographic camera into a heightmap at runtime, filters and accumulates it, then displaces or shades the snow from it ([GDC Vault](https://www.gdcvault.com/play/1020177/Deformable-Snow-Rendering-in-Batman), [slides](https://www.slideshare.net/colinbb/gdc2014-deformable-snow-rendering-in-batman-arkham-origins)). High confidence. The same idea works as a coarse field-wide "trample / wear" render target.
- **Procedural grass blades.** Ghost of Tsushima generates and animates each blade on the GPU ([GDC Vault](https://gdcvault.com/play/1027214/contactUs)). The blade-level details I found are from secondary write-ups, so medium confidence on those. TAG already does a close-up version of this.
- **Surface split.** In 2026, 16 of 30 NFL stadiums are natural grass and 14 synthetic, with FieldTurf the most common ([LawnStarter](https://www.lawnstarter.com/blog/sports-turf/nfl-stadiums-turf-or-grass/)). Synthetic turf throws black rubber infill when players cut and tackle. That is my own observation from broadcasts; I found no source for it. Medium confidence.

### 1.3 Stadium, crowd and sidelines

- **Crowd at scale uses vertex animation textures (VAT) and impostors, not skeletal meshes.**
  - City Sample's background crowd is static meshes with vertex animation in the material, baked with the AnimToTexture plugin. Skeletal meshes are swapped in only up close ([Epic forum tutorial](https://forums.unrealengine.com/t/community-tutorial-animtotexture-plugin-how-to-use-it-to-make-vertex-animation-textures-for-crowds/746648)).
  - Pixomondo's UE soccer spot ran 18,000 real-time crowd agents with "bone animation textures" in the vertex shader ([Unreal spotlight](https://www.unrealengine.com/spotlights/pixomondo-gives-viewers-a-kick-with-new-virtual-production-based-soccer-commercial)).
  - The geometry-near / impostor-far hybrid is classic research ([Dobbyn et al., Geopostors](https://history.siggraph.org/?p=113932)). SideFX has a talk on VAT for crowds ([SideFX](https://www.sidefx.com/learn/talks/vertex-animation-textures-for-crowds/)).
  - High confidence that VAT is the standard UE route. Epic staff recommended VAT for large crowds in the forum threads, per search snippets (medium).
- **Crowd states and density are marketed features.** CFB 27 lists "Crowd density", "New crowd excitement animations", bibs and banners ([CFB 27](https://www.ea.com/games/ea-sports-college-football/news/college-football-27-presentation)).
- **Boards and edge walls carry live graphics.** Madden 26: "motion graphics that appear on the scoreboards and ribbon boards", and "Stadium edge walls and field arts are being aligned with specific uniforms" ([presentation deep dive](https://www.ea.com/ar/games/madden-nfl/madden-nfl-26/news/madden-26-gridiron-notes-presentation-deep-dive), [welcome](https://www.ea.com/games/madden-nfl/madden-nfl-26/news/gridiron-notes-welcome-to-madden-26)). High confidence.
- **Sideline life.** Madden 26 puts mascots on the sidelines during play and gives coaches signature sideline looks (same pages). High confidence.

### 1.4 Lighting and image (the "TV look")

- **Broadcast cameras.** Network sports cameras are 2/3-inch, B4-mount systems such as the Sony HDC series ([Sony HDC](https://pro.sony/en_DJ/products/4k-and-hd-camera-systems/hdc-series), [Broadcast Bridge](https://www.thebroadcastbridge.com/content/entry/6605/studio-and-field-cameras-for-uhd-production?cat_id=10)). They use box lenses up to 122x: the Canon UHD-DIGISUPER 122 is 8.2–1000 mm, f/1.7 to 340 mm, f/5.0 at 1000 mm, with a 0.55° field of view at the long end ([Canon specs](https://canon-europe.com/lenses/uhd_digisuper_122/specifications)). Canon markets a "shallow depth of field" look for live sports from these lenses ([Canon broadcast lenses](https://global.canon/en/technology/canon-tech/tech/bclenses/)). The HDC-2500 offers shutters of 1/60 s through 1/2000 s ([Sony HDC-2500](https://pro.sony/en_GB/pdf/products/4k-and-hd-camera-systems/hdc-2500)).
  - Inference, high confidence: close-ups on TV are shot from far away on long lenses. Backgrounds look compressed (big, close and blurred), not the wide-angle perspective of a camera standing next to the player.
- **Exposure is held by a person.** Video operators ("shaders") at the camera control units match cameras and "adjust iris, gamma, black levels, and color balance across multiple camera feeds to maintain a cohesive on-air look" ([job postings summarized here](https://www.entertainmentcareers.net/espn/video-operations-engineer/job/524827/), [vision control article](https://www.gtc.org.uk/media/fm/Zerb%20articles/vision-control.pdf)). Medium-high confidence.
  - Inference: TV exposure changes slowly and deliberately. It does not pump like a game's eye adaptation.
  - Broadcast cameras compress highlights with a "knee" circuit, so white jerseys keep their folds instead of clipping **[memory, unsourced]**.
- **Stadium light levels.** Top events light the pitch to roughly 1,500–2,000 lux, flicker-free for slow motion ([Signify](https://www.assets.signify.com/is/content/Signify/Assets/signify/global/20260820-signify-blog-stadium-lighting-design-myths.pdf), [UEFA flicker guidance](https://documents.uefa.com/r/UEFA-Stadium-Lighting-Guide-2023-2023/Flicker-factor-guidance-Online)). Inference: at night the field is far brighter than the stands and sky. The bowl falls off into darkness and the light banks read as glaring sources.
- **Motion blur and frame rate.** UE's Motion Blur Target FPS makes blur "independent of actual frame rate and relative to the specified target FPS" ([UE API](https://docs.unrealengine.com/4.26/en-US/API/Runtime/Engine/Engine/FPostProcessSettings/MotionBlurTargetFPS/index.html)). Forum users report the default target is 30 ([forum](https://forums.unrealengine.com/t/motion-blur-is-wrong-by-default/137777)). Medium confidence on the default.
- **Temporal AA and ghosting.**
  - TSR trades ghosting against flicker. Thin-geometry detection relaxes history rejection for sub-pixel detail. A "Temporal Responsiveness" material node exists for translucent ghosting ([TSR doc](https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine), [thin geometry](https://dev.epicgames.com/documentation/en-us/unreal-engine/thin-geometry-detection-with-temporal-super-resolution), [FAQ](https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-frequently-asked-questions-for-unreal-engine)).
  - Users report fast small objects smearing under TSR ([TSR feedback thread](https://forums.unrealengine.com/t/tsr-feedback-thread/883977?page=4)).
  - NVIDIA's UE plugin supports DLSS 4.5 Super Resolution, DLAA and Ray Reconstruction. Its transformer model is credited with less ghosting ([NVIDIA DLSS](https://developer.nvidia.com/rtx/dlss)). Medium confidence on how this compares for a football specifically; nobody has published a test.

### 1.5 What UE5 gives a solo developer, and what it costs

| Feature | What the source states | Link | What it means for TAG |
|---|---|---|---|
| Lumen | Console budgets are 4 ms (High, 60 fps) and 8 ms (Epic, 30 fps) at 1080p for GI, reflections and volumetric fog. | [Lumen performance guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine) | Already paid for in TAG's 120+ fps. Keep it. |
| Virtual shadow maps | "Any light movement or rotation will invalidate all cached pages for that light." Skinned and WPO geometry invalidates every frame. "Multiple local lights will perform far better with Nanite geometry." | [VSM doc](https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine) | A moving sun must step, not glide. TAG's bowl is a non-Nanite procedural mesh, which hurts shadowed night lights. |
| MegaLights | Experimental. Requires hardware RT. Roughly constant cost regardless of light count; all lights can cast shadows. Each light can use RT or VSM shadows. | [MegaLights doc](https://dev.epicgames.com/documentation/en-us/unreal-engine/megalights-in-unreal-engine) | TAG keeps skinned meshes out of the RT scene for performance, so RT-shadowed MegaLights would not shadow the players. Not now. |
| Nanite skinned meshes | Epic staff, Aug 2026: "The state hasn't changed all the much… most of the other items are still missing", progress hoped by Q1 2027. | [forum](https://forums.unrealengine.com/t/status-of-skinned-meshes-and-metahuman-and-nanite-support/2835575) | Do not plan the crowd or players on it. |
| Substrate | Production-ready and on by default as of 5.7. One user reports about 1 ms from its subsurface scattering and anisotropy. | [5.7 notes](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-7-release-notes), [forum report](https://forums.unrealengine.com/t/substrate-performance-concerns-questions/2683717) | See the open questions. The legacy Clear Coat, Subsurface Profile and Cloth models cover TAG's needs. |
| ML Deformer | About 15,000 frames of training data with the default settings. The neural morph model runs on the CPU. | [ML Deformer](https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-use-the-machine-learning-deformer-in-unreal-engine) | Needs a simulated-cloth dataset per kit. Not for a solo developer. |
| Sun Position plugin | Computes sun from latitude, longitude, date, time and zone. Can be changed at runtime (Blueprint documented). | [Sun position doc](https://dev.epicgames.com/documentation/en-us/unreal-engine/geographically-accurate-sun-positioning-tool-in-unreal-engine) | The math is small enough to port into C++. |

---

## 2. Where TAG Gridiron stands

### What it already does well (verified in code)

- **Turf material** `M_TagTurf2` (`Unreal/TAGGridiron/Tools/Art/ue_build_turf.py`, used at `Render/Stadium.cpp:388-478`). It is one world-mapped material for:
  - mowing stripes that change with view direction (lines 147-154)
  - paint that lies on the blades
  - a static season-wear texture `Wear` × `WearAmount`, which thins grass to dirt
  - rain (darker, deeper green, a sheen rather than a mirror)
  - snow that is "trampled away where the game is played", using the same static wear map
  - derivative-filtered border lines (`fwidth`, line 100)

  Close-up 3D blades (`BuildGrass3D`, ~90k blades) wrap around the shot's subject in the vertex shader. They cost nothing when hidden and about 0.4 ms of GPU when shown (memory note `handoff-catch-grass-batch.md`). This is better than most indie turf.
- **Character materials** (`Tools/Art/hero_import.py:283-645`):
  - Subsurface Profile skin, with a per-tone roughness/specular/metal (RSM) map swapped per player (`Athlete.cpp:2381-2417`) and pack pore detail normals
  - jersey wrinkle normal plus an in-house knit detail normal
  - helmet vent and hardware normal on UV1
  - packed gear maps tinted per player, cleat decals, team stripe masks
  - numbers projected from pre-skinned position, including TV numbers on the shoulders (`Athlete.cpp:2418-2452`)
  - hair hidden under helmets (`HideHair`)
- **Crowd.** 1,951 pack fans as skeletal meshes in the front rows: 3 sideline rows, 8 end-zone rows and 3 corner rows (`Stadium.cpp:601-693`). They tick at about 30 Hz each (`Stadium.cpp:1105-1113`), stay out of ray tracing, use fixed bounds, and cheer or clap with `Hype` and a staggered delay (`UpdateHeroCrowd`, `Stadium.cpp:1271-1296`). Every 24th seat is a story character and 15% hold props.
- **Lighting rig.**
  - SkyAtmosphere, a real-time sky light, volumetric clouds by day, and height fog with volumetric fog at night or in precipitation (`Stadium.cpp:710-743`).
  - At night, 12 LED banks on the roof edge, 6 of them shadowed, at 5700 K, with sizes scaled by throw distance (`Stadium.cpp:745-797`).
  - Night exposure is pinned at EV 4.5 and day auto exposure is held to EV 0.6–2.0 (`Stadium.cpp:799-811`).
  - Lumen with hardware ray tracing, VSM, TSR (`r.AntiAliasingMethod=4`), local exposure 0.8/0.8, and skinned meshes out of the RT scene (`Config/DefaultEngine.ini:7-29`).
- **Sideline.** Pack photographers, broadcast crews, coordinators and sideline players (`Stadium.cpp:961-1035`). Chain rods and a down box (`Stadium.cpp:1503-1555`).
- **Performance headroom** (memory `perf-100fps.md`):
  - user game 121–125 fps at 1080p, about 112 at 1440p
  - the render thread (~8 ms) is the bound, with GPU about 8 ms at an earlier bench (game/draw/GPU 7.46/8.30/8.14 ms)

  **Inference:** at the 100 fps target (10 ms) there is roughly 1–2 ms of GPU and very little render-thread time left for everything in this report combined, less at 1440p. Prefer work that adds shader math, GPU particles or render-target stamps over work that adds components or draw calls.

### Where it falls short (evidence)

1. **Weather does not read on screen.** In the snow film `to18`, frames `c013_00300`, `c024_00790`, `c004_01000` and crane `brk/c001_00900` show no falling snow and no snow on the playing surface. Only the apron greys.
   - Precipitation is a column of tiny opaque cubes (`BuildRain`, `Stadium.cpp:1333-1377`): 9,000 snowflakes 3 cm across, or 12,000 rain streaks 1 cm × 60 cm, each laid twice. The column spans 160 × 120 × 60 yd, about 0.02 flakes per m³.
   - Inference: at broadcast distance a 3 cm flake is about 1 px, so the density cannot read.
   - The memory note already flags "night-rain grey veil is the rain fog, snow turf reads grey".
   - Players carry no rain or snow at all: no wetness, no snow on shoulders, no breath vapor, even though `tempF` exists in `tag::Weather` (`Sim/PlaySim.h:199-204`).
2. **The night game does not read as night.** In `brk/c001_00900` (night) the upper deck and roof are lit about as evenly as day, and no light bank is in frame.
   - The painted crowd material's self-glow is 0.07 at night against 0.08 by day (`Stadium.cpp:311`). With exposure pinned, the stands look the same in both.
   - Players show one dominant shadow, not the multi-bank shadow fan of a night broadcast (`c004_01000`).
3. **The field-level wall fills the top of every TV frame with flat colour.** The bowl's padding is a plain 2.2 m team-colour sweep, `TagColorMat(HomeC*0.6)` (`Stadium.cpp:245, 319`). In `c013_00300` and `c024_00790` it is a featureless blue band across the top 10–40% of the frame. Benches are coloured cubes (`Stadium.cpp:700-707`).
4. **Uniforms stay pristine all game.** The source has no dirt, grime, stain, wet or sweat system: a grep of `Source/` for Dirt/Grime/Wet/Sweat finds only the turf. `PaintHero` sets team colours only (`Athlete.cpp:2370-2562`), and the hero master has no dirt input (`hero_import.py`).
5. **Field wear is fixed for the whole game.** `Wear` is a static texture and `WearAmount` is set once at build (`Stadium.cpp:421-456`). In the crane frame the wear is one long uniform brown band between the hashes.
6. **Image pipeline.**
   - Vignette 0.35 and film grain 0.04 are on (`Stadium.cpp:822-825`). Inference, medium confidence: neither belongs in a TV image.
   - White jerseys clip to flat white in close and overhead shots (`c026_00740`, `c002_00500`).
   - Fast legs leave visible ghost trails in close shots (`c002_00500`). The cause is not isolated: TSR history, motion blur 0.15, or the 30 fps fixed-step capture, which makes temporal artifacts larger than live play at 120 fps.
   - No DLSS plugin is in the project (`TAGGridiron.uproject`).
7. **One sun position.** Day versus night is a single switch, and the sun is fixed at pitch −48°, yaw 135° (`Stadium.cpp:712`). There are no kickoff windows, no low late-afternoon sun, no stadium shadow line and no dusk transition.
8. **Video boards are text.** The end-zone boards draw two `UTextRenderComponent` lines on a dark LED material (`Stadium.cpp:327-356`). The ribbon boards only scroll colours. There are no motion graphics or replays.
9. **Close-up lens.** Close shots use 50 mm at f/1.2 on a 36 mm sensor, from close range (`Game/MatchDirector.cpp:7514-7519`; the game camera is 24–35 mm at f/5.6). This gives wide-angle perspective with paper-thin focus: a "cinematic", not "broadcast", look. (Inference, high confidence, from the lens facts in 1.4.)
10. **Smaller gaps.**
    - The helmet shell is Default Lit with roughness 0.16 and metal 0.15. There is no clear coat (`hero_import.py:589-592`).
    - Eyes are Default Lit with roughness 0.08 (`hero_import.py:557-558`).
    - Body variation is 4 pack builds by weight plus a uniform height scale of 0.93–1.08 (`Athlete.cpp:2233, 2287`).
    - The midfield mark reads as a flat disc with letters in the crane shot (`brk/c001_00900`). That is logo art, outside this scope.
    - Rows past 8 (end) and 3 (sideline) are a static painted texture.
    - Niagara is linked in `TAGGridiron.Build.cs:12`, but nothing in `Source/` uses it.

---

## 3. Actions (ranked by player-visible impact per unit of effort)

Frame costs marked "est." are my estimates and have not been measured. Measure every action with `Tools/Perf/bench.ps1` (seed 7) and, for weather, `-TagWeather=3/4`. Compare like with like; the memory notes say weather alone swings results ±10%.

### A0. Prerequisite: a fixed "look matrix" capture (S, 0 ms)

**What to build.** One script that films the same short game under four settings: day dry, day rain, night dry and night snow. Use `-TagHighlights -TagHlFull -TagHlMaxClips=6 -UseFixedTimeStep -FPS=30 -TagWeatherSeed=<n>`, with `-TagDay` for the day runs. Also capture one `-TagLineup` close-up set per setting. Then crop the same frame numbers into a contact sheet.

**Why.** Every recent film is night snow (seed 7, no `-TagDay`), so the day look is unwatched. Every action below verifies against this sheet. Capture at 1440p as well, because flakes and lines that vanish at 720p may read on Fred's monitor.

### A1. Broadcast grade and white-jersey highlights (S, ~0 ms)

**What to build.** In the stadium post-process block (`Render/Stadium.cpp:799-826`):
- Set vignette and film grain to 0.
- Lower the tonemapper's shoulder and white clip (`FilmShoulder`, `FilmWhiteClip`), and/or lower `LocalExposure.HighlightContrastScale` from 0.8, so white jerseys keep their fold shading.
- Re-check night `NightEV` 4.5 against white kits.
- Keep the narrow day exposure range. It matches a shader holding the iris.

**Visible result.** White uniforms show folds and pad shapes instead of flat white. The picture edges no longer darken, and there is no grain. It looks like a TV feed, not a film.

**Dependencies.** A0.

**Verify.** On matrix frames that hold white kits (today's `c026_00740` and `c002_00500` equivalents), measure the share of jersey pixels with any channel ≥ 250 in 8-bit before and after. Target: most of the jersey below clip, folds visible. Show Fred a side-by-side, since this is partly taste.

### A2. Weather you can see: a camera-local precipitation volume (M, est. 0.2–0.6 ms GPU, saves the 18–24k cube instances)

**What to build.** Replace `BuildRain`'s field-wide cube column with the same trick `BuildGrass3D` already uses:
- One static mesh of about 40–60k camera-facing quads, wrapped in the vertex shader (`frac`) into a box of about 40 × 40 × 25 m around the camera's subject. It falls with time and drifts with `Wind`.
- Rain streaks are stretched along the fall direction plus camera motion. Snow flakes get a minimum on-screen size, so a flake never drops below about 1.5 px at broadcast distance.
- Fade by distance into the existing height fog. Thicken the fog with intensity to get the far "veil".
- For close shots, add a second, sparser layer of large near flakes or drops.
- Niagara (already linked) is the alternative if Fred will author one system in the editor. The material route keeps everything in C++ and Python, the way the turf and grass are built.
- Couple intensity to a per-game ramp, so it starts light and gets heavier. CFB 27 does this.

**Visible result.** Rain and snow games look like rain and snow from the TV and gameplay cameras, not like a dry night.

**Dependencies.** A0. Pairs with A5 and A6.

**Verify.**
- In the night-snow matrix film, the frames that match today's `to18/c013_00300` and `brk/c001_00900` show falling snow at 720p and 1440p.
- `bench.ps1 -TagWeather=4` and `-TagWeather=3` stay within +0.6 ms of today. The memory note's snow soak is 115 fps.

### A3. Make night read as night (S, ~0 ms)

**What to build.**
- At night, drop the painted crowd's `Glow` well below the day value (`Stadium.cpp:311`), and darken the upper deck and roof underside. Let the spill from the light banks light the lower bowl.
- Make the lamp housings read as glaring sources: emissive is already 60 at night, so raise bloom on them, or add a restrained lens-flare setting for shots that look up at them.
- Check why only one shadow per player reads when 6 banks are shadowed. Bank intensities or VSM resolution may let one bank dominate. Consider giving 4 shadowed banks per side lower intensity each, but budget the extra VSM local lights (see A14).

**Visible result.** A bright field under a dark bowl with a black sky. Glaring banks. A faint fan of shadows around feet.

**Dependencies.** A0. Gets cheaper after A14.

**Verify.** On the night matrix crane frame (today's `brk/c001_00900` equivalent), measure the mean luminance of the stands region relative to the field region. Target the stands clearly darker than the field: my guess is at most half, judged against real night-broadcast reference frames used for comparison only. Bench the night dry setting before and after.

### A4. Field-level wall and sideline dressing (S for the wall, M with props; ~0 ms)

**What to build.**
- Replace the plain padding sweep (`Stadium.cpp:245, 319`) with segments using the existing `LED()` material: scrolling team colours, the team wordmark, and fictional "TAG" sponsor panels.
- Keep a dark padded band below the LEDs, add tunnel mouths at the 50 and the corners, and colour the wall per uniform, as Madden 26 does.
- Replace the cube benches (`Stadium.cpp:700-707`) with proper benches and sideline props: coolers, kicking net, heaters in cold games, tent. Use pack items or licensed marketplace props.

**Visible result.** The band across the top of every TV frame, today a flat blue slab, becomes a lit, branded stadium edge.

**Dependencies.** None. A10 shares the board graphics.

**Verify.** Matrix frames that match `to18/c013_00300` and `c024_00790`. Bench at the play-call camera.

### A5. Turf that changes over the game (M, est. ≤0.1 ms)

**What to build.**
- Turn `M_TagTurf2`'s `Wear` input into a `UTextureRenderTarget2D` of about 1024×512 over the 170 × 100 yd plane, which is about 15 cm per texel. Seed it at build by drawing the current static wear texture into it. Use R for wear, G for trampled snow, B for mud.
- Each snap, stamp the line-of-scrimmage band: linemen's feet and hands, a few texels deep.
- Stamp tackle and pile spots (from the director's contact and tackle events), and in snow, coarse trample lanes along player paths every few frames. Use `UKismetRenderingLibrary::DrawMaterialToRenderTarget` or a canvas.
- Ramp `Snow` and `Wet` with game time while it is snowing or raining, so snow piles up off the trampled lanes and lines get re-dusted between plays.
- The blade world-position-offset code already reads `Wear`, so close-up blades thin out on worn spots for free.
- Batman: Arkham Origins is the reference for the method.

**Visible result.** By the fourth quarter the hashes and the busy yard lines are scuffed. Snow games show white piled on the field with dark trampled channels where the plays ran.

**Dependencies.** A0. A2 for snow fall.

**Verify.**
- Crane frames at Q1 and Q4 of the same seeded film.
- Export the render target to a PNG at game end for inspection.
- Confirm no extra cost in `bench.ps1`.

### A6. Uniforms and skin that get dirty, wet and snowy (M, ~0 ms; 3–5 days)

**What to build.** Add to the hero master's colour code (`hero_import.py` `COLOR_HLSL` and the `SCALARS` list, rebuilt with `TAG_HERO_MATS=1 TAG_HERO_SKIP_MESH=1`):
- **Regions:** five dirt accumulators (knees and shins, elbows and forearms, chest and front, back and seat, helmet). The masks come from the pre-skinned position the material already uses for numbers.
- **Look:** a tiling in-house grime noise texture. The colour follows the surface: grass-green streaks, mud brown, or snow-wet.
- **Rain:** Lagarde-style wetness. Cloth darkens and its gloss rises, porosity is high on cloth and zero on the helmet shell and visor, and drying takes about a minute after the rain stops.
- **Snow:** a world-normal-up mask that collects white on shoulder tops and helmet tops.
- **Sweat:** skin gloss from the existing `Exertion` value (`Athlete.cpp:5436`).
- **Code:** a new `AAthlete::AddGrime(region, amount)` called from:
  - tackles and falls (face-down get-up → front; face-up → back)
  - dives and catches to the ground
  - pile-ons
  - a small per-snap increment for linemen's knees and hands

  Keep the totals per player id for the whole game.

**Visible result.** Linemen and backs are visibly stained by the fourth quarter. Rain games darken jerseys. Snow sits on shoulders between plays.

**Dependencies.** A5 sets the surface colour (grass, mud, snow). The material gotcha in `native-anim-fidelity.md`: check the log for "Failed to compile Material" after every master change.

**Verify.**
- A debug flag that forces full grime, checked across a `-TagHeroGallery=Kit` pass, so no stain lands on numbers or bare skin wrongly.
- Then the same lineman cropped at Q1 and Q4 in a matrix film.
- `hero_item_check.py` still passes.

### A7. Real kickoff times, a moving sun and the stadium shadow line (S–M, ~0 ms if stepped)

**What to build.**
- Replace the fixed sun (`Stadium.cpp:710-718`) with a kickoff slot (early, late or night) and a date by week. Give each team a latitude, longitude and stadium orientation.
- Compute the sun in C++. NOAA's formula is about 30 lines; the Sun Position plugin is the Blueprint equivalent.
- Advance the sun with the game clock in steps (one per dead ball or about every 60 s), never per frame, because VSM invalidates the light's whole cache on any rotation.
- Late games cross into dusk: lights fade up, sky intensity and exposure ride down slowly, as a shader would.

**Visible result.** Long late-autumn shadows. The bowl's roof casts a hard shadow line across the field that moves during the game. Golden 4 pm light. No two day games look the same.

**Dependencies.** A0. Night pieces from A3.

**Verify.**
- `-TagDay` late-slot matrix film: frames at kickoff and in Q4 show the shadow line moved. Log the sun elevation.
- Bench for one-frame spikes when the sun steps.

### A8. Anti-aliasing A/B: tuned TSR versus DLSS/DLAA (S–M, likely saves 1–3 ms GPU at 1440p, est.)

**What to build.**
- Add NVIDIA's DLSS UE plugin, which supports DLSS 4.5 Super Resolution and DLAA. Its UE 5.8 build availability is unverified.
- Film a fixed-step pass play with close shots and compare:
  - TSR default
  - TSR with `r.TSR.ShadingRejection.SampleCount` and `r.TSR.Velocity.WeightClampingSampleCount` lowered
  - DLAA
  - DLSS Quality at 1440p
- Run one comparison at 60 or 120 fps fixed step, because the 30 fps film exaggerates temporal ghosting.
- Keep TSR as the fallback.

**Visible result.** Less smearing behind legs and the football. If upscaling is used, GPU budget is freed to pay for A2, A5 and A10.

**Dependencies.** None.

**Verify.** Ghost-trail length behind a fast receiver's legs and the ball in matched frames (pixel diff against the next frame), plus `bench.ps1` fps for each mode.

### A9. Helmet clear coat, visor and water beads (S, est. <0.05 ms)

**What to build.**
- Give the `Shell` slot a Clear Coat master. Under the coat: team paint, which can be matte, gloss or metallic flake per team, with the vent normal kept. The coat: roughness about 0.03–0.06 with its own normal.
- In rain, add a scrolling droplet normal on the coat, driven by A6's wetness.
- The visor stays as it is.
- Create the master as a third one in `hero_import.py` (`master()` with the `MSM_CLEAR_COAT` shading model), assigned when `role == "Shell"`.

**Visible result.** Helmets catch crisp highlights of the light banks over a softer paint layer. Matte helmets become possible. Beaded water appears in rain close-ups.

**Dependencies.** A6 for wetness.

**Verify.** `-TagLineup` helmet close-ups before and after. `hero_item_check.py` (149 helmets). "Failed to compile Material" absent from the log.

### A10. Breath vapor in cold games (S, est. <0.05 ms)

**What to build.** When `tempF` < about 45, each player near the camera puffs a short-lived, soft translucent plume on the exhale phase of the existing `Breath` signal (`Athlete.cpp:5436`), from the head bone. Use a Niagara sprite emitter, or a pooled puff mesh with a dissolve material if staying asset-free. Plumes are bigger and more frequent with `Exertion`. Sideline people and the officials do the same.

**Visible result.** The cold-game signature in pre-snap close shots, huddles and post-play shots.

**Dependencies.** None.

**Verify.** A `-TagWeather=4` lineup or close-up capture, plus a bench in snow.

### A11. Live video boards and ribbon motion graphics (M, ~0 ms for graphics; a replay feed costs extra)

**What to build.**
- Draw the end-zone boards and ribbon boards from a `UCanvasRenderTarget2D`, redrawn only on events: team logos, down and distance, "FIRST DOWN", "TOUCHDOWN", crowd prompts, player cards.
- During replays and breaks only, feed the board from a low-resolution, low-rate `SceneCapture2D` (for example 640×360 at 15 Hz). Never during live play: the render thread is the frame bound.

**Visible result.** Stadium boards and ribbons react to the game, as Madden 26 does.

**Dependencies.** A4 shares the panels.

**Verify.** Crane and break frames before and after. Bench with the capture on during a replay window.

### A12. Broadcast-lens close shots (S, ~0 ms; overlaps the presentation researcher)

**What to build.** Move the close-up and cut-in cameras back, to 20–60 m from the subject, and use long full-frame-equivalent focal lengths (150–600 mm). Use f-stops nearer f/2.8–5.6 than f/1.2. On TV the compression and the distance do the background separation. The constants are at `MatchDirector.cpp:7514-7519`, plus the camera placement code.

**Visible result.** Close-ups look like a sideline camera's: a large compressed crowd behind the player and natural perspective on faces and helmets, not a wide-angle "game cinematic".

**Dependencies.** The camera framing and occlusion checks the presentation work already has (`CloseupTarget`).

**Verify.** Matched post-play close-ups before and after. The background crowd's on-screen size is a simple measurable proxy.

### A13. VAT crowd past the skeletal rows (L, est. 0.5–1.5 ms GPU, few draw calls)

**What to build.**
- Bake the pack crowd-rig fans' idle, clap, cheer and stand clips (`hero_retarget.py` already makes them) to vertex animation textures, using the engine's AnimToTexture plugin or a Blender bake. Use 16-bit float position textures.
- Place them as instanced static meshes on rows 4 and up and the upper deck, replacing the painted `T_Crowd` sweep.
- In the material: per-instance random phase, team-colour tint and section state (seated, standing on 3rd down, cheering, dejected away fans) from per-instance custom data. Update the custom data only when hype changes.
- Far LOD is a 2–4 quad impostor.

**Visible result.** A moving crowd in the crane, kickoff, break and gameplay-camera shots, which today show a static painted bowl above row 8.

**Dependencies.** Look at A3 first, because night darkening hides much of the upper bowl.

**Verify.** Crane frames, plus bench and Insights (`Tools/Perf/trace.ps1`) for render-thread and GPU cost against today's 1,951-fan baseline.

### A14. Bake the procedural bowl to a static Nanite mesh (M, perf enabler)

**What to build.** Bake the procedural bowl mesh (`BuildBowl`) to a static asset per stadium design with an editor commandlet, and turn on Nanite.

**Visible result.** None directly. VSM shadow caching and local-light shadows get cheaper (Epic's VSM doc), which pays for A3's extra shadowed banks. It also removes part of the double stadium build the perf note lists (6–9 s loading screen).

**Dependencies.** None.

**Verify.** Night dry bench and Insights local-light shadow time. Loading-screen time from the log.

### A15. Turf spray on cuts and tackles (S–M, est. <0.1 ms)

**What to build.** Small GPU particle bursts at foot plants on hard cuts and at tackle impacts: black rubber infill on synthetic fields, torn grass and soil on natural grass, powder in snow, spray in rain. Plant moments are already measured (`FTagRetargetProxy::MeasurePlants`). Add a surface type per team.

**Visible result.** Mostly close-ups and replays.

**Dependencies.** Per-team surface data.

**Verify.** Replay frames from a matrix film.

---

## 4. Not worth doing (now)

- **Chaos Cloth on jerseys.** There are 304 kit builds, each needing a cloth section and painted weights. Jerseys are tight over pads and players are small at TV distance. FC and Madden have dedicated cloth teams. A pose-driven wrinkle-normal blend is the cheap later option if close-ups demand it.
- **ML Deformer.** Needs about 15,000 frames of ground-truth deformation per garment from an offline simulation (Epic doc). There is no art pipeline for that.
- **Strand grooms.** Helmets hide hair for 95% of screen time, and grooms cost GPU on every visible coach.
- **Baked lighting.** The stadium is built at runtime, A7 wants a moving sun, and Lumen is already inside the frame budget.
- **Nanite skinned meshes or MetaHuman crowds.** Epic says most pieces are still missing (Aug 2026). Skinned VSM pages invalidate every frame regardless.
- **MegaLights for the light banks.** Experimental, hardware-RT shadows, and TAG keeps skinned meshes out of the RT scene for a measured ~4 ms. Players would lose shadows from those lights unless each light uses VSM, which brings back the cost it was meant to avoid. Revisit after A14.
- **More skeletal fans.** Measured: 8 rows all round = 106 fps, 12 = 92 fps (memory `tv-fidelity-goal.md`). VAT (A13) is the way to more crowd.
- **Whole-field geometric grass, shell or fin grass.** From the TV camera the material already carries grain and stripes. The 3D blades exist where they read (close-ups).
- **Converting to physical light units.** No visible gain. Exposure is already pinned and ranged by hand.
- **A custom eye shader.** Epic advises against building one. Eyes are a few pixels under facemasks.
- **A per-frame scene-capture video board.** It re-renders the scene on the render thread that is already the bound. Graphics plus capture-only-in-replays (A11) gets most of the effect.

---

## 5. Open questions

1. **Is Substrate on?** `DefaultEngine.ini` has no `r.Substrate` line, and Substrate is "enabled by default as of 5.7". Check the startup log or `r.Substrate` in the console. If it is on, the legacy models in `hero_import.py` are being converted, which bears on A9 and the reported ~1 ms cost.
2. **Real headroom by setting.** The fps figures above are 1080p, mostly with seed 7 snow. I have no measured GPU or render-thread split for night-dry and day-rain at 1440p, which is what A2–A13 must fit in.
3. **Does a DLSS plugin build exist for UE 5.8?** NVIDIA's page says DLSS 4.5 is in its UE plugin, but I could not confirm 5.8 support.
4. **The ghosting cause** in `c002_00500`: TSR history, motion blur 0.15 at the default 30 fps target, or the 30 fps fixed-step capture. Not isolated.
5. **Which camera Fred plays on.** The gameplay camera sees the end-zone stands and upper deck much more than the TV camera does, which moves A13 up or down.
6. **Crowd clips.** Does the pack hold more crowd clips than idle, cheer, clap and kneel (boo, seated-dejected, towel wave, stand-and-clap on 3rd down)? That decides A13's reaction states.
7. **HDR.** Is Fred's monitor HDR? An HDR output path is a separate look decision.
8. **The broadcast knee claim** in 1.4 is from memory; the search budget ran out before I could source it.
9. **Logo and branding art.** The midfield and end-zone art in the crane shot reads as a placeholder. That is a branding job, not rendering, but it shows in every crane shot.
