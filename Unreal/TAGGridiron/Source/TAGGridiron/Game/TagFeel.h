// The feel options and their numbers (Game/MatchFeel.cpp plays them): Settings > Movement (Authentic / Responsive),
// Settings > Hit effects (the hit-stop and the camera's shake), Settings > Switch assist (the stick left alone for a
// beat after a switch), each kept in GameUserSettings [TAG.Settings]. The A/B flags for the comparisons are here too.
//
// RESPONSIVE MOVEMENT - the numbers to tune are FTagResponsiveTune below (one place). Authentic is the sim's own steering
// law (PlaySim::steerVel), fitted to tracking data, and stays exactly that: it is the default and nothing changes unless
// Responsive is chosen. Responsive applies to the men local players drive, and only on the steps their stick drives
// them; the CPU's men and every CPU-vs-CPU game run the sim's law untouched (the director never touches a man no
// player has). It is done in the sim: before each step the director gives each player's man his setting
// (PlaySim::setUserSteer), and PlaySim::steerVel steers that man by it on the steps his stick drives him, never faster
// than the sim's own law would have him or than he was (top speed is the sim's).
//
// Measured with a copy of the law (the -TagFeelTest check prints the table; 06-controls-feel-ui.md item 6), a man at
// full speed with the stick turned 90 deg, ms until he heads 45 deg off / stick reversed, ms to a stop:
//                        Authentic    Responsive (the defaults below)
//   80/80/80 back        700 / 917    550 / 833
//   95/95/95 elite       750 / 950    533 / 867
//   60/60/60 lineman     650 / 867    567 / 783
// (The report's first guess - lateral +20 %, braking +10 %, 0.06 per agility point - only reaches 667 ms: at full speed
// the whole push's grip ceiling binds before the lateral limit, so Responsive lifts that ceiling a little too.)
#pragma once

#include "CoreMinimal.h"
#include "Sim/PlaySim.h"

/** Responsive movement's numbers. Each is applied on top of the sim's own term and never takes it below the sim's. */
struct FTagResponsiveTune
{
	/** The lateral (cornering) limit at a jog and at speed, x this. */
	double LatMul = 1.20;
	/** Braking, x this. */
	double BrakeMul = 1.10;
	/** At speed, the lateral limit's yd/s^2 per agility point over 50 (the sim: 0.03 - an elite back turned no quicker
	 *  in degrees than a lineman). */
	double LatAgiAtSpeed = 0.09;
	/** The whole push's ceiling (speeding up, braking and cornering together), x this ... */
	double GripMul = 1.04;
	/** ... and its yd/s^2 per agility point over 50 (the sim: 0.03). */
	double GripAgi = 0.09;
};

namespace TagFeel
{
	// ---- the settings (GameUserSettings [TAG.Settings]; -TagMenuAudio, the scripted menu walk, saves nothing)
	/** Settings > Movement: false Authentic (the default), true Responsive. -TagMovement=responsive|authentic overrides it. */
	bool Responsive();
	void ToggleMovement();
	const TCHAR* MovementName();
	/** Settings > Hit effects: the hit-stop and the camera's shake on big hits (On by default). */
	bool HitFx();
	void ToggleHitFx();
	/** Settings > Switch assist: after a switch the new man's stick is left alone for a beat (On by default). */
	bool SwitchAssist();
	void ToggleSwitchAssist();
	/** The self-check's settings, whatever the player has saved (each -1: his own; 0 off / Authentic, 1 on / Responsive). */
	void SetTestOverrides(int32 InResponsive, int32 InHitFx, int32 InSwitchAssist);

	// ---- the A/B flags
	/** -TagThrowOnRelease: the old pass timing (a tap waits 0.22 s for a second tap, a bullet goes on the release or at
	 *  0.3 s, the windup only then). */
	bool ThrowOnRelease();
	/** -TagSwitchOld: the old switch (the man nearest the ball, at once, no grace, no live hold-switch). */
	bool OldSwitch();
	/** -TagNoHitStop (or Hit effects Off): no hit-stop. */
	bool HitStopOn();
	/** -TagOldShake: the old camera shake (per-frame random jitter on the camera's position). Hit effects Off: none. */
	bool OldShake();

	/** The Responsive numbers in use (FTagResponsiveTune's defaults; -TagResponsive=LatMul=1.3,GripMul=1.1,... sets them). */
	const FTagResponsiveTune& ResponsiveTune();
	/** Responsive's numbers as the sim takes them (PlaySim::setUserSteer). */
	tag::SteerBoost ToSim(const FTagResponsiveTune& T);
	/** The sim's steering law (PlaySim::steerLaw) with Responsive's numbers on it, uncapped (bNeutral: the sim's own law).
	 *  The -TagFeelTest check holds PlaySim::steerVel to it: the sim's law for a CPU man and for a stick-less step, and
	 *  this capped to the sim's speed for a player's man on a step his stick drove. */
	tag::V2 Steer(const tag::Ent* E, const tag::V2& Vel, const tag::V2& Want, double Dt, const FTagResponsiveTune& T, bool bNeutral);

	/** The time a man of these ratings, at full speed (Half: half speed), takes to turn 45 deg with the stick turned 90
	 *  (bStop false), or to come to a stop with it reversed (bStop true), ms at 60 Hz; < 0 never within 10 s. Steps with
	 *  Steer (bNeutral: the sim's law) or, given Sim, the sim's own PlaySim::steerVel. */
	double TurnMs(double Speed, double Accel, double Agility, bool bStop, bool bHalf, const FTagResponsiveTune& T, bool bNeutral, const tag::PlaySim* Sim = nullptr);
}

