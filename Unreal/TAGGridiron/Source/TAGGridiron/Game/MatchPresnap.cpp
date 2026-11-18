// Pre-snap: the play art on the field, hot routes, audibles, protection slides and motion for the offense; coverage
// audibles, line / linebacker shifts, the coverage shell and player assignments for the defense. The adjustments are
// PlaySim's pre-snap API (deterministic, applied before the first step); this file is the controls and the drawing.
#include "Game/MatchDirector.h"

#include "Render/Athlete.h"
#include "Render/Coords.h"
#include "Render/PlayArt.h"
#include "Game/PlayArtStrokes.h"
#include "UI/GridironOverlay.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Sim/Playbook.h"
#include "UnrealClient.h"
#include "InputKeyEventArgs.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
#include <set>

using namespace TagCoords;

namespace
{
	FString S(const std::string& s) { return FString(s.c_str()); }


	// hot route families: the D-pad direction (and R1 / L1) picks the family, pressing it again cycles inside it
	const std::vector<std::string> FamUp = {"go", "fade", "seam"};
	const std::vector<std::string> FamDown = {"curl", "hitch", "comeback", "stick"};
	const std::vector<std::string> FamOut = {"out", "quickout", "corner", "flat", "arrow"};
	const std::vector<std::string> FamIn = {"slant", "in", "dig", "drag", "shallow", "cross"};
	const std::vector<std::string> FamR1 = {"post", "wheel", "sail", "swing", "angle"};

	FString RouteName(const std::string& R)
	{
		static const std::map<std::string, const TCHAR*> N = {
			{"go", TEXT("Streak")}, {"fade", TEXT("Fade")}, {"seam", TEXT("Seam")}, {"curl", TEXT("Curl")}, {"hitch", TEXT("Hitch")},
			{"comeback", TEXT("Comeback")}, {"stick", TEXT("Stick")}, {"out", TEXT("Out")}, {"quickout", TEXT("Quick Out")},
			{"corner", TEXT("Corner")}, {"flat", TEXT("Flat")}, {"arrow", TEXT("Arrow")}, {"slant", TEXT("Slant")}, {"in", TEXT("In")},
			{"dig", TEXT("Dig")}, {"drag", TEXT("Drag")}, {"shallow", TEXT("Shallow")}, {"cross", TEXT("Crosser")},
			{"post", TEXT("Post")}, {"wheel", TEXT("Wheel")}, {"sail", TEXT("Sail")}, {"swing", TEXT("Swing")}, {"angle", TEXT("Angle")},
			{"checkdown", TEXT("Check Down")}, {"screen", TEXT("Screen")}, {"block", TEXT("Block")}};
		auto It = N.find(R);
		return It != N.end() ? FString(It->second) : S(R);
	}

	FString Family(const std::vector<std::string>& F)
	{
		FString Out;
		for (const std::string& R : F) Out += (Out.IsEmpty() ? TEXT("") : TEXT(" / ")) + RouteName(R);
		return Out;
	}

	bool InFamily(const std::vector<std::string>& F, const std::string& R) { return std::find(F.begin(), F.end(), R) != F.end(); }

	std::string NextIn(const std::vector<std::string>& F, const std::string& Cur)
	{
		auto It = std::find(F.begin(), F.end(), Cur);
		return It == F.end() || ++It == F.end() ? F.front() : *It;
	}

	FString PlayerTag(const tag::Ent* E)
	{
		return FString::Printf(TEXT("%s  #%d %s"), *S(E->role), E->p->number, *S(E->p->last));
	}

	const TCHAR* CoverageLabel(tag::Coverage C)
	{
		switch (C)
		{
		case tag::Coverage::C0: return TEXT("Cover 0");
		case tag::Coverage::C1: return TEXT("Cover 1");
		case tag::Coverage::C2: return TEXT("Cover 2");
		case tag::Coverage::C3: return TEXT("Cover 3");
		case tag::Coverage::C4: return TEXT("Quarters");
		case tag::Coverage::C2M: return TEXT("2-Man");
		case tag::Coverage::C3Fire: return TEXT("Fire zone");
		}
		return TEXT("");
	}
}

int32 AMatchDirector::ScreenRightSign() const
{
	FVector F = CamLook - CamPos;
	F.Z = 0;
	F.Normalize();
	const FVector R(-F.Y, F.X, 0);                  // the screen's right on the ground (as the left stick reads it)
	const FVector OffLeft(0, Dir > 0 ? -1 : 1, 0);  // the offense's +y in the world
	return FVector::DotProduct(R, OffLeft) >= 0 ? 1 : -1;
}

int32 AMatchDirector::LabelOf(const tag::Ent* E) const
{
	auto It = Labels.find(const_cast<tag::Ent*>(E));
	return It == Labels.end() ? -1 : FCString::Atoi(*It->second) - 1;
}

namespace
{
	/** The audibles: up to eight, the two kinds taken in turn (two passes and a run; two coverages and a pressure). */
	template <class T, class Pred>
	std::vector<const T*> Mix(const std::vector<const T*>& All, Pred bSecond)
	{
		std::vector<const T*> A, B, Out;
		for (const T* X : All) (bSecond(X) ? B : A).push_back(X);
		size_t i = 0, j = 0;
		while (Out.size() < 8 && (i < A.size() || j < B.size()))
		{
			for (int k = 0; k < 2 && i < A.size() && Out.size() < 8; k++) Out.push_back(A[i++]);
			if (j < B.size() && Out.size() < 8) Out.push_back(B[j++]);
			if (i >= A.size())
				while (j < B.size() && Out.size() < 8) Out.push_back(B[j++]);
		}
		return Out;
	}
}

std::vector<const tag::OffPlay*> AMatchDirector::AudibleOff() const
{
	// the team's plays out of the same formation (same personnel on the field)
	std::vector<const tag::OffPlay*> All;
	for (const tag::OffPlay* P : tag::offBookFor(Game->offense()).playsIn(Sim->cfg.offPlay->formation))
		if (P != Sim->cfg.offPlay && P->type != tag::PlayType::Kneel && P->type != tag::PlayType::Spike) All.push_back(P);
	if (All.empty())
		for (const tag::OffPlay& P : tag::OFF_PLAYS())
			if (P.formation == Sim->cfg.offPlay->formation && &P != Sim->cfg.offPlay && P.type != tag::PlayType::Kneel && P.type != tag::PlayType::Spike)
				All.push_back(&P);
	return Mix<tag::OffPlay>(All, [](const tag::OffPlay* P) { return P->type == tag::PlayType::Run; });
}

std::vector<const tag::DefCall*> AMatchDirector::AudibleDef() const
{
	// the team's calls out of the same front (no substitutions at the line)
	std::vector<const tag::DefCall*> All;
	for (const tag::DefCall* D : tag::defBookFor(Game->defense()).calls)
		if (D->front == Sim->cfg.defCall->front && D != Sim->cfg.defCall) All.push_back(D);
	if (All.empty())
		for (const tag::DefCall& D : tag::DEF_PLAYS())
			if (D.front == Sim->cfg.defCall->front && &D != Sim->cfg.defCall) All.push_back(&D);
	return Mix<tag::DefCall>(All, [](const tag::DefCall* D) { return D->rushers() > 4; });
}

void AMatchDirector::BeginPresnapUI()
{
	PreMode = EPre::Base;
	PreEnt = nullptr;
	PresnapClock = 0.f;
	SetClock = 0.f;
	AudibleSel = 0;
	const bool bUser = UserOff() || UserDef();
	ArtAuto = bUser ? 2.4f : 0.f;
	if (!PlayArt && GetWorld()) PlayArt = GetWorld()->SpawnActor<ATagPlayArt>();
	if (PlayArt) PlayArt->SetShown(false);
	RefreshPresnapPanel();
}

void AMatchDirector::EndPresnapUI()
{
	PreMode = EPre::Base;
	PreEnt = nullptr;
	if (PlayArt) PlayArt->SetShown(false);
	if (Overlay) Overlay->ClearPresnapPanel();
	// only receivers out on routes keep a throwing icon once the ball is snapped
	for (auto It = Labels.begin(); It != Labels.end();)
		It = It->first->state == tag::EntState::Route ? std::next(It) : Labels.erase(It);
}

