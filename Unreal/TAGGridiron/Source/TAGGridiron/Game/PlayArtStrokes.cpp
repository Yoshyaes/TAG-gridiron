#include "Game/PlayArtStrokes.h"

#include "Sim/Game.h"
#include "Sim/PlaySim.h"
#include "Sim/Playbook.h"
#include "UI/GridironOverlay.h"
#include <algorithm>

namespace
{
	// play art colours (receivers use their button colours: ButtonColor)
	const FLinearColor RunPath(1.f, 0.93f, 0.18f);
	const FLinearColor RunBlockC(1.f, 0.36f, 0.14f);
	const FLinearColor PassProC(1.f, 0.70f, 0.22f, 0.85f);
	const FLinearColor QbDrop(0.92f, 0.94f, 1.f, 0.55f);
	const FLinearColor Unlabeled(0.75f, 0.78f, 0.84f);
	const FLinearColor DeepC(0.18f, 0.46f, 1.f);
	const FLinearColor FlatC(0.22f, 0.86f, 1.f);
	const FLinearColor HookC(1.f, 0.84f, 0.12f);
	const FLinearColor MidHookC(0.72f, 0.40f, 1.f);
	const FLinearColor RushC(1.f, 0.20f, 0.16f);
	const FLinearColor ManC(1.f, 0.45f, 0.42f);
	const FLinearColor SpyC(1.f, 0.56f, 0.10f);
	const FLinearColor OffMark(0.92f, 0.94f, 0.97f);
	const FLinearColor DefMark(0.95f, 0.45f, 0.40f);

	void AddLine(FArtPicture& Out, TArray<tag::V2> Pts, const FLinearColor& C, float Width, EArtEnd End, bool bGlow = false)
	{
		FArtStroke& S = Out.Strokes.AddDefaulted_GetRef();
		S.Kind = FArtStroke::Line;
		S.Pts = MoveTemp(Pts);
		S.Color = C;
		S.Width = Width;
		S.End = End;
		S.bGlow = bGlow;
	}

	void AddZone(FArtPicture& Out, const tag::V2& Ctr, double RX, double RY, const FLinearColor& C)
	{
		FArtStroke& S = Out.Strokes.AddDefaulted_GetRef();
		S.Kind = FArtStroke::Zone;
		S.Pts = {Ctr};
		S.RX = RX;
		S.RY = RY;
		S.Color = C;
	}
}

std::vector<tag::Ent*> ArtLabelOrder(const tag::PlaySim& P)
{
	std::vector<tag::Ent*> Order;
	for (const std::string& R : P.readOrder)
		if (tag::Ent* E = P.offRole(R))
			if (E->state == tag::EntState::Route) Order.push_back(E);
	for (tag::Ent* E : P.off)
		if (E->state == tag::EntState::Route && std::find(Order.begin(), Order.end(), E) == Order.end()) Order.push_back(E);
	for (tag::Ent* E : P.off)
		if (E->eligible && std::find(Order.begin(), Order.end(), E) == Order.end()) Order.push_back(E);
	return Order;
}

