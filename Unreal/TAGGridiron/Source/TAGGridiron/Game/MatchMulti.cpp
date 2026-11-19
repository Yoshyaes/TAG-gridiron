// Local multiplayer: up to four people on one screen. Each joins on his own pad (or the keyboard), picks a side in the
// lobby (UI/Lobby.cpp), and drives one man at a time; on a side with two or more, one of them calls the plays.
//
// The play's sim has a user slot per person on the field (PlaySim::mates; slot 0 is user / input). Slot 0 is the play's
// first person: the offense's caller on the quarterback, or - against a CPU offense - the defense's caller. The rest
// get a man of their own (a back or a receiver; a linebacker, a safety...) and keep him from play to play. Every
// person's controls are read from his own device through FTagInputHub (Game/TagInput.h): his man and his control state
// are swapped into slot 0 and the director's members while his input is read, so the one-player code serves all four.
// A menu one person works (his side's play call, the try) takes only his device's buttons.
#include "Game/MatchDirector.h"

#include "Render/Athlete.h"
#include "Render/Coords.h"
#include "UI/GridironOverlay.h"
#include "Audio/TagAudio.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "MoviePlayer.h"

using namespace TagCoords;

// ------------------------------------------------------------------ who is who
bool AMatchDirector::PadUI() const
{
	if (!bMulti) return bPadActive;
	// the buttons drawn are the ones of the person the screen is for: the menu's owner, the kicker's caller, the
	// player in slot 0
	int32 H = MenuOwner;
	if (!Humans.IsValidIndex(H) && UK.bOn && Game) H = CallerOf(UK.Kind == 0 ? Game->kicking : Game->poss);
	if (!Humans.IsValidIndex(H))
		for (int32 i = 0; i < Humans.Num(); ++i)
			if (Humans[i].Slot == 0) H = i;
	return Humans.IsValidIndex(H) ? Humans[H].bPad : bPadActive;
}

bool AMatchDirector::TeamHuman(tag::TeamKey K) const
{
	if (!bMulti) return UserTeam.has_value() && *UserTeam == K;
	for (const FTagHuman& H : Humans)
		if (H.Team >= 0 && KeyOf(H.Team) == K) return true;
	return false;
}

int32 AMatchDirector::CallerOf(tag::TeamKey K) const
{
	int32 First = -1;
	for (int32 i = 0; i < Humans.Num(); ++i)
	{
		if (Humans[i].Team < 0 || KeyOf(Humans[i].Team) != K) continue;
		if (Humans[i].bCaller) return i;
		if (First < 0) First = i;
	}
	return First;
}

bool AMatchDirector::LobbyReady() const
{
	for (const FTagHuman& H : Humans)
		if (H.Team >= 0) return true;
	return false;
}

FString AMatchDirector::HumanDevice(int32 H) const
{
	if (!Humans.IsValidIndex(H)) return FString();
	const FTagDevice* D = FTagInputHub::Get().Dev(Humans[H].Dev);
	return D ? D->Label() : FString(TEXT("Controller"));
}

bool AMatchDirector::TeamHumanInLobby(bool bHome) const
{
	for (const FTagHuman& H : Humans)
		if (H.Team == (bHome ? 1 : 0)) return true;
	return false;
}

FString AMatchDirector::LobbyTags(bool bHome) const
{
	FString Out;
	for (int32 i = 0; i < Humans.Num(); ++i)
		if (Humans[i].Team == (bHome ? 1 : 0)) Out += (Out.IsEmpty() ? TEXT("") : TEXT("  ")) + HumanTag(i) + (Humans[i].bCaller && [this, bHome] { int32 N = 0; for (const FTagHuman& O : Humans) N += O.Team == (bHome ? 1 : 0) ? 1 : 0; return N; }() > 1 ? TEXT("*") : TEXT(""));
	return Out;
}

void AMatchDirector::LobbySwapSides()
{
	for (FTagHuman& H : Humans)
		if (H.Team >= 0) H.Team = 1 - H.Team;
}

TArray<int32> AMatchDirector::HumanDevices(int32 H) const
{
	TArray<int32> Out;
	if (!Humans.IsValidIndex(H)) return Out;
	Out.Add(Humans[H].Dev);
	Out.Append(Humans[H].Twins);
	return Out;
}

void AMatchDirector::SetMenuOwner(int32 H)
{
	MenuOwner = bMulti && Humans.IsValidIndex(H) ? H : -1;
	if (!Overlay) return;
	if (MenuOwner >= 0)
		Overlay->SetCallWho(FString::Printf(TEXT("%s CALLING  -  %s"), *HumanTag(MenuOwner), *HumanDevice(MenuOwner).ToUpper()), PlayerColor(MenuOwner));
	else Overlay->SetCallWho(FString(), FLinearColor::White);
}

FTagReader AMatchDirector::PrimaryReader() const
{
	if (!bMulti)
	{
		FTagReader R(GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr);
		R.bPad = PadUI();
		return R;
	}
	for (const FTagHuman& H : Humans)
		if (H.Slot == 0) return H.Reader();
	return FTagReader();
}

// ------------------------------------------------------------------ the lobby
void AMatchDirector::ShowLobby()
{
	State = EMatchState::Menu;
	bLobby = true;
	bMulti = false;
	SetMenuOwner(-1);
	FTagInputHub::Get(); // (registered with Slate now, before the first button)
	if (Overlay) Overlay->ShowLobby();
}

void AMatchDirector::LobbyJoin(int32 Dev)
{
	const FTagDevice* D = FTagInputHub::Get().Dev(Dev);
	if (!D || Humans.Num() >= 4) return;
	FTagHuman H;
	H.Dev = Dev;
	H.bPad = D->bPad;
	Humans.Add(H);
	ATagAudio::Ui(TEXT("UiSelect"));
	UE_LOG(LogTemp, Display, TEXT("TAG lobby: %s joined on %s (device %d)"), *HumanTag(Humans.Num() - 1), *D->Label(), Dev);
}

