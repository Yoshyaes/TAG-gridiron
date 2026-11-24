// Code-built Slate HUD + menus for TAG Gridiron.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "InputCoreTypes.h"
#include <functional>

class AMatchDirector;
class SBox;
struct FCtlState;
struct FCallPicCache;
namespace tag { struct OffPlay; struct DefCall; }

struct FCallChoice
{
	enum EKind { Off, Def, Punt, FG, Auto } Kind = Auto;
	const tag::OffPlay* Play = nullptr;
	const tag::DefCall* Call = nullptr;
};

/** Controller glyphs (drawn as shapes; keyboard players see the key instead). */
/** (DUpDown / DLeftRight: the d-pad with both arms of the pair lit, one glyph for "move" hints; L3 / R3: a stick click) */
enum class EPadGlyph : uint8 { None, Cross, Circle, Square, Triangle, R1, L1, R2, L2, DUp, DDown, DLeft, DRight, DUpDown, DLeftRight, L3, R3 };

/** A row of the pre-snap panel: the button, what it does, and whether it is the current choice. */
struct FPreRow
{
	EPadGlyph Glyph = EPadGlyph::None;
	FString Key;     // keyboard key ("" = the glyph's default key)
	FString Label;
	bool bOn = false;
	FLinearColor Tint = FLinearColor::White;  // the glyph's colour when it stands for a receiver
};

/** A receiver's button icon over his head (screen position in viewport pixels). */
struct FArtIcon
{
	FVector2D Screen = FVector2D::ZeroVector;
	int32 Button = 0;  // 0..4: cross, circle, square, triangle, R1 (keys 1..5)
	bool bPrimary = false;
	bool bHot = false;  // hot routed
	bool bPicked = false;  // being hot routed / motioned right now
};

/** The highlight stick's cue: the man about to tackle the user's ball carrier (a ring at his feet) and the move that
 *  beats him over the carrier (its button and its name). Screen positions in viewport pixels. */
struct FHighlightCue
{
	bool bOn = false;
	FVector2D Carrier = FVector2D::ZeroVector;
	FVector2D Threat = FVector2D::ZeroVector;
	float ThreatR = 40.f;  // the ring's radius on screen
	FString Move;          // "TRUCK"
	EPadGlyph Glyph = EPadGlyph::None;  // a face button, or None for a right-stick flick (Key says which way)
	FString Key;
};

/** The kick meter (a user kick, Madden style): the aim arrow's flight on screen, the power swing, the accuracy needle. */
struct FKickMeterUI
{
	bool bOn = false;
	int32 Stage = 0;        // 0 aiming, 1 power (the swing), 2 accuracy (keyboard / face button), 3 away
	float Power = 0.f;      // 0..1.15 (past 1.0 the red: no farther, only wilder)
	float Acc = 0.f;        // -1..1, 0 dead centre
	bool bAccShown = false;
	TArray<FVector2D> Arc;  // the aim arrow: the ball's flight projected (viewport px)
	FVector2D Viewport = FVector2D(1920, 1080);
	FString Title;          // "FIELD GOAL  -  47 YD"
	FString Sub;            // wind
	bool bPad = false;
};

/** Engaged in the trenches: a pass rusher's energy (his moves cost it) and his blocker's power, over their heads. */
struct FLineBarUI
{
	FVector2D Screen = FVector2D::ZeroVector; // above the pair (viewport px)
	float Energy = 1.f;     // the rusher: 0..1
	float Power = 1.f;      // the blocker: 0..1
	float Edge = 0.f;       // who is winning the rep, -1 blocker .. +1 rusher
	bool bUser = false;     // the user's man (drawn larger, with the move buttons)
	FString Move;           // the last move tried ("SWIM"), shown a moment
	bool bMoveWon = false;
};

/** The man the user controls (Madden's marker): a ring at his feet, a notch the way he faces, a chevron over his head. */
struct FUserMarkerUI
{
	bool bOn = false;
	TArray<FVector2D> Ring, Notch;
	FVector2D Head = FVector2D::ZeroVector;
	FVector2D Viewport = FVector2D(1920, 1080);
	FString Number;
	/** Local multiplayer: the player's colour and tag ("P2") over the number, and his own sprint bar (V < 0: none). */
	FLinearColor Color = FLinearColor(0.45f, 0.95f, 1.f);
	FString Tag;
	FVector2D StaminaAt = FVector2D::ZeroVector;
	float StaminaV = -1.f;
	bool bSprint = false;
};