/** The camera's shake (Eiserloh's trauma): a hit adds trauma (0..1), it drains away over about half a second, and the
 *  shake is trauma squared driving smooth noise on the camera's pitch, yaw and roll (and a few cm of position) - not
 *  random jitter every frame. A touchdown's is a slow sway. The angle is per lens: a long lens magnifies it. */
struct FTagTrauma
{
	float Trauma = 0.f;
	/** 0..1: how much of what is on now is the slow sway (a touchdown) rather than a hit's quick shake. */
	float Sway = 0.f;
	/** The noise's clock (real seconds: it runs on through a hit-stop). */
	double T = 0.0;
	void Add(float Amount, bool bSway);
	/** Advance by RealDt and the shake now: rotation (deg) and position (cm) offsets for a lens of FocalMm. */
	void Tick(float RealDt, float FocalMm, FRotator& OutRot, FVector& OutPos);
	bool Active() const { return Trauma > 1e-3f; }
};

/** One player's feel controls between frames: the throw's windup and the switch's grace. The director keeps one
 *  (FeelCtl); in a multiplayer game each player's is swapped in while his input is read (SwapHuman), so it is per player. */
struct FTagFeelCtl
{
	// ---- the throw on the press (AMatchDirector::FeelThrow)
	int32 WindIdx = -1;              // the receiver's button whose windup is on (-1: none)
	double WindT0 = 0.0;             // platform s of the press
	bool bWindReleased = false;      // the button has come up (before 0.15 s: a tap, the lob / touch)
	double WindReleaseT = 0.0;
	double WindUpAt = -1.0;          // platform s the sim started the windup (-1: not yet)
	int32 WindStep0 = 0, WindUpSteps = -1; // the director's sim steps at the press; press to windup in steps
	int32 WindStyle = 0;             // the pass as it stands: 0 bullet, 1 lob / touch, 2 high, 3 low
	unsigned long long WindSerial = 0;
	double OldPressT = -1.0;         // -TagThrowOnRelease: the press, for the press-to-windup count
	int32 OldStep0 = 0;
	// ---- the switch (AMatchDirector::FeelSwitch)
	unsigned long long SwitchSerial = 0;
	const tag::Ent* GraceWho = nullptr; // the man switched to, his stick left alone ...
	double GraceUntil = -1.0;        // ... until then (platform s) ...
	tag::V2 GraceStick{0, 0};        // ... unless the stick turns 45 deg from where it was (or is let go)
	int32 GraceFrames = 0;           // frames the stick was held off him (this grace)
	bool bHeld = false;              // the switch button held since its press: the stick picks a man that way
	bool bArmed = false;             // ... once it has come back to the middle (or turned 45 deg)
	tag::V2 HeldStick{0, 0};
};

/** A hit-stop: the world held for a few sim steps' worth of real time on a big hit (AMatchDirector::StartHitStop). The
 *  sim's steps are only delayed - none is dropped or reordered - so nothing it computes changes. */
struct FTagHitStop
{
	bool bOn = false;
	double Until = -1.0;             // platform s it lets go
	double Began = 0.0;
	int32 Steps = 0;                 // how long, in sim steps (60 Hz)
	int32 StepsDuring = 0;           // sim steps taken while it was on (must stay 0)
	int32 Count = 0, Skipped = 0;    // stops made / asked for with it off (-TagNoHitStop, Hit effects Off)
	// a big hit seen in the sim's events, waiting for the bodies to meet on screen (or 0.2 s)
	double ArmedAt = -1.0;
	int32 ArmA = -1, ArmB = -1, ArmSteps = 0;
	float ArmTrauma = 0.f;
	FString ArmWhy;
	// the event scan
	unsigned long long Serial = 0;
	size_t EvSeen = 0;
	double LastHitT = -9.0;          // the sim's last tackle / big hit / truck / wrap, seen (platform s): contact sounds keep off it
};

/** The last switch and the last pass, as the self-check (-TagFeelTest) reads them. */
struct FTagSwitchNote
{
	unsigned long long Serial = 0;
	int32 Mode = 0;                  // 1 the ball in the air (the catch point), 2 a run (the carrier), 0 the sim's nearest-the-ball, 3 the stick's way
	const tag::Ent* From = nullptr;
	const tag::Ent* To = nullptr;
	double Score = -1.0, Best = -1.0; // his time to get there (s), the best of every man who could
	const tag::Ent* OldPick = nullptr; // the man the old rule (nearest the ball) would have taken
	double OldScore = -1.0;
	tag::V2 Stick{0, 0};
	int32 Count = 0;
};

struct FTagThrowNote
{
	unsigned long long Serial = 0;
	int32 Style = -1;                // 0 bullet, 1 lob / touch, 2 high, 3 low; -1 none
	bool bPump = false;
	int32 WindupSteps = -1;
	double WindupMs = -1.0, ReleaseMs = -1.0;
	bool bBallLob = false;
	int32 Count = 0, Pumps = 0;
	// the windup's throw clip as the ball left (SyncPlay): picked by the pass type (standing, clear of the rush), the type it
	// was picked for, its key, and whether it was switched in the windup (AAthlete::RetimeThrow)
	bool bClipStand = false, bClipTouch = false, bClipSwitched = false;
	int32 ClipKey = -1;
};
