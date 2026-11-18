// The play art as strokes in the sim's yards (x along the field, y across, the offense attacking +x): routes, blocks,
// the drop, the run path, rush lanes, zones, man assignments. The field art (ATagPlayArt) and the play-call cards
// (SPlayDiagram) draw the same picture, so a play looks the same on the card as it does on the turf.
#pragma once

#include "CoreMinimal.h"
#include "Render/PlayArt.h"
#include "Sim/Vec.h"
#include <vector>

namespace tag { class PlaySim; class Ent; struct OffPlay; struct DefCall; class Game; }

struct FArtStroke
{
	enum EKind : uint8 { Line, Zone };
	EKind Kind = Line;
	/** Line: the polyline. Zone: one point, the centre. */
	TArray<tag::V2> Pts;
	/** Zone radii in yards (along the field, across it). */
	double RX = 0, RY = 0;
	FLinearColor Color = FLinearColor::White;
	/** Width on the field in cm (the card scales it). */
	float Width = 24.f;
	EArtEnd End = EArtEnd::None;
	bool bGlow = false;
};

/** A man's spot at the snap (for the cards: the field has the real players). */
struct FArtMark
{
	tag::V2 Pos;
	bool bOffense = true;
	bool bCenter = false;   // the centre (drawn as a square)
	bool bQB = false;
	int32 Label = -1;       // a receiver's button colour (0..4) or -1
	bool bMe = false;       // the user's man
	FLinearColor Color = FLinearColor::White;
};

struct FArtPicture
{
	TArray<FArtStroke> Strokes;
	TArray<FArtMark> Marks;
	double Los = 0, BallY = 0;
};

/** The receivers in button order: the QB's progression, then the other route runners, then the eligible blockers. */
std::vector<tag::Ent*> ArtLabelOrder(const tag::PlaySim& P);

/** The art for one side of the ball from the sim as it stands before the snap. LabelOf: a receiver's button (0..4) or -1;
 *  PreEnt: the receiver being hot routed (his route glows); UserEnt: the user's defender (his assignment is bolder). */
void BuildArtPicture(const tag::PlaySim& P, bool bOffense, const tag::Ent* PreEnt, const tag::Ent* UserEnt,
	TFunctionRef<int32(const tag::Ent*)> LabelOf, FArtPicture& Out);

/** The picture for a play-call card: a fresh pre-snap sim of the play at the ball against a stock opponent call (the
 *  game's rng is untouched). Op or Dc may be null: a stock play stands in for the side not being called. */
void BuildCardPicture(tag::Game& G, const tag::OffPlay* Op, const tag::DefCall* Dc, bool bOffense, double Los, double BallY,
	FArtPicture& Out);
