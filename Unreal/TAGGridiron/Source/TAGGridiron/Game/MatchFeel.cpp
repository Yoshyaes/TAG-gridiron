// The feel of the controls between the press and the sim: the move buffer (Game/TagMoveBuffer.h: a move pressed a beat
// before the sim will take it fires when it can, for 150 ms; one that runs out unused greys the player's marker) and the
// rumble's events (Game/TagRumble.h: which player's man a sim event was, and what he feels of it). Then
// (06-controls-feel-ui.md 3, 5, 7, 8, 10; 05-audio-commentary.md A8):
//   - the pass started on the press (FeelThrow): the windup as the receiver's button goes down, the pass type and a pump
//     fake settled during it (-TagThrowOnRelease: the old timing);
//   - the switch that follows the ball (FeelSwitch): the catch point, the carrier, the stick held with the button, and the
//     new man's stick left alone for a beat (-TagSwitchOld: the sim's nearest-the-ball switch);
//   - Responsive movement for the players' own men (FeelBeforeStep / FeelAfterStep, Game/TagFeel.h);
//   - the bodies meeting (Game/TagContact.h, found by DetectContacts): their sound, and on a player's big hit the
//     hit-stop and the camera's shake (-TagNoHitStop, -TagOldShake);
//   - the latency log's presses (Game/TagLatency.h).
#include "Game/MatchDirector.h"
#include "Game/TagRumble.h"
#include "Game/TagLatency.h"
#include "Audio/TagAudio.h"

#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sim/Playbook.h"
#include "UI/GridironOverlay.h"
#include <algorithm>
#include <functional>

namespace
{
	bool MoveBufferOff()
	{
		static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("TagNoMoveBuffer"));
		return bOff;
	}

	/** The move's name on the marker's "too early" pill. */
	FString MoveLabel(tag::Move M)
	{
		switch (M)
		{
		case tag::Move::Truck: return TEXT("TRUCK");
		case tag::Move::StiffArm: return TEXT("STIFF ARM");
		case tag::Move::Spin: return TEXT("SPIN");
		case tag::Move::Hurdle: return TEXT("HURDLE");
		case tag::Move::Hit: return TEXT("HIT STICK");
		case tag::Move::Cut: return TEXT("CUT");
		default: return TEXT("JUKE");
		}
	}

	/** How long the grey pill stays (it fades over this). */
	constexpr double RefusedShow = 0.7;

	/** The sim's step (the director steps it at 60 Hz). */
	constexpr double kFeelStep = 1.0 / 60.0;
	/** A receiver's button let go within this of its press is a tap: the lob / touch pass (Madden's). */
	constexpr double kFeelTapSecs = 0.15;
	/** After a switch the stick as it was does not steer the new man for this long (Switch assist). */
	constexpr double kFeelGraceSecs = 0.25;
	/** A yard in cm. */
	constexpr float kFeelYd = 91.44f;

	const TCHAR* FeelPassName(int32 Style)
	{
		switch (Style)
		{
		case 1: return TEXT("lob / touch");
		case 2: return TEXT("high");
		case 3: return TEXT("low");
		default: return TEXT("bullet");
		}
	}

	/** The receivers' buttons by their labels 1..5 (ReadUserInputSlot's Pads). */
	const FKey& FeelPad(int32 i)
	{
		static const FKey Pads[5] = {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top,
			EKeys::Gamepad_RightShoulder};
		return Pads[FMath::Clamp(i, 0, 4)];
	}

	/** The angle between two stick directions (deg; 0 when either is all but nothing). */
	double FeelAngleDeg(const tag::V2& A, const tag::V2& B)
	{
		const double La = tag::len(A), Lb = tag::len(B);
		if (La < 1e-6 || Lb < 1e-6) return 0.0;
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(tag::dot(A, B) / (La * Lb), -1.0, 1.0)));
	}

	FString FeelRole(const tag::Ent* E) { return E ? FString(UTF8_TO_TCHAR(E->role.c_str())) : FString(TEXT("-")); }
}

// ------------------------------------------------------------------ the move buffer
void AMatchDirector::TakeMovePresses()
{
	if (!Sim || MoveBufferOff()) return;
	tag::PlaySim& P = *Sim;
	const int32 Slots = FMath::Min(P.userSlots(), static_cast<int32>(UE_ARRAY_COUNT(MoveBufs)));
	for (int32 s = 0; s < Slots; ++s)
	{
		FTagMoveBuf& B = MoveBufs[s];
		tag::UserInput& In = P.slotInput(s);
		const tag::Ent* U = P.slotEnt(s);
		if (B.Serial != P.serial)
		{
			// a new play: the last one's count, then nothing kept
			if (B.Asked > 0)
				UE_LOG(LogTemp, Display, TEXT("TAG move buffer slot %d, last play: %d moves asked, %d taken (%d of them late, from the buffer), %d ran out unused"),
					s, B.Asked, B.Taken, B.Late, B.Expired);
			B = FTagMoveBuf();
			B.Serial = P.serial;
		}
		if (U != B.Who)
		{
			B.Drop(); // (another man: what was pressed for the last one is not his)
			B.Who = U;
		}
		if (!U) continue;
		// this frame's presses: into the buffer (a new press of a kind replaces the one waiting), out of the input - the
		// buffer hands them to the sim on each step (OfferBufferedMoves)
		if (In.stickMove != tag::Move::None)
		{
			B.Move = In.stickMove;
			B.Side = In.stickSide;
			B.MoveLeft = FTagMoveBuf::Steps;
			B.MoveAge = 0;
			++B.Asked;
			In.stickMove = tag::Move::None;
		}
		if (In.juke)
		{
			// (SPACE is the juke with the ball and the dive without it: only a carrier's is a juke)
			if (U->hasBall)
			{
				B.JukeLeft = FTagMoveBuf::Steps;
				B.JukeAge = 0;
				++B.Asked;
			}
			In.juke = false;
		}
		if (!In.rushMove.empty())
		{
			B.Rush = In.rushMove;
			B.RushLeft = FTagMoveBuf::Steps;
			B.RushAge = 0;
			++B.Asked;
			In.rushMove.clear();
		}
		if (In.dive)
		{
			if (U->side == tag::Side::Def) B.DiveLeft = FTagMoveBuf::Steps; // (a dive is a defender's: a tackle's reach, a kick's block)
			In.dive = false;
		}
	}
}