void AMatchDirector::PlayAudibleCue(bool bOffense, bool bSignal)
{
	if (!Sim || !Game) return;
	const FAthleteAssets& AA = FAthleteAssets::Get();
	const tag::TeamKey OffK = Game->poss;
	auto Ath = [&](const tag::Ent* E) { return AthleteFor(E->p, E->side == tag::Side::Off ? OffK : tag::other(OffK)); };
	if (bOffense)
	{
		if (Sim->qb)
			if (AAthlete* Q = Ath(Sim->qb))
			{
				const bool bGun = Sim->qb->pos.x < Sim->los - 2.5;
				UAnimSequence* C = bSignal && AA.QBSignal.IsValid() && bGun ? AA.QBSignal.Get() : bGun ? AA.QBAudibleGun.Get() : AA.QBAudibleUnder.Get();
				if (C) Q->PlayOneShot(C);
			}
		if (bSignal) return; // (a hot route is between the quarterback and his receiver)
		// the men who hear it look up and settle again: the back beside him, and across the line the linebackers
		for (const tag::Ent* E : Sim->off)
			if (E != Sim->qb && (E->p->pos == tag::Pos::RB || E->p->pos == tag::Pos::FB) && AA.HBAudible.IsValid())
				if (AAthlete* A = Ath(E)) A->PlayOneShot(AA.HBAudible.Get());
		for (const tag::Ent* E : Sim->def)
			if (E->p->pos == tag::Pos::LB && AA.LBHearsAudible.IsValid())
				if (AAthlete* A = Ath(E)) A->PlayOneShot(AA.LBHearsAudible.Get());
	}
	else
	{
		// the defense's call: the linebacker nearest the middle signals it
		const tag::Ent* Mike = nullptr;
		for (const tag::Ent* E : Sim->def)
			if (E->p->pos == tag::Pos::LB && (!Mike || FMath::Abs(E->pos.y - Sim->by) < FMath::Abs(Mike->pos.y - Sim->by))) Mike = E;
		if (Mike && AA.LBDefAudible.IsValid())
			if (AAthlete* A = Ath(Mike)) A->PlayOneShot(AA.LBDefAudible.Get());
	}
}

void AMatchDirector::RebuildPlay(const tag::OffPlay* Op, const tag::DefCall* Dc, bool bFlip)
{
	tag::Game& G = *Game;
	// everyone walks to his new spot from where he stands now
	std::map<tag::Player*, tag::V2> At;
	std::set<tag::Player*> Was;
	for (tag::Ent* E : Sim->ents)
	{
		At[E->p] = E->pos;
		Was.insert(E->p);
	}
	tag::Player* UserP = Sim->user ? Sim->user->p : nullptr;
	std::optional<tag::Side> US;
	if (UserOff()) US = tag::Side::Off;
	else if (UserDef()) US = tag::Side::Def;
	Sim = bTwoPoint ? G.twoPointSim(*Op, *Dc, US, bFlip) : G.makePlay(*Op, *Dc, US, bFlip);
	bool bSame = Sim->ents.size() == Was.size();
	for (tag::Ent* E : Sim->ents)
	{
		auto It = At.find(E->p);
		if (It != At.end()) Sim->walkFrom(E, It->second);
		else bSame = false;
	}
	if (US && *US == tag::Side::Def && UserP)
		for (tag::Ent* E : Sim->ents)
			if (E->p == UserP && E != Sim->user)
			{
				if (Sim->user) Sim->user->userControlled = false;
				Sim->user = E;
				E->userControlled = true;
			}
	if (!bSame)
	{
		std::vector<tag::Player*> Ps;
		for (tag::Ent* E : Sim->ents) Ps.push_back(E->p);
		ShowOnly(Ps);
	}
	Prev.clear();
	CapturePrev();
	Labels.clear();
	if (US && *US == tag::Side::Off) AssignLabels();
	PreEnt = nullptr;
}

