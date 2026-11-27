// The move buffer (Game/MatchFeel.cpp): a move a player presses a beat before the sim will take it is kept for 150 ms
// (FTagMoveBuf::Steps sim steps) and offered to the sim again on every step until it is taken, instead of being dropped.
// The sim refuses a ball carrier's move in the first 0.6 s of his last one (a catch, a juke: PlaySim::startMove's
// animT), a hit stick within 0.9 s of the last (moveT), a pass rush move on its cooldown (PlaySim::rushMove's moveCd);
// a flick in the first moments after a catch did nothing and said nothing. A move that runs out unused greys the
// player's marker for a moment ("TRUCK - TOO EARLY"), so he learns the timing rather than thinking the pad missed it.
//
// What is kept, per user slot (four local players): the highlight stick's move (stickMove), the keyboard's SPACE juke,
// a pass rush move, and the dive (its longer tackle reach stays on for the 150 ms; a dive is never "refused", so it has
// no cue). The strip (R1) and the switch are taken by the sim on the next step whatever happens, so they need none.
// Human input only: a CPU game never fills it, and -TagNoMoveBuffer turns it off (a refused press is dropped, as before).
#pragma once

#include "CoreMinimal.h"
#include "Sim/PlaySim.h"
#include <string>

struct FTagMoveBuf
{
	/** 150 ms at the sim's 60 Hz step. */
	static constexpr int32 Steps = 9;
	unsigned long long Serial = 0;     // the play these presses belong to (a new play starts it empty)
	const tag::Ent* Who = nullptr;     // the man they were pressed for (another man, the switch: they are dropped)
	// the highlight stick's move (or a face button's: spin, hurdle, stiff arm)
	tag::Move Move = tag::Move::None;
	double Side = 0;
	int32 MoveLeft = 0, MoveAge = 0;
	// the keyboard's SPACE juke (the side from the stick's lean)
	int32 JukeLeft = 0, JukeAge = 0;
	// a pass rush move ("swim", "bull"...)
	std::string Rush;
	int32 RushLeft = 0, RushAge = 0;
	// the dive: its reach is on for these steps
	int32 DiveLeft = 0;
	// the man as he was before the step: a move the sim took restarts moveT, a rush move stamps lastMoveT
	double PreMoveT = 0, PreLastMoveT = 0, PreTackleCd = 0;
	// the last press that ran out unused (the marker's grey pill), platform seconds
	FString Refused;
	double RefusedAt = -10.0;
	// this play's count (logged as the next play starts)
	int32 Asked = 0, Taken = 0, Late = 0, Expired = 0;

	bool Pending() const { return MoveLeft > 0 || JukeLeft > 0 || RushLeft > 0 || DiveLeft > 0; }
	void Drop() { MoveLeft = JukeLeft = RushLeft = DiveLeft = 0; Move = tag::Move::None; Rush.clear(); }
};