void AMatchDirector::OfferBufferedMoves()
{
	if (!Sim || MoveBufferOff()) return;
	tag::PlaySim& P = *Sim;
	const int32 Slots = FMath::Min(P.userSlots(), static_cast<int32>(UE_ARRAY_COUNT(MoveBufs)));
	for (int32 s = 0; s < Slots; ++s)
	{
		FTagMoveBuf& B = MoveBufs[s];
		if (B.Serial != P.serial || !B.Who || !B.Pending()) continue;
		tag::UserInput& In = P.slotInput(s);
		// a switch on this step hands the slot another man: the moves were for this one
		if (In.switchReq || P.slotEnt(s) != B.Who)
		{
			B.Drop();
			continue;
		}
		if (B.MoveLeft > 0)
		{
			In.stickMove = B.Move;
			In.stickSide = B.Side;
		}
		if (B.JukeLeft > 0) In.juke = true;
		if (B.RushLeft > 0) In.rushMove = B.Rush;
		if (B.DiveLeft > 0) In.dive = true;
		B.PreMoveT = B.Who->moveT;
		B.PreLastMoveT = B.Who->lastMoveT;
		B.PreTackleCd = B.Who->tackleCd;
	}
}

void AMatchDirector::SettleBufferedMoves()
{
	if (!Sim || MoveBufferOff()) return;
	tag::PlaySim& P = *Sim;
	const double Now = FPlatformTime::Seconds();
	const int32 Slots = FMath::Min(P.userSlots(), static_cast<int32>(UE_ARRAY_COUNT(MoveBufs)));
	for (int32 s = 0; s < Slots; ++s)
	{
		FTagMoveBuf& B = MoveBufs[s];
		tag::UserInput& In = P.slotInput(s);
		// (what was handed over is the buffer's: none of it left lying in the input; the director clears the juke and the
		// stick's move after the step itself)
		In.rushMove.clear();
		In.dive = false;
		if (B.Serial != P.serial || !B.Who || !B.Pending()) continue;
		const tag::Ent* U = B.Who;
		const bool bLive = P.isLive();
		const bool bHeld = P.ball.mode == tag::BallMode::Held;
		const bool bCarrierOk = bLive && U->hasBall && bHeld && U->state != tag::EntState::Down;
		const bool bTacklerOk = bLive && U->state != tag::EntState::Down && bHeld && P.ball.holder && P.ball.holder->side != U->side;
		// a press the sim took on this step: startMove restarts the man's moveT (0 after the step) with the move on him
		auto Took = [&](const FString& What, int32 Age)
		{
			++B.Taken;
			if (Age > 0)
			{
				++B.Late;
				UE_LOG(LogTemp, Display, TEXT("TAG move taken from the buffer: %s, %.0f ms after the press (slot %d)"), *What, Age * 1000.0 / 60.0, s);
			}
		};
		// a press the sim never took: the grey pill if the move could still have come (not a man tackled meanwhile)
		auto RanOut = [&](const FString& What, bool bCould)
		{
			++B.Expired;
			UE_LOG(LogTemp, Display, TEXT("TAG move ran out: %s not taken by the sim in the %.0f ms after the press (slot %d, animT %.2f, moveT %.2f, %s)"), *What,
				FTagMoveBuf::Steps * 1000.0 / 60.0, s, U->animT, U->moveT, bCould ? TEXT("shown") : TEXT("the moment is gone: not shown"));
			if (!bCould) return;
			B.Refused = What;
			B.RefusedAt = Now;
		};
		if (B.MoveLeft > 0)
		{
			const FString What = MoveLabel(B.Move);
			if (U->move == B.Move && U->moveT <= 1e-9)
			{
				Took(What, B.MoveAge);
				B.MoveLeft = 0;
			}
			else
			{
				++B.MoveAge;
				if (--B.MoveLeft <= 0) RanOut(What, B.Move == tag::Move::Hit || B.Move == tag::Move::Cut ? bTacklerOk : bCarrierOk && !U->protecting);
			}
		}
		if (B.JukeLeft > 0)
		{
			if (U->move == tag::Move::Juke && U->moveT <= 1e-9)
			{
				Took(TEXT("JUKE"), B.JukeAge);
				B.JukeLeft = 0;
			}
			else
			{
				++B.JukeAge;
				if (--B.JukeLeft <= 0) RanOut(TEXT("JUKE"), bCarrierOk && !U->protecting);
			}
		}
		if (B.RushLeft > 0)
		{
			const FString What = FString(UTF8_TO_TCHAR(B.Rush.c_str())).ToUpper();
			if (U->lastMoveT != B.PreLastMoveT && FMath::IsNearlyEqual(U->lastMoveT, P.t, 1e-6))
			{
				Took(What, B.RushAge);
				B.RushLeft = 0;
			}
			else
			{
				++B.RushAge;
				if (--B.RushLeft <= 0) RanOut(What, bLive && !U->engagedBy.empty());
			}
		}
		if (B.DiveLeft > 0)
		{
			// the dive's reach was used (a tackle tried, a pile, a layout) or the man is down: it is over
			const bool bUsed = U->state == tag::EntState::Down || P.inWrap(U) || U->tackleCd > B.PreTackleCd + 1e-9 || U->looseDiveT > 0.0;
			if (bUsed || --B.DiveLeft <= 0) B.DiveLeft = 0;
		}
	}
}

float AMatchDirector::MoveRefusedA(int32 Slot, FString* What) const
{
	if (Slot < 0 || Slot >= static_cast<int32>(UE_ARRAY_COUNT(MoveBufs)) || !Sim || MoveBufs[Slot].Serial != Sim->serial) return 0.f;
	const FTagMoveBuf& B = MoveBufs[Slot];
	const double Age = FPlatformTime::Seconds() - B.RefusedAt;
	if (Age < 0.0 || Age > RefusedShow) return 0.f;
	if (What) *What = B.Refused;
	return static_cast<float>(1.0 - Age / RefusedShow);
}

void AMatchDirector::MoveRefusedCue(int32 Slot, FUserMarkerUI& M) const
{
	FString What;
	M.RefusedA = MoveRefusedA(Slot, &What);
	if (M.RefusedA > 0.f) M.Refused = What + TEXT("  -  TOO EARLY");
}