void AMatchDirector::TickPresnap(float Dt)
{
	bPreConsumed = false;
	if (!Sim || !Sim->preSnap()) return;
	Sim->presnapStep(Dt);
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const bool bOff = UserOff(), bDef = UserDef();
	// the button that picked the play is not a pre-snap call: nothing is read for the first third of a second (SQUARE
	// picks a play on the call screen and opens the audible list here - the list came up on its own)
	if (PC && (bOff || bDef) && PresnapClock > 0.35f)
	{
		auto Hit = [&](std::initializer_list<FKey> Ks)
		{
			for (const FKey& K : Ks)
				if (PC->WasInputKeyJustPressed(K)) return true;
			return false;
		};
		const bool kCross = Hit({EKeys::Gamepad_FaceButton_Bottom, EKeys::SpaceBar, EKeys::Enter});
		const bool kCircle = Hit({EKeys::Gamepad_FaceButton_Right, EKeys::BackSpace});
		const bool kSquare = Hit({EKeys::Gamepad_FaceButton_Left, EKeys::J});
		const bool kTri = Hit({EKeys::Gamepad_FaceButton_Top, EKeys::H});
		const bool kL1 = Hit({EKeys::Gamepad_LeftShoulder, EKeys::Tab});
		const bool kR1 = Hit({EKeys::Gamepad_RightShoulder, EKeys::N});
		const bool kUp = Hit({EKeys::Gamepad_DPad_Up, EKeys::Up});
		const bool kDown = Hit({EKeys::Gamepad_DPad_Down, EKeys::Down});
		const bool kLeft = Hit({EKeys::Gamepad_DPad_Left, EKeys::Left});
		const bool kRight = Hit({EKeys::Gamepad_DPad_Right, EKeys::Right});
		const bool kBack = Hit({EKeys::Gamepad_LeftTrigger, EKeys::BackSpace});
		// R3 (F1): hide / show the pre-play panel (kept for the plays after)
		if (Hit({EKeys::Gamepad_RightThumbstick, EKeys::F1}))
		{
			bPresnapPanelHidden = !bPresnapPanelHidden; // (no toast: it sat big in the middle of the field)
		}
		// a receiver by his button (cross, circle, square, triangle, R1 / keys 1-5)
		auto Receiver = [&]() -> tag::Ent*
		{
			const FKey Pads[5] = {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left,
				EKeys::Gamepad_FaceButton_Top, EKeys::Gamepad_RightShoulder};
			const FKey Nums[5] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five};
			for (int32 i = 0; i < 5; ++i)
				if (PC->WasInputKeyJustPressed(Pads[i]) || PC->WasInputKeyJustPressed(Nums[i]))
					for (auto& KV : Labels)
						if (KV.second == FString::FromInt(i + 1)) return KV.first;
			return nullptr;
		};
		const int32 RS = ScreenRightSign();
		const tag::DefCall* Dc = Sim->cfg.defCall;
		const tag::OffPlay* Op = Sim->cfg.offPlay;
		const EPre Mode = PreMode;
		const float StickX = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX) + (PC->WasInputKeyJustPressed(EKeys::RightBracket) ? 1.f : 0.f) - (PC->WasInputKeyJustPressed(EKeys::LeftBracket) ? 1.f : 0.f);
		auto Use = [&] { bPreConsumed = true; };
		if (bOff)
		{
			switch (Mode)
			{
			case EPre::Base:
				// a flick of the right stick sends a run the other way (the play flipped at the line, the quarterback
				// calling it out)
				if (Op && Op->run && FMath::Abs(StickX) > 0.65f && FMath::Abs(PrevStickX) < 0.4f)
				{
					Use();
					const int32 Want = (StickX > 0.f ? 1 : -1) * RS;                       // the offense's +y (its left) is +1
					const double Now = Op->run->aimDy * (Sim->cfg.flip ? -1.0 : 1.0);
					if (FMath::Abs(Now) < 0.05 || (Now > 0 ? 1 : -1) != Want)
					{
						RebuildPlay(Op, Dc, !Sim->cfg.flip);
						PlayAudibleCue(true);
					}
					Overlay->Toast(StickX > 0.f ? TEXT("Run right") : TEXT("Run left"));
				}
				else if (kTri)
				{
					Use();
					if (Sim->passPlay()) PreMode = EPre::HotPick;
					else Overlay->Toast(TEXT("No hot routes on a run: audible to a pass"));
				}
				else if (kSquare) { Use(); PreMode = EPre::Audible; AudibleSel = 0; }
				else if (kL1 || kDown)
				{
					// pass protection (Madden: L1)
					Use();
					if (Sim->passPlay()) PreMode = EPre::Protect;
					else Overlay->Toast(TEXT("Protection calls are for pass plays"));
				}
				else if (kCircle || kLeft || kRight) { Use(); PreMode = EPre::MotionPick; } // motion (Madden: CIRCLE, then the man)
				else if (kR1) { Use(); PlayFakeSnap(); } // fake snap (Madden: R1)
				break;
			case EPre::HotPick:
			case EPre::MotionPick:
				// every face button (and R1) is a receiver here: L2 / backspace backs out
				if (tag::Ent* E = Receiver())
				{
					Use();
					PreEnt = E;
					PreMode = Mode == EPre::HotPick ? EPre::HotRoute : EPre::Motion;
				}
				else if (kBack) { Use(); PreMode = EPre::Base; }
				else if (kCross || kCircle || kSquare || kTri || kR1) Use();  // a button with nobody on it
				break;
			case EPre::HotRoute:
			{
				if (!PreEnt) { PreMode = EPre::Base; break; }
				const std::string Cur = PreEnt->routeName;
				std::string To;
				if (kUp) To = NextIn(FamUp, Cur);
				else if (kDown) To = NextIn(FamDown, Cur);
				else if (kLeft || kRight)
				{
					// toward his sideline: the out-breaking family; toward the ball: the in-breaking one
					const int32 Pressed = kRight ? RS : -RS;
					To = Pressed == static_cast<int32>(PreEnt->outSign) ? NextIn(FamOut, Cur) : NextIn(FamIn, Cur);
				}
				else if (kL1) To = "block";
				else if (kR1) To = NextIn(FamR1, Cur);
				if (!To.empty())
				{
					Use();
					if (Sim->hotRoute(PreEnt, To) && (PlayAudibleCue(true, true), true))
					{
						Sim->previewBlocks();
						Overlay->Toast(FString::Printf(TEXT("Hot route: %s  %s"), *S(PreEnt->role), *RouteName(To)));
					}
				}
				else if (kTri) { Use(); PreMode = EPre::HotPick; PreEnt = nullptr; }
				else if (kCross || kCircle) { Use(); PreMode = EPre::Base; PreEnt = nullptr; }
				break;
			}
			case EPre::Motion:
			{
				if (!PreEnt) { PreMode = EPre::Base; break; }
				// the left stick flicked left / right (Madden), or the D-pad
				const float LSX = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
				const bool bFlickR = LSX > 0.7f && PrevMotionX <= 0.7f, bFlickL = LSX < -0.7f && PrevMotionX >= -0.7f;
				PrevMotionX = LSX;
				const bool kMR = kRight || bFlickR, kML = kLeft || bFlickL;
				if (kMR || kML)
				{
					Use();
					if (Sim->motion(PreEnt, (kMR ? RS : -RS) * 4.0))
					{
						Sim->previewBlocks();
						Overlay->Toast(FString::Printf(TEXT("Motion: %s"), *S(PreEnt->role)));
					}
				}
				else if (kCircle) { Use(); PreMode = EPre::MotionPick; PreEnt = nullptr; } // another man
				else if (kCross) { PreMode = EPre::Base; PreEnt = nullptr; }             // (not used: CROSS snaps, the man still moving)
				break;
			}
			case EPre::Protect:
				if (kLeft || kRight) { Use(); Sim->setProtection(kRight ? RS : -RS, Sim->maxProtect); }
				else if (kUp) { Use(); Sim->setProtection(Sim->slide, !Sim->maxProtect); }
				else if (kSquare) { Use(); Sim->setProtection(0, false); }
				else if (kCross || kCircle) { Use(); PreMode = EPre::Base; }
				if (bPreConsumed && PreMode == EPre::Protect) Sim->previewBlocks();
				break;
			case EPre::Audible:
			{
				const std::vector<const tag::OffPlay*> L = AudibleOff();
				if (kUp) { Use(); AudibleSel = (AudibleSel + static_cast<int32>(L.size()) - 1) % FMath::Max(1, static_cast<int32>(L.size())); }
				else if (kDown) { Use(); AudibleSel = (AudibleSel + 1) % FMath::Max(1, static_cast<int32>(L.size())); }
				else if (kCross && !L.empty())
				{
					Use();
					const tag::OffPlay* To = L[FMath::Clamp(AudibleSel, 0, static_cast<int32>(L.size()) - 1)];
					RebuildPlay(To, Dc, Sim->cfg.flip);
					PlayAudibleCue(true);
					Overlay->Toast(FString::Printf(TEXT("Audible: %s"), *S(To->name)));
					PreMode = EPre::Base;
				}
				else if (kR1 || kL1)
				{
					Use();
					RebuildPlay(Op, Dc, !Sim->cfg.flip);
					PlayAudibleCue(true);
					Overlay->Toast(TEXT("Flip play"));
					PreMode = EPre::Base;
				}
				else if (kCircle) { Use(); PreMode = EPre::Base; }
				break;
			}
			default:
				PreMode = EPre::Base;
				break;
			}
		}
		else
		{
			using LA = tag::PlaySim::LineAdj;
			using SA = tag::PlaySim::ShellAdj;
			using DA = tag::PlaySim::DefAssign;
			// the left stick (W A S D) walks the man you have anywhere on your side of the ball, as in Madden: a
			// linebacker walked down into a gap, a safety rolled over the top, an end kicked out wide. Screen-relative,
			// as the stick reads in the play.
			{
				float MX = 0.f, MY = 0.f;
				if (PC->IsInputKeyDown(EKeys::W)) MY += 1.f;
				if (PC->IsInputKeyDown(EKeys::S)) MY -= 1.f;
				if (PC->IsInputKeyDown(EKeys::D)) MX += 1.f;
				if (PC->IsInputKeyDown(EKeys::A)) MX -= 1.f;
				const float GX = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX), GY = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
				if (FMath::Abs(GX) + FMath::Abs(GY) > 0.2f) { MX = GX; MY = GY; }
				FVector Fw = CamLook - CamPos;
				Fw.Z = 0;
				Fw.Normalize();
				const FVector Rt(-Fw.Y, Fw.X, 0);
				const FVector Wv = (Rt * MX + Fw * MY).GetClampedToMaxSize(1.f);
				const tag::V2 Stick = Dir > 0 ? tag::V2{Wv.X, -Wv.Y} : tag::V2{-Wv.X, Wv.Y};
				if (Sim->userPresnapMove(Stick, Dt) && !bPreMoved)
				{
					bPreMoved = true;
					Sim->previewBlocks();
				}
				else if (bPreMoved && Wv.IsNearlyZero())
				{
					bPreMoved = false;
					Sim->previewBlocks(); // (the line re-sets its blocking against where he settled)
				}
			}
			switch (Mode)
			{
			case EPre::Base:
				// Madden 26 pre-play defense: SQUARE audible, TRIANGLE coverage audibles, CROSS defensive hot route,
				// CIRCLE switch player (the D-pad keeps the line and linebacker shifts)
				if (kSquare) { Use(); PreMode = EPre::Audible; AudibleSel = 0; }
				else if (kCross) { Use(); PreMode = EPre::Assign; }
				else if (kUp) { Use(); PreMode = EPre::DLine; }
				else if (kLeft) { Use(); PreMode = EPre::LBs; }
				else if (kTri || kRight) { Use(); PreMode = EPre::Shell; }
				else if (kL1 || kCircle) { Use(); Sim->switchUser(); }
				break;
			case EPre::DLine:
			case EPre::LBs:
			{
				std::optional<LA> A;
				if (kLeft || kRight) A = (kRight ? RS : -RS) > 0 ? LA::ShiftL : LA::ShiftR;
				else if (kUp) A = LA::Pinch;
				else if (kDown) A = LA::Spread;
				else if (kSquare) A = LA::Base;
				if (A)
				{
					Use();
					if (Mode == EPre::DLine) Sim->adjustLine(*A);
					else Sim->adjustLBs(*A);
					Sim->previewBlocks();
				}
				else if (kCross || kCircle) { Use(); PreMode = EPre::Base; }
				break;
			}
			case EPre::Shell:
			{
				std::optional<SA> A;
				if (kLeft) A = SA::Press;
				else if (kRight) A = SA::Off;
				else if (kUp) A = SA::ShowBlitz;
				else if (kDown) A = SA::Disguise;
				else if (kSquare) A = SA::Base;
				if (A) { Use(); Sim->adjustShell(*A); Sim->previewBlocks(); }
				else if (kCross || kCircle) { Use(); PreMode = EPre::Base; }
				break;
			}
			case EPre::Assign:
			{
				std::optional<DA> A;
				if (kUp) A = DA::Deep;
				else if (kDown) A = DA::Blitz;
				else if (kLeft) A = DA::Flat;
				else if (kRight) A = DA::Hook;
				else if (kSquare) A = DA::Spy;
				else if (kTri) A = DA::Man;
				if (A && Sim->user)
				{
					Use();
					if (Sim->assignDefender(Sim->user, *A))
					{
						Sim->previewBlocks();
						static const TCHAR* Names[] = {TEXT("Blitz"), TEXT("QB spy"), TEXT("Hook zone"), TEXT("Flat zone"), TEXT("Deep zone"), TEXT("Man coverage")};
						Overlay->Toast(FString::Printf(TEXT("%s: %s"), *S(Sim->user->role), Names[static_cast<int32>(*A)]));
					}
				}
				else if (kL1) { Use(); Sim->switchUser(); }
				else if (kCross || kCircle) { Use(); PreMode = EPre::Base; }
				break;
			}
			case EPre::Audible:
			{
				const std::vector<const tag::DefCall*> L = AudibleDef();
				if (kUp) { Use(); AudibleSel = (AudibleSel + static_cast<int32>(L.size()) - 1) % FMath::Max(1, static_cast<int32>(L.size())); }
				else if (kDown) { Use(); AudibleSel = (AudibleSel + 1) % FMath::Max(1, static_cast<int32>(L.size())); }
				else if (kCross && !L.empty())
				{
					Use();
					const tag::DefCall* To = L[FMath::Clamp(AudibleSel, 0, static_cast<int32>(L.size()) - 1)];
					RebuildPlay(Op, To, Sim->cfg.flip);
					PlayAudibleCue(false);
					Overlay->Toast(FString::Printf(TEXT("Audible: %s"), *S(To->name)));
					PreMode = EPre::Base;
				}
				else if (kCircle) { Use(); PreMode = EPre::Base; }
				break;
			}
			default:
				PreMode = EPre::Base;
				break;
			}
		}
	}
	if (PC) PrevStickX = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
	// the art: hold R2 (Q), and while any pre-snap menu is open; it also shows itself briefly as the play comes in
	ArtAuto -= Dt;
	const bool bHold = PC && (PC->IsInputKeyDown(EKeys::Gamepad_LeftTrigger) || PC->IsInputKeyDown(EKeys::Gamepad_RightTrigger) || PC->IsInputKeyDown(EKeys::Q));
	const bool bShow = (bOff || bDef) && (bHold || ArtAuto > 0.f || PreMode != EPre::Base);
	if (bHold) ArtAuto = 0.f;
	if (PlayArt)
	{
		PlayArt->SetShown(bShow);
		if (bShow || PlayArt->Alpha() > 0.f) BuildPlayArt();
	}
	RefreshPresnapPanel();
}