void AMatchDirector::TickLobby()
{
	if (!bLobby || !Overlay) return;
	static double LobbyOpened = 0.0;
	static bool bWasLobby = false;
	if (!bWasLobby) LobbyOpened = FPlatformTime::Seconds();
	bWasLobby = bLobby;
	// (the button that opened the lobby is not a join)
	if (FPlatformTime::Seconds() - LobbyOpened < 0.3) return;
	FTagInputHub& Hub = FTagInputHub::Get();
	auto OwnerOf = [this](int32 Dev)
	{
		for (int32 i = 0; i < Humans.Num(); ++i)
			if (Humans[i].Dev == Dev || Humans[i].Twins.Contains(Dev)) return i;
		return -1;
	};
	bool bChanged = false, bGo = false, bBack = false;
	TSet<int32> Done; // (a pad seen twice moves its player once)
	// a twin that presses what its player did not is a pad of its own after all
	for (FTagHuman& Hm : Humans)
		for (int32 t = Hm.Twins.Num() - 1; t >= 0; --t)
			for (const TPair<FKey, double>& P : Hub.Dev(Hm.Twins[t]) ? Hub.Dev(Hm.Twins[t])->PressTime : TMap<FKey, double>())
				if (Hub.Pressed(Hm.Twins[t], P.Key) && FMath::Abs(Hub.PressTime(Hm.Dev, P.Key) - P.Value) > 0.15)
				{
					UE_LOG(LogTemp, Display, TEXT("TAG lobby: device %d is not %s's pad after all"), Hm.Twins[t], *HumanTag(static_cast<int32>(&Hm - Humans.GetData())));
					Hm.Twins.RemoveAt(t);
					break;
				}
	for (const int32 D : Hub.PressedThisFrame())
	{
		const FTagDevice* Dv = Hub.Dev(D);
		if (!Dv) continue;
		const FTagReader R(D, Dv->bPad);
		auto P = [&R](std::initializer_list<FKey> Ks)
		{
			for (const FKey& K : Ks)
				if (R.WasInputKeyJustPressed(K)) return true;
			return false;
		};
		int32 H = OwnerOf(D);
		if (H < 0)
		{
			// the same pad through a second interface (an Xbox pad is XInput's and GameInput's): the same button within
			// a tenth of a second of a joined pad's - it is his, not a new player
			// (only through another interface: two GameInput pads pressed together are two players)
			auto Iface = [](const FTagDevice* X) { FString L, R; return X && X->Hardware.Split(TEXT("/"), &L, &R) ? L : FString(); };
			bool bTwin = false;
			if (Dv->bPad && !Iface(Dv).IsEmpty())
				for (FTagHuman& Hm : Humans)
				{
					if (!Hm.bPad || Iface(Hub.Dev(Hm.Dev)) == Iface(Dv) || Iface(Hub.Dev(Hm.Dev)).IsEmpty()) continue;
					for (const FKey& K : Dv->Down)
					{
						const double A = Hub.PressTime(D, K), B = Hub.PressTime(Hm.Dev, K);
						if (A > 0 && B > 0 && FMath::Abs(A - B) < 0.1)
						{
							Hm.Twins.AddUnique(D);
							bTwin = true;
							UE_LOG(LogTemp, Display, TEXT("TAG lobby: device %d is %s's pad again (a second interface)"), D, *HumanTag(static_cast<int32>(&Hm - Humans.GetData())));
							break;
						}
					}
					if (bTwin) break;
				}
			if (bTwin) continue;
			if (P({EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_Special_Right, EKeys::Enter, EKeys::SpaceBar}))
			{
				if (Humans.Num() < 4) { LobbyJoin(D); bChanged = true; }
			}
			else if (Humans.Num() == 0 && P({EKeys::Gamepad_FaceButton_Right, EKeys::Escape, EKeys::BackSpace})) bBack = true;
			continue;
		}
		if (Done.Contains(H)) continue;
		Done.Add(H);
		FTagHuman& Hm = Humans[H];
		const int32 Was = Hm.Team;
		if (P({EKeys::Gamepad_DPad_Left, EKeys::Gamepad_LeftStick_Left, EKeys::Left, EKeys::A})) Hm.Team = Hm.Team == 1 ? -1 : 0;
		else if (P({EKeys::Gamepad_DPad_Right, EKeys::Gamepad_LeftStick_Right, EKeys::Right, EKeys::D})) Hm.Team = Hm.Team == 0 ? -1 : 1;
		if (Hm.Team != Was)
		{
			ATagAudio::Ui(TEXT("UiMove"));
			Hm.bCaller = false;
			bChanged = true;
		}
		if (P({EKeys::Gamepad_FaceButton_Top, EKeys::C}) && Hm.Team >= 0)
		{
			// he calls his side's plays
			for (FTagHuman& O : Humans)
				if (O.Team == Hm.Team) O.bCaller = false;
			Hm.bCaller = true;
			ATagAudio::Ui(TEXT("UiTab"));
			bChanged = true;
			UE_LOG(LogTemp, Display, TEXT("TAG lobby: %s calls the plays for the %s side"), *HumanTag(H), Hm.Team == 1 ? TEXT("home") : TEXT("away"));
		}
		if (P({EKeys::Gamepad_FaceButton_Right, EKeys::BackSpace, EKeys::Escape}))
		{
			ATagAudio::Ui(TEXT("UiBack"));
			if (Hm.Team >= 0) { Hm.Team = -1; Hm.bCaller = false; }
			else
			{
				UE_LOG(LogTemp, Display, TEXT("TAG lobby: %s left"), *HumanTag(H));
				Humans.RemoveAt(H);
				Done.Reset(); // (the indexes moved)
			}
			bChanged = true;
			continue;
		}
		if (P({EKeys::Gamepad_Special_Right, EKeys::Enter}) && Was == Hm.Team) bGo = true;
	}
	// one caller on each side with people on it (the first there, unless someone took it)
	for (int32 T = 0; T < 2; ++T)
	{
		int32 Callers = 0, First = -1;
		for (int32 i = 0; i < Humans.Num(); ++i)
		{
			if (Humans[i].Team != T) { Humans[i].bCaller = Humans[i].Team >= 0 && Humans[i].bCaller; continue; }
			if (First < 0) First = i;
			Callers += Humans[i].bCaller ? 1 : 0;
		}
		if (First >= 0 && Callers == 0) { Humans[First].bCaller = true; bChanged = true; }
	}
	for (FTagHuman& Hm : Humans)
		if (Hm.Team < 0) Hm.bCaller = false;
	if (bBack)
	{
		bLobby = false;
		bWasLobby = false;
		ATagAudio::Ui(TEXT("UiBack"));
		ShowMainMenu();
		return;
	}
	if (bGo && LobbyReady())
	{
		bLobby = false;
		bWasLobby = false;
		ATagAudio::Ui(TEXT("UiSelect"));
		FString Line;
		for (int32 i = 0; i < Humans.Num(); ++i)
			Line += FString::Printf(TEXT(" %s=%s%s"), *HumanTag(i), Humans[i].Team == 1 ? TEXT("home") : Humans[i].Team == 0 ? TEXT("away") : TEXT("watch"), Humans[i].bCaller ? TEXT("(calls)") : TEXT(""));
		UE_LOG(LogTemp, Display, TEXT("TAG lobby done:%s"), *Line);
		Overlay->bSelMulti = true;
		Overlay->ShowTeamSelect(true);
		return;
	}
	if (bChanged) Overlay->ShowLobby();
}