void BuildArtPicture(const tag::PlaySim& P, bool bOffense, const tag::Ent* PreEnt, const tag::Ent* UserEnt,
	TFunctionRef<int32(const tag::Ent*)> LabelOf, FArtPicture& Out)
{
	Out.Strokes.Reset();
	Out.Marks.Reset();
	Out.Los = P.los;
	Out.BallY = P.by;
	const tag::Ent* Q = P.qb;
	// the men at their spots
	for (const tag::Ent* E : P.ents)
	{
		FArtMark& M = Out.Marks.AddDefaulted_GetRef();
		M.Pos = E->shiftTo ? *E->shiftTo : E->pos;
		M.bOffense = E->side == tag::Side::Off;
		M.bCenter = E->role == "C";
		M.bQB = E == Q;
		M.bMe = E == UserEnt;
		M.Label = M.bOffense && bOffense ? LabelOf(E) : -1;
		M.Color = M.Label >= 0 ? ButtonColor(M.Label) : M.bOffense ? OffMark : DefMark;
	}
	if (bOffense)
	{
		const tag::OffPlay& Op = *P.cfg.offPlay;
		const std::string First = P.readOrder.empty() ? std::string() : P.readOrder.front();
		for (const tag::Ent* E : P.off)
		{
			if (E->state == tag::EntState::Route && !E->route.empty())
			{
				// the route, capped at ~24 yards of stem (a streak points on up the field)
				TArray<tag::V2> Pts;
				Pts.Add(E->pos);
				const tag::V2 Start = E->shiftTo ? *E->shiftTo : E->pos;
				double Left = 24.0;
				tag::V2 Last = Start;
				for (const tag::V2& R : E->route)
				{
					const double Seg = tag::dist(Last, R);
					if (Seg >= Left)
					{
						Pts.Add(tag::add(Last, tag::scale(tag::norm(tag::sub(R, Last)), Left)));
						break;
					}
					Pts.Add(R);
					Left -= Seg;
					Last = R;
				}
				const int32 L = LabelOf(E);
				const FLinearColor C = L >= 0 ? ButtonColor(L) : Unlabeled;
				const bool bPrimary = E->role == First;
				AddLine(Out, MoveTemp(Pts), C, bPrimary ? 40.f : 32.f, EArtEnd::Arrow, bPrimary || E == PreEnt);
			}
			else if (E->state == tag::EntState::PassBlock || E->state == tag::EntState::RunBlock)
			{
				const bool bRun = E->state == tag::EntState::RunBlock;
				const tag::V2 From = E->shiftTo ? *E->shiftTo : E->pos;
				const tag::Ent* T = E->blockTarget;
				if (!T && bRun)
				{
					// receivers stalk the nearest man
					double Bd = 12.0;
					for (const tag::Ent* D : P.def)
						if (tag::dist(D->pos, From) < Bd) { Bd = tag::dist(D->pos, From); T = D; }
				}
				tag::V2 To;
				if (T)
				{
					const tag::V2 Tp = T->shiftTo ? *T->shiftTo : T->pos;
					const double D = tag::dist(Tp, From);
					const double Reach = bRun ? FMath::Max(0.6, D - 0.75) : FMath::Min(1.7, FMath::Max(0.5, D - 0.6));
					To = tag::add(From, tag::scale(tag::norm(tag::sub(Tp, From)), Reach));
				}
				else
					To = tag::V2{From.x - 1.3, From.y + (From.y - P.by) * 0.15};  // sets back in his pass set
				AddLine(Out, {E->pos, To}, bRun ? RunBlockC : PassProC, bRun ? 28.f : 24.f, EArtEnd::Block);
			}
		}
		// the quarterback's drop, the run path
		if (Q && P.passPlay())
		{
			const tag::Formation* F = tag::formationById(Op.formation);
			const double DropX = P.los - (F ? F->qbDepth : 5.0) - Op.dropDepth.value_or(2.0);
			if (Q->pos.x - DropX > 0.6) AddLine(Out, {Q->pos, tag::V2{DropX, P.by}}, QbDrop, 22.f, EArtEnd::None);
		}
		if (Op.run)
		{
			const tag::RunScheme& Rs = *Op.run;
			if (const tag::Ent* C = P.offRole(Rs.carrier))
			{
				const double AimY = P.by + (P.cfg.flip ? -Rs.aimDy : Rs.aimDy);
				const double Side = FMath::Sign(AimY - P.by) == 0 ? 1.0 : FMath::Sign(AimY - P.by);
				TArray<tag::V2> Pts;
				Pts.Add(C->pos);
				if (C != Q && Q)
				{
					// the mesh point beside the quarterback (a draw waits on it; a stretch bends out)
					Pts.Add(tag::V2{Q->pos.x + (Rs.style == tag::RunStyle::Draw ? -0.6 : 0.5), Q->pos.y + Side * 0.9});
				}
				const bool bWide = Rs.style == tag::RunStyle::Stretch;
				Pts.Add(tag::V2{P.los - (bWide ? 1.0 : 0.3), AimY});
				Pts.Add(tag::V2{P.los + 2.5, AimY + (bWide ? Side * 1.5 : 0)});
				Pts.Add(tag::V2{P.los + 6.5, AimY + (bWide ? Side * 1.0 : 0)});
				AddLine(Out, MoveTemp(Pts), RunPath, 44.f, EArtEnd::Arrow, true);
			}
		}
	}
	else
	{
		const tag::V2 Qp = Q ? Q->pos : tag::V2{P.los - 5, P.by};
		for (const tag::Ent* E : P.def)
		{
			const tag::V2 From = E->shiftTo ? *E->shiftTo : E->pos;
			const bool bMe = E == UserEnt;
			switch (E->state)
			{
			case tag::EntState::Rush:
			{
				// rush lanes: straight at the quarterback; the edges bend around the tackles
				const double Out1 = FMath::Sign(From.y - P.by) == 0 ? 1.0 : FMath::Sign(From.y - P.by);
				const bool bEdge = E->role == "LE" || E->role == "RE" || (!E->isDL && FMath::Abs(From.y - P.by) > 4);
				TArray<tag::V2> Pts{E->pos};
				if (bEdge) Pts.Add(tag::V2{P.los - 1.6, From.y + Out1 * 0.4});
				const tag::V2 Last = bEdge ? tag::V2{P.los - 1.6, From.y} : From;
				Pts.Add(tag::add(Qp, tag::scale(tag::norm(tag::sub(Last, Qp)), 1.8)));
				AddLine(Out, MoveTemp(Pts), RushC, E->isDL ? 26.f : 34.f, EArtEnd::Arrow, !E->isDL || bMe);
				break;
			}
			case tag::EntState::Zone:
				if (E->zoneSpot)
				{
					const std::string& Z = E->zone;
					const bool bDeep = tag::PlaySim::deepZone(Z);
					const bool bFlat = Z.rfind("flat", 0) == 0;
					const FLinearColor C = bDeep ? DeepC : bFlat ? FlatC : Z == "hookM" ? MidHookC : HookC;
					double RX = 4.0, RY = 4.5;  // yards: along the field, across it
					if (Z == "deepM") { RX = 7; RY = 10; }
					else if (Z.rfind("third", 0) == 0) { RX = 6.5; RY = 8; }
					else if (Z.rfind("half", 0) == 0) { RX = 7; RY = 11; }
					else if (Z.size() == 2 && Z[0] == 'q') { RX = 6; RY = 6.5; }
					else if (bFlat) { RX = 4; RY = 5; }
					AddZone(Out, *E->zoneSpot, RX, RY, C);
					AddLine(Out, {E->pos, *E->zoneSpot}, C, bMe ? 28.f : 18.f, EArtEnd::None, bMe);
				}
				break;
			case tag::EntState::Man:
				if (E->man)
				{
					const tag::V2 Mp = E->man->shiftTo ? *E->man->shiftTo : E->man->pos;
					const tag::V2 End = tag::add(Mp, tag::scale(tag::norm(tag::sub(From, Mp)), 0.9));
					AddLine(Out, {E->pos, End}, ManC, bMe ? 28.f : 20.f, EArtEnd::Dot, bMe);
				}
				break;
			case tag::EntState::Spy:
			{
				const tag::V2 Spot{FMath::Max(P.los + 4.5, Qp.x + 6), Qp.y};
				AddZone(Out, Spot, 2.5, 3.5, SpyC);
				AddLine(Out, {E->pos, Spot}, SpyC, bMe ? 28.f : 20.f, EArtEnd::None, bMe);
				break;
			}
			default:
				break;
			}
		}
	}
}