// ------------------------------------------------------------------ the rumble
void AMatchDirector::TickRumble(float Dt)
{
	FTagRumble& R = FTagRumble::Get();
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	// held (and stopped at once) while the game is paused and while a replay runs: the replay is the play again, not a
	// second one to feel
	const bool bHold = bEnginePaused || State == EMatchState::Paused || State == EMatchState::Replay || UGameplayStatics::IsGamePaused(this);
	R.Tick(Dt, PC, bHold);
	if (!Game || State == EMatchState::Menu)
	{
		bRumbleKick = UK.Stage == 3; // (the last match's kick, swung long ago: not felt again when the next one starts)
		return;
	}
	// who can feel it. One player: the user, on a pad (the keyboard has nothing to rumble). Several: the engine sends the
	// motors to every pad, or to the one used last (TagRumble.h), so a hit on one player's man would rumble the others'
	// pads - only a game with one player on a pad (the rest on the keyboard) rumbles
	int32 Felt = -1;
	const TCHAR* NotSent = nullptr;
	if (!bMulti)
	{
		if (!UserTeam) return; // (a CPU game: nobody's)
		Felt = 0;
		if (!bPadActive) NotSent = TEXT("the player is on the keyboard");
	}
	else
	{
		int32 Pads = 0;
		for (int32 i = 0; i < Humans.Num(); ++i)
			if (Humans[i].bPad && Humans[i].Team >= 0) { ++Pads; Felt = i; }
		if (Pads != 1)
		{
			NotSent = Pads > 1 ? TEXT("two or more players on pads: the engine cannot send it to one player's pad") : TEXT("nobody on a pad");
			if (Pads > 1 && RumbleWarned != Pads)
			{
				RumbleWarned = Pads;
				UE_LOG(LogTemp, Display, TEXT("TAG haptic: %d players on pads - every pad is the one platform user's, so rumble cannot reach one of them: off for this game"), Pads);
			}
		}
	}
	// the player whose man E is (-1: the CPU's)
	auto HumanOf = [this](const tag::Ent* E) -> int32
	{
		if (!E || !Sim) return -1;
		if (!bMulti) return E == Sim->user ? 0 : -1;
		for (int32 i = 0; i < Humans.Num(); ++i)
			if (Humans[i].Slot >= 0 && Sim->slotEnt(Humans[i].Slot) == E) return i;
		return -1;
	};
	auto Fire = [&](int32 H, ETagRumble Fx, float Scale, const TCHAR* What)
	{
		if (H < 0) return;
		R.Play(Fx, Scale, FString::Printf(TEXT("%s, %s"), What, *HumanTag(H)), H == Felt ? NotSent : TEXT("another player's man"));
	};
	// a whole side's: the players on team K
	auto FireTeam = [&](tag::TeamKey K, ETagRumble Fx, float Scale, const TCHAR* What)
	{
		if (!bMulti) { if (UserTeam && *UserTeam == K) Fire(0, Fx, Scale, What); return; }
		for (int32 i = 0; i < Humans.Num(); ++i)
			if (Humans[i].Team >= 0 && KeyOf(Humans[i].Team) == K) Fire(i, Fx, Scale, What);
	};
	// the kick meter's swing locked: the kicker's player feels the strike (the swing is played in the frame it locks, so
	// the meter is off again by now: its stage stays at 3, away, until the next kick's meter)
	if (UK.Stage == 3)
	{
		if (!bRumbleKick && !bHold) Fire(bMulti ? CallerOf(UK.Kind == 0 ? Game->kicking : Game->poss) : 0, ETagRumble::KickStrike, 1.f, TEXT("the kick"));
		bRumbleKick = true;
	}
	else bRumbleKick = false;
	// the play's events
	if (!Sim || (State != EMatchState::Live && State != EMatchState::Post && State != EMatchState::Kick)) return;
	if (Sim->serial != RumbleSerial || Sim->events.size() < RumbleSeen)
	{
		RumbleSerial = Sim->serial;
		RumbleSeen = 0;
		bRumbleTd = false;
	}
	if (bHold)
	{
		RumbleSeen = Sim->events.size(); // (nothing piles up to go off after the pause)
		return;
	}
	tag::PlaySim& P = *Sim;
	for (; RumbleSeen < P.events.size(); ++RumbleSeen)
	{
		const tag::PlayEvent& V = P.events[RumbleSeen];
		const std::string& Ty = V.type;
		const int32 Ha = HumanOf(V.a), Hb = HumanOf(V.b);
		if (Ty == "tackle")
		{
			// (a sack: the quarterback brought down in the pocket, as the sim counts it - PlaySim::tryTackle)
			const bool bSack = V.b && V.b == P.qb && !P.passThrown && !P.scramble && P.cfg.offPlay && P.cfg.offPlay->type != tag::PlayType::Run && !P.cfg.kick;
			// how hard: the two men's closing speed (yd/s) at the hit
			const float Jolt = V.a && V.b ? FMath::Clamp(0.55f + static_cast<float>(tag::len(tag::sub(V.a->vel, V.b->vel))) / 16.f, 0.55f, 1.f) : 0.8f;
			Fire(Ha, bSack ? ETagRumble::SackGiven : ETagRumble::TackleGiven, Jolt, bSack ? TEXT("sack") : TEXT("tackle"));
			Fire(Hb, bSack ? ETagRumble::SackTaken : ETagRumble::TackleTaken, Jolt, bSack ? TEXT("sacked") : TEXT("tackled"));
		}
		else if (Ty == "bighit")
		{
			Fire(Ha, ETagRumble::BigHitGiven, 1.f, TEXT("hit stick"));
			Fire(Hb, ETagRumble::BigHitTaken, 1.f, TEXT("hit stick taken"));
		}
		else if (Ty == "trucked")
		{
			Fire(Ha, ETagRumble::BigHitGiven, 0.85f, TEXT("truck"));
			Fire(Hb, ETagRumble::BigHitTaken, 1.f, TEXT("trucked"));
		}
		else if (Ty == "wrap" || Ty == "broken")
		{
			Fire(Ha, ETagRumble::Contact, 0.8f, Ty == "wrap" ? TEXT("wrapped up") : TEXT("missed tackle"));
			Fire(Hb, ETagRumble::Contact, 1.f, Ty == "wrap" ? TEXT("wrapped") : TEXT("broke the tackle"));
		}
		else if (Ty == "pancake")
		{
			Fire(Ha, ETagRumble::BigHitTaken, 0.7f, TEXT("pancaked"));
			Fire(Hb, ETagRumble::Contact, 1.f, TEXT("pancake block"));
		}
		else if (Ty == "fumble")
		{
			Fire(Ha, ETagRumble::Fumble, 1.f, TEXT("fumble"));
			Fire(Hb, ETagRumble::Fumble, 0.8f, TEXT("forced fumble"));
		}
		else if (Ty == "catch" || Ty == "divecatch" || Ty == "int")
		{
			// in traffic: a man of the other side within 2 yd as it is caught
			bool bTraffic = Ty == "int";
			if (V.a)
				for (const tag::Ent* E : P.ents)
					if (E->side != V.a->side && tag::dist(E->pos, V.a->pos) < 2.0) { bTraffic = true; break; }
			const ETagRumble Fx = bTraffic ? ETagRumble::CatchTraffic : ETagRumble::Catch;
			Fire(Ha, Fx, 1.f, Ty == "int" ? TEXT("interception") : TEXT("catch"));
			// (the quarterback's player feels his completion land; the receiver is his man a moment later)
			const int32 Hq = HumanOf(P.qb);
			if (Ty != "int" && Hq >= 0 && Hq != Ha && V.a && P.qb && V.a->side == P.qb->side && P.passThrown) Fire(Hq, ETagRumble::Catch, 0.7f, TEXT("completion"));
		}
		else if (Ty == "pbu")
		{
			Fire(Ha, ETagRumble::Catch, 0.8f, TEXT("pass broken up"));
			Fire(Hb, ETagRumble::Contact, 0.6f, TEXT("pass broken up on him"));
		}
		else if (Ty == "recover") Fire(Ha, ETagRumble::Catch, 0.8f, TEXT("recovery"));
		else if (Ty == "snap" && V.a && V.a == P.qb) Fire(Ha, ETagRumble::Snap, 1.f, TEXT("snap"));
	}
	// a touchdown: the scoring side's players, once a play
	if (P.result && P.result->td && !bRumbleTd)
	{
		bRumbleTd = true;
		const tag::TeamKey K = *P.result->td == tag::Side::Off ? Game->poss : tag::other(Game->poss);
		FireTeam(K, ETagRumble::Touchdown, 1.f, TEXT("touchdown"));
	}
}

