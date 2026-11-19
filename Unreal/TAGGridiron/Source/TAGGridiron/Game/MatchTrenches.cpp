// The trenches' bars, Madden style: over each engaged pass rush in a user game on defense, the rusher's energy (blue:
// his moves cost it) and his blocker's power (orange: worn by the rep's length, a pile going backwards and the power
// moves he takes), a tick for who is winning the rep, and the move just tried. The sim keeps the numbers
// (tag::Ent::rushEnergy / blockPower, PlaySim::rushMove); this is the drawing.
#include "Game/MatchDirector.h"

#include "Render/Athlete.h"
#include "UI/GridironOverlay.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"

void AMatchDirector::UpdateLineBars()
{
	if (!Overlay) return;
	TArray<FLineBarUI> Bars;
	FVector2D VP(1920, 1080);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(VP);
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	static const bool bAllBars = FParse::Param(FCommandLine::Get(), TEXT("TagLineBars")); // automation: shown in a CPU game
	if (Sim && Game && PC && State == EMatchState::Live && (UserDef() || bAllBars) && !Sim->passThrown)
	{
		const tag::TeamKey OffK = Game->poss;
		auto AthOf = [&](const tag::Ent* X) { return AthleteFor(X->p, X->side == tag::Side::Off ? OffK : tag::other(OffK)); };
		// -TagAutoRush (with -TagLineBars, look-dev): the first engaged rusher works the user's moves in turn, a shot with
		// the bars up after each
		static const bool bAutoRush = FParse::Param(FCommandLine::Get(), TEXT("TagAutoRush"));
		static int32 GRushSeq = 0, GRushShots = 0;
		const tag::Ent* AutoD = nullptr;
		if (bAutoRush && Sim->t > 0.9)
			for (tag::Ent* D : Sim->def)
				if (!D->engagedBy.empty() && D->isDL && D->engagedBy[0]->state == tag::EntState::PassBlock)
				{
					AutoD = D;
					if (D->moveCd <= 0 && Sim->t - D->lastMoveT > 1.0)
					{
						static const char* Mv[] = {"swim", "bull", "spin", "club", "rip", "longarm"};
						Sim->rushMove(D, Mv[GRushSeq++ % 6]);
					}
					static double GShotFor = -1.0;
					if (D->lastMoveT != GShotFor && Sim->t - D->lastMoveT > 0.35 && GRushShots < 10)
					{
						GShotFor = D->lastMoveT;
						FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_rush_%d.png"), GRushShots++), true, false);
					}
					break;
				}
		for (const tag::Ent* D : Sim->def)
		{
			if (D->engagedBy.empty()) continue;
			const tag::Ent* B = D->engagedBy[0];
			// (only the user's own rush, or the look-dev rusher: bars over every pass-blocking pair gave the play away)
			if (!D->userControlled && !(bAllBars && bAutoRush && D == AutoD)) continue;
			AAthlete* Ad = AthOf(D);
			AAthlete* Ab = AthOf(B);
			if (!Ad || !Ab || Ad->IsHidden()) continue;
			FVector2D Scr;
			if (!PC->ProjectWorldLocationToScreen((Ad->GetActorLocation() + Ab->GetActorLocation()) * 0.5f + FVector(0, 0, 205), Scr, true)) continue;
			FLineBarUI L;
			L.Screen = Scr;
			L.Energy = static_cast<float>(D->rushEnergy);
			double Pw = 1.0;
			for (const tag::Ent* Bb : D->engagedBy) Pw = FMath::Min(Pw, Bb->blockPower);
			L.Power = static_cast<float>(Pw);
			L.Edge = static_cast<float>(D->linePush);
			L.bUser = D->userControlled || (bAutoRush && D == AutoD);
			// (the move he tried is called out over the user's own man only: over every pair it was a wall of text)
			if (L.bUser && !D->lastMove.empty() && Sim->t - D->lastMoveT < 1.3)
			{
				static const TMap<FString, FString> Names = {{TEXT("swim"), TEXT("SWIM")}, {TEXT("rip"), TEXT("RIP")}, {TEXT("spin"), TEXT("SPIN")},
					{TEXT("bull"), TEXT("BULL RUSH")}, {TEXT("club"), TEXT("CLUB")}, {TEXT("longarm"), TEXT("LONG ARM")}, {TEXT("diprip"), TEXT("DIP & RIP")}};
				const FString K = UTF8_TO_TCHAR(D->lastMove.c_str());
				const FString* N = Names.Find(K);
				L.Move = (N ? *N : K.ToUpper()) + (D->lastMoveWon ? TEXT("  WON") : TEXT("  STONED"));
				L.bMoveWon = D->lastMoveWon;
			}
			Bars.Add(L);
		}
	}
	Overlay->SetLineBars(Bars, VP);
	if (bMulti)
	{
		// every player's man in his colour, his sprint under him (Game/MatchMulti.cpp)
		UpdateHumanMarkers(PC, VP);
		Overlay->SetStamina(FVector2D::ZeroVector, VP, -1.f, false);
		return;
	}
	{
		FUserMarkerUI M;
		M.Viewport = VP;
		const bool bShow = Sim && Game && PC && (State == EMatchState::Live || State == EMatchState::Presnap) && Sim->user && (UserOff() || UserDef());
		AAthlete* U = bShow ? AthleteFor(Sim->user->p, Sim->user->side == tag::Side::Off ? Game->poss : tag::other(Game->poss)) : nullptr;
		if (U && !U->IsHidden())
		{
			const FVector Feet = FVector(U->GetActorLocation().X, U->GetActorLocation().Y, 3.f);
			const FVector Fw = U->GetActorForwardVector().GetSafeNormal2D();
			FVector2D S;
			bool bOk = true;
			for (int32 k = 0; k <= 36 && bOk; ++k)
			{
				const float A = 2.f * PI * k / 36.f;
				bOk = PC->ProjectWorldLocationToScreen(Feet + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * 62.f, S, true);
				M.Ring.Add(S);
			}
			// the notch: a short wedge out of the ring the way he faces
			const FVector Rt(-Fw.Y, Fw.X, 0.f);
			for (const FVector& Pt : {Feet + Fw * 62.f + Rt * 20.f, Feet + Fw * 92.f, Feet + Fw * 62.f - Rt * 20.f})
			{
				bOk = bOk && PC->ProjectWorldLocationToScreen(Pt, S, true);
				M.Notch.Add(S);
			}
			bOk = bOk && PC->ProjectWorldLocationToScreen(U->GetActorLocation() + FVector(0, 0, 225.f), M.Head, true);
			M.bOn = bOk;
			M.Number = Sim->user->p ? FString::FromInt(Sim->user->p->number) : FString();
		}
		Overlay->SetUserMarker(M);
	}
	// the user's sprint stamina, under his man's feet while the play is live (pad users: R2 is the sprint)
	{
		FVector2D Scr;
		const bool bShow = Sim && Game && PC && PadUI() && State == EMatchState::Live && Sim->user && (UserOff() || UserDef());
		AAthlete* U = bShow ? AthleteFor(Sim->user->p, Sim->user->side == tag::Side::Off ? Game->poss : tag::other(Game->poss)) : nullptr;
		if (U && !U->IsHidden() && PC->ProjectWorldLocationToScreen(U->GetActorLocation(), Scr, true)) Overlay->SetStamina(Scr, VP, SprintStamina, bSprinting);
		else Overlay->SetStamina(FVector2D::ZeroVector, VP, -1.f, false);
	}
}