void AMatchDirector::StartMultiMatch(int HomeIdx, int AwayIdx, int WeatherKind, bool bNight, double QuarterLen)
{
	if (!LobbyReady()) return;
	bool bHomeFirst = true;
	for (const FTagHuman& H : Humans)
		if (H.Team >= 0) { bHomeFirst = H.Team == 1; break; }
	bMultiNext = true;
	FString Line;
	for (int32 i = 0; i < Humans.Num(); ++i)
		Line += FString::Printf(TEXT(" %s %s %s%s;"), *HumanTag(i), *HumanDevice(i), Humans[i].Team == 1 ? TEXT("home") : Humans[i].Team == 0 ? TEXT("away") : TEXT("watching"),
			Humans[i].bCaller ? TEXT(" (calls the plays)") : TEXT(""));
	UE_LOG(LogTemp, Display, TEXT("TAG multiplayer match:%s"), *Line);
	StartMatch(HomeIdx, AwayIdx, true, bHomeFirst, WeatherKind, bNight, QuarterLen);
}

// ------------------------------------------------------------------ each play's men
void AMatchDirector::SyncHumanLast()
{
	if (!bMulti || !Sim) return;
	for (FTagHuman& H : Humans)
		if (H.Slot >= 0)
			if (const tag::Ent* E = Sim->slotEnt(H.Slot)) H.Last = E->p;
}

void AMatchDirector::SetupHumans()
{
	if (!bMulti || !Sim || !Game) return;
	for (FTagHuman& H : Humans)
	{
		H.Slot = -1;
		H.Ctl.PadThrowIdx = H.Ctl.PadTapIdx = -1;
		H.Ctl.bSwitchHeld = H.Ctl.bPreMoved = false;
	}
	const tag::TeamKey OffK = Game->poss;
	auto SideOf = [OffK](const FTagHuman& H) { return KeyOf(H.Team) == OffK ? tag::Side::Off : tag::Side::Def; };
	auto EntOf = [this](tag::Player* P, tag::Side S) -> tag::Ent*
	{
		if (!P) return nullptr;
		for (tag::Ent* E : Sim->ents)
			if (E->p == P && E->side == S) return E;
		return nullptr;
	};
	const bool bReturn = Sim->isReturn();
	// slot 0: the man the sim gave the play's first side (the quarterback, the middle linebacker, the returner, the man
	// nearest the kick's landing spot), for that side's caller
	if (Sim->user)
	{
		const tag::Side S0 = Sim->user->side;
		const int32 P0 = CallerOf(S0 == tag::Side::Off ? OffK : tag::other(OffK));
		if (Humans.IsValidIndex(P0))
		{
			Humans[P0].Slot = 0;
			// a defender keeps the man he had (the caller on offense is the quarterback)
			if (S0 == tag::Side::Def && !bReturn)
				if (tag::Ent* E = EntOf(Humans[P0].Last, S0)) Sim->setSlotEnt(0, E);
		}
		else Sim->setSlotEnt(0, nullptr);
	}
	// everyone else on a side: a man of his own (the one he had, if he is on the field and free)
	for (FTagHuman& H : Humans)
	{
		if (H.Team < 0 || H.Slot == 0) continue;
		const tag::Side S = SideOf(H);
		tag::Ent* Want = bReturn ? nullptr : EntOf(H.Last, S);
		if (Want && Want == Sim->qb) Want = nullptr;
		H.Slot = Sim->addMate(S, Want);
	}
	FString Line;
	for (int32 i = 0; i < Humans.Num(); ++i)
	{
		const tag::Ent* E = Humans[i].Slot >= 0 ? Sim->slotEnt(Humans[i].Slot) : nullptr;
		Line += FString::Printf(TEXT(" %s slot %d %s;"), *HumanTag(i), Humans[i].Slot, E ? UTF8_TO_TCHAR(E->role.c_str()) : TEXT("-"));
	}
	UE_LOG(LogTemp, Display, TEXT("TAG multiplayer men:%s"), *Line);
}

