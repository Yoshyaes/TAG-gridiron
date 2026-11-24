// The feel of the controls between the press and the sim: the move buffer (Game/TagMoveBuffer.h: a move pressed a beat
// before the sim will take it fires when it can, for 150 ms; one that runs out unused greys the player's marker) and the
// rumble's events (Game/TagRumble.h: which player's man a sim event was, and what he feels of it).
#include "Game/MatchDirector.h"
#include "Game/TagRumble.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sim/Playbook.h"
#include "UI/GridironOverlay.h"

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