void AMatchDirector::RefreshPresnapPanel()
{
	if (!Overlay) return;
	if (!Sim || State != EMatchState::Presnap || !(UserOff() || UserDef()))
	{
		Overlay->ClearPresnapPanel();
		return;
	}
	// hidden by the player (R3 / F1) on the base screen: the menus he opens still show theirs
	if (bPresnapPanelHidden && PreMode == EPre::Base)
	{
		// a small tag in the panel's corner instead, saying how to bring it back
		using G = EPadGlyph;
		TArray<FPreRow> Tag;
		FPreRow X;
		X.Glyph = G::None;
		X.Key = PadUI() ? TEXT("R3") : TEXT("F1");
		X.Label = TEXT("Show controls");
		Tag.Add(X);
		Overlay->SetPresnapPanel(FString(), FString(), Tag, PadUI());
		return;
	}
	// holding R2 on the base screen: the field to itself (the menus keep their panel)
	if (PreMode == EPre::Base)
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			if (PC->IsInputKeyDown(EKeys::Gamepad_LeftTrigger) || PC->IsInputKeyDown(EKeys::Gamepad_RightTrigger) || PC->IsInputKeyDown(EKeys::Q))
			{
				Overlay->ClearPresnapPanel();
				return;
			}
	using G = EPadGlyph;
	TArray<FPreRow> R;
	auto Row = [&](G Gl, const FString& Label, bool bOn = false, const FString& Key = FString(), FLinearColor Tint = FLinearColor::White)
	{
		FPreRow X;
		X.Glyph = Gl;
		X.Label = Label;
		X.bOn = bOn;
		X.Key = Key;
		X.Tint = Tint;
		R.Add(X);
	};
	static const G Face[5] = {G::Cross, G::Circle, G::Square, G::Triangle, G::R1};
	const int32 RS = ScreenRightSign();
	const G ScreenL = G::DLeft, ScreenR = G::DRight;
	FString Title, Sub;
	const tag::OffPlay& Op = *Sim->cfg.offPlay;
	const tag::DefCall& Dc = *Sim->cfg.defCall;
	auto ReceiverRows = [&]()
	{
		for (int32 i = 0; i < 5; ++i)
			for (auto& KV : Labels)
				if (KV.second == FString::FromInt(i + 1))
					Row(Face[i], FString::Printf(TEXT("%s   %s"), *PlayerTag(KV.first),
						KV.first->routeName.empty() ? TEXT("") : *RouteName(KV.first->routeName)), false, FString::FromInt(i + 1), ButtonColor(i));
	};
	switch (PreMode)
	{
	case EPre::Base:
		if (UserOff())
		{
			Title = S(Op.name);
			Sub = FString::Printf(TEXT("%s  -  %s"), *S(tag::formationById(Op.formation) ? tag::formationById(Op.formation)->name : Op.formation),
				Sim->cfg.flip ? TEXT("flipped") : TEXT("as called"));
			Row(G::L2, TEXT("Show play art (hold)"), false, TEXT("Q"));
			if (Sim->passPlay()) Row(G::Triangle, TEXT("Hot route"));
			Row(G::Square, TEXT("Audible / flip play"));
			if (!Sim->passPlay()) Row(G::None, TEXT("Flip the run"), false, PadUI() ? TEXT("R-STICK") : TEXT("[ ]"));
			if (Sim->passPlay()) Row(G::L1, Sim->slide || Sim->maxProtect ? TEXT("Pass protection (adjusted)") : TEXT("Pass protection"), Sim->slide || Sim->maxProtect);
			Row(G::Circle, TEXT("Motion"));
			Row(G::R1, TEXT("Fake snap"));
			Row(G::Cross, TEXT("Snap"));
			Row(G::None, TEXT("Hide these controls"), false, PadUI() ? TEXT("R3") : TEXT("F1"));
		}
		else
		{
			Title = S(Dc.name);
			Sub = FString::Printf(TEXT("You: %s"), Sim->user ? *PlayerTag(Sim->user) : TEXT(""));
			Row(G::L2, TEXT("Show play art (hold)"), false, TEXT("Q"));
			Row(G::Square, TEXT("Audible"));
			Row(G::Triangle, TEXT("Coverage audibles"), Sim->shellAdj != tag::PlaySim::ShellAdj::Base);
			Row(G::Cross, TEXT("Defensive hot route"));
			Row(G::DUp, TEXT("D-line shift"), Sim->dlAdj != tag::PlaySim::LineAdj::Base);
			Row(G::DLeft, TEXT("Linebacker shift"), Sim->lbAdj != tag::PlaySim::LineAdj::Base);
			Row(G::Circle, TEXT("Switch player"));
			Row(G::None, TEXT("Move your player"), bPreMoved, PadUI() ? TEXT("L-STICK") : TEXT("WASD"));
			Row(G::None, TEXT("Hide these controls"), false, PadUI() ? TEXT("R3") : TEXT("F1"));
		}
		break;
	case EPre::HotPick:
		Title = TEXT("HOT ROUTE");
		Sub = TEXT("Pick a receiver");
		ReceiverRows();
		Row(G::L2, TEXT("Back"));
		break;
	case EPre::MotionPick:
		Title = TEXT("MOTION");
		Sub = TEXT("Pick a receiver");
		ReceiverRows();
		Row(G::L2, TEXT("Back"));
		break;
	case EPre::HotRoute:
		if (PreEnt)
		{
			const std::string& C = PreEnt->routeName;
			Title = FString::Printf(TEXT("HOT ROUTE  -  %s"), *S(PreEnt->role));
			Sub = FString::Printf(TEXT("%s   now: %s"), *PlayerTag(PreEnt), *RouteName(C));
			const bool bOutRight = static_cast<int32>(PreEnt->outSign) == RS;  // his sideline is screen right
			Row(G::DUp, Family(FamUp), InFamily(FamUp, C));
			Row(G::DDown, Family(FamDown), InFamily(FamDown, C));
			Row(bOutRight ? ScreenR : ScreenL, Family(FamOut), InFamily(FamOut, C));
			Row(bOutRight ? ScreenL : ScreenR, Family(FamIn), InFamily(FamIn, C));
			Row(G::R1, Family(FamR1), InFamily(FamR1, C));
			Row(G::L1, TEXT("Block"), C == "block");
			Row(G::Triangle, TEXT("Another receiver"));
			Row(G::Cross, TEXT("Done"));
		}
		break;
	case EPre::Motion:
		if (PreEnt)
		{
			Title = FString::Printf(TEXT("MOTION  -  %s"), *S(PreEnt->role));
			Sub = PlayerTag(PreEnt);
			Row(G::None, TEXT("Send him left / right"), false, PadUI() ? TEXT("L-STICK") : TEXT("< >"));
			Row(G::Circle, TEXT("Another man"));
			Row(G::Cross, TEXT("Snap"));
		}
		break;
	case EPre::Protect:
		Title = TEXT("PROTECTION");
		Sub = TEXT("The line slides one gap; max protect keeps the backs and tight ends in");
		Row(G::DLeft, TEXT("Slide left"), Sim->slide == -RS);
		Row(G::DRight, TEXT("Slide right"), Sim->slide == RS);
		Row(G::DUp, TEXT("Max protect"), Sim->maxProtect);
		Row(G::Square, TEXT("Reset"), !Sim->slide && !Sim->maxProtect);
		Row(G::Cross, TEXT("Done"));
		break;
	case EPre::Audible:
		if (UserOff())
		{
			Title = TEXT("AUDIBLE");
			Sub = FString::Printf(TEXT("Out of %s"), *S(tag::formationById(Op.formation) ? tag::formationById(Op.formation)->name : Op.formation));
			const std::vector<const tag::OffPlay*> L = AudibleOff();
			for (int32 i = 0; i < static_cast<int32>(L.size()); ++i)
				Row(i == AudibleSel ? G::Cross : G::None, FString::Printf(TEXT("%s   (%s)"), *S(L[i]->name), *S(tag::playTypeName(L[i]->type))), i == AudibleSel,
					i == AudibleSel ? TEXT("ENTER") : TEXT(""));
			Row(G::R1, Sim->cfg.flip ? TEXT("Flip play (back as called)") : TEXT("Flip play"));
			Row(G::Circle, TEXT("Back"));
		}
		else
		{
			Title = TEXT("COVERAGE AUDIBLE");
			Sub = FString::Printf(TEXT("Now: %s"), *S(Dc.name));
			const std::vector<const tag::DefCall*> L = AudibleDef();
			for (int32 i = 0; i < static_cast<int32>(L.size()); ++i)
				Row(i == AudibleSel ? G::Cross : G::None, FString::Printf(TEXT("%s   (%s%s)"), *S(L[i]->name), CoverageLabel(L[i]->coverage),
					L[i]->blitz.empty() ? TEXT("") : TEXT(", blitz")), i == AudibleSel, i == AudibleSel ? TEXT("ENTER") : TEXT(""));
			Row(G::Circle, TEXT("Back"));
		}
		break;
	case EPre::DLine:
	case EPre::LBs:
	{
		const bool bDL = PreMode == EPre::DLine;
		const tag::PlaySim::LineAdj A = bDL ? Sim->dlAdj : Sim->lbAdj;
		using LA = tag::PlaySim::LineAdj;
		Title = bDL ? TEXT("D-LINE SHIFT") : TEXT("LINEBACKER SHIFT");
		Sub = bDL ? TEXT("Move the front a gap, pinch inside or spread the ends wide") : TEXT("Walk the linebackers over, in or out");
		// shift left / right are the screen's; the sim's ShiftL is toward the offense's left
		Row(G::DLeft, TEXT("Shift left"), A == (RS > 0 ? LA::ShiftR : LA::ShiftL));
		Row(G::DRight, TEXT("Shift right"), A == (RS > 0 ? LA::ShiftL : LA::ShiftR));
		Row(G::DUp, TEXT("Pinch"), A == LA::Pinch);
		Row(G::DDown, TEXT("Spread"), A == LA::Spread);
		Row(G::Square, TEXT("Reset"), A == LA::Base);
		Row(G::Cross, TEXT("Done"));
		break;
	}
	case EPre::Shell:
	{
		using SA = tag::PlaySim::ShellAdj;
		const SA A = Sim->shellAdj;
		Title = TEXT("COVERAGE SHELL");
		Sub = FString::Printf(TEXT("Playing %s"), CoverageLabel(Dc.coverage));
		Row(G::DLeft, TEXT("Press the receivers"), A == SA::Press);
		Row(G::DRight, TEXT("Off coverage"), A == SA::Off);
		Row(G::DUp, TEXT("Show blitz"), A == SA::ShowBlitz);
		Row(G::DDown, TEXT("Disguise the safeties"), A == SA::Disguise);
		Row(G::Square, TEXT("Reset"), A == SA::Base);
		Row(G::Cross, TEXT("Done"));
		break;
	}
	case EPre::Assign:
	{
		const tag::Ent* U = Sim->user;
		Title = TEXT("ASSIGNMENT");
		Sub = U ? PlayerTag(U) : FString();
		const bool bZone = U && U->state == tag::EntState::Zone;
		const std::string Z = U ? U->zone : std::string();
		Row(G::DUp, TEXT("Deep zone"), bZone && tag::PlaySim::deepZone(Z));
		Row(G::DDown, TEXT("Blitz"), U && U->state == tag::EntState::Rush);
		Row(G::DLeft, TEXT("Flat zone"), bZone && Z.rfind("flat", 0) == 0);
		Row(G::DRight, TEXT("Hook zone"), bZone && Z.rfind("hook", 0) == 0);
		Row(G::Square, TEXT("QB spy"), U && U->state == tag::EntState::Spy);
		Row(G::Triangle, TEXT("Man coverage"), U && U->state == tag::EntState::Man);
		Row(G::L1, TEXT("Switch player"));
		Row(G::Cross, TEXT("Done"));
		break;
	}
	default:
		break;
	}
	Overlay->SetPresnapPanel(Title, Sub, R, PadUI());
}