// ------------------------------------------------------------------ the pass started on the press
void AMatchDirector::FeelThrow(const FTagReader& PC, int32 ModStyle, bool bOldTiming)
{
	if (!Sim || !PC) return;
	tag::PlaySim& P = *Sim;
	FTagFeelCtl& F = FeelCtl;
	const double Now = FPlatformTime::Seconds();
	if (F.WindSerial != P.serial)
	{
		F.WindIdx = -1;
		F.OldPressT = -1.0;
		F.WindSerial = P.serial;
	}
	tag::Ent* const Q = P.qb;
	if (!Q || P.user != Q)
	{
		F.WindIdx = -1;
		F.OldPressT = -1.0;
		return;
	}
	const bool bCanThrow = !Labels.empty() && P.ball.holder == Q && P.ball.mode == tag::BallMode::Held && !P.passThrown;
	auto PressedIdx = [&PC]() -> int32
	{
		for (int32 i = 0; i < 5; ++i)
			if (PC->WasInputKeyJustPressed(FeelPad(i))) return i;
		return -1;
	};
	if (bOldTiming)
	{
		// (the old timing throws in ReadUserInputSlot; the press is only counted here, to the windup's step in
		// FeelAfterStep, for the A/B)
		if (F.OldPressT < 0.0 && bCanThrow && !P.pendingThrow && PressedIdx() >= 0)
		{
			F.OldPressT = Now;
			F.OldStep0 = FeelSteps;
		}
		else if (F.OldPressT >= 0.0 && (!bCanThrow || Now - F.OldPressT > 1.0)) F.OldPressT = -1.0; // (a pump fake, the play over)
		return;
	}
	// a windup on: the pass is settled over it
	if (F.WindIdx >= 0)
	{
		const FKey& K = FeelPad(F.WindIdx);
		if (!F.bWindReleased && !PC->IsInputKeyDown(K))
		{
			F.bWindReleased = true;
			F.WindReleaseT = Now;
		}
		if (P.passThrown || P.ball.mode == tag::BallMode::Air)
		{
			LastThrow.Serial = P.serial;
			LastThrow.Style = F.WindStyle;
			LastThrow.ReleaseMs = (Now - F.WindT0) * 1000.0;
			LastThrow.bBallLob = P.ball.lob;
			LastThrow.bClipStand = ThrowTimedSerial == P.serial && bThrowTimedStand;
			LastThrow.bClipTouch = bThrowTimedTouch;
			LastThrow.ClipKey = ThrowTimedKey;
			LastThrow.bClipSwitched = ThrowRetimeSerial == P.serial;
			++LastThrow.Count;
			UE_LOG(LogTemp, Display, TEXT("TAG throw: %s to receiver %d, the windup %d sim steps (%.0f ms) after the press, the ball away %.0f ms after it (%s in the air)"),
				FeelPassName(F.WindStyle), F.WindIdx + 1, F.WindUpSteps, F.WindUpAt >= 0.0 ? (F.WindUpAt - F.WindT0) * 1000.0 : -1.0, LastThrow.ReleaseMs,
				P.ball.lob ? TEXT("lobbed") : TEXT("driven"));
			F.WindIdx = -1;
			return;
		}
		if (P.ball.holder != Q || Q->state == tag::EntState::Down)
		{
			UE_LOG(LogTemp, Display, TEXT("TAG throw: the windup ended without a throw (the ball not in his hands any more)"));
			F.WindIdx = -1;
			return;
		}
		if (P.pendingThrow && !P.pendingThrow->away)
		{
			if (F.WindUpAt < 0.0)
			{
				F.WindUpAt = Now;
				F.WindUpSteps = FeelSteps - F.WindStep0;
				LastThrow.Serial = P.serial;
				LastThrow.WindupSteps = F.WindUpSteps;
				LastThrow.WindupMs = (Now - F.WindT0) * 1000.0;
			}
			// the button tapped again in the windup: a pump fake (the arm comes through, the ball stays; the sim's "pump")
			if (F.bWindReleased && PC->WasInputKeyJustPressed(K))
			{
				P.cancelUserThrow();
				P.input.pumpFake = true;
				ThrowTimedSerial = 0; // (the windup's clip was the pump's: the real throw after it starts its own, SyncPlay)
				LastThrow.Serial = P.serial;
				LastThrow.bPump = true;
				++LastThrow.Pumps;
				UE_LOG(LogTemp, Display, TEXT("TAG throw: pump fake (receiver %d tapped twice, %.0f ms apart), the ball stays"), F.WindIdx + 1, (Now - F.WindT0) * 1000.0);
				F.WindIdx = -1;
				return;
			}
			// the pass: L1 / L2 held in the windup high / low, the button let go inside 0.15 s the lob / touch, else (held to
			// the release, or let go later) the bullet
			const double HeldFor = (F.bWindReleased ? F.WindReleaseT : Now) - F.WindT0;
			int32 Style = 0;
			if (ModStyle >= 0) Style = ModStyle;
			else if (F.bWindReleased && HeldFor < kFeelTapSecs) Style = 1;
			F.WindStyle = Style;
			// (the ball does not leave while the press can still turn out a tap: a quick release, 0.13 s, waits out the 0.15;
			// the sim leaves a shovel's timing alone)
			P.setUserThrow(Style, Style == 1, !F.bWindReleased && HeldFor < kFeelTapSecs ? P.t + (kFeelTapSecs - HeldFor) : -1.0);
			if (!F.bWindReleased && HeldFor < kFeelTapSecs && ModStyle < 0) QbStyleOpenFrame = GFrameCounter; // (SyncPlay's windup throw)
		}
		else if (F.WindUpAt >= 0.0 || (P.input.throwTo.empty() && Now - F.WindT0 > 0.25))
		{
			// the windup gone without a throw, or never started (the man not open to a throw from there: past the line)
			F.WindIdx = -1;
		}
		return;
	}
	// a receiver's button down: the windup starts now (the sim's next step), a bullet unless L1 / L2 is held
	if (!bCanThrow || P.pendingThrow) return;
	const int32 Idx = PressedIdx();
	if (Idx < 0) return;
	std::string ToRole;
	for (const auto& KV : Labels)
		if (KV.second == FString::FromInt(Idx + 1)) ToRole = KV.first->role;
	if (ToRole.empty()) return;
	P.input.throwTo = ToRole;
	P.input.passStyle = ModStyle >= 0 ? ModStyle : 0;
	P.input.lob = false;
	F.WindIdx = Idx;
	F.WindT0 = Now;
	F.bWindReleased = false;
	F.WindReleaseT = 0.0;
	F.WindUpAt = -1.0;
	F.WindStep0 = FeelSteps;
	F.WindUpSteps = -1;
	F.WindStyle = P.input.passStyle;
	if (ModStyle < 0) QbStyleOpenFrame = GFrameCounter; // (the type still open: SyncPlay starts the windup as the set throw)
	LastThrow.Serial = P.serial;
	LastThrow.Style = -1;
	LastThrow.bPump = false;
	LastThrow.WindupSteps = -1;
	LastThrow.WindupMs = LastThrow.ReleaseMs = -1.0;
	UE_LOG(LogTemp, Display, TEXT("TAG throw pressed: receiver %d (%s), the windup on the press"), Idx + 1, UTF8_TO_TCHAR(ToRole.c_str()));
}