void AMatchDirector::SwapHuman(FTagHuman& H)
{
	if (Sim && H.Slot > 0 && H.Slot - 1 < static_cast<int32>(Sim->mates.size()))
	{
		std::swap(Sim->user, Sim->mates[H.Slot - 1].ent);
		std::swap(Sim->input, Sim->mates[H.Slot - 1].input);
	}
	FHumanCtl& C = H.Ctl;
	Swap(PadThrowIdx, C.PadThrowIdx);
	Swap(PadTapIdx, C.PadTapIdx);
	Swap(PadThrowT0, C.PadThrowT0);
	Swap(PadTapT, C.PadTapT);
	Swap(bRStickArmed, C.bRStickArmed);
	Swap(SprintStamina, C.SprintStamina);
	Swap(bSprinting, C.bSprinting);
	Swap(bSprintSpent, C.bSprintSpent);
	Swap(bSwitchHeld, C.bSwitchHeld);
	Swap(bSwitchPicked, C.bSwitchPicked);
	Swap(bSwitchStickArmed, C.bSwitchStickArmed);
	Swap(SwitchHeldT, C.SwitchHeldT);
	Swap(bPreMoved, C.bPreMoved);
	Swap(bCarrierHint, C.bCarrierHint);
	Swap(bRushHint, C.bRushHint);
}

int32 AMatchDirector::DefMenuHuman() const
{
	if (!bMulti || !Sim || !Game || !UserOff() || !UserDef()) return -1;
	const int32 H = CallerOf(tag::other(Game->poss));
	return Humans.IsValidIndex(H) && Humans[H].Slot > 0 ? H : -1;
}

void AMatchDirector::SwapDefMenus()
{
	Swap(PreMode, DefPreMode);
	Swap(AudibleSel, DefAudibleSel);
	Swap(bPresnapPanelHidden, bDefPanelHidden);
}

void AMatchDirector::TickMatePresnap(float Dt)
{
	if (!bMulti || !Sim || !Sim->preSnap() || PresnapClock <= 0.35f) return;
	const int32 DefCaller = DefMenuHuman();
	for (FTagHuman& H : Humans)
	{
		if (H.Slot <= 0) continue;
		tag::Ent* E = Sim->slotEnt(H.Slot);
		if (!E) continue;
		const FTagReader R = H.Reader();
		auto P = [&R](std::initializer_list<FKey> Ks)
		{
			for (const FKey& K : Ks)
				if (R.WasInputKeyJustPressed(K)) return true;
			return false;
		};
		const int32 Me = static_cast<int32>(&H - Humans.GetData());
		if (Me == DefCaller && E->side == tag::Side::Def)
		{
			// head to head, the defense's caller: the defense's whole pre-snap menu on his own pad (audible, coverage
			// shell, line and linebacker shifts, his man's assignment, walk + switch), with his own panel (bottom left)
			SwapHuman(H);
			SwapDefMenus();
			const bool bWasUsed = bPreConsumed; // (his buttons are not the snap's: the offense's caller's are)
			if (P({EKeys::Gamepad_RightThumbstick, EKeys::F1})) bPresnapPanelHidden = !bPresnapPanelHidden;
			PresnapDefMenus(R, Dt);
			bPreConsumed = bWasUsed;
			if (Overlay)
			{
				FString Title, Sub;
				TArray<FPreRow> Rows;
				if (PresnapPanelRows(false, R.IsPad(), R, Title, Sub, Rows))
					Overlay->SetDefPresnapPanel(Title, Sub, Rows, R.IsPad(), HumanTag(Me), PlayerColor(Me));
				else Overlay->ClearDefPresnapPanel();
			}
			SwapDefMenus();
			SwapHuman(H);
			if (DefAudibleTo)
			{
				// the audible remakes the play: every player's slot is handed out again (so not inside his swap)
				const tag::DefCall* To = DefAudibleTo;
				DefAudibleTo = nullptr;
				RebuildPlay(Sim->cfg.offPlay, To, Sim->cfg.flip);
				PlayAudibleCue(false);
				UE_LOG(LogTemp, Display, TEXT("TAG multiplayer: %s audibled the defense to %s"), *HumanTag(Me), UTF8_TO_TCHAR(To->name.c_str()));
				return; // (the men are new: the others read on the next frame)
			}
			continue;
		}
		SwapHuman(H);
		if (E->side == tag::Side::Def)
		{
			// a defender: the left stick walks him, CIRCLE / L1 (BACKSPACE / TAB) switches - held, the stick picks the man
			const bool bPress = P({EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_LeftShoulder, EKeys::BackSpace, EKeys::Tab});
			PresnapWalkSwitch(R, Dt, true, bPress);
		}
		else
		{
			// on offense: the man he plays, stepped across the eligible men (L1 / R1, Q / E, TAB)
			const tag::Ent* Was = Sim->user;
			if (P({EKeys::Gamepad_RightShoulder, EKeys::Gamepad_FaceButton_Right, EKeys::E, EKeys::Tab})) Sim->cycleEligible(0, 1);
			else if (P({EKeys::Gamepad_LeftShoulder, EKeys::Q})) Sim->cycleEligible(0, -1);
			if (Sim->user != Was)
			{
				ATagAudio::Ui(TEXT("UiMove"));
				UE_LOG(LogTemp, Display, TEXT("TAG multiplayer: %s now plays %s"), *HumanTag(static_cast<int32>(&H - Humans.GetData())), Sim->user ? UTF8_TO_TCHAR(Sim->user->role.c_str()) : TEXT("-"));
			}
		}
		SwapHuman(H);
	}
}