void AMatchDirector::UpdateIcons()
{
	if (!Overlay) return;
	TArray<FArtIcon> Out;
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const bool bPre = State == EMatchState::Presnap && Sim && UserOff() &&
		(Sim->passPlay() || PreMode == EPre::MotionPick || PreMode == EPre::Motion);
	const bool bLive = State == EMatchState::Live && Sim && UserOff() && !Sim->passThrown;
	FVector2D VP(1920, 1080);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(VP);
	if (PC && (bPre || bLive))
	{
		const std::string First = Sim->readOrder.empty() ? std::string() : Sim->readOrder.front();
		for (auto& KV : Labels)
		{
			tag::Ent* E = KV.first;
			if (bLive && (E->state != tag::EntState::Route || Sim->ball.holder != Sim->qb)) continue;
			AAthlete* A = AthleteFor(E->p, E->side == tag::Side::Off ? Game->poss : tag::other(Game->poss));
			if (!A || A->IsHidden()) continue;
			FVector2D Scr;
			if (!PC->ProjectWorldLocationToScreen(A->GetActorLocation() + FVector(0, 0, 245), Scr, true)) continue;
			FArtIcon I;
			I.Screen = Scr;
			I.Button = FCString::Atoi(*KV.second) - 1;
			I.bPrimary = E->role == First && E->state == tag::EntState::Route;
			const std::string* Called = Sim->cfg.offPlay->routeFor(E->role);
			I.bHot = bPre && !E->routeName.empty() && (!Called || *Called != E->routeName) && Sim->passPlay();
			I.bPicked = bPre && E == PreEnt;
			Out.Add(I);
		}
	}
	Overlay->SetIcons(Out, VP, PadUI());
}