// ------------------------------------------------------------------ the switch
double AMatchDirector::ArriveSecs(const tag::Ent* E, const tag::V2& Spot) const
{
	const tag::V2 To = tag::sub(Spot, E->pos);
	const double D = tag::len(To);
	constexpr double Reach = 0.8; // (yd: his arms)
	if (D <= Reach) return 0.0;
	const double Vm = FMath::Max(E->vmax, 1.0);
	const double Sp = E->speed();
	const double Cos = Sp > 0.5 ? tag::dot(E->vel, To) / (Sp * D) : 1.0;
	// running the other way he has to turn first (up to 0.9 s at full speed); from a stand he has to get going (the lag of
	// the sprint curve, tau)
	const double Turn = 0.45 * (1.0 - Cos) * FMath::Min(1.0, Sp / Vm);
	const double Going = E->tau * (1.0 - FMath::Min(1.0, Sp / Vm));
	return (D - Reach) / Vm + Turn + Going;
}

tag::Ent* AMatchDirector::SmartSwitchPick(tag::Ent* From, FTagSwitchNote& Note) const
{
	if (!Sim || !From) return nullptr;
	const tag::PlaySim& P = *Sim;
	const std::vector<tag::Ent*>& Team = From->side == tag::Side::Off ? P.off : P.def;
	const tag::Ball& B = P.ball;
	// the old rule's man (PlaySim::switchUser: the nearest to the ball now, never another player's), for the log
	const tag::V2 BallAt{B.pos.x, B.pos.y};
	double OldD = 1e9;
	Note.OldPick = nullptr;
	for (const tag::Ent* E : Team)
		if (E != From && E->state != tag::EntState::Down && !E->userControlled && tag::dist(E->pos, BallAt) < OldD)
		{
			OldD = tag::dist(E->pos, BallAt);
			Note.OldPick = E;
		}
	// who can be had: on his feet, nobody's, not locked up in a block, not in a pile or a dive
	auto Open = [&P, From](const tag::Ent* E)
	{
		return E != From && E->state != tag::EntState::Down && !E->userControlled && !E->engaged && E->engagedBy.empty() && E->looseDiveT < 0 && !P.inWrap(E);
	};
	std::function<double(const tag::Ent*)> Score;
	if (B.mode == tag::BallMode::Air)
	{
		// a pass (or a kick) in the air: the man who gets to where it comes down first
		Note.Mode = 1;
		const tag::V2 Land = P.ballLanding();
		Score = [this, Land](const tag::Ent* E) { return ArriveSecs(E, Land); };
	}
	else if (B.mode == tag::BallMode::Held && B.holder && B.holder->side != From->side && (B.holder != P.qb || P.scramble || B.holder->state == tag::EntState::Carrier))
	{
		// a run: the man who gets to the carrier first, on his path as he runs it; a man he is past already is beaten
		Note.Mode = 2;
		const tag::Ent* Cr = B.holder;
		Score = [this, &P, Cr](const tag::Ent* E)
		{
			const double Cs = Cr->speed();
			const tag::V2 Fd = Cs > 0.5 ? tag::scale(Cr->vel, 1.0 / Cs) : tag::V2{0, 0};
			const bool bBeaten = Cs > 3.0 && tag::dot(tag::sub(E->pos, Cr->pos), Fd) < -1.5;
			double Tm = 3.0 + ArriveSecs(E, P.predictPos(Cr, 3.0));
			for (int32 k = 0; k <= 90; ++k)
			{
				const double Tk = k / 30.0;
				if (ArriveSecs(E, P.predictPos(Cr, Tk)) <= Tk)
				{
					Tm = Tk;
					break;
				}
			}
			return Tm + (bBeaten ? 5.0 : 0.0);
		};
	}
	else
	{
		// before the throw, the catch, the handoff: the sim's nearest-the-ball
		Note.Mode = 0;
		return nullptr;
	}
	tag::Ent* Best = nullptr;
	double BestS = 1e9;
	for (tag::Ent* E : Team)
		if (Open(E))
		{
			const double Sc = Score(E);
			if (Sc < BestS)
			{
				BestS = Sc;
				Best = E;
			}
		}
	Note.Score = Note.Best = Best ? BestS : -1.0;
	Note.OldScore = Note.OldPick ? Score(Note.OldPick) : -1.0;
	return Best;
}