// ------------------------------------------------------------------ the markers
void AMatchDirector::UpdateHumanMarkers(APlayerController* PC, const FVector2D& VP)
{
	TArray<FUserMarkerUI> Ms;
	const bool bShow = Sim && Game && PC && (State == EMatchState::Live || State == EMatchState::Presnap);
	for (int32 i = 0; bShow && i < Humans.Num(); ++i)
	{
		const FTagHuman& H = Humans[i];
		const tag::Ent* E = H.Slot >= 0 ? Sim->slotEnt(H.Slot) : nullptr;
		AAthlete* U = E ? AthleteFor(E->p, E->side == tag::Side::Off ? Game->poss : tag::other(Game->poss)) : nullptr;
		if (!U || U->IsHidden()) continue;
		FUserMarkerUI M;
		M.Viewport = VP;
		M.Color = PlayerColor(i);
		M.Tag = HumanTag(i);
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
		const FVector Rt(-Fw.Y, Fw.X, 0.f);
		for (const FVector& Pt : {Feet + Fw * 62.f + Rt * 20.f, Feet + Fw * 92.f, Feet + Fw * 62.f - Rt * 20.f})
		{
			bOk = bOk && PC->ProjectWorldLocationToScreen(Pt, S, true);
			M.Notch.Add(S);
		}
		bOk = bOk && PC->ProjectWorldLocationToScreen(U->GetActorLocation() + FVector(0, 0, 225.f), M.Head, true);
		M.bOn = bOk;
		M.Number = E->p ? FString::FromInt(E->p->number) : FString();
		// his sprint (a pad's R2) while the play is live
		if (H.bPad && State == EMatchState::Live && PC->ProjectWorldLocationToScreen(U->GetActorLocation(), M.StaminaAt, true))
		{
			M.StaminaV = H.Ctl.SprintStamina;
			M.bSprint = H.Ctl.bSprinting;
		}
		if (M.bOn) Ms.Add(M);
	}
	Overlay->SetUserMarkers(Ms);
}

// ------------------------------------------------------------------ -TagMultiTest
namespace
{
	/** A key on a made-up device, sent through Slate as a pad's or the keyboard's would be (the hub, the menus' filter
	 *  and focus all see it). */
	void SendKey(int32 DeviceId, const FKey& K, bool bDown)
	{
		if (!FSlateApplication::IsInitialized()) return;
		const FKeyEvent E(K, FModifierKeysState(), FInputDeviceId::CreateFromInternalId(DeviceId), false, 0, 0, TOptional<int32>(0));
		if (bDown) FSlateApplication::Get().ProcessKeyDownEvent(E);
		else FSlateApplication::Get().ProcessKeyUpEvent(E);
	}
	void SendAxis(int32 DeviceId, const FKey& K, float V)
	{
		if (!FSlateApplication::IsInitialized()) return;
		const FAnalogInputEvent E(K, FModifierKeysState(), FInputDeviceId::CreateFromInternalId(DeviceId), false, 0, 0, V, TOptional<int32>(0));
		FSlateApplication::Get().ProcessAnalogInputEvent(E);
	}
	struct FHeld { int32 Dev; FKey K; double Up; };
	TArray<FHeld> GHeld;
	void Tap(int32 Dev, const FKey& K, double Hold = 0.08)
	{
		SendKey(Dev, K, true);
		GHeld.Add({Dev, K, FPlatformTime::Seconds() + Hold});
	}
}