void AMatchDirector::UpdateHighlight()
{
	if (!Overlay) return;
	FHighlightCue Cue;
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	tag::Ent* U = Sim ? Sim->user : nullptr;
	static const bool bNoHighlight = FParse::Param(FCommandLine::Get(), TEXT("TagNoHighlight"));
	if (!bNoHighlight && PC && UserTeam && State == EMatchState::Live && U && U->hasBall && Sim->ball.mode == tag::BallMode::Held &&
		(U != Sim->qb || U->state == tag::EntState::Carrier || Sim->scramble) && U->state != tag::EntState::Down)
	{
		const tag::Ent* Th = nullptr;
		const tag::Move M = Sim->suggestMove(U, &Th);
		AAthlete* Me = AthleteFor(U->p, U->side == tag::Side::Off ? Game->poss : tag::other(Game->poss));
		AAthlete* Him = Th ? AthleteFor(Th->p, Th->side == tag::Side::Off ? Game->poss : tag::other(Game->poss)) : nullptr;
		FVector2D A, B, B2;
		if (M != tag::Move::None && Me && Him && PC->ProjectWorldLocationToScreen(Me->GetActorLocation() + FVector(0, 0, 250), A, true) &&
			PC->ProjectWorldLocationToScreen(Him->GetActorLocation() + FVector(0, 0, 4), B, true) &&
			PC->ProjectWorldLocationToScreen(Him->GetActorLocation() + FVector(0, 55, 4), B2, true))
		{
			Cue.bOn = true;
			Cue.Carrier = A;
			Cue.Threat = B;
			Cue.ThreatR = FMath::Clamp(static_cast<float>(FVector2D::Distance(B, B2)) * 1.3f, 18.f, 90.f);
			const bool bPad = PadUI();
			switch (M)
			{
			case tag::Move::Truck: Cue.Move = TEXT("TRUCK"); Cue.Key = bPad ? TEXT("R\u2191") : TEXT("F"); break;
			case tag::Move::StiffArm: Cue.Move = TEXT("STIFF ARM"); Cue.Key = bPad ? TEXT("R\u2193") : TEXT("G"); break;
			case tag::Move::Spin: Cue.Move = TEXT("SPIN"); Cue.Glyph = bPad ? EPadGlyph::Circle : EPadGlyph::None; Cue.Key = bPad ? FString() : TEXT("R"); break;
			case tag::Move::Hurdle: Cue.Move = TEXT("HURDLE"); Cue.Glyph = bPad ? EPadGlyph::Triangle : EPadGlyph::None; Cue.Key = bPad ? FString() : TEXT("V"); break;
			default: Cue.Move = TEXT("JUKE"); Cue.Key = bPad ? TEXT("R\u2194") : TEXT("Q/E"); break;
			}
		}
	}
	Overlay->SetHighlight(Cue);
}

void AMatchDirector::BuildPlayArt()
{
	if (!PlayArt || !Sim) return;
	// the strokes in the sim's yards (shared with the play-call cards), laid on the turf in world space
	PlayArt->Begin();
	if (UserOff() || UserDef())
	{
		FArtPicture Pic;
		BuildArtPicture(*Sim, UserOff(), PreEnt, Sim->user, [this](const tag::Ent* E) { return LabelOf(E); }, Pic);
		auto W = [&](const tag::V2& V) { return ToWorld(V.x, V.y, Dir); };
		for (const FArtStroke& S : Pic.Strokes)
		{
			if (S.Kind == FArtStroke::Zone)
				PlayArt->Zone(W(S.Pts[0]), static_cast<float>(S.RX * YD), static_cast<float>(S.RY * YD), S.Color);
			else
			{
				TArray<FVector> Pts;
				Pts.Reserve(S.Pts.Num());
				for (const tag::V2& V : S.Pts) Pts.Add(W(V));
				PlayArt->Line(Pts, S.Color, S.Width, S.End, S.bGlow);
			}
		}
	}
	PlayArt->Commit();
}

void AMatchDirector::TickArtTest(float Dt)
{
#if WITH_EDITOR
	if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0) return; // wait for shaders
#endif
	if (bPadTest) return TickPadTest(Dt);
	auto Shot = [](const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_art_%s.png"), Name), true, false);
		UE_LOG(LogTemp, Display, TEXT("TAG art test shot %s"), Name);
	};
	if (bArtDidOff && bArtDidDef && (ArtExitT += Dt) > 3.f)
	{
		FPlatformMisc::RequestExit(false);
		return;
	}
	// a throwing icon over the receivers once the ball is snapped
	if (bArtDidOff && !bArtLiveShot && State == EMatchState::Live && UserOff() && Sim && Sim->t > 0.45)
	{
		bArtLiveShot = true;
		Shot(TEXT("o9_live"));
	}
	if (State != EMatchState::Presnap || !Sim || !Sim->preSnap() || !(UserOff() || UserDef()))
	{
		ArtT = 0.f;
		ArtStep = 0;
		return;
	}
	const bool bO = UserOff();
	if (bO ? bArtDidOff : bArtDidDef) return;
	ArtT += Dt;
	ArtAuto = 99.f;
	Timer = 999.f;
	const int32 RS = ScreenRightSign();
	auto ByLabel = [&](int32 N) -> tag::Ent*
	{
		for (auto& KV : Labels)
			if (KV.second == FString::FromInt(N)) return KV.first;
		return nullptr;
	};
	// (time, action) steps; each action runs once
	auto At = [&](float T) { if (ArtT < T || ArtStep >= 100) return false; return true; };
	if (bO)
	{
		switch (ArtStep)
		{
		case 0: if (At(1.6f)) { Shot(TEXT("o1_base")); ++ArtStep; } break;
		case 1: if (At(1.7f)) { PreMode = EPre::HotPick; ++ArtStep; } break;
		case 2: if (At(2.6f)) { Shot(TEXT("o2_pick")); ++ArtStep; } break;
		case 3:
			if (At(2.7f))
			{
				PreEnt = ByLabel(2);
				PreMode = EPre::HotRoute;
				if (PreEnt) Sim->hotRoute(PreEnt, PreEnt->routeName == "go" ? "slant" : "go");
				Sim->previewBlocks();
				++ArtStep;
			}
			break;
		case 4: if (At(3.8f)) { Shot(TEXT("o3_hot")); ++ArtStep; } break;
		case 5: if (At(3.9f)) { PreMode = EPre::Protect; Sim->setProtection(RS, true); Sim->previewBlocks(); ++ArtStep; } break;
		case 6: if (At(4.8f)) { Shot(TEXT("o4_protect")); ++ArtStep; } break;
		case 7:
			if (At(4.9f))
			{
				Sim->setProtection(0, false);
				PreEnt = ByLabel(4);
				PreMode = EPre::Motion;
				if (PreEnt) Sim->motion(PreEnt, RS * 4.0);
				Sim->previewBlocks();
				++ArtStep;
			}
			break;
		case 8: if (At(5.5f)) { Shot(TEXT("o5_motion")); ++ArtStep; } break;
		case 9: if (At(6.0f)) { PreMode = EPre::Audible; AudibleSel = 1; ++ArtStep; } break;
		case 10: if (At(6.8f)) { Shot(TEXT("o6_audible")); ++ArtStep; } break;
		case 11:
			if (At(6.9f))
			{
				for (const tag::OffPlay* P : AudibleOff())
					if (P->type == tag::PlayType::Run) { RebuildPlay(P, Sim->cfg.defCall, Sim->cfg.flip); break; }
				PreMode = EPre::Base;
				++ArtStep;
			}
			break;
		case 12: if (At(8.2f)) { Shot(TEXT("o7_run")); ++ArtStep; } break;
		case 13:
			if (At(8.3f))
			{
				// flip back to a pass out of the same formation
				for (const tag::OffPlay* P : AudibleOff())
					if (P->type == tag::PlayType::Pass) { RebuildPlay(P, Sim->cfg.defCall, !Sim->cfg.flip); break; }
				++ArtStep;
			}
			break;
		case 14: if (At(9.6f)) { Shot(TEXT("o8_flip")); ++ArtStep; } break;
		case 15: if (At(9.8f)) { ArtAuto = 0.f; bArtDidOff = true; Timer = 0.f; ++ArtStep; } break;
		default: break;
		}
	}
	else
	{
		using LA = tag::PlaySim::LineAdj;
		switch (ArtStep)
		{
		case 0: if (At(1.6f)) { Shot(TEXT("d1_base")); ++ArtStep; } break;
		case 1: if (At(1.7f)) { PreMode = EPre::DLine; Sim->adjustLine(LA::Spread); Sim->previewBlocks(); ++ArtStep; } break;
		case 2: if (At(2.8f)) { Shot(TEXT("d2_dline")); ++ArtStep; } break;
		case 3: if (At(2.9f)) { PreMode = EPre::LBs; Sim->adjustLBs(LA::Pinch); Sim->previewBlocks(); ++ArtStep; } break;
		case 4: if (At(3.8f)) { Shot(TEXT("d3_lbs")); ++ArtStep; } break;
		case 5: if (At(3.9f)) { PreMode = EPre::Shell; Sim->adjustShell(tag::PlaySim::ShellAdj::Press); Sim->previewBlocks(); ++ArtStep; } break;
		case 6: if (At(4.9f)) { Shot(TEXT("d4_shell")); ++ArtStep; } break;
		case 7:
			if (At(5.0f))
			{
				PreMode = EPre::Assign;
				if (Sim->user) Sim->assignDefender(Sim->user, tag::PlaySim::DefAssign::Blitz);
				Sim->previewBlocks();
				++ArtStep;
			}
			break;
		case 8: if (At(5.9f)) { Shot(TEXT("d5_assign")); ++ArtStep; } break;
		case 9: if (At(6.0f)) { PreMode = EPre::Audible; AudibleSel = 0; ++ArtStep; } break;
		case 10: if (At(6.8f)) { Shot(TEXT("d6_audible")); ++ArtStep; } break;
		case 11:
			if (At(6.9f))
			{
				const std::vector<const tag::DefCall*> L = AudibleDef();
				if (!L.empty()) RebuildPlay(Sim->cfg.offPlay, L[0], Sim->cfg.flip);
				PreMode = EPre::Base;
				++ArtStep;
			}
			break;
		case 12: if (At(8.0f)) { Shot(TEXT("d7_called")); ++ArtStep; } break;
		case 13: if (At(8.1f)) { ArtAuto = 0.f; bArtDidDef = true; Timer = 0.f; ++ArtStep; } break;
		default: break;
		}
	}
}