void AMatchDirector::FeelSwitch(bool bPress, bool bKeyHeld)
{
	if (!Sim) return;
	tag::PlaySim& P = *Sim;
	if (TagFeel::OldSwitch())
	{
		if (bPress) P.input.switchReq = true;
		return;
	}
	FTagFeelCtl& F = FeelCtl;
	const double Now = FPlatformTime::Seconds();
	if (F.SwitchSerial != P.serial)
	{
		F.SwitchSerial = P.serial;
		F.GraceWho = nullptr;
		F.bHeld = false;
	}
	tag::Ent* const Cur = P.user;
	if (!Cur) return;
	const tag::V2 Stick = P.input.move;
	const double StickLen = tag::len(Stick);
	// the switch assist: the stick as it is now does not steer the man switched to (a stick in the middle steers nobody
	// the wrong way: no grace)
	auto Grace = [&](const tag::Ent* Who)
	{
		F.GraceFrames = 0;
		if (StickLen >= 0.3 && TagFeel::SwitchAssist())
		{
			F.GraceWho = Who;
			F.GraceUntil = Now + kFeelGraceSecs;
			F.GraceStick = Stick;
		}
		else F.GraceWho = nullptr;
	};
	if (bPress)
	{
		FTagSwitchNote N;
		N.Serial = P.serial;
		N.From = Cur;
		N.Stick = Stick;
		N.Count = LastSwitch.Count + 1;
		tag::Ent* Pick = SmartSwitchPick(Cur, N);
		if (Pick) P.setSlotEnt(0, Pick);
		else P.switchUser(0);
		N.To = P.user;
		LastSwitch = N;
		if (P.user != Cur) Grace(P.user);
		F.bHeld = true;
		F.bArmed = StickLen < 0.3;
		F.HeldStick = Stick;
		static const TCHAR* const ModeName[] = {TEXT("nearest the ball"), TEXT("the ball in the air: first to where it comes down"), TEXT("a run: first to the carrier"), TEXT("the stick")};
		UE_LOG(LogTemp, Display, TEXT("TAG switch (%s): %s -> %s, %.2f s to get there (the nearest-the-ball rule's man %s: %.2f s)%s"), ModeName[FMath::Clamp(N.Mode, 0, 3)],
			*FeelRole(Cur), *FeelRole(P.user), N.Score, *FeelRole(N.OldPick), N.OldScore, F.GraceWho ? TEXT("; the stick held off him for 0.25 s") : TEXT(""));
	}
	else if (F.bHeld && !bKeyHeld) F.bHeld = false;
	if (F.bHeld && bKeyHeld)
	{
		// the button held: the stick picks the man that way from the one he has - one a push, the stick back to the middle
		// (or turned 45 deg) for the next - and steers nobody meanwhile (the pre-snap's hold, PresnapWalkSwitch)
		if (StickLen < 0.3 || (StickLen > 0.55 && FeelAngleDeg(Stick, F.HeldStick) > 45.0)) F.bArmed = true;
		if (F.bArmed && StickLen > 0.55)
		{
			F.bArmed = false;
			F.HeldStick = Stick;
			tag::Ent* const Was = P.user;
			if (P.switchUserToward(Stick, 0))
			{
				FTagSwitchNote N;
				N.Serial = P.serial;
				N.Mode = 3;
				N.From = Was;
				N.To = P.user;
				N.Stick = Stick;
				N.Count = LastSwitch.Count + 1;
				LastSwitch = N;
				Grace(P.user);
				UE_LOG(LogTemp, Display, TEXT("TAG switch by the stick (%.2f, %.2f): %s -> %s"), Stick.x, Stick.y, *FeelRole(Was), *FeelRole(P.user));
			}
			else UE_LOG(LogTemp, Display, TEXT("TAG switch by the stick (%.2f, %.2f): nobody that way from %s"), Stick.x, Stick.y, *FeelRole(Was));
		}
		P.input.move = tag::V2{0, 0};
		return;
	}
	if (F.GraceWho && (F.GraceWho != P.user || Now >= F.GraceUntil))
	{
		UE_LOG(LogTemp, Display, TEXT("TAG switch assist over: %d frames with the stick held off him"), F.GraceFrames);
		F.GraceWho = nullptr;
	}
	if (F.GraceWho)
	{
		// (the stick turned 45 deg from where it was, or let go: the player means it - his again)
		if (StickLen < 0.3 || FeelAngleDeg(Stick, F.GraceStick) > 45.0)
		{
			UE_LOG(LogTemp, Display, TEXT("TAG switch assist over: the stick moved, after %d frames"), F.GraceFrames);
			F.GraceWho = nullptr;
		}
		else
		{
			P.input.move = tag::V2{0, 0}; // (the AI drives him on: PlaySim::applyUser leaves a man with no stick to it)
			++F.GraceFrames;
		}
	}
}

// ------------------------------------------------------------------ Responsive movement
void AMatchDirector::FeelBeforeStep()
{
	for (int32 s = 0; s < 4; ++s) PreStepEnt[s] = nullptr;
	if (!Sim) return;
	// each player's man steers by his player's setting: Responsive's numbers, or the sim's own law (the sim reads them only
	// on a step that man's stick drives him: PlaySim::steerVel)
	tag::SteerBoost B;
	if (TagFeel::Responsive())
	{
		const FTagResponsiveTune& Tn = TagFeel::ResponsiveTune();
		B = TagFeel::ToSim(Tn);
	}
	const int32 Slots = FMath::Min(Sim->userSlots(), 4);
	for (int32 s = 0; s < Slots; ++s)
	{
		Sim->setUserSteer(s, B);
		PreStepEnt[s] = Sim->slotEnt(s);
	}
	PreStepBoosted.Reset();
	for (const tag::Ent* E : Sim->ents) PreStepBoosted.Add(E->steerBoosted);
}