void BuildCardPicture(tag::Game& G, const tag::OffPlay* Op, const tag::DefCall* Dc, bool bOffense, double Los, double BallY,
	FArtPicture& Out)
{
	Out.Strokes.Reset();
	Out.Marks.Reset();
	if (!Op)
	{
		Op = tag::playById("ace_curl");
		if (!Op && !tag::OFF_PLAYS().empty()) Op = &tag::OFF_PLAYS()[0];
	}
	if (!Dc)
	{
		Dc = tag::defById("base_c3");
		if (!Dc && !tag::DEF_PLAYS().empty()) Dc = &tag::DEF_PLAYS()[0];
	}
	if (!Op || !Dc || Op->type == tag::PlayType::Kneel || Op->type == tag::PlayType::Spike) return;
	// the same configuration Game::makePlay builds, with a fixed seed so the cards cost the game no randomness
	tag::PlayConfig Cfg;
	Cfg.off = &G.offense();
	Cfg.def = &G.defense();
	Cfg.offPlay = Op;
	Cfg.defCall = Dc;
	Cfg.los = Los;
	Cfg.ballY = BallY;
	Cfg.flip = BallY > tag::FIELD_W / 2 + 0.5;  // strength to the wide side, as the real call will be
	Cfg.weather = G.weather;
	Cfg.worldDir = G.worldDir();
	Cfg.rng = tag::Rng(7u);
	Cfg.down = static_cast<double>(G.down);
	Cfg.toGo = G.toGo;
	const tag::PlaySim Sim(Cfg);
	const std::vector<tag::Ent*> Order = ArtLabelOrder(Sim);
	BuildArtPicture(Sim, bOffense, nullptr, nullptr, [&Order](const tag::Ent* E)
	{
		const auto It = std::find(Order.begin(), Order.end(), E);
		const int32 I = It == Order.end() ? -1 : static_cast<int32>(It - Order.begin());
		return I < 5 ? I : -1;
	}, Out);
}