void AMatchDirector::PadPress(const FKey& K, float Hold)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;
	FViewport* VP = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
	PC->InputKey(FInputKeyEventArgs(VP, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), K, IE_Pressed, 1.f, false, FPlatformTime::Cycles64()));
	PadHeld.Add(TPair<FKey, float>(K, GetWorld()->GetTimeSeconds() + Hold));
	UE_LOG(LogTemp, Display, TEXT("TAG pad test press %s"), *K.ToString());
}

void AMatchDirector::PadCheck(bool bOk, const TCHAR* What)
{
	(bOk ? PadPasses : PadFails)++;
	UE_LOG(LogTemp, Display, TEXT("TAG pad test %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), What);
}

void AMatchDirector::TickPadTest(float Dt)
{
	// let go of held buttons
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		FViewport* VP = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
		const float Now = GetWorld()->GetTimeSeconds();
		for (int32 i = PadHeld.Num() - 1; i >= 0; --i)
			if (PadHeld[i].Value <= Now)
			{
				PC->InputKey(FInputKeyEventArgs(VP, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), PadHeld[i].Key, IE_Released, 0.f, false, FPlatformTime::Cycles64()));
				PadHeld.RemoveAt(i);
			}
	}
	static bool bAwaitSnap = false;
	static const tag::OffPlay* SavedOp = nullptr;
	static const tag::DefCall* SavedDc = nullptr;
	static std::string SavedRoute;
	static tag::Player* SavedUser = nullptr;
	static double SavedY = 0;
	if (bArtDidOff && bArtDidDef)
	{
		if ((ArtExitT += Dt) > 3.f)
		{
			UE_LOG(LogTemp, Display, TEXT("TAG pad test: %d passed, %d failed"), PadPasses, PadFails);
			FPlatformMisc::RequestExit(false);
		}
		return;
	}
	if (bAwaitSnap && State != EMatchState::Presnap)
	{
		bAwaitSnap = false;
		PadCheck(State == EMatchState::Live, TEXT("cross snaps the ball from the base screen"));
		bArtDidOff = true;
	}
	if (State != EMatchState::Presnap || !Sim || !Sim->preSnap() || !(UserOff() || UserDef()))
	{
		ArtT = 0.f;
		ArtStep = 0;
		return;
	}
	const bool bO = UserOff();
	if (bO ? bArtDidOff : bArtDidDef) return;
	if (bAwaitSnap) return;
	ArtT += Dt;
	Timer = 999.f;
	auto At = [&](float T) { return ArtT >= T; };
	auto Shown = [&] { return PlayArt && PlayArt->IsShown(); };
	auto ByLabel = [&](int32 N) -> tag::Ent*
	{
		for (auto& KV : Labels)
			if (KV.second == FString::FromInt(N)) return KV.first;
		return nullptr;
	};
	auto Next = [&] { ++ArtStep; };
	if (bO)
	{
		switch (ArtStep)
		{
		case 0: if (At(3.0f)) { PadCheck(!Shown(), TEXT("offense: art hidden once the intro is over")); PadPress(EKeys::Gamepad_LeftTrigger, 1.5f); Next(); } break;
		case 1:
			if (At(3.25f) && !bArtDrawShot)
			{
				// the art drawing itself out from the players, a quarter second into the hold
				bArtDrawShot = true;
				FScreenshotRequest::RequestScreenshot(TEXT("tag_pad_drawin.png"), true, false);
			}
			if (At(3.6f)) { PadCheck(Shown(), TEXT("offense: holding L2 shows the play art")); FScreenshotRequest::RequestScreenshot(TEXT("tag_pad_r2.png"), true, false); Next(); }
			break;
		case 2: if (At(5.2f)) { PadCheck(!Shown(), TEXT("offense: letting go of L2 hides it")); PadPress(EKeys::Gamepad_FaceButton_Top); Next(); } break;
		case 3: if (At(5.5f)) { PadCheck(PreMode == EPre::HotPick, TEXT("triangle opens hot routes")); PadPress(EKeys::Gamepad_FaceButton_Right); Next(); } break;
		case 4:
			if (At(5.8f))
			{
				PadCheck(PreMode == EPre::HotRoute && PreEnt && PreEnt == ByLabel(2), TEXT("circle picks receiver 2"));
				SavedRoute = PreEnt ? PreEnt->routeName : std::string();
				PadPress(EKeys::Gamepad_DPad_Up);
				Next();
			}
			break;
		case 5:
			if (At(6.1f))
			{
				PadCheck(PreEnt && PreEnt->routeName != SavedRoute && InFamily(FamUp, PreEnt->routeName), TEXT("D-pad up gives him a vertical route"));
				SavedRoute = PreEnt ? PreEnt->routeName : std::string();
				PadPress(EKeys::Gamepad_DPad_Up);
				Next();
			}
			break;
		case 6:
			if (At(6.4f))
			{
				PadCheck(PreEnt && PreEnt->routeName != SavedRoute && InFamily(FamUp, PreEnt->routeName), TEXT("D-pad up again cycles the vertical routes"));
				FScreenshotRequest::RequestScreenshot(TEXT("tag_pad_hot.png"), true, false);
				PadPress(EKeys::Gamepad_FaceButton_Bottom);
				Next();
			}
			break;
		case 7:
			if (At(6.7f))
			{
				PadCheck(PreMode == EPre::Base && State == EMatchState::Presnap, TEXT("cross closes the hot route menu without snapping"));
				SavedOp = Sim->cfg.offPlay;
				PadPress(EKeys::Gamepad_FaceButton_Left);
				Next();
			}
			break;
		case 8: if (At(7.0f)) { PadCheck(PreMode == EPre::Audible, TEXT("square opens audibles")); PadPress(EKeys::Gamepad_DPad_Down); Next(); } break;
		case 9: if (At(7.3f)) { PadCheck(AudibleSel == 1, TEXT("D-pad down moves the audible list")); PadPress(EKeys::Gamepad_FaceButton_Bottom); Next(); } break;
		case 10:
			if (At(7.6f))
			{
				PadCheck(Sim->cfg.offPlay != SavedOp && PreMode == EPre::Base && State == EMatchState::Presnap, TEXT("cross calls the audible without snapping"));
				if (Sim->passPlay()) PadPress(EKeys::Gamepad_LeftShoulder);
				else ArtStep = 12;  // a run: no protection calls
				Next();
			}
			break;
		case 11: if (At(7.9f)) { PadCheck(PreMode == EPre::Protect, TEXT("L1 opens pass protection")); PadPress(EKeys::Gamepad_DPad_Left); Next(); } break;
		case 12: if (At(8.2f)) { PadCheck(Sim->slide != 0, TEXT("D-pad left slides the line")); PadPress(EKeys::Gamepad_FaceButton_Right); Next(); } break;
		case 13: if (At(8.5f)) { PadCheck(PreMode == EPre::Base, TEXT("back to the base screen")); PadPress(EKeys::Gamepad_FaceButton_Right); Next(); } break;
		case 14: if (At(8.8f)) { PadCheck(PreMode == EPre::MotionPick, TEXT("circle opens motion")); PadPress(EKeys::Gamepad_FaceButton_Bottom); Next(); } break;
		case 15:
			if (At(9.1f))
			{
				PadCheck(PreMode == EPre::Motion && PreEnt == ByLabel(1), TEXT("cross picks receiver 1 for motion"));
				SavedY = PreEnt ? (PreEnt->shiftTo ? PreEnt->shiftTo->y : PreEnt->pos.y) : 0;
				PadPress(EKeys::Gamepad_DPad_Right);
				Next();
			}
			break;
		case 16:
			if (At(9.4f))
			{
				const double Y = PreEnt ? (PreEnt->shiftTo ? PreEnt->shiftTo->y : PreEnt->pos.y) : SavedY;
				PadCheck(FMath::Abs(Y - SavedY) > 1.0, TEXT("D-pad right sends him in motion"));
				PadPress(EKeys::Gamepad_FaceButton_Bottom); // (Madden: CROSS snaps with the man in motion)
				bAwaitSnap = true;
				Next();
			}
			break;
		default: break;
		}
	}
	else
	{
		switch (ArtStep)
		{
		case 0: if (At(3.0f)) { PadCheck(!Shown(), TEXT("defense: art hidden once the intro is over")); PadPress(EKeys::Gamepad_LeftTrigger, 1.5f); Next(); } break;
		case 1: if (At(3.6f)) { PadCheck(Shown(), TEXT("defense: holding L2 shows the play art")); FScreenshotRequest::RequestScreenshot(TEXT("tag_pad_def_r2.png"), true, false); Next(); } break;
		case 2: if (At(5.2f)) { PadCheck(!Shown(), TEXT("defense: letting go of L2 hides it")); PadPress(EKeys::Gamepad_DPad_Up); Next(); } break;
		case 3: if (At(5.5f)) { PadCheck(PreMode == EPre::DLine, TEXT("D-pad up opens the D-line shifts")); PadPress(EKeys::Gamepad_DPad_Right); Next(); } break;
		case 4:
			if (At(5.8f))
			{
				PadCheck(Sim->dlAdj == tag::PlaySim::LineAdj::ShiftL || Sim->dlAdj == tag::PlaySim::LineAdj::ShiftR, TEXT("D-pad right shifts the line"));
				PadPress(EKeys::Gamepad_FaceButton_Bottom);
				Next();
			}
			break;
		case 5: if (At(6.1f)) { PadCheck(PreMode == EPre::Base && State == EMatchState::Presnap, TEXT("cross closes the D-line menu")); PadPress(EKeys::Gamepad_FaceButton_Bottom); Next(); } break;
		case 6: if (At(6.4f)) { PadCheck(PreMode == EPre::Assign, TEXT("cross opens the defensive hot route")); PadPress(EKeys::Gamepad_DPad_Down); Next(); } break;
		case 7:
			if (At(6.7f))
			{
				PadCheck(Sim->user && Sim->user->state == tag::EntState::Rush, TEXT("D-pad down sends him on a blitz"));
				PadPress(EKeys::Gamepad_FaceButton_Right);
				Next();
			}
			break;
		case 8:
			if (At(7.0f))
			{
				PadCheck(PreMode == EPre::Base, TEXT("circle closes the assignment"));
				SavedUser = Sim->user ? Sim->user->p : nullptr;
				PadPress(EKeys::Gamepad_LeftShoulder);
				Next();
			}
			break;
		case 9: if (At(7.3f)) { PadCheck(Sim->user && Sim->user->p != SavedUser, TEXT("L1 switches player")); PadPress(EKeys::Gamepad_FaceButton_Top); Next(); } break;
		case 10: if (At(7.6f)) { PadCheck(PreMode == EPre::Shell, TEXT("triangle opens the coverage audibles")); PadPress(EKeys::Gamepad_DPad_Left); Next(); } break;
		case 11:
			if (At(7.9f))
			{
				PadCheck(Sim->shellAdj == tag::PlaySim::ShellAdj::Press, TEXT("D-pad left presses the receivers"));
				PadPress(EKeys::Gamepad_FaceButton_Bottom);
				Next();
			}
			break;
		case 12:
			if (At(8.2f))
			{
				PadCheck(PreMode == EPre::Base, TEXT("cross closes the shell menu"));
				SavedDc = Sim->cfg.defCall;
				PadPress(EKeys::Gamepad_FaceButton_Left);
				Next();
			}
			break;
		case 13: if (At(8.5f)) { PadCheck(PreMode == EPre::Audible, TEXT("square opens coverage audibles")); PadPress(EKeys::Gamepad_FaceButton_Bottom); Next(); } break;
		case 14:
			if (At(8.8f))
			{
				PadCheck(Sim->cfg.defCall != SavedDc && PreMode == EPre::Base, TEXT("cross calls the coverage audible"));
				Next();
			}
			break;
		case 15:
			if (At(9.6f))
			{
				// the defender walked on the stick (W held a second), then CIRCLE to the next man
				if (Sim->user) PadMovedFrom = Sim->user->pos;
				SavedUser = Sim->user ? Sim->user->p : nullptr;
				PadPress(EKeys::W, 1.0f);
				Next();
			}
			break;
		case 16:
			if (At(10.9f))
			{
				const double Moved = Sim->user ? tag::dist(Sim->user->pos, PadMovedFrom) : 0.0;
				PadCheck(Sim->user && Sim->user->p == SavedUser && Moved > 1.5, *FString::Printf(TEXT("the stick walks the defender pre-snap (%.1f yd)"), Moved));
				FScreenshotRequest::RequestScreenshot(TEXT("tag_pad_def_move.png"), true, false);
				PadPress(EKeys::Gamepad_FaceButton_Right);
				Next();
			}
			break;
		case 17:
			if (At(11.2f))
			{
				PadCheck(Sim->user && Sim->user->p != SavedUser, TEXT("circle switches player"));
				bArtDidDef = true;
				Timer = 0.f;
				Next();
			}
			break;
		default: break;
		}
	}
}