void AMatchDirector::FeelAfterStep()
{
	++FeelSteps;
	FTagLatency::Get().NoteStep();
	if (HitStop.bOn) ++HitStop.StepsDuring;
	if (!Sim) return;
	// the press to the windup, counted to the step that started it (the quarterback's player: slot 0's - in a multiplayer
	// game his controls are kept with him between frames, SwapHuman)
	if (Sim->qb && Sim->user == Sim->qb && Sim->qb->anim == tag::Anim::Throw && Sim->pendingThrow && !Sim->pendingThrow->away)
	{
		FTagFeelCtl* F = &FeelCtl;
		if (bMulti)
			for (FTagHuman& Hm : Humans)
				if (Hm.Slot == 0) F = &Hm.Ctl.Feel;
		const double Now = FPlatformTime::Seconds();
		if (F->WindIdx >= 0 && F->WindUpAt < 0.0)
		{
			F->WindUpAt = Now;
			F->WindUpSteps = FeelSteps - F->WindStep0;
			LastThrow.Serial = Sim->serial;
			LastThrow.WindupSteps = F->WindUpSteps;
			LastThrow.WindupMs = (Now - F->WindT0) * 1000.0;
		}
		if (F->OldPressT >= 0.0)
		{
			LastThrow.Serial = Sim->serial;
			LastThrow.WindupSteps = FeelSteps - F->OldStep0;
			LastThrow.WindupMs = (Now - F->OldPressT) * 1000.0;
			LastThrow.Style = Sim->pendingThrow->style;
			UE_LOG(LogTemp, Display, TEXT("TAG throw (old timing): the windup %d sim steps (%.0f ms) after the press, %s"), LastThrow.WindupSteps, LastThrow.WindupMs,
				FeelPassName(LastThrow.Style));
			F->OldPressT = -1.0;
		}
	}
	// Responsive movement is the sim's now (PlaySim::steerVel with each player's setting, given in FeelBeforeStep): the
	// steps it changed a man's steering on, counted - a player's man, and any man the CPU had (must stay 0)
	// (who is the players' is settled at the step's start - a switch is made there - so it is who they are now)
	const tag::PlaySim& P = *Sim;
	if (PreStepBoosted.Num() != static_cast<int32>(P.ents.size())) return;
	for (int32 i = 0; i < PreStepBoosted.Num(); ++i)
	{
		const tag::Ent* E = P.ents[static_cast<size_t>(i)];
		const int32 D = E->steerBoosted - PreStepBoosted[i];
		if (D <= 0) continue;
		if (E->userControlled) ResponsiveSteps += D;
		else ResponsiveCpu += D;
	}
}

// ------------------------------------------------------------------ the bodies meeting: sound, hit-stop, shake
void AMatchDirector::OnContactsFound()
{
	if (!Sim) return;
	tag::PlaySim& P = *Sim;
	FTagHitStop& H = HitStop;
	const double Now = FPlatformTime::Seconds();
	if (ContactSoundSerial != P.serial)
	{
		if (CsContacts > 0)
			UE_LOG(LogTemp, Display, TEXT("TAG contact sounds, last play: %d contacts on screen, %d sounded (%d a frame late); not sounded: %d in a tackle (the sim's hit sounds it), %d away from the ball, %d just after a hit, %d over the cap"),
				CsContacts, CsSounds, CsLate, CsTackle, CsFar, CsAfterHit, CsCap);
		ContactSoundSerial = P.serial;
		CsContacts = CsSounds = CsTackle = CsFar = CsAfterHit = CsCap = CsLate = 0;
	}
	if (H.Serial != P.serial)
	{
		H.Serial = P.serial;
		H.EvSeen = 0;
		H.ArmedAt = -1.0;
		H.ArmSteps = 0;
	}
	// the sim's events since the last look: its hits (the contact sounds keep off them) and a player's big hit, armed for
	// the hit-stop (in -TagHitStopTest anybody's)
	auto Mine = [this](const tag::Ent* E) { return E && (E->userControlled || bHitStopTest); };
	for (; H.EvSeen < P.events.size(); ++H.EvSeen)
	{
		const tag::PlayEvent& V = P.events[H.EvSeen];
		const std::string& Ty = V.type;
		if (Ty == "tackle" || Ty == "bighit" || Ty == "trucked" || Ty == "wrap" || Ty == "assist") H.LastHitT = Now;
		if (!V.a || !V.b || !V.a->p || !V.b->p || !(Mine(V.a) || Mine(V.b))) continue;
		int32 Steps = 0;
		float Tr = 0.f;
		const TCHAR* What = nullptr;
		const double Close = tag::len(tag::sub(V.a->vel, V.b->vel));
		if (Ty == "bighit") { Steps = 5; Tr = 0.8f; What = TEXT("hit stick"); }
		else if (Ty == "trucked") { Steps = 4; Tr = 0.65f; What = TEXT("truck"); }
		else if (Ty == "tackle" && Close >= 9.0) { Steps = 3; Tr = 0.5f; What = TEXT("a big tackle"); } // (the sound's heavy hit: TagAudio)
		if (Steps <= H.ArmSteps) continue;
		H.ArmedAt = Now;
		H.ArmA = V.a->p->id;
		H.ArmB = V.b->p->id;
		H.ArmSteps = Steps;
		H.ArmTrauma = Tr;
		H.ArmWhy = FString::Printf(TEXT("%s, #%d on #%d closing %.1f yd/s"), What, V.a->p->number, V.b->p->number, Close);
	}
	const TArrayView<const FTagContact> Here = FTagContacts::Get().ThisFrame();
	if (H.ArmedAt >= 0.0)
	{
		// the hit lands as the two bodies meet on screen (or 0.2 s on, if a capture carries them together unmeasured)
		bool bMet = false;
		for (const FTagContact& C : Here) bMet = bMet || (C.Involves(H.ArmA) && C.Involves(H.ArmB));
		if (bMet || Now - H.ArmedAt > 0.2)
		{
			StartHitStop(H.ArmSteps, H.ArmTrauma, H.ArmWhy + (bMet ? TEXT(", as they met") : TEXT(", 0.2 s after the sim's hit")));
			H.ArmedAt = -1.0;
			H.ArmSteps = 0;
		}
	}
	// the sound of the meetings the sim's events do not sound: blocks, collisions in traffic, pile-ups - near the ball or
	// the shot, not in the 0.3 s after a hit, at most 4 in any half second
	// (each contact once: SyncPlay can run twice in a frame - the last play's sim, then the next one's at the call - and the
	// second pass found the frame's list already holding the first's; it was counted and sounded again)
	const FVector BallW = Ball ? Ball->GetActorLocation() : CamLook;
	for (const FTagContact& C : Here)
	{
		if (C.Serial <= ContactsHeardSerial) continue;
		ContactsHeardSerial = C.Serial;
		++CsContacts;
		++CsAllContacts;
		if (C.bTackle) { ++CsTackle; continue; }
		if (FVector::Dist2D(C.Pos, BallW) > 12.f * kFeelYd && FVector::Dist2D(C.Pos, CamLook) > 12.f * kFeelYd) { ++CsFar; continue; }
		if (Now - H.LastHitT < 0.3) { ++CsAfterHit; continue; }
		ContactSoundT.RemoveAll([Now](double T) { return Now - T > 0.5; });
		if (ContactSoundT.Num() >= 4) { ++CsCap; continue; }
		ContactSoundT.Add(Now);
		++CsSounds;
		++CsAllSounds;
		CsMostInWindow = FMath::Max(CsMostInWindow, ContactSoundT.Num());
		if (C.Frame != GFrameCounter) { ++CsLate; ++CsAllLate; }
		if (Audio) Audio->ContactHit(C.Pos, C.Strength());
		UE_LOG(LogTemp, Display, TEXT("TAG contact sound: players %d / %d closing %.0f cm/s, strength %.2f (frame %llu, the contact's %llu)"), C.IdA, C.IdB, C.ClosingSpeed, C.Strength(),
			static_cast<unsigned long long>(GFrameCounter), static_cast<unsigned long long>(C.Frame));
	}
}