void AMatchDirector::MultiCheck(bool bOk, const FString& What)
{
	(bOk ? MultiPasses : MultiFails)++;
	UE_LOG(LogTemp, Display, TEXT("TAG multitest %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
}

void AMatchDirector::TickMultiTest(float Dt)
{
	// four made-up devices: three pads (A, B, C) and a keyboard (K). Home: A and B (B takes the call), away: C (the
	// caller) and K. Each step waits for its moment, sends its keys and checks what came of them.
	const int32 PadA = 9101, PadB = 9102, PadC = 9103, Kbd = 9104;
	for (int32 i = GHeld.Num() - 1; i >= 0; --i)
		if (FPlatformTime::Seconds() >= GHeld[i].Up) { SendKey(GHeld[i].Dev, GHeld[i].K, false); GHeld.RemoveAt(i); }
	if (IsMoviePlayerEnabled() && GetMoviePlayer()->IsMovieCurrentlyPlaying()) return;
	MultiT += Dt;
	auto Next = [this] { ++MultiStep; MultiT = 0.f; };
	auto HumanOfDev = [this](int32 Id)
	{
		const int32 D = FTagInputHub::Get().Find(FInputDeviceId::CreateFromInternalId(Id), Id != Kbd);
		for (int32 i = 0; i < Humans.Num(); ++i)
			if (Humans[i].Dev == D) return i;
		return -1;
	};
	auto EntOfDev = [&](int32 Id) -> const tag::Ent*
	{
		const int32 H = HumanOfDev(Id);
		return Sim && Humans.IsValidIndex(H) && Humans[H].Slot >= 0 ? Sim->slotEnt(Humans[H].Slot) : nullptr;
	};
	auto Shot = [](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_multi_%s.png"), Name), true, false); };
	auto Finish = [this]
	{
		UE_LOG(LogTemp, Display, TEXT("TAG multitest done: %d pass, %d fail"), MultiPasses, MultiFails);
		bMultiTest = false;
		FPlatformMisc::RequestExit(false);
	};
	if (MultiT > 90.f) { MultiCheck(false, FString::Printf(TEXT("step %d timed out"), MultiStep)); return Finish(); }
	switch (MultiStep)
	{
	case 0:
		if (MultiT > 2.f && State == EMatchState::Menu) { ShowLobby(); Next(); }
		break;
	case 1:
		// everyone joins: CROSS on each pad, ENTER on the keyboard
		if (MultiT > 0.6f) { Tap(PadA, EKeys::Gamepad_FaceButton_Bottom); Tap(PadB, EKeys::Gamepad_FaceButton_Bottom); Next(); }
		break;
	case 2:
		if (MultiT > 0.3f) { Tap(PadC, EKeys::Gamepad_FaceButton_Bottom); Tap(Kbd, EKeys::Enter); Next(); }
		break;
	case 3:
		if (MultiT > 0.4f)
		{
			MultiCheck(Humans.Num() == 4, FString::Printf(TEXT("four players joined (%d)"), Humans.Num()));
			MultiCheck(Humans.Num() == 4 && Humans[3].bPad == false && Humans[0].bPad, TEXT("the keyboard joined as a keyboard player, the pads as pads"));
			if (Humans.Num() != 4) return Finish(); // (the rest needs all four)
			Shot(TEXT("lobby_joined"));
			// sides: A and B right (home), C and the keyboard left (away)
			Tap(PadA, EKeys::Gamepad_DPad_Right);
			Tap(PadB, EKeys::Gamepad_DPad_Right);
			Tap(PadC, EKeys::Gamepad_DPad_Left);
			Tap(Kbd, EKeys::A);
			Next();
		}
		break;
	case 4:
		if (MultiT > 0.4f)
		{
			const int32 A = HumanOfDev(PadA), B = HumanOfDev(PadB), C = HumanOfDev(PadC), K = HumanOfDev(Kbd);
			MultiCheck(A >= 0 && B >= 0 && C >= 0 && K >= 0 && Humans[A].Team == 1 && Humans[B].Team == 1 && Humans[C].Team == 0 && Humans[K].Team == 0,
				TEXT("sides picked: two home, two away"));
			if (A < 0 || B < 0 || C < 0 || K < 0) return Finish();
			MultiCheck(Humans[A].bCaller && Humans[C].bCaller && !Humans[B].bCaller && !Humans[K].bCaller, TEXT("the first on each side calls the plays by default"));
			Tap(PadB, EKeys::Gamepad_FaceButton_Top); // B takes the home side's call
			Next();
		}
		break;
	case 5:
		if (MultiT > 0.4f)
		{
			const int32 A = HumanOfDev(PadA), B = HumanOfDev(PadB);
			MultiCheck(B >= 0 && Humans[B].bCaller && A >= 0 && !Humans[A].bCaller, TEXT("TRIANGLE hands the home call to P2"));
			Shot(TEXT("lobby_sides"));
			Tap(PadA, EKeys::Gamepad_Special_Right); // OPTIONS: on to the team select
			Next();
		}
		break;
	case 6:
		if (MultiT > 0.6f)
		{
			MultiCheck(!bLobby && Overlay && Overlay->IsMultiSelect(), TEXT("OPTIONS goes on to the multiplayer team select"));
			Shot(TEXT("teamselect"));
			StartMultiMatch(7, 3, 1, true, 240);
			Next();
		}
		break;
	case 7:
	{
		// a play call: the right player's, and only his buttons reach it
		if (State != EMatchState::Call || !Overlay || !Overlay->IsPlayCall() || MenuOwner < 0) break;
		if (MultiT < 1.0f) break;
		const int32 Off = CallerOf(Game->poss), Def = CallerOf(tag::other(Game->poss));
		const bool bOffCall = Overlay->bCallOffense;
		const int32 Want = bOffCall ? Off : Def;
		MultiCheck(MenuOwner == Want, FString::Printf(TEXT("the %s call is %s's (owner %s)"), bOffCall ? TEXT("offensive") : TEXT("defensive"), *HumanTag(Want), *HumanTag(MenuOwner)));
		Shot(bOffCall ? TEXT("call_off") : TEXT("call_def"));
		// someone else's CROSS: nothing
		const int32 Other = (MenuOwner + 1) % Humans.Num();
		int32 OtherDevId = -1, OwnerDevId = -1;
		for (const int32 Id : {PadA, PadB, PadC, Kbd})
		{
			if (HumanOfDev(Id) == Other) OtherDevId = Id;
			if (HumanOfDev(Id) == MenuOwner) OwnerDevId = Id;
		}
		Tap(OtherDevId, OtherDevId == Kbd ? EKeys::Enter : EKeys::Gamepad_FaceButton_Bottom);
		MultiStep = 8;
		MultiT = 0.f;
		MultiOwnerDev = OwnerDevId;
		bMultiCallOff = bOffCall;
		break;
	}
	case 8:
		if (MultiT > 0.5f)
		{
			MultiCheck(Overlay->HasModal() && Overlay->bCallOffense == bMultiCallOff && State == EMatchState::Call, TEXT("another player's button does not make the call"));
			Tap(MultiOwnerDev, MultiOwnerDev == Kbd ? EKeys::Enter : EKeys::Gamepad_FaceButton_Bottom);
			Next();
		}
		break;
	case 9:
		if (MultiT > 0.6f)
		{
			if (State == EMatchState::Call && Overlay->IsPlayCall())
			{
				// the offense has called: now the defense's caller
				MultiCheck(bMultiCallOff && !Overlay->bCallOffense, TEXT("after the offense's call the defense's caller gets his"));
				MultiStep = 7;
				MultiT = 1.0f;
				break;
			}
			if (State != EMatchState::Presnap) break;
			// the men: every player one of his own, nobody's twice, the offense's caller on the quarterback
			TSet<const tag::Ent*> Seen;
			bool bUnique = true, bAll = true;
			for (const FTagHuman& H : Humans)
			{
				const tag::Ent* E = H.Slot >= 0 ? Sim->slotEnt(H.Slot) : nullptr;
				if (!E) { bAll = false; continue; }
				bUnique = bUnique && !Seen.Contains(E) && E->userControlled;
				Seen.Add(E);
			}
			MultiCheck(bAll && bUnique && Sim->userSlots() == 4, FString::Printf(TEXT("four players, four men (%d slots)"), Sim->userSlots()));
			const int32 Oc = CallerOf(Game->poss);
			MultiCheck(Humans.IsValidIndex(Oc) && Humans[Oc].Slot == 0 && Sim->user == Sim->qb, TEXT("the offense's caller is on the quarterback"));
			Shot(TEXT("presnap"));
			Next();
		}
		break;
	case 10:
		if (MultiT > 1.0f)
		{
			// the offense's other player steps to another receiver (R1); a defender switches (CIRCLE tap)
			MultiPrevOff = MultiPrevDef = nullptr;
			for (const int32 Id : {PadA, PadB, PadC, Kbd})
			{
				const int32 H = HumanOfDev(Id);
				const tag::Ent* E = EntOfDev(Id);
				if (!E || Humans[H].Slot == 0) continue;
				if (E->side == tag::Side::Off && !MultiPrevOff) { MultiPrevOff = E; MultiOffDev = Id; Tap(Id, Id == Kbd ? EKeys::Tab : EKeys::Gamepad_RightShoulder); }
				else if (E->side == tag::Side::Def && !MultiPrevDef) { MultiPrevDef = E; MultiDefDev = Id; Tap(Id, Id == Kbd ? EKeys::Tab : EKeys::Gamepad_FaceButton_Right, 0.1); }
			}
			Next();
		}
		break;
	case 11:
		if (MultiT > 0.6f)
		{
			if (MultiPrevOff)
			{
				const tag::Ent* E = EntOfDev(MultiOffDev);
				MultiCheck(E && E != MultiPrevOff && E->eligible && E->userControlled && E != Sim->qb, FString::Printf(TEXT("the offense's second player steps to another receiver (%s -> %s)"),
					UTF8_TO_TCHAR(MultiPrevOff->role.c_str()), E ? UTF8_TO_TCHAR(E->role.c_str()) : TEXT("-")));
			}
			if (MultiPrevDef)
			{
				const tag::Ent* E = EntOfDev(MultiDefDev);
				int32 Owners = 0;
				for (const FTagHuman& H : Humans) Owners += H.Slot >= 0 && Sim->slotEnt(H.Slot) == E ? 1 : 0;
				MultiCheck(E && E != MultiPrevDef && Owners == 1, FString::Printf(TEXT("a defender switches to a man nobody else has (%s -> %s)"),
					UTF8_TO_TCHAR(MultiPrevDef->role.c_str()), E ? UTF8_TO_TCHAR(E->role.c_str()) : TEXT("-")));
				// and walks him: the left stick pushed for a moment
				MultiWalkFrom = E ? E->pos : tag::V2{0, 0};
				SendAxis(MultiDefDev, EKeys::Gamepad_LeftX, 1.f);
			}
			Next();
		}
		break;
	case 12:
		if (MultiT > 0.7f)
		{
			if (MultiPrevDef)
			{
				SendAxis(MultiDefDev, EKeys::Gamepad_LeftX, 0.f);
				const tag::Ent* E = EntOfDev(MultiDefDev);
				MultiCheck(E && tag::dist(E->pos, MultiWalkFrom) > 1.0, FString::Printf(TEXT("the defender walks his own man pre-snap (%.1f yd)"), E ? tag::dist(E->pos, MultiWalkFrom) : 0.0));
			}
			MultiStep = 20; // (the defense's caller's menus, then the snap)
			MultiT = 0.f;
		}
		break;
	case 20:
		if (MultiT > 0.4f)
		{
			// head to head, the defense's caller opens his audible list (SQUARE / J) on his own pad
			const int32 Dc = DefMenuHuman();
			MultiCheck(Dc >= 0, FString::Printf(TEXT("head to head, the defense's caller has his own pre-snap menus (%s)"), Dc >= 0 ? *HumanTag(Dc) : TEXT("-")));
			MultiDefCallDev = -1;
			for (const int32 Id : {PadA, PadB, PadC, Kbd})
				if (HumanOfDev(Id) == Dc && Dc >= 0) MultiDefCallDev = Id;
			if (MultiDefCallDev < 0) { MultiStep = 26; MultiT = 0.f; break; }
			MultiPrevCall = Sim->cfg.defCall;
			Tap(MultiDefCallDev, MultiDefCallDev == Kbd ? EKeys::J : EKeys::Gamepad_FaceButton_Left);
			Next();
		}
		break;
	case 21:
		if (MultiT > 0.4f)
		{
			MultiCheck(DefPreMode == EPre::Audible && PreMode == EPre::Base, FString::Printf(TEXT("his SQUARE opens the defense's audibles, not the offense's (def %d, off %d)"),
				static_cast<int32>(DefPreMode), static_cast<int32>(PreMode)));
			MultiCheck(Overlay && Overlay->DefPreRows.Num() > 1 && Overlay->PreRows.Num() > 0 && Overlay->DefPreWho == HumanTag(DefMenuHuman()),
				TEXT("both callers' panels are up, the defense's with its caller's tag"));
			Shot(TEXT("def_audible"));
			Tap(MultiDefCallDev, MultiDefCallDev == Kbd ? EKeys::Down : EKeys::Gamepad_DPad_Down);
			Next();
		}
		break;
	case 22:
		if (MultiT > 0.3f)
		{
			Tap(MultiDefCallDev, MultiDefCallDev == Kbd ? EKeys::Enter : EKeys::Gamepad_FaceButton_Bottom);
			Next();
		}
		break;
	case 23:
		if (MultiT > 0.5f)
		{
			MultiCheck(Sim->cfg.defCall != MultiPrevCall && DefPreMode == EPre::Base && State == EMatchState::Presnap,
				FString::Printf(TEXT("his CROSS audibles the defense, the ball still not snapped (%s -> %s)"), MultiPrevCall ? UTF8_TO_TCHAR(MultiPrevCall->name.c_str()) : TEXT("-"),
					UTF8_TO_TCHAR(Sim->cfg.defCall->name.c_str())));
			TSet<const tag::Ent*> Seen;
			bool bOk = Sim->user == Sim->qb && Humans.IsValidIndex(CallerOf(Game->poss)) && Humans[CallerOf(Game->poss)].Slot == 0;
			for (const FTagHuman& H : Humans)
			{
				const tag::Ent* E = H.Slot >= 0 ? Sim->slotEnt(H.Slot) : nullptr;
				bOk = bOk && E && !Seen.Contains(E) && E->userControlled;
				Seen.Add(E);
			}
			MultiCheck(bOk, TEXT("after the defense's audible every player still has a man of his own, the offense's caller the quarterback"));
			// the coverage shell: TRIANGLE (H), then press (D-pad left)
			Tap(MultiDefCallDev, MultiDefCallDev == Kbd ? EKeys::H : EKeys::Gamepad_FaceButton_Top);
			Next();
		}
		break;
	case 24:
		if (MultiT > 0.3f)
		{
			Tap(MultiDefCallDev, MultiDefCallDev == Kbd ? EKeys::Left : EKeys::Gamepad_DPad_Left);
			Next();
		}
		break;
	case 25:
		if (MultiT > 0.4f)
		{
			MultiCheck(DefPreMode == EPre::Shell && Sim->shellAdj == tag::PlaySim::ShellAdj::Press && PreMode == EPre::Base,
				FString::Printf(TEXT("his coverage shell: press (def mode %d, shell %d, offense mode %d)"), static_cast<int32>(DefPreMode),
					static_cast<int32>(Sim->shellAdj), static_cast<int32>(PreMode)));
			Shot(TEXT("def_shell"));
			Tap(MultiDefCallDev, MultiDefCallDev == Kbd ? EKeys::Enter : EKeys::Gamepad_FaceButton_Bottom); // (done)
			Next();
		}
		break;
	case 26:
		if (MultiT > 0.4f)
		{
			// the snap: not on another player's CROSS (one not calling the defense: his CROSS is his assignment menu), then on the caller's
			for (const int32 Id : {PadA, PadB, PadC, Kbd})
				if (HumanOfDev(Id) >= 0 && Humans[HumanOfDev(Id)].Slot > 0 && HumanOfDev(Id) != DefMenuHuman()) { Tap(Id, Id == Kbd ? EKeys::SpaceBar : EKeys::Gamepad_FaceButton_Bottom); break; }
			MultiStep = 13;
			MultiT = 0.f;
		}
		break;
	case 13:
		// (the snap waits for every man to be set, as a player's does: no snap with men still walking in)
		if (MultiT > 0.4f && State == EMatchState::Presnap && Sim && (Sim->walking() || SetClock < 0.3f)) break;
		if (MultiT > 0.4f)
		{
			MultiCheck(State == EMatchState::Presnap, TEXT("only the offense's caller snaps the ball"));
			for (const int32 Id : {PadA, PadB, PadC, Kbd})
				if (HumanOfDev(Id) >= 0 && Humans[HumanOfDev(Id)].Slot == 0) Tap(Id, Id == Kbd ? EKeys::SpaceBar : EKeys::Gamepad_FaceButton_Bottom);
			Next();
		}
		break;
	case 14:
		if (MultiT > 0.6f)
		{
			// (a pre-snap flag rolled for the play is called on the snap press instead: the press still ended the pre-snap)
			MultiCheck(State != EMatchState::Presnap, FString::Printf(TEXT("the caller's CROSS snaps it (state %d)"), static_cast<int32>(State)));
			// a defender runs his man: the stick held toward the ball's side
			if (MultiPrevDef) SendAxis(MultiDefDev, EKeys::Gamepad_LeftY, 1.f);
			Next();
		}
		break;
	case 15:
		if (MultiT > 0.8f && State == EMatchState::Live)
		{
			const tag::Ent* E = MultiPrevDef ? EntOfDev(MultiDefDev) : nullptr;
			MultiCheck(!MultiPrevDef || (E && E->userControlled), TEXT("the defender's man is his in the live play"));
			if (MultiPrevDef && Sim)
			{
				// his stick is in his slot's input, and in nobody else's
				const int32 Me = HumanOfDev(MultiDefDev);
				const int32 MySlot = Humans.IsValidIndex(Me) ? Humans[Me].Slot : -1;
				double Mine = 0.0, Others = 0.0;
				for (int32 Sl = 0; Sl < Sim->userSlots(); ++Sl)
					(Sl == MySlot ? Mine : Others) += tag::len(Sim->slotInput(Sl).move);
				MultiCheck(MySlot >= 0 && Mine > 0.5 && Others < 0.05, FString::Printf(TEXT("a player's stick drives his own man only (his %.2f, the others' %.2f)"), Mine, Others));
			}
			Shot(TEXT("live"));
		}
		if (MultiT > 0.8f)
		{
			if (MultiPrevDef) SendAxis(MultiDefDev, EKeys::Gamepad_LeftY, 0.f);
			Next();
		}
		break;
	case 16:
		// a second play, the other way round if the ball changed hands: the calls and the men again
		if (State == EMatchState::Call && ++MultiPlays < 3) { MultiStep = 7; MultiT = 0.f; }
		else if (MultiPlays >= 3 || MultiT > 60.f) Finish();
		break;
	default:
		Finish();
		break;
	}
}