/** The local players' colours (P1 blue, P2 red, P3 green, P4 gold): their markers, their lobby cards. */
inline FLinearColor PlayerColor(int32 Index)
{
	static const FLinearColor C[4] = {FLinearColor(0.3f, 0.66f, 1.f), FLinearColor(1.f, 0.32f, 0.3f), FLinearColor(0.32f, 0.95f, 0.45f), FLinearColor(1.f, 0.82f, 0.2f)};
	return C[FMath::Clamp(Index, 0, 3)];
}

/** The receivers' button colours: their icons and their routes in the play art share them. */
inline FLinearColor ButtonColor(int32 Button)
{
	static const FLinearColor C[5] = {
		FLinearColor(0.36f, 0.62f, 1.f), FLinearColor(1.f, 0.30f, 0.33f), FLinearColor(1.f, 0.47f, 0.84f),
		FLinearColor(0.26f, 0.92f, 0.62f), FLinearColor(0.86f, 0.87f, 0.92f)};
	return C[FMath::Clamp(Button, 0, 4)];
}

class SGridironOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGridironOverlay) {}
		SLATE_ARGUMENT(AMatchDirector*, Director)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** bKeepFocus: the same screen rebuilt (a selection changed): keep the focus on the same button. */
	void ShowModal(TSharedRef<SWidget> W, bool bKeepFocus = false);
	void ClearModal();
	bool HasModal() const { return bModal; }
	/** The play call screen is up (waiting for its pick). */
	bool IsPlayCall() const { return bModal && CallDone != nullptr; }
	/** The screen the pause menu came over (a play call, the try choice): back as it was, or nothing. */
	void RestoreSuspended();
	void DropSuspended() { Suspended.Reset(); }
	/** Which pad's buttons the glyphs are: Sony's shapes, else Xbox letters. */
	void SetPadKind(bool bSony) { bSonyPad = bSony; }
	void Banner(const FString& Text, const FString& Sub, const FLinearColor& Color);
	void Toast(const FString& Text);
	/** The toast is a big play's stat line ("#89 R. Ochoa   2 rec, 25 yds, 1 TD"): shown as a lower third. */
	bool IsStatLine() const { return ToastText.Contains(TEXT("   ")) && ToastText.Contains(TEXT(" yds")); }
	void SetHint(const FString& Text) { HintText = Text; }
	void SetHudVisible(bool b) { bHud = b; }
	/** Broadcast only: the score bug without the ticker and the weather panel (the highlight reel). */
	void SetBroadcastOnly(bool b) { bBroadcastOnly = b; }
	void SetClockOffset(double s) { ClockOffset = s; }
	void SetFlag(bool b) { bFlag = b; }
	/** The pre-snap panel (hot routes, audibles, protection, shifts): empty rows hide it. */
	void SetPresnapPanel(const FString& Title, const FString& Sub, const TArray<FPreRow>& Rows, bool bPad)
	{
		PreTitle = Title;
		PreSub = Sub;
		PreRows = Rows;
		bPrePad = bPad;
	}
	void ClearPresnapPanel() { PreRows.Reset(); }
	/** Multiplayer: whose the panel is (his tag chip in his colour on its title line; empty: nobody's). */
	void SetPresnapWho(const FString& Who, const FLinearColor& C) { PreWho = Who; PreWhoColor = C; }
	/** Head to head: the defense's caller's own pre-snap panel, bottom left (the offense's is bottom right). */
	void SetDefPresnapPanel(const FString& Title, const FString& Sub, const TArray<FPreRow>& Rows, bool bPad, const FString& Who, const FLinearColor& C)
	{
		DefPreTitle = Title;
		DefPreSub = Sub;
		DefPreRows = Rows;
		bDefPrePad = bPad;
		DefPreWho = Who;
		DefPreWhoColor = C;
	}
	void ClearDefPresnapPanel() { DefPreRows.Reset(); }
	void SetIcons(const TArray<FArtIcon>& In, const FVector2D& ViewportSize, bool bPad)
	{
		Icons = In;
		IconViewport = ViewportSize;
		bIconPad = bPad;
	}
	void SetHighlight(const FHighlightCue& C) { Highlight = C; }
	void SetKickMeter(const FKickMeterUI& K) { KickMeter = K; }
	/** The swing made: the meter stays up with it (the arrow gone: the camera has cut away). */
	void HoldKickResult() { KickMeter.Arc.Reset(); KickMeter.Stage = 3; KickMeter.bAccShown = true; }
	void SetUserMarker(const FUserMarkerUI& M) { UserMarkers = {M}; }
	/** Every local player's man (multiplayer): one marker each, in his colour. */
	void SetUserMarkers(const TArray<FUserMarkerUI>& M) { UserMarkers = M; }
	/** The user's sprint stamina under his man (Screen: viewport px; V < 0 hides it). */
	void SetStamina(const FVector2D& Screen, const FVector2D& Viewport, float V, bool bSprint) { StaminaAt = Screen; StaminaVp = Viewport; StaminaV = V; bStaminaSprint = bSprint; }
	void SetLineBars(const TArray<FLineBarUI>& B, const FVector2D& Viewport) { LineBars = B; LineViewport = Viewport; }

	// screens
	void ShowMainMenu();
	void ShowTeamSelect(bool bUserPlays);
	void ShowPlayCall(bool bOffense, bool bFourthDown, bool bTwoPoint, std::function<void(FCallChoice)> Done);
	/** The defensive call screen's line on what the offense has come out in (its formation and personnel); empty: none. */
	void SetCallOffenseLook(const FString& S) { CallOffLook = S; }
	/** Multiplayer: the player making this call ("P2 CALLING  -  PS5 CONTROLLER") and his colour; empty: none shown. */
	void SetCallWho(const FString& S, const FLinearColor& C) { CallWho = S; CallWhoColor = C; }
	void ShowPatChoice(std::function<void(bool)> Done);
	void ShowPause(std::function<void()> Resume, std::function<void()> SimToEnd, std::function<void()> Quit);
	/** The settings screen (commentary captions, ...); Back returns to whatever opened it. */
	void ShowSettings(std::function<void()> Back);
	/** Settings > Controls (UI/ControlsScreen.cpp): every button in every part of the game on the pad plugged in (a
	 *  DualSense or an Xbox pad, drawn) or the keyboard, each action movable to any button (Game/TagBindings.h). */
	void ShowControls(std::function<void()> Back);
	/** -TagControlsTest: the controls screen driven by its buttons, a swap read back in a game (UI/ControlsScreen.cpp). */
	void StartControlsTest();
	/** The glyphs drawn are PlayStation's (else Xbox's). */
	bool SonyPad() const { return bSonyPad; }
	/** A pad is plugged in (or the player is on one). */
	bool CtlHasPad() const;
	void ShowBoxScore(const FString& Title, const FString& Button, std::function<void()> Done);
	/** Superstars & X-Factors (UI/AbilityScreen.cpp): a team's graded players and their ability loadouts; Back returns
	 *  to whatever opened it. The saved loadouts are laid over the coach's picks at startup (LoadAbilityLoadouts). */
	void ShowAbilities(int32 TeamIdx, std::function<void()> Back);
	/** Local multiplayer (UI/Lobby.cpp): up to four players join (a button on their pad, or the keyboard), each picks
	 *  a side (away / home) and, on a side with two or more, who calls the plays; then the team select. */
	void ShowLobby();
	/** The team select for a multiplayer game (the sides were picked in the lobby). */
	bool IsMultiSelect() const { return bSelMulti; }
	static void LoadAbilityLoadouts(AMatchDirector* D);
	/** The depth chart (UI/DepthChart.cpp): every slot a team fills (the eleven a side by their spots, the specialists,
	 *  the situational subs) and who is listed there, in order; the user's order is what the game plays. FocusSlot /
	 *  FocusPlayerId (a DSlot, a Player::id; -1 none) open it on that slot and that man. Back returns to the opener. */
	void ShowDepthChart(int32 TeamIdx, std::function<void()> Back, int32 FocusSlot = -1, int32 FocusPlayerId = -1);
	/** Create-a-Player (UI/CreatePlayer.cpp): the created players, a new one (any position, any team), edits; the
	 *  men released to make room. Back returns to the opener. */
	void ShowCreatePlayer(int32 TeamIdx, std::function<void()> Back);
	/** The saved roster edits laid onto the league at startup: the created men, the cuts, the depth charts. */
	static void LoadRosterEdits(AMatchDirector* D);
	/** -TagRosterShots: the scripted pictures of the depth chart and Create-a-Player (nothing saved), then exit. */
	void StartRosterShots();
	/** -TagInputTest: one press sent twice (an Xbox pad read through two interfaces) moves the menus once (UI/InputTest.cpp). */
	void StartInputTest();
	/** -TagUiShots: a picture of every menu screen, then exit (UI/InputTest.cpp). */
	void StartUiShots();
	/** -TagCallTour: pictures of the play-call screens (tag_call_NN_*.png: offense families, formations, type tabs and
	 *  pages, the whole-family list, the defense's), then the team select's signature play; then exit (UI/InputTest.cpp). */
	void StartCallTour();
	/** -TagMenuAudio: a walk through every screen before the kickoff by the pad, for the menus' sound (UI/MenuAudioTest.cpp). */
	void StartMenuAudioTest();
	/** -TagChartTest: at the end of a soak, who played (the passers, the runners, the returner) against the chart. */
	static void LogChartTest(AMatchDirector* D);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	/** A controller button (or keyboard key) centred at C, Size across, in G's local space (the glyph widgets use it too). */
	void PaintGlyph(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& C, float Size, EPadGlyph Gl,
		const FString& Key, bool bPad, const FLinearColor& Tint, float Alpha = 1.f) const;
	bool PadGlyphs() const;
	/** How much room a glyph takes (its widget's size): a shoulder button is wider than a face button, a keycap as wide as
	 *  its key. Every screen's glyph widget sizes itself by this, so a label never runs into its button. */
	FVector2D GlyphExtent(EPadGlyph Gl, const FString& Key, float Size, bool bPad) const;
	/** A glyph as a widget, and a hint (the glyph and what it does) for a screen's button bar. */
	TSharedRef<SWidget> GlyphWidget(EPadGlyph Gl, const FString& Key, float Size, const FLinearColor& Tint = FLinearColor::White) const;
	TSharedRef<SWidget> HintItem(EPadGlyph Gl, const FString& Key, const FString& What, float Size = 24.f) const;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	friend class AMatchDirector; // (the multiplayer test reads which call is up)
	TSharedRef<SWidget> MakeButton(const FString& Label, const FString& Sub, std::function<void()> OnClick, float Width = 420.f);
	TSharedRef<SWidget> MakeScoreBug();
	FText ClockText() const;
	FText DownText() const;
	FText TickerText() const;
	FText WeatherText() const;

	AMatchDirector* Director = nullptr;
	TSharedPtr<SBox> ModalBox;
	bool bModal = false;
	TSharedPtr<SWidget> ModalContent, Suspended;
	int32 SuspendedFocus = -1;
	bool bSonyPad = false;
	bool bHud = false;
	bool bBroadcastOnly = false;
	bool bFlag = false;
	double ClockOffset = 0;
	FString HintText;
	FString PreTitle, PreSub;
	TArray<FPreRow> PreRows;
	bool bPrePad = false;
	FString PreWho;
	FLinearColor PreWhoColor = FLinearColor::White;
	FString DefPreTitle, DefPreSub, DefPreWho;
	TArray<FPreRow> DefPreRows;
	bool bDefPrePad = false;
	FLinearColor DefPreWhoColor = FLinearColor::White;
	TArray<FArtIcon> Icons;
	FHighlightCue Highlight;
	FKickMeterUI KickMeter;
	TArray<FUserMarkerUI> UserMarkers;
	FVector2D StaminaAt = FVector2D::ZeroVector, StaminaVp = FVector2D(1920, 1080);
	float StaminaV = -1.f;
	bool bStaminaSprint = false;
	TArray<FLineBarUI> LineBars;
	FVector2D LineViewport = FVector2D(1920, 1080);
	int32 PaintKickMeter(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const;
	int32 PaintLineBars(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const;
	FVector2D IconViewport = FVector2D(1920, 1080);
	bool bIconPad = false;
	int32 PaintPresnap(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const;
	// abilities (UI/AbilityScreen.cpp): the zone's banners and the badges over the X-Factor men before the snap
	void PumpAbilities();
	int32 PaintAbilities(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const;
	int32 AbTeam = -1, AbRow = 0, AbPick = 0, AbCol = 0;
	std::function<void()> AbBack;
	FString BannerText;
	FString BannerSub;
	FLinearColor BannerColor = FLinearColor::White;
	float BannerT = 99.f;
	FString ToastText;
	float ToastT = 99.f;
	// team select state
	int SelHome = 0;
	int SelAway = 1;
	bool bSelUserHome = true;
	int SelWeather = 0;
	bool bSelNight = true;
	int SelQuarter = 1;
	// (UI/TeamSelect.cpp) the column the pad is on (0 the away team, 1 the game options, 2 the home team), the options
	// row, and which screen it is (Play Now or Watch a Game: a fresh visit puts the pad on the user's team)
	int32 SelCol = 2;
	int32 SelRow = 0;
	bool bSelUserPlays = false;
	bool bSelMulti = false;   // the team select of a multiplayer game (no "You control": the lobby picked the sides)
	int32 LobbyFocus = 0;     // (UI/Lobby.cpp) the options row the keyboard / first pad is on
	FString CallTab;
	FString CallOffLook;
	FString CallWho;
	FLinearColor CallWhoColor = FLinearColor::White;
	/** The formation (offense: its id) or set (defense: its name) shown inside the tab's family. */
	FString CallSet;
	/** The formations / sets of the current tab, in the book's order (D-pad up / down steps through them). */
	TArray<FString> CallSets;
	// the play call (Madden's layout): the team's playbook by formation family (L1 / R1), the formations of the family
	// (D-pad up / down), three plays a page called by their face buttons (square, cross, circle), the pages by D-pad
	// left / right, the coach's pick on triangle
	struct FCallCard
	{
		FString Kind, Name, Note;
		const tag::OffPlay* Op = nullptr;
		const tag::DefCall* Dc = nullptr;
		FCallChoice Choice;
		/** Its play type tab ("RUN", "PASS", "PA", "SCREEN", "RPO", "TRICK"; defense "ZONE", "MAN", "BLITZ"); empty: none. */
		FString Cat;
		/** The concept line under the type ("Inside Zone", "Quick", "Cover 3  -  4 rush"). */
		FString Concept;
		/** The formation (set) it is in: shown on the card when the tab's whole family is listed. */
		FString Form;
		/** The whole family's list: how many other formations (sets) run the same play. */
		int32 Alt = 0;
	};
	TArray<FCallCard> CallCards;
	TArray<TPair<FString, FString>> CallTabs; // id, label
	int32 CallPage = 0;
	/** Plays a page: the three face buttons (square, cross, circle). */
	static constexpr int32 CallPerPage = 3;
	/** CallSets' entry for the whole family (every formation of the tab in one list). */
	static const TCHAR* CallAllSet() { return TEXT("*all"); }
	/** The play type tab (L2 / R2) of each side's call: "" every play, else a FCallCard::Cat. Kept between calls. */
	FString CallFilters[2];
	FString& CallFilter() { return CallFilters[bCallOffense ? 1 : 0]; }
	/** The type tabs the tab's family has (the first, "", is ALL), in the order L2 / R2 step through them. */
	TArray<FString> CallCats;
	/** The last play each side called on this screen (R3 calls it again). */
	const tag::OffPlay* CallLastOff = nullptr;
	const tag::DefCall* CallLastDef = nullptr;
	/** The repeat call for this screen, if there is one (Kind Auto: none). */
	FCallChoice CallRepeat() const;
	/** The cards' pictures while the screen is up (a page turned back costs no sim). */
	TSharedPtr<FCallPicCache> CallPics;
	/** The overlay's size (Slate units) the screen was laid out for: a window resized under it lays it out again. */
	FVector2D CallBuiltAvail = FVector2D::ZeroVector;
	bool bCallOffense = false, bCallFourth = false, bCallTwo = false;
	std::function<void(FCallChoice)> CallDone;
	TSharedPtr<SWidget> CallRoot;
	void BuildPlayCall();
	bool CallKey(const FKey& K, bool bRepeat);
	void CallPick(FCallChoice C);
	// gamepad navigation: focus the menu's button (by its order in the menu) once the menu is laid out
	int32 PendingFocus = -1;
	/** When the screen now up came in (it fades and rises in over 0.22 s; a screen rebuilt in place does not). */
	double ModalInT = -10.0;
	/** The main menu: the tile the detail card shows, and when it changed (the card fades over to the new one). */
	int32 MenuCardIdx = -1;
	double MenuCardT = -10.0;
	static void Focusables(const TSharedRef<SWidget>& W, TArray<TSharedRef<SWidget>>& Out);
	// the controls screen (UI/ControlsScreen.cpp)
	TSharedPtr<FCtlState> Ctl;
	bool CtlKey(const FKey& K, bool bRepeat);
	void CtlTick();
	void CtlSay(const FString& Msg, const FLinearColor& C);
	int32 FocusedIndex() const;
};