void AMatchDirector::StartHitStop(int32 Steps, float Trauma, const FString& Why)
{
	FTagHitStop& H = HitStop;
	if (TagFeel::HitFx() && !TagFeel::OldShake()) CamTrauma.Add(Trauma, false);
	if (!TagFeel::HitStopOn())
	{
		++H.Skipped;
		UE_LOG(LogTemp, Display, TEXT("TAG hit-stop off (-TagNoHitStop / Hit effects Off): %s"), *Why);
		return;
	}
	if (H.bOn) return; // (one at a time)
	const double Now = FPlatformTime::Seconds();
	H.bOn = true;
	H.Steps = Steps;
	H.Began = Now;
	H.Until = Now + Steps * kFeelStep;
	++H.Count;
	// the whole world held (the bodies, the ball, the camera's moves, the director's clock and with it the sim's steps,
	// which wait - none is dropped: the play is the same play, 50-80 ms later); the shake and the rumble run on real time
	UGameplayStatics::SetGlobalTimeDilation(this, 0.0001f);
	UE_LOG(LogTemp, Display, TEXT("TAG hit-stop: %s - the world held %d sim steps (%.0f ms), sim t %.2f"), *Why, Steps, Steps * kFeelStep * 1000.0, Sim ? Sim->t : 0.0);
}

void AMatchDirector::TickHitStop()
{
	FTagHitStop& H = HitStop;
	if (!H.bOn) return;
	const double Now = FPlatformTime::Seconds();
	// (cut short by the pause menu, or once the play is gone: a replay, a kick, the next call)
	const bool bCut = bEnginePaused || (State != EMatchState::Live && State != EMatchState::Post);
	if (Now < H.Until && !bCut) return;
	H.bOn = false;
	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	UE_LOG(LogTemp, Display, TEXT("TAG hit-stop over: held %.0f ms, %d sim steps taken while held (all stops so far)%s"), (Now - H.Began) * 1000.0, H.StepsDuring,
		bCut ? TEXT(" - cut short") : TEXT(""));
}

float AMatchDirector::FeelRealDt(float Dt) const
{
	return HitStop.bOn ? static_cast<float>(FMath::Min(FApp::GetDeltaTime(), 0.1)) : Dt;
}

void AMatchDirector::ShakeCam(float Old, float Trauma, bool bSway)
{
	Shake = Old; // (-TagOldShake's)
	if (TagFeel::HitFx() && !TagFeel::OldShake()) CamTrauma.Add(Trauma, bSway);
}

FVector AMatchDirector::CameraShake(float Dt, FRotator& OutRot)
{
	OutRot = FRotator::ZeroRotator;
	if (TagFeel::OldShake())
	{
		// the old shake: the camera's position jumped at random every frame, decaying (touchdowns and sacks only)
		FVector J = FVector::ZeroVector;
		if (Shake > 0.1f && TagFeel::HitFx()) J = FVector(FMath::FRandRange(-Shake, Shake), FMath::FRandRange(-Shake, Shake), FMath::FRandRange(-Shake, Shake));
		Shake *= FMath::Exp(-Dt * 6.f);
		return J;
	}
	FVector Pos = FVector::ZeroVector;
	CamTrauma.Tick(FeelRealDt(Dt), FMath::Max(CamFocal, 20.f), OutRot, Pos);
	return Pos;
}

// ------------------------------------------------------------------ the latency log's presses
void AMatchDirector::FeelNotePresses(const FTagReader& PC)
{
	if (!FTagLatency::On() || !PC || State != EMatchState::Live) return;
	static const FKey Keys[] = {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top,
		EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_RightShoulder, EKeys::Gamepad_LeftTrigger, EKeys::Gamepad_RightTrigger, EKeys::Gamepad_LeftThumbstick,
		EKeys::Gamepad_RightThumbstick, EKeys::SpaceBar, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::F, EKeys::G, EKeys::Q, EKeys::E, EKeys::R,
		EKeys::V, EKeys::Tab, EKeys::J, EKeys::K, EKeys::X, EKeys::P, EKeys::T};
	const FTagInputHub& Hub = FTagInputHub::Get();
	const double Now = FPlatformTime::Seconds();
	for (const FKey& K : Keys)
	{
		if (!PC->WasInputKeyJustPressed(K)) continue;
		// when the device's event for it came in (the hub sees every device ahead of Slate); an older press of the key is
		// not this one, and an injected press has none
		double Arrived = -1.0;
		for (int32 d = 0; d < Hub.Devices().Num(); ++d) Arrived = FMath::Max(Arrived, Hub.PressTime(d, K));
		if (Arrived > 0.0 && Now - Arrived > 0.25) Arrived = -1.0;
		FTagLatency::Get().NotePress(K, Arrived);
	}
}