void AMatchDirector::PlayFakeSnap()
{
	if (!Sim || !Sim->qb || !Game) return;
	AAthlete* Q = AthleteFor(Sim->qb->p, Game->poss);
	if (!Q) return;
	// the pack's fake snaps (a dozen quarterbacks' own, shotgun and under center)
	const bool bGun = Sim->qb->pos.x < Sim->los - 2.5;
	static const TCHAR* Gun[] = {TEXT("FakeSnap_Shotgun_Brady"), TEXT("FakeSnap_Shotgun_PManning"), TEXT("FakeSnap_Shotgun_Brees"), TEXT("FakeSnap_Shotgun_ARodgers"), TEXT("FakeSnap_Shotgun_Romo")};
	static const TCHAR* Uc[] = {TEXT("FakeSnap_UnderCenter_Brady_MADNMAY04_0964"), TEXT("FakeSnap_UnderCenter_Bulger_MADNMAY04_0964"), TEXT("FakeSnap_UnderCenter_Delhomme_MADNMAY04_0964")};
	static int32 GSeq = 0;
	const int32 N = bGun ? UE_ARRAY_COUNT(Gun) : UE_ARRAY_COUNT(Uc);
	const FString Name = FString::Printf(TEXT("A_TAG_Pack_%s__P01"), bGun ? Gun[GSeq % N] : Uc[GSeq % N]);
	++GSeq;
	if (UAnimSequence* C = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/TAG/AnimH/Pack/Kicking/%s.%s"), *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
		Q->PlayOneShot(C);
	if (Overlay) Overlay->Toast(TEXT("Hard count"));
	UE_LOG(LogTemp, Display, TEXT("TAG fake snap: %s"), *Name);
}
