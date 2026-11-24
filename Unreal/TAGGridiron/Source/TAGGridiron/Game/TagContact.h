// The bodies meeting this frame: one FTagContact per pair of men whose torsos came within reach closing at speed, as the
// director finds them (AMatchDirector::DetectContacts, at the end of SyncPlay: after the sim's steps and the bodies'
// placing, once a frame in every state that draws a play - pre-snap, live, after the whistle). A pair is reported once,
// then not again for 0.6 s. Game thread only.
//
// HOW TO READ IT
//   - This frame's contacts:  for (const FTagContact& C : FTagContacts::Get().ThisFrame()) ...
//     ThisFrame() is empty unless the director's pass has already run in this frame (GFrameCounter). The athletes tick
//     after the director (AAthlete::TickAfter), so from AAthlete::Tick it is this frame's; from an anim instance's
//     NativeUpdateAnimation copy what you need in the athlete's Tick rather than reading it on a worker thread.
//   - Every contact exactly once, whatever the tick order (a reader that may run before the director some frames):
//         uint64 Cursor = 0;                      // keep per reader
//         FTagContacts::Get().ForEachSince(Cursor, [&](const FTagContact& C) { ... });
//     hands over the contacts with Serial > Cursor still held (the last 64), oldest first, and moves Cursor on.
//   - One man's: C.Involves(PlayerId), C.DirFor(PlayerId) (the way the hit pushes him), C.DvFor(PlayerId) (how hard,
//     cm/s), C.Other(PlayerId). PlayerId is tag::Player::id - AAthlete::Player->id, the key of AMatchDirector::Athletes.
//   - AthA / AthB are the two bodies, for comparing against `this` in the frame of the contact; do not keep them.
//
// What it is: the impulse of an inelastic meeting of the two masses (restitution 0.15) along the line between them; each
// man's share of the velocity change by mass, a man already falling or down taking a third (the paired fall moves him
// with it). That share is what the director hands to AAthlete::AddImpact (the body layer's springs) unless -TagNoImpact,
// which still reports the contacts (bImpactApplied false). The sound of them (Game/MatchFeel.cpp) reads the same events.
#pragma once

#include "CoreMinimal.h"

class AAthlete;

struct FTagContact
{
	/** Running number since the game started (1, 2, 3...): a reader's cursor (FTagContacts::ForEachSince). */
	uint64 Serial = 0;
	/** GFrameCounter of the frame it was found in. */
	uint64 Frame = 0;
	/** The play's sim clock (s) and tag::PlaySim::serial of the play (0: no play's sim). */
	double SimT = 0.0;
	unsigned long long PlaySerial = 0;
	/** The two men: tag::Player::id (-1: not a player - none today). A is the first of the pair, B the second. */
	int32 IdA = -1, IdB = -1;
	/** Their bodies, valid in this frame only (compare, never keep). */
	const AAthlete* AthA = nullptr;
	const AAthlete* AthB = nullptr;
	/** World (cm): midway between the two actors, at the actors' height. */
	FVector Pos = FVector::ZeroVector;
	/** Horizontal unit vector from A to B. */
	FVector Normal = FVector::ForwardVector;
	/** How fast they closed on each other along Normal (cm/s; 180 is the least that counts). */
	float ClosingSpeed = 0.f;
	/** The collision's impulse (kg cm/s). */
	float Impulse = 0.f;
	/** The velocity change each takes (cm/s): A along -Normal, B along +Normal, already a third for a man falling or down. */
	float DvA = 0.f, DvB = 0.f;
	/** Part of a tackle: the sim has a tackle, wrap, big hit, truck or assist between these two in the last 0.35 s of
	 *  the play, or both are falling (a paired tackle's fall). A block, a collision in traffic, a pile-up: false. */
	bool bTackle = false;
	/** A man a local player drives is one of the two (SlotA / SlotB: the sim's user slot 0..3 of each, -1 the CPU's). */
	bool bHuman = false;
	int32 SlotA = -1, SlotB = -1;
	/** The director handed DvA / DvB to the bodies (false under -TagNoImpact). */
	bool bImpactApplied = true;

	/** 0..1: a brush (just over the threshold) .. a full-speed collision (900 cm/s closing and more). */
	float Strength() const { return FMath::Clamp((ClosingSpeed - 180.f) / 720.f, 0.f, 1.f); }
	bool Involves(int32 PlayerId) const { return PlayerId >= 0 && (PlayerId == IdA || PlayerId == IdB); }
	/** The other man's id (-1 if PlayerId is neither). */
	int32 Other(int32 PlayerId) const { return PlayerId == IdA ? IdB : PlayerId == IdB ? IdA : -1; }
	/** The way the hit pushes PlayerId (horizontal unit vector; zero if he is neither). */
	FVector DirFor(int32 PlayerId) const { return PlayerId == IdA ? -Normal : PlayerId == IdB ? Normal : FVector::ZeroVector; }
	/** How hard it pushes him (cm/s of velocity change; 0 if he is neither). */
	float DvFor(int32 PlayerId) const { return PlayerId == IdA ? DvA : PlayerId == IdB ? DvB : 0.f; }
};

class FTagContacts
{
public:
	static FTagContacts& Get();

	/** This frame's contacts; empty if the director's pass has not run in this frame (GFrameCounter). */
	TArrayView<const FTagContact> ThisFrame() const;
	/** Every held contact with Serial > Cursor (the last 64), oldest first; Cursor ends at the newest handed over. */
	template <typename FnType>
	void ForEachSince(uint64& Cursor, FnType&& Fn) const
	{
		const int32 N = Ring.Num();
		for (int32 k = 0; k < N; ++k)
		{
			const FTagContact& C = Ring[(Head + k) % N];
			if (C.Serial > Cursor)
			{
				Fn(C);
				Cursor = C.Serial;
			}
		}
	}
	/** The newest contact's Serial (0: none yet). */
	uint64 LastSerial() const { return NextSerial - 1; }

	// ---- the director's side (AMatchDirector::DetectContacts)
	/** A new frame's pass starts (this frame's list is emptied). */
	void BeginFrame(uint64 InFrame);
	/** Add one: Serial and Frame are stamped here. Returns the stored copy. */
	const FTagContact& Add(const FTagContact& C);
	/** A new match: nothing kept (Serial numbers go on rising). */
	void Reset();

private:
	static constexpr int32 RingSize = 64;
	TArray<FTagContact> FrameList;
	uint64 FrameNo = 0;
	TArray<FTagContact> Ring;  // oldest at Head once full
	int32 Head = 0;
	uint64 NextSerial = 1;
};
