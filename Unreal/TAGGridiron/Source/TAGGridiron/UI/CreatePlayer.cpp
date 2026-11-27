// Create-a-Player: make a man and put him on any team; and the roster edits' saves (the created men, the men cut to
// make room for them, the depth charts), laid onto the league at startup.
//
// The hub lists the created players (cross edits one, triangle deletes - twice to be sure), "New player", and the
// men released to make room (cross re-signs one if his team has a spot). Square opens the depth chart of the team
// under the cursor.
//
// The form: the profile (team, position, archetype, talent, name, number, age, height, weight), the look (face,
// helmet) and every rating, with the man's card live on the right - his OVR as it moves, the grade it would earn
// him against the league at his position, where he would sit on his team's depth chart. Left / right change the row
// (L1 / R1 by five), L2 / R2 jump a section, cross on a name types it, square rolls a new profile, triangle puts the
// ratings back to the archetype's, OPTIONS / F creates (or saves). A full team (53) releases a man to make room:
// the form asks which (the lowest rated at his position first).
//
// Saved in GameUserSettings.ini: [TAG.CreatedPlayers], [TAG.Cut], [TAG.DepthChart] (by jersey numbers).
#include "UI/RosterUI.h"

#include "Audio/TagAudio.h"
#include "Game/MatchDirector.h"
#include "Game/TagResult.h"
#include "Render/Athlete.h"
#include "Sim/Abilities.h"
#include "Sim/League.h"
#include "Containers/Ticker.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/PointLight.h"
#include "Components/PointLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "UnrealClient.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include <algorithm>
#include <vector>

using namespace RosterUI;

// ------------------------------------------------------------------ the saved roster edits
namespace CapStore
{
	const TCHAR* SecCreated = TEXT("TAG.CreatedPlayers");
	const TCHAR* SecCut = TEXT("TAG.Cut");
	const TCHAR* SecChart = TEXT("TAG.DepthChart");
	/** The men cut this session and the created men deleted: off their teams, alive (a game or an athlete may still
	 *  hold one's address). Cut men can be re-signed. */
	struct FGone
	{
		int32 TeamId = -1;
		bool bCut = false;
		std::unique_ptr<tag::Player> P;
	};
	std::vector<FGone> Gone;
	int32 NextId = 700000;

	FString Clean(const FString& In)
	{
		FString S;
		for (TCHAR C : In)
			if (C != ';' && C != '=' && C != '|' && C != ',' && C != ':' && C != '"') S.AppendChar(C);
		S.TrimStartAndEndInline();
		return S.Left(16);
	}
	tag::Team* TeamByAbbr(AMatchDirector* D, const FString& A)
	{
		for (auto& T : D->League)
			if (Str(T->abbr) == A) return T.get();
		return nullptr;
	}
	tag::Team* TeamOf(AMatchDirector* D, const tag::Player* P)
	{
		for (auto& T : D->League)
			if (T->hasPlayer(P)) return T.get();
		return nullptr;
	}
	FString PlayerLine(const tag::Team& T, const tag::Player& P)
	{
		FString R;
		for (const tag::RatingDef& Def : tag::ratingDefs())
			R += FString::Printf(TEXT("%s%s:%d"), R.IsEmpty() ? TEXT("") : TEXT(","), ANSI_TO_TCHAR(Def.abbr), FMath::RoundToInt(P.r.*(Def.m)));
		return FString::Printf(TEXT("team=%s;first=%s;last=%s;pos=%s;num=%d;age=%d;h=%d;w=%d;arch=%d;face=%d;helmet=%d;r=%s"),
			*Str(T.abbr), *Str(P.first), *Str(P.last), *Pos(P.pos), P.number, P.age, FMath::RoundToInt(P.heightIn), FMath::RoundToInt(P.weightLb),
			P.archetype, P.face, P.helmet, *R);
	}
	bool PosByName(const FString& S, tag::Pos& Out)
	{
		for (int32 i = 0; i <= static_cast<int32>(tag::Pos::P); ++i)
			if (S == ANSI_TO_TCHAR(tag::posName(static_cast<tag::Pos>(i)))) { Out = static_cast<tag::Pos>(i); return true; }
		return false;
	}
	/** A saved line back into a man (nullptr: not a line this build reads). */
	std::unique_ptr<tag::Player> ParseLine(const FString& Line, FString& TeamAbbr)
	{
		TMap<FString, FString> KV;
		TArray<FString> Parts;
		Line.ParseIntoArray(Parts, TEXT(";"));
		for (const FString& Part : Parts)
		{
			FString K, V;
			if (Part.Split(TEXT("="), &K, &V)) KV.Add(K, V);
		}
		tag::Pos Ps = tag::Pos::QB;
		if (!KV.Contains(TEXT("team")) || !KV.Contains(TEXT("pos")) || !PosByName(KV[TEXT("pos")], Ps)) return nullptr;
		TeamAbbr = KV[TEXT("team")];
		auto P = std::make_unique<tag::Player>();
		auto Int = [&KV](const TCHAR* K, int32 Def) { const FString* V = KV.Find(K); return V ? FCString::Atoi(**V) : Def; };
		P->pos = Ps;
		P->first = Std(Clean(KV.FindRef(TEXT("first"))));
		P->last = Std(Clean(KV.FindRef(TEXT("last"))));
		if (P->first.empty()) P->first = "Player";
		if (P->last.empty()) P->last = "Created";
		P->number = Int(TEXT("num"), -1);
		P->age = FMath::Clamp(Int(TEXT("age"), 22), 20, 45);
		P->heightIn = FMath::Clamp(Int(TEXT("h"), 74), 62, 84);
		P->weightLb = FMath::Clamp(Int(TEXT("w"), 220), 150, 400);
		P->archetype = Int(TEXT("arch"), 0);
		P->face = Int(TEXT("face"), -1);
		P->helmet = Int(TEXT("helmet"), -1);
		P->r = tag::capRatings(Ps, P->archetype, 0);
		TArray<FString> Rs;
		KV.FindRef(TEXT("r")).ParseIntoArray(Rs, TEXT(","));
		for (const FString& RV : Rs)
		{
			FString A, V;
			if (!RV.Split(TEXT(":"), &A, &V)) continue;
			for (const tag::RatingDef& Def : tag::ratingDefs())
				if (A == ANSI_TO_TCHAR(Def.abbr)) P->r.*(Def.m) = FMath::Clamp(FCString::Atoi(*V), 25, 99);
		}
		P->ovr = tag::computeOvr(Ps, P->r);
		P->created = true;
		return P;
	}
	/** A man onto a team: a free number if his is taken, the bottom of every set slot of his position. */
	void Sign(tag::Team& T, std::unique_ptr<tag::Player> P)
	{
		if (P->number < 0 || !tag::numberFree(T, P->number, P.get())) P->number = tag::freeNumber(T, P->pos, P->number);
		P->injuredWeeks = 0;
		P->fatigue = 0;
		P->onFieldLast = false;
		tag::chartAddPlayer(T, *P);
		T.roster.push_back(std::move(P));
	}
	/** A man off his team, kept alive in Gone. */
	void Remove(tag::Team& T, tag::Player* P, bool bCut)
	{
		for (size_t i = 0; i < T.roster.size(); ++i)
			if (T.roster[i].get() == P)
			{
				tag::chartRemovePlayer(T, *P);
				FGone G;
				G.TeamId = T.id;
				G.bCut = bCut;
				G.P = std::move(T.roster[i]);
				T.roster.erase(T.roster.begin() + static_cast<std::ptrdiff_t>(i));
				Gone.push_back(std::move(G));
				return;
			}
	}
	std::unique_ptr<tag::Player> TakeGone(tag::Player* P)
	{
		for (size_t i = 0; i < Gone.size(); ++i)
			if (Gone[i].P.get() == P)
			{
				std::unique_ptr<tag::Player> Out = std::move(Gone[i].P);
				Gone.erase(Gone.begin() + static_cast<std::ptrdiff_t>(i));
				return Out;
			}
		return nullptr;
	}
	/** Moves a man from one team to another (the pointer stays the same man). */
	void Move(tag::Team& From, tag::Team& To, tag::Player* P)
	{
		for (size_t i = 0; i < From.roster.size(); ++i)
			if (From.roster[i].get() == P)
			{
				tag::chartRemovePlayer(From, *P);
				std::unique_ptr<tag::Player> U = std::move(From.roster[i]);
				From.roster.erase(From.roster.begin() + static_cast<std::ptrdiff_t>(i));
				Sign(To, std::move(U));
				return;
			}
	}
}

bool RosterUI::NoSave()
{
	// (-TagRosterSave: the scripted visit saves for real - the save / load check)
	static const bool b = (FParse::Param(FCommandLine::Get(), TEXT("TagRosterShots")) && !FParse::Param(FCommandLine::Get(), TEXT("TagRosterSave")))
		|| FParse::Param(FCommandLine::Get(), TEXT("TagChartTest")) || FParse::Param(FCommandLine::Get(), TEXT("TagMenuAudio"));
	return b;
}

// -TagChartTest (with -TagSoak=): the soak's teams with depth chart changes and a created player, the user's saves
// left alone; at the end of the soak the log says who played (LogChartTest)
namespace CapUI { std::vector<tag::Player*> ReleaseCands(const tag::Team& T, tag::Pos Pos); }
namespace ChartTest
{
	int32 Home = 3, Away = 7;
	FString Expect;
	tag::Player* Made = nullptr;
	FString Tag(const tag::Player* P) { return P ? FString::Printf(TEXT("#%d %s %s"), P->number, *Pos(P->pos), *Short(*P)) : FString(TEXT("-")); }
	void Apply(AMatchDirector* D)
	{
		FParse::Value(FCommandLine::Get(), TEXT("TagHome="), Home);
		FParse::Value(FCommandLine::Get(), TEXT("TagAway="), Away);
		const int32 NT = static_cast<int32>(D->League.size());
		Home = FMath::Clamp(Home, 0, NT - 1);
		Away = FMath::Clamp(Away, 0, NT - 1);
		for (int32 Ti : {Home, Away})
		{
			tag::Team& T = *D->League[Ti];
			// the backup quarterback starts
			std::vector<tag::Player*> Q = tag::chartOrder(T, tag::DSlot::QB);
			if (Q.size() >= 2) std::swap(Q[0], Q[1]);
			tag::setChart(T, tag::DSlot::QB, Q);
			// a corner returns kicks and punts; the second tight end snaps; the punter kicks off
			std::vector<tag::Player*> CB = tag::chartOrder(T, tag::DSlot::CB), TE = tag::chartOrder(T, tag::DSlot::TE), P = tag::chartOrder(T, tag::DSlot::P);
			if (CB.size() >= 3) tag::setChart(T, tag::DSlot::KR, {CB[2]});
			if (CB.size() >= 3) tag::setChart(T, tag::DSlot::PR, {CB[2]});
			if (TE.size() >= 2) tag::setChart(T, tag::DSlot::LS, {TE[1]});
			if (!P.empty()) tag::setChart(T, tag::DSlot::KOS, {P[0]});
			Expect += FString::Printf(TEXT("  %s: QB %s, KR/PR %s, LS %s, KOS %s"), *Str(T.abbr), *Tag(tag::chartSpecialist(T, tag::DSlot::QB)),
				*Tag(tag::chartSpecialist(T, tag::DSlot::KR)), *Tag(tag::chartSpecialist(T, tag::DSlot::LS)), *Tag(tag::chartSpecialist(T, tag::DSlot::KOS)));
		}
		// a created power back on the home team, at the top of the halfback chart
		tag::Team& H = *D->League[Home];
		if (H.roster.size() >= tag::ROSTER_MAX)
		{
			std::vector<tag::Player*> C = CapUI::ReleaseCands(H, tag::Pos::RB);
			if (!C.empty()) CapStore::Remove(H, C[0], true);
		}
		auto M = std::make_unique<tag::Player>();
		M->id = CapStore::NextId++;
		M->first = "Chart";
		M->last = "Tester";
		M->pos = tag::Pos::RB;
		M->archetype = 0;
		M->age = 23;
		M->heightIn = 71;
		M->weightLb = 228;
		M->r = tag::capRatings(tag::Pos::RB, 0, 7);
		M->ovr = tag::computeOvr(tag::Pos::RB, M->r);
		M->created = true;
		M->number = tag::freeNumber(H, tag::Pos::RB, 28);
		Made = M.get();
		CapStore::Sign(H, std::move(M));
		std::vector<tag::Player*> HB = tag::chartOrder(H, tag::DSlot::HB);
		HB.erase(std::remove(HB.begin(), HB.end(), Made), HB.end());
		HB.insert(HB.begin(), Made);
		tag::setChart(H, tag::DSlot::HB, HB);
		std::vector<tag::Team*> Teams;
		for (auto& T : D->League) Teams.push_back(T.get());
		tag::gradeLeague(Teams);
		Expect += FString::Printf(TEXT("  %s: HB %s (created, %d OVR)"), *Str(H.abbr), *Tag(Made), Made->ovr);
		UE_LOG(LogTemp, Display, TEXT("TAG chart test set:%s"), *Expect);
	}
}

void SGridironOverlay::LogChartTest(AMatchDirector* D)
{
	if (!D || !D->Game || !FParse::Param(FCommandLine::Get(), TEXT("TagChartTest"))) return;
	const tag::Game& G = *D->Game;
	UE_LOG(LogTemp, Display, TEXT("TAG chart test expected:%s"), *ChartTest::Expect);
	for (const tag::Team* T : {G.home, G.away})
	{
		FString Pass, Rush;
		for (const tag::PlayerLine* L : G.stats.forTeam(T->abbr))
		{
			if (L->passAtt > 0) Pass += FString::Printf(TEXT(" %s(%s) %d/%d"), *Str(L->name), *Str(L->pos), L->passComp, L->passAtt);
			if (L->rushAtt > 0) Rush += FString::Printf(TEXT(" %s(%s) %d-%d"), *Str(L->name), *Str(L->pos), L->rushAtt, L->rushYds);
		}
		// the returner: the plays the log names him in on a kick or punt return
		const tag::Player* KR = tag::chartSpecialist(*T, tag::DSlot::KR);
		int32 Ret = 0, Returns = 0;
		for (const auto& E : G.log)
		{
			const bool bRet = E.desc.find("return") != std::string::npos;
			Returns += bRet;
			if (bRet && KR && E.desc.find(KR->last) != std::string::npos) ++Ret;
		}
		UE_LOG(LogTemp, Display, TEXT("TAG chart test %s passing:%s"), *Str(T->abbr), *Pass);
		UE_LOG(LogTemp, Display, TEXT("TAG chart test %s rushing:%s"), *Str(T->abbr), *Rush);
		UE_LOG(LogTemp, Display, TEXT("TAG chart test %s returner %s named in %d of %d return plays"), *Str(T->abbr), *ChartTest::Tag(KR), Ret, Returns);
	}
	// the verdict (Game/TagResult.h): on each side the chart's quarterback threw the most passes, and the created back
	// at the top of the home halfback chart carried the ball - checked once a side has thrown / run enough to tell
	int32 Pass = 0, Fail = 0;
	auto Check = [&Pass, &Fail](bool bOk, const FString& What)
	{
		(bOk ? Pass : Fail)++;
		UE_LOG(LogTemp, Display, TEXT("TAG chart test %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	for (const tag::Team* T : {G.home, G.away})
	{
		const tag::Player* Q = tag::chartSpecialist(*T, tag::DSlot::QB);
		int32 QAtt = 0, MostAtt = 0, TeamAtt = 0, TeamRush = 0, MadeRush = 0;
		for (const tag::PlayerLine* L : G.stats.forTeam(T->abbr))
		{
			TeamAtt += L->passAtt;
			TeamRush += L->rushAtt;
			MostAtt = FMath::Max(MostAtt, L->passAtt);
			if (Q && L->pid == Q->id) QAtt = L->passAtt;
			if (ChartTest::Made && L->pid == ChartTest::Made->id) MadeRush = L->rushAtt;
		}
		if (TeamAtt >= 3) Check(QAtt > 0 && QAtt == MostAtt, FString::Printf(TEXT("%s: the chart's QB %s threw the most passes (%d of %d)"), *Str(T->abbr), *ChartTest::Tag(Q), QAtt, TeamAtt));
		else UE_LOG(LogTemp, Display, TEXT("TAG chart test %s: %d passes, too few to check the QB"), *Str(T->abbr), TeamAtt);
		if (T == G.home)
		{
			if (TeamRush >= 5) Check(MadeRush > 0, FString::Printf(TEXT("%s: the created HB %s carried the ball (%d of %d rushes)"), *Str(T->abbr), *ChartTest::Tag(ChartTest::Made), MadeRush, TeamRush));
			else UE_LOG(LogTemp, Display, TEXT("TAG chart test %s: %d rushes, too few to check the HB"), *Str(T->abbr), TeamRush);
		}
	}
	TagResult::Finish(TEXT("TagChartTest"), Pass, Fail, false); // (the soak that runs it quits the game)
}

void RosterUI::SaveChart(const tag::Team& T)
{
	if (!GConfig || NoSave()) return;
	FString Line;
	for (int32 i = 0; i < tag::DSLOTS; ++i)
	{
		const tag::DSlot S = static_cast<tag::DSlot>(i);
		if (!tag::chartSet(T, S)) continue;
		FString Nums;
		for (const tag::Player* P : tag::chartOrder(T, S)) Nums += (Nums.IsEmpty() ? TEXT("") : TEXT(".")) + FString::FromInt(P->number);
		Line += FString::Printf(TEXT("%s%s:%s"), Line.IsEmpty() ? TEXT("") : TEXT("|"), ANSI_TO_TCHAR(tag::dslotInfo(S).abbr), *Nums);
	}
	if (Line.IsEmpty()) GConfig->RemoveKey(CapStore::SecChart, *Str(T.abbr), GGameUserSettingsIni);
	else GConfig->SetString(CapStore::SecChart, *Str(T.abbr), *Line, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void RosterUI::SaveCreated(AMatchDirector* D)
{
	if (!GConfig || NoSave() || !D) return;
	GConfig->EmptySection(CapStore::SecCreated, GGameUserSettingsIni);
	int32 N = 0;
	for (auto& T : D->League)
		for (auto& P : T->roster)
			if (P->created) GConfig->SetString(CapStore::SecCreated, *FString::Printf(TEXT("P%d"), N++), *CapStore::PlayerLine(*T, *P), GGameUserSettingsIni);
	GConfig->SetInt(CapStore::SecCreated, TEXT("Count"), N, GGameUserSettingsIni);
	// (a man's number may have changed: the charts are saved by number)
	for (auto& T : D->League) SaveChart(*T);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void RosterUI::SaveCuts(AMatchDirector* D)
{
	if (!GConfig || NoSave() || !D) return;
	GConfig->EmptySection(CapStore::SecCut, GGameUserSettingsIni);
	for (auto& T : D->League)
	{
		FString Nums;
		for (const CapStore::FGone& G : CapStore::Gone)
			if (G.bCut && G.TeamId == T->id && G.P && !G.P->created) Nums += (Nums.IsEmpty() ? TEXT("") : TEXT(",")) + FString::FromInt(G.P->number);
		if (!Nums.IsEmpty()) GConfig->SetString(CapStore::SecCut, *Str(T->abbr), *Nums, GGameUserSettingsIni);
	}
	GConfig->Flush(false, GGameUserSettingsIni);
}

void RosterUI::Regrade(AMatchDirector* D)
{
	if (!D) return;
	std::vector<tag::Team*> Teams;
	for (auto& T : D->League) Teams.push_back(T.get());
	tag::gradeLeague(Teams);
	if (!FParse::Param(FCommandLine::Get(), TEXT("TagCoachPicks"))) SGridironOverlay::LoadAbilityLoadouts(D);
}

std::unique_ptr<tag::Player> RosterUI::Retire(tag::Team& T, tag::Player* P)
{
	CapStore::Remove(T, P, false);
	return nullptr;
}

TArray<TPair<int32, tag::Player*>> RosterUI::CutMen(int32 TeamId)
{
	TArray<TPair<int32, tag::Player*>> Out;
	for (const CapStore::FGone& G : CapStore::Gone)
		if (G.bCut && (TeamId < 0 || G.TeamId == TeamId)) Out.Add(TPair<int32, tag::Player*>(G.TeamId, G.P.get()));
	return Out;
}

bool RosterUI::Uncut(tag::Team& T, tag::Player* P)
{
	if (T.roster.size() >= tag::ROSTER_MAX) return false;
	std::unique_ptr<tag::Player> U = CapStore::TakeGone(P);
	if (!U) return false;
	CapStore::Sign(T, std::move(U));
	return true;
}

void SGridironOverlay::LoadRosterEdits(AMatchDirector* D)
{
	if (D && FParse::Param(FCommandLine::Get(), TEXT("TagChartTest"))) { ChartTest::Apply(D); return; }
	if (!D || !GConfig || FParse::Param(FCommandLine::Get(), TEXT("TagRosterShots")) || FParse::Param(FCommandLine::Get(), TEXT("TagStockRosters"))) return;
	int32 NCut = 0, NMade = 0, NCharts = 0;
	// the men cut to make room (the league's own, by number)
	for (auto& T : D->League)
	{
		FString Line;
		if (!GConfig->GetString(CapStore::SecCut, *Str(T->abbr), Line, GGameUserSettingsIni) || Line.IsEmpty()) continue;
		TArray<FString> Nums;
		Line.ParseIntoArray(Nums, TEXT(","));
		for (const FString& N : Nums)
		{
			const int32 Num = FCString::Atoi(*N);
			for (auto& P : T->roster)
				if (!P->created && P->number == Num)
				{
					CapStore::Remove(*T, P.get(), true);
					++NCut;
					break;
				}
		}
	}
	// the created players
	int32 Count = 0;
	GConfig->GetInt(CapStore::SecCreated, TEXT("Count"), Count, GGameUserSettingsIni);
	for (int32 i = 0; i < Count; ++i)
	{
		FString Line, Abbr;
		if (!GConfig->GetString(CapStore::SecCreated, *FString::Printf(TEXT("P%d"), i), Line, GGameUserSettingsIni)) continue;
		std::unique_ptr<tag::Player> P = CapStore::ParseLine(Line, Abbr);
		tag::Team* T = P ? CapStore::TeamByAbbr(D, Abbr) : nullptr;
		if (!T) continue;
		if (T->roster.size() >= tag::ROSTER_MAX)
		{
			UE_LOG(LogTemp, Warning, TEXT("TAG roster: %s is full; created player %s %s not added"), *Abbr, *Str(P->first), *Str(P->last));
			continue;
		}
		P->id = CapStore::NextId++;
		CapStore::Sign(*T, std::move(P));
		++NMade;
	}
	// the depth charts ("QB:12.7.3|FS:24.31")
	for (auto& T : D->League)
	{
		FString Line;
		if (!GConfig->GetString(CapStore::SecChart, *Str(T->abbr), Line, GGameUserSettingsIni) || Line.IsEmpty()) continue;
		TArray<FString> Slots;
		Line.ParseIntoArray(Slots, TEXT("|"));
		for (const FString& SL : Slots)
		{
			FString A, Nums;
			tag::DSlot S = tag::DSlot::QB;
			if (!SL.Split(TEXT(":"), &A, &Nums) || !tag::dslotByAbbr(Std(A), S)) continue;
			TArray<FString> Ns;
			Nums.ParseIntoArray(Ns, TEXT("."));
			std::vector<tag::Player*> Order;
			for (const FString& N : Ns)
			{
				const int32 Num = FCString::Atoi(*N);
				for (auto& P : T->roster)
					if (P->number == Num) { Order.push_back(P.get()); break; }
			}
			// (men of the slot's position the save does not list - a new signing - go to the bottom)
			for (tag::Player* P : tag::autoChart(*T, S))
				if (std::find(Order.begin(), Order.end(), P) == Order.end() && P->pos == tag::dslotInfo(S).natural) Order.push_back(P);
			if (!Order.empty())
			{
				tag::setChart(*T, S, Order);
				++NCharts;
			}
		}
	}
	if (NCut || NMade)
	{
		std::vector<tag::Team*> Teams;
		for (auto& T : D->League) Teams.push_back(T.get());
		tag::gradeLeague(Teams);
	}
	UE_LOG(LogTemp, Display, TEXT("TAG rosters: %d created players, %d men cut, %d depth chart slots set"), NMade, NCut, NCharts);
}

// ------------------------------------------------------------------ the screen's state
namespace CapUI
{
	enum class EMode { Hub, Form };
	EMode Mode = EMode::Hub;
	int32 HubRow = 0;
	int32 DeleteArm = -1;  // the created man the next triangle deletes
	std::function<void()> Back;
	FString Msg;
	FLinearColor MsgColor = Dim;
	void Say(const FString& S, const FLinearColor& C) { Msg = S; MsgColor = C; }

	struct FTalent { const TCHAR* Name; double V; };
	// (a league starter's template is ~78 OVR and a talent point ~1.2 OVR: Pro Bowl ~83, Elite ~87, Legend ~93 - the
	// league's own best are in the high 80s)
	const FTalent Talents[] = {{TEXT("Backup"), -8}, {TEXT("Starter"), 0}, {TEXT("Pro Bowl"), 4}, {TEXT("Elite"), 7}, {TEXT("Legend"), 12}};
	constexpr int32 NTalents = 5;

	/** The man being made (or edited). */
	struct FDraft
	{
		int32 TeamIdx = 0;
		tag::Pos Pos = tag::Pos::QB;
		int32 Arch = 0;
		int32 Talent = 1;
		FString First, Last;
		int32 Number = 7;
		int32 Age = 22;
		int32 HeightIn = 75;
		int32 WeightLb = 220;
		int32 Face = 0;
		int32 Helmet = 0;
		tag::Ratings R;
		tag::Player* Editing = nullptr;  // the created man being edited (null: a new one)
		int32 ReleaseId = -1;            // the man a full team releases to make room
	};
	FDraft Draft;
	int32 FormRow = 0;
	bool bTyping = false;
	TSharedPtr<SEditableTextBox> TypeBox;

	enum ERow : int32 { RTeam, RPos, RArch, RTalent, RFirst, RLast, RNum, RAge, RHeight, RWeight, RFace, RHelmet, RRelease, RCreate, RDelete, RCancel, RRating = 100 };

	int32 FaceCount()
	{
		const int32 N = FAthleteAssets::Get().HeroOf(TEXT("Head")).Num();
		return FMath::Max(1, N);
	}
	/** The helmets of the man's facemask style (Athlete.cpp SetupHero's pick). */
	int32 HelmetCount(tag::Pos P)
	{
		using tag::Pos;
		const bool bLine = P == Pos::OT || P == Pos::OG || P == Pos::C || P == Pos::DE || P == Pos::DT;
		const FString Mask = bLine ? TEXT("cage") : (P == Pos::RB || P == Pos::FB || P == Pos::TE || P == Pos::LB) ? TEXT("std")
			: (P == Pos::K || P == Pos::P) ? TEXT("kick") : TEXT("open");
		int32 N = FAthleteAssets::Get().HeroOf(TEXT("Helmet"), [&](const FHeroItem& I) { return I.Style == Mask; }).Num();
		if (!N) N = FAthleteAssets::Get().HeroOf(TEXT("Helmet")).Num();
		return FMath::Max(1, N);
	}
	int32 FaceTone(int32 Face)
	{
		const FAthleteAssets& A = FAthleteAssets::Get();
		const TArray<int32> Heads = A.HeroOf(TEXT("Head"));
		if (!Heads.Num()) return 0;
		return A.HeroItems[Heads[((Face % Heads.Num()) + Heads.Num()) % Heads.Num()]].Tone;
	}
	void Rate(FDraft& D)
	{
		D.R = tag::capRatings(D.Pos, D.Arch, Talents[FMath::Clamp(D.Talent, 0, NTalents - 1)].V);
	}
	void Frame(FDraft& D)
	{
		const auto& A = tag::capArchetypes(D.Pos);
		if (D.Arch >= 0 && D.Arch < static_cast<int32>(A.size()))
		{
			D.HeightIn = FMath::RoundToInt(A[static_cast<size_t>(D.Arch)].heightIn);
			D.WeightLb = FMath::RoundToInt(A[static_cast<size_t>(D.Arch)].weightLb);
		}
	}
	FString PickName(const std::vector<std::string>& Pool, int32 I)
	{
		const int32 N = static_cast<int32>(Pool.size());
		return Str(Pool[static_cast<size_t>(((I % N) + N) % N)]);
	}
	int32 NameIndex(const std::vector<std::string>& Pool, const FString& S)
	{
		for (size_t i = 0; i < Pool.size(); ++i)
			if (Str(Pool[i]) == S) return static_cast<int32>(i);
		return -1;
	}
	/** A new man's profile rolled (name, number, look, age), the position and ratings kept. */
	void Roll(FDraft& D, const tag::Team& T)
	{
		const int32 Seed = static_cast<int32>(FPlatformTime::Cycles() & 0x7fffffff);
		FRandomStream Rs(Seed);
		D.First = PickName(tag::firstNamePool(), Rs.RandRange(0, 9999));
		D.Last = PickName(tag::lastNamePool(), Rs.RandRange(0, 9999));
		D.Face = Rs.RandRange(0, FaceCount() - 1);
		D.Helmet = Rs.RandRange(0, HelmetCount(D.Pos) - 1);
		D.Age = Rs.RandRange(21, 24);
		const int32 N = tag::freeNumber(T, D.Pos, -1);
		if (N >= 0 && (!D.Editing || !tag::numberFree(T, D.Number, D.Editing))) D.Number = N;
	}
	/** The men a full team could release for him: the lowest rated at his position first, then the rest of the roster. */
	std::vector<tag::Player*> ReleaseCands(const tag::Team& T, tag::Pos Pos)
	{
		std::vector<tag::Player*> Same, Rest;
		for (const auto& P : T.roster)
			if (!P->created) (P->pos == Pos ? Same : Rest).push_back(P.get());
		auto ByOvr = [](const tag::Player* A, const tag::Player* B) { return A->ovr < B->ovr; };
		std::stable_sort(Same.begin(), Same.end(), ByOvr);
		std::stable_sort(Rest.begin(), Rest.end(), ByOvr);
		Same.insert(Same.end(), Rest.begin(), Rest.end());
		return Same;
	}
	bool NeedsRelease(AMatchDirector* Dir, const FDraft& D)
	{
		const tag::Team& T = *Dir->League[D.TeamIdx];
		if (D.Editing && T.hasPlayer(D.Editing)) return false;
		return T.roster.size() >= tag::ROSTER_MAX;
	}
	/** The form's rows, top to bottom. */
	TArray<int32> FormRows(AMatchDirector* Dir)
	{
		TArray<int32> R = {RTeam, RPos, RArch, RTalent, RFirst, RLast, RNum, RAge, RHeight, RWeight, RFace, RHelmet};
		if (NeedsRelease(Dir, Draft)) R.Add(RRelease);
		// the ratings: the position's key ones first, then the rest in their groups
		const std::vector<const tag::RatingDef*> Key = tag::keyRatings(Draft.Pos, 8);
		const auto& All = tag::ratingDefs();
		for (const tag::RatingDef* K : Key) R.Add(RRating + static_cast<int32>(K - All.data()));
		for (size_t i = 0; i < All.size(); ++i)
			if (std::find(Key.begin(), Key.end(), &All[i]) == Key.end()) R.Add(RRating + static_cast<int32>(i));
		R.Add(RCreate);
		if (Draft.Editing) R.Add(RDelete);
		R.Add(RCancel);
		return R;
	}
	/** Projected grade: where his OVR would stand among the league's men at his position. */
	tag::StarGrade Project(AMatchDirector* Dir, tag::Pos Pos, int32 Ovr, const tag::Player* Except, int32& Better, int32& Of)
	{
		Better = 0;
		Of = 1;
		for (auto& T : Dir->League)
			for (auto& P : T->roster)
				if (P->pos == Pos && P.get() != Except)
				{
					++Of;
					Better += P->ovr > Ovr;
				}
		const double N = Of;
		if (Better < FMath::Max(1.0, FMath::RoundToDouble(N * 0.03))) return tag::StarGrade::XFactor;
		if (Better < FMath::Max(2.0, FMath::RoundToDouble(N * 0.10))) return tag::StarGrade::Superstar;
		if (Better < FMath::Max(4.0, FMath::RoundToDouble(N * 0.22))) return tag::StarGrade::Star;
		return tag::StarGrade::None;
	}
	FLinearColor GradeColor(tag::StarGrade G)
	{
		switch (G)
		{
		case tag::StarGrade::XFactor: return FLinearColor(FColor::FromHex(TEXT("FF6A1A")));
		case tag::StarGrade::Superstar: return FLinearColor(FColor::FromHex(TEXT("B58CFF")));
		case tag::StarGrade::Star: return FLinearColor(FColor::FromHex(TEXT("72B8FF")));
		default: return Faint;
		}
	}
	tag::DSlot BaseSlot(tag::Pos P)
	{
		using tag::Pos;
		using DS = tag::DSlot;
		switch (P)
		{
		case Pos::QB: return DS::QB;
		case Pos::RB: return DS::HB;
		case Pos::FB: return DS::FB;
		case Pos::WR: return DS::WR;
		case Pos::TE: return DS::TE;
		case Pos::OT: return DS::LT;
		case Pos::OG: return DS::LG;
		case Pos::C: return DS::C;
		case Pos::DE: return DS::LE;
		case Pos::DT: return DS::DT;
		case Pos::LB: return DS::MLB;
		case Pos::CB: return DS::CB;
		case Pos::S: return DS::FS;
		case Pos::K: return DS::K;
		case Pos::P: return DS::P;
		}
		return DS::QB;
	}
	/** The hub's rows: New, the created men, the released men. */
	struct FHubRow
	{
		int32 Kind = 0;  // 0 new, 1 created, 2 released
		tag::Player* P = nullptr;
		int32 TeamId = -1;
	};
	TArray<FHubRow> HubRows(AMatchDirector* Dir)
	{
		TArray<FHubRow> R;
		R.Add(FHubRow());
		for (auto& T : Dir->League)
			for (auto& P : T->roster)
				if (P->created) R.Add(FHubRow{1, P.get(), T->id});
		for (const TPair<int32, tag::Player*>& C : CutMen(-1)) R.Add(FHubRow{2, C.Value, C.Key});
		return R;
	}
}

using namespace CapUI;

// ------------------------------------------------------------------ the man on the turntable
// The form's live picture: an athlete dressed as the draft (his team's home uniform, his build, face, helmet and
// number) standing alone 300 m over the field, turning slowly under his own studio lights (key, fill, rim: the game
// may be a night game), filmed by a scene capture that sees only him at a fixed exposure; the card shows the
// capture. Rebuilt when his look changes, gone when the form closes.
namespace CapPreview
{
	TWeakObjectPtr<AAthlete> Actor;
	TWeakObjectPtr<ASceneCapture2D> Cam;
	TArray<TWeakObjectPtr<APointLight>> Lights;
	UTextureRenderTarget2D* RT = nullptr;
	FSlateBrush Brush;
	tag::Player Man;  // (the athlete holds his address)
	FString Key;
	float Yaw = 205.f;
	bool bHelmet = true;
	FTSTicker::FDelegateHandle Spin;
	const FVector At(0.0, 0.0, 30000.0);

	FUniform Uniform(const tag::Team& T)
	{
		FUniform U;
		U.Jersey = HexCol(T.colors.primary);
		U.Pants = HexCol(T.colors.secondary);
		U.Helmet = HexCol(T.colors.primary);
		U.Trim = HexCol(T.colors.secondary);
		U.Number = HexCol(T.colors.accent);
		U.TeamId = T.id;
		U.Logo = HexCol(T.colors.secondary);
		return U;
	}
	void Stop()
	{
		if (Actor.IsValid()) Actor->Destroy();
		if (Cam.IsValid()) Cam->Destroy();
		for (TWeakObjectPtr<APointLight>& L : Lights)
			if (L.IsValid()) L->Destroy();
		Lights.Reset();
		Actor.Reset();
		Cam.Reset();
		Key.Reset();
		if (Spin.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Spin);
		Spin.Reset();
	}
	/** The capture's brush with the athlete as the draft looks, or null (no world). */
	const FSlateBrush* Update(AMatchDirector* Dir, const CapUI::FDraft& D, const tag::Team& T, bool bHelmetOn)
	{
		UWorld* W = Dir ? Dir->GetWorld() : nullptr;
		if (!W || FParse::Param(FCommandLine::Get(), TEXT("TagNoCapPreview"))) return nullptr;
		if (!RT)
		{
			RT = NewObject<UTextureRenderTarget2D>();
			RT->AddToRoot();
			RT->ClearColor = FLinearColor(0.02f, 0.025f, 0.04f, 1.f);
			RT->InitCustomFormat(560, 700, PF_B8G8R8A8, false);
			RT->UpdateResourceImmediate(true);
			Brush.SetResourceObject(RT);
			Brush.ImageSize = FVector2D(280, 350);
			Brush.DrawAs = ESlateBrushDrawType::Image;
		}
		Man.id = 950001;  // (not a bench man: the helmet stays as the form says)
		Man.first = Std(D.First);
		Man.last = Std(D.Last);
		Man.pos = D.Pos;
		Man.number = D.Number;
		Man.heightIn = D.HeightIn;
		Man.weightLb = D.WeightLb;
		Man.face = D.Face;
		Man.helmet = D.Helmet;
		Man.r = D.R;
		Man.created = true;
		const FString K = FString::Printf(TEXT("%d|%d|%d|%d|%d|%d|%d"), T.id, static_cast<int32>(D.Pos), D.HeightIn, D.WeightLb, D.Face, D.Helmet, D.Number);
		if (K != Key || !Actor.IsValid())
		{
			Key = K;
			if (Actor.IsValid()) Actor->Destroy();
			AAthlete* A = W->SpawnActor<AAthlete>(AAthlete::StaticClass(), FTransform::Identity);
			if (!A) return nullptr;
			A->TickAfter(Dir);
			A->Setup(&Man, Uniform(T));
			A->StandOnSideline(At, Yaw, 1);
			A->SetHelmetOn(bHelmetOn);
			bHelmet = bHelmetOn;
			Actor = A;
			if (Cam.IsValid())
			{
				Cam->GetCaptureComponent2D()->ShowOnlyActors.Reset();
				Cam->GetCaptureComponent2D()->ShowOnlyActors.Add(A);
			}
		}
		else if (bHelmet != bHelmetOn)
		{
			Actor->SetHelmetOn(bHelmetOn);
			bHelmet = bHelmetOn;
		}
		// in front of him, a touch above: the whole man, or (the face row) his head and shoulders
		const double Tall = D.HeightIn * 2.54;
		const FVector Look = At + FVector(0, 0, bHelmetOn ? Tall * 0.52 : Tall * 0.9);
		const FVector From = Look + (bHelmetOn ? FVector(410.0, 0.0, 30.0) : FVector(150.0, 0.0, 8.0));
		if (Cam.IsValid()) Cam->SetActorLocationAndRotation(From, (Look - From).Rotation());
		if (!Cam.IsValid())
		{
			ASceneCapture2D* C = W->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), FTransform((Look - From).Rotation(), From));
			if (!C) return nullptr;
			USceneCaptureComponent2D* SC = C->GetCaptureComponent2D();
			SC->TextureTarget = RT;
			SC->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			SC->ShowOnlyActors.Add(Actor.Get());
			SC->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
			SC->FOVAngle = 24.f;
			SC->bCaptureEveryFrame = true;
			SC->bCaptureOnMovement = false;
			SC->bAlwaysPersistRenderingState = true;
			SC->ShowFlags.SetAtmosphere(false);
			SC->ShowFlags.SetFog(false);
			SC->ShowFlags.SetVolumetricFog(false);
			SC->ShowFlags.SetMotionBlur(false);
			// a fixed exposure for the studio lights (eye adaptation over a black frame would wander)
			float Ev = -9.5f;  // (exposure compensation: about EV100 9.5 for ~700 lux on him)
			FParse::Value(FCommandLine::Get(), TEXT("TagCapEV="), Ev);
			SC->PostProcessSettings.bOverride_AutoExposureMethod = true;
			SC->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
			SC->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
			SC->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
			SC->PostProcessSettings.bOverride_AutoExposureBias = true;
			SC->PostProcessSettings.AutoExposureBias = Ev;
			SC->PostProcessBlendWeight = 1.f;
			Cam = C;
			// key (front left, high), fill (front right), rim (behind)
			struct FL { FVector Off; float Cd; FLinearColor C; };
			const FL Rig[] = {{FVector(320, -260, 330), 9000.f, FLinearColor(1.f, 0.96f, 0.9f)},
				{FVector(360, 280, 160), 3500.f, FLinearColor(0.85f, 0.9f, 1.f)},
				{FVector(-280, 60, 320), 8000.f, FLinearColor(1.f, 1.f, 1.f)}};
			for (const FL& R : Rig)
			{
				APointLight* L = W->SpawnActor<APointLight>(APointLight::StaticClass(), FTransform(At + R.Off));
				if (!L) continue;
				UPointLightComponent* PC = L->PointLightComponent;
				PC->SetMobility(EComponentMobility::Movable);
				PC->SetIntensityUnits(ELightUnits::Candelas);
				PC->SetIntensity(R.Cd);
				PC->SetLightColor(R.C);
				PC->SetAttenuationRadius(1500.f);
				PC->SetCastShadows(false);
				Lights.Add(L);
			}
		}
		if (!Spin.IsValid())
			Spin = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float Dt)
			{
				if (!Actor.IsValid()) return true;
				Yaw = FMath::Fmod(Yaw + Dt * 22.f, 360.f);
				Actor->SetActorRotation(FRotator(0.f, Yaw, 0.f));
				return true;
			}));
		return &Brush;
	}
}

// ------------------------------------------------------------------ the screen
void SGridironOverlay::ShowCreatePlayer(int32 InTeam, std::function<void()> InBack)
{
	if (!Director || Director->League.empty()) return;
	AMatchDirector* Dir = Director;
	ATagAudio::Screen(TEXT("CreatePlayer"));
	const int32 NTeams = static_cast<int32>(Dir->League.size());
	if (InBack)
	{
		Back = InBack;
		Mode = EMode::Hub;
		HubRow = 0;
		DeleteArm = -1;
		Msg.Reset();
		Draft.TeamIdx = FMath::Clamp(InTeam, 0, NTeams - 1);
	}
	auto Rebuild = [this] { ShowCreatePlayer(-1, nullptr); };
	const bool bPad = PadGlyphs();
	auto Hint = [this](EPadGlyph Gl, const FString& Key, const TCHAR* What) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SPadGlyph).Owner(this).Glyph(Gl).Key(Key).Size(22.f) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(*What ? 6.f : 1.f, 0, *What ? 18.f : 1.f, 0)[ Label(What, 10, White90, "Regular") ];
	};
	auto OpenForm = [this, Dir, Rebuild](tag::Player* Edit, int32 TeamIdx)
	{
		FDraft D;
		if (Edit)
		{
			tag::Team* T = CapStore::TeamOf(Dir, Edit);
			D.TeamIdx = T ? T->id : 0;
			for (int32 i = 0; i < static_cast<int32>(Dir->League.size()); ++i)
				if (Dir->League[i].get() == T) D.TeamIdx = i;
			D.Pos = Edit->pos;
			D.Arch = FMath::Max(0, Edit->archetype);
			D.Talent = 1;
			D.First = Str(Edit->first);
			D.Last = Str(Edit->last);
			D.Number = Edit->number;
			D.Age = Edit->age;
			D.HeightIn = FMath::RoundToInt(Edit->heightIn);
			D.WeightLb = FMath::RoundToInt(Edit->weightLb);
			D.Face = FMath::Max(0, Edit->face);
			D.Helmet = FMath::Max(0, Edit->helmet);
			D.R = Edit->r;
			D.Editing = Edit;
		}
		else
		{
			D.TeamIdx = TeamIdx;
			D.Pos = tag::Pos::QB;
			D.Arch = 0;
			D.Talent = 1;
			Rate(D);
			Frame(D);
			Roll(D, *Dir->League[TeamIdx]);
		}
		Draft = D;
		Mode = EMode::Form;
		FormRow = 0;
		bTyping = false;
		Msg.Reset();
		Rebuild();
	};

	TSharedPtr<SWidget> PageP;
	std::function<bool(const FKey&)> OnKey;
	FLinearColor Wash = FLinearColor(0.2f, 0.4f, 0.6f);

	if (Mode == EMode::Hub)
	{
		// ================================================================ the hub
		CapPreview::Stop();
		const TArray<FHubRow> Rows = HubRows(Dir);
		HubRow = FMath::Clamp(HubRow, 0, Rows.Num() - 1);
		TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
		bool bCutHead = false;
		const int32 Window = 16;
		const int32 First = FMath::Clamp(HubRow - Window / 2, 0, FMath::Max(0, Rows.Num() - Window));
		for (int32 i = First; i < Rows.Num() && i < First + Window; ++i)
		{
			const FHubRow& H = Rows[i];
			const bool bSel = i == HubRow;
			TSharedRef<SWidget> In = SNullWidget::NullWidget;
			if (H.Kind == 0)
			{
				const tag::Team& T = *Dir->League[Draft.TeamIdx];
				In = SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 10)[ Label(TEXT("+"), 20, Good) ]
					+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ Label(TEXT("NEW PLAYER"), 14, White90) ]
						+ SVerticalBox::Slot().AutoHeight()[ Label(FString::Printf(TEXT("Any position, any team  ·  starts on %s (%d / %d)"), *Str(T.abbr), static_cast<int32>(T.roster.size()), static_cast<int32>(tag::ROSTER_MAX)), 9, Faint, "Regular") ]
					];
			}
			else
			{
				if (H.Kind == 2 && !bCutHead)
				{
					bCutHead = true;
					List->AddSlot().AutoHeight().Padding(0, 10, 0, 4)[ Heading(TEXT("Released to make room  ·  cross re-signs him")) ];
				}
				const tag::Player& P = *H.P;
				const tag::Team* T = nullptr;
				for (auto& Tt : Dir->League)
					if (Tt->id == H.TeamId) T = Tt.get();
				FString Arch;
				if (P.archetype >= 0 && P.archetype < static_cast<int32>(tag::capArchetypes(P.pos).size())) Arch = UTF8_TO_TCHAR(tag::capArchetypes(P.pos)[static_cast<size_t>(P.archetype)].name);
				const bool bArmed = H.Kind == 1 && DeleteArm == P.id;
				In = SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(4)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(T ? TeamInk(*T) : Faint) ] ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 6, 0, 6)[ SNew(SBox).WidthOverride(52)[ Label(T ? Str(T->abbr) : FString(), 11, T ? TeamInk(*T) : Faint) ] ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(70)[ Label(FString::Printf(TEXT("%s #%d"), *Pos(P.pos), P.number), 10, Dim) ] ]
					+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ Clip(Full(P).ToUpper(), 12, H.Kind == 2 ? Dim : White90) ]
						+ SVerticalBox::Slot().AutoHeight()[ Label(bArmed ? FString(TEXT("Triangle again to delete him")) : Arch, 8, bArmed ? Warn : Faint, "Regular") ]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)[ H.Kind == 1 ? Chip(TEXT("CAP"), Cap, Ink, 7) : Chip(TEXT("RELEASED"), Warn, Ink, 7) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 12, 0)[ SNew(SBox).WidthOverride(30).HAlign(HAlign_Right)[ Label(FString::FromInt(P.ovr), 15, OvrColor(P.ovr)) ] ];
			}
			List->AddSlot().AutoHeight().Padding(0, 0, 0, 3)[ Click(RosterUI::Frame(bSel, true, In), [this, i, Rebuild] { HubRow = i; Rebuild(); }) ];
		}
		int32 NMade = 0;
		for (const FHubRow& H : Rows) NMade += H.Kind == 1;
		TSharedRef<SWidget> Hints = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::DUpDown, TEXT("↑↓"), TEXT("Move")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::L2, TEXT("Z"), TEXT("")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::R2, TEXT("C"), TEXT("New player's team")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Cross, TEXT("Enter"), TEXT("Create / edit / re-sign")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Square, TEXT("R"), TEXT("Depth chart")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Triangle, TEXT("T"), TEXT("Delete")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Circle, TEXT("Esc"), TEXT("Done")) ];
		PageP = SNew(SBox).WidthOverride(900)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[ TagTheme::Title(TEXT("Roster"), TEXT("Create a Player"), 36) ]
					+ SHorizontalBox::Slot().FillWidth(1)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(0, 0, 0, 5)[ Label(FString::Printf(TEXT("%d CREATED  ·  ROSTERS UP TO %d"), NMade, static_cast<int32>(tag::ROSTER_MAX)), 10, Dim) ]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[ SNew(TagTheme::SPanel).Fill(Card).Radius(6.f).Padding(FMargin(12, 10))[ List ] ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)
				[ SNew(SBox).HeightOverride(18)[ Label(Msg.IsEmpty() ? FString(TEXT("A created player plays like anyone: he is on the depth chart, graded for abilities, saved for every game.")) : Msg, 11, Msg.IsEmpty() ? Faint : MsgColor) ] ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)[ TagTheme::HintBar(Hints) ]
			];
		OnKey = [this, Dir, Rebuild, OpenForm](const FKey& K) -> bool
		{
			const FNav N(K);
			TArray<FHubRow> Rs = HubRows(Dir);
			HubRow = FMath::Clamp(HubRow, 0, Rs.Num() - 1);
			const FHubRow H = Rs[HubRow];
			if (N.Moves()) ATagAudio::Ui(TEXT("UiMove"));
			if (!N.bTriangle) DeleteArm = -1;
			if (N.bUp || N.bDown) { HubRow = FMath::Clamp(HubRow + (N.bDown ? 1 : -1), 0, Rs.Num() - 1); Rebuild(); return true; }
			if (N.bPrevT || N.bNextT || N.bPrev || N.bNext)
			{
				const int32 NT = static_cast<int32>(Dir->League.size());
				Draft.TeamIdx = (Draft.TeamIdx + ((N.bNextT || N.bNext) ? 1 : NT - 1)) % NT;
				HubRow = 0;
				Rebuild();
				return true;
			}
			if (N.bOk)
			{
				if (H.Kind <= 1) ATagAudio::Ui(TEXT("UiSelect"));
				if (H.Kind == 0) OpenForm(nullptr, Draft.TeamIdx);
				else if (H.Kind == 1) OpenForm(H.P, 0);
				else
				{
					tag::Team* T = nullptr;
					for (auto& Tt : Dir->League)
						if (Tt->id == H.TeamId) T = Tt.get();
					if (T && Uncut(*T, H.P))
					{
						SaveCuts(Dir);
						SaveCreated(Dir);
						Regrade(Dir);
						ATagAudio::Ui(TEXT("UiSave"));
						Say(FString::Printf(TEXT("%s re-signed with %s (#%d)"), *Full(*H.P), *Str(T->abbr), H.P->number), Good);
					}
					else { ATagAudio::Ui(TEXT("UiError")); Say(FString::Printf(TEXT("%s is full: release someone first"), T ? *Str(T->abbr) : TEXT("The team")), Warn); }
					Rebuild();
				}
				return true;
			}
			if (N.bSquare)
			{
				// the depth chart of the team under the cursor (a created man's: his slot)
				ATagAudio::Ui(TEXT("UiTab"));
				int32 TI = Draft.TeamIdx, Slot = -1, Pid = -1;
				if (H.P && H.Kind == 1)
				{
					for (int32 i = 0; i < static_cast<int32>(Dir->League.size()); ++i)
						if (Dir->League[i]->hasPlayer(H.P)) TI = i;
					Slot = static_cast<int32>(BaseSlot(H.P->pos));
					Pid = H.P->id;
				}
				ShowDepthChart(TI, [this] { ShowCreatePlayer(-1, nullptr); }, Slot, Pid);
				return true;
			}
			if (N.bTriangle && H.Kind == 1)
			{
				if (DeleteArm != H.P->id)
				{
					ATagAudio::Ui(TEXT("UiTab"));
					DeleteArm = H.P->id;
					Rebuild();
					return true;
				}
				ATagAudio::Ui(TEXT("UiDelete"));
				DeleteArm = -1;
				if (tag::Team* T = CapStore::TeamOf(Dir, H.P))
				{
					const FString Who = Full(*H.P);
					Retire(*T, H.P);
					SaveCreated(Dir);
					Regrade(Dir);
					Say(FString::Printf(TEXT("%s deleted"), *Who), Dim);
				}
				Rebuild();
				return true;
			}
			if (N.bBack || N.bStart)
			{
				ATagAudio::Ui(TEXT("UiBack"));
				if (Back) { const std::function<void()> B = Back; Back = nullptr; B(); }
				else ShowMainMenu();
				return true;
			}
			return false;
		};
		Wash = TeamInk(*Dir->League[Draft.TeamIdx]);
	}
	else
	{
		// ================================================================ the form
		FDraft& D = Draft;
		D.TeamIdx = FMath::Clamp(D.TeamIdx, 0, NTeams - 1);
		tag::Team& Tm = *Dir->League[D.TeamIdx];
		const TArray<int32> Rows = FormRows(Dir);
		FormRow = FMath::Clamp(FormRow, 0, Rows.Num() - 1);
		const int32 Cur = Rows[FormRow];
		const FLinearColor Pri = TeamInk(Tm);
		const auto& Arches = tag::capArchetypes(D.Pos);
		D.Arch = FMath::Clamp(D.Arch, 0, static_cast<int32>(Arches.size()) - 1);
		const tag::CapArchetype& Ar = Arches[static_cast<size_t>(D.Arch)];
		const int32 Ovr = tag::computeOvr(D.Pos, D.R);
		const auto& Defs = tag::ratingDefs();
		const std::vector<const tag::RatingDef*> KeyR = tag::keyRatings(D.Pos, 8);
		if (D.ReleaseId < 0 || !NeedsRelease(Dir, D))
		{
			const std::vector<tag::Player*> C = ReleaseCands(Tm, D.Pos);
			D.ReleaseId = NeedsRelease(Dir, D) && !C.empty() ? C[0]->id : -1;
		}
		tag::Player* Rel = nullptr;
		for (auto& P : Tm.roster)
			if (P->id == D.ReleaseId) Rel = P.get();

		// one row: its name, its value, the cursor's frame
		auto RowW = [&](int32 Id, const FString& Name, TSharedRef<SWidget> Value, bool bArrows) -> TSharedRef<SWidget>
		{
			const bool bSel = Id == Cur;
			const int32 Idx = Rows.Find(Id);
			return Click(RosterUI::Frame(bSel, true,
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 5, 0, 5)[ SNew(SBox).WidthOverride(120)[ Label(Name, 9, bSel ? Gold : Faint) ] ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(18)[ Label(bSel && bArrows ? TEXT("◀") : TEXT(""), 9, Gold) ] ]
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ Value ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 10, 0)[ SNew(SBox).WidthOverride(18)[ Label(bSel && bArrows ? TEXT("▶") : TEXT(""), 9, Gold) ] ]),
				[this, Idx, Rebuild] { if (Idx >= 0) { FormRow = Idx; Rebuild(); } });
		};
		auto Val = [](const FString& S, const FLinearColor& C = White90) { return Clip(S, 12, C); };
		TSharedRef<SVerticalBox> Left = SNew(SVerticalBox);
		Left->AddSlot().AutoHeight()[ Heading(TEXT("Profile")) ];
		const int32 Room = static_cast<int32>(tag::ROSTER_MAX) - static_cast<int32>(Tm.roster.size()) + (D.Editing && Tm.hasPlayer(D.Editing) ? 1 : 0);
		Left->AddSlot().AutoHeight().Padding(0, 0, 0, 2)[ RowW(RTeam, TEXT("TEAM"), SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Val(FString::Printf(TEXT("%s %s"), *Str(Tm.city), *Str(Tm.name)).ToUpper(), Pri) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[ Label(FString::Printf(TEXT("%d / %d"), static_cast<int32>(Tm.roster.size()), static_cast<int32>(tag::ROSTER_MAX)), 9, Room > 0 ? Faint : Warn) ], true) ];
		Left->AddSlot().AutoHeight().Padding(0, 0, 0, 2)[ RowW(RPos, TEXT("POSITION"), Val(Pos(D.Pos)), true) ];
		Left->AddSlot().AutoHeight().Padding(0, 0, 0, 2)[ RowW(RArch, TEXT("ARCHETYPE"), Val(UTF8_TO_TCHAR(Ar.name)), true) ];
		Left->AddSlot().AutoHeight().Padding(0, 0, 0, 2)[ RowW(RTalent, TEXT("TALENT"), Val(Talents[D.Talent].Name), true) ];
		TSharedRef<SWidget> FirstV = Val(D.First.ToUpper());
		TSharedRef<SWidget> LastV = Val(D.Last.ToUpper());
		if (bTyping && (Cur == RFirst || Cur == RLast))
		{
			TSharedRef<SEditableTextBox> Box = SAssignNew(TypeBox, SEditableTextBox).Font(Font(12, "Bold")).Text(Tx(Cur == RFirst ? D.First : D.Last))
				.SelectAllTextWhenFocused(true).ClearKeyboardFocusOnCommit(false)
				.OnTextChanged_Lambda([this](const FText&) { if (bTyping) ATagAudio::Ui(TEXT("UiType")); })
				.OnTextCommitted_Lambda([this, Rebuild, Cur](const FText& Tt, ETextCommit::Type How)
				{
					if (!bTyping) return;
					bTyping = false;
					const FString S = CapStore::Clean(Tt.ToString());
					ATagAudio::Ui(How == ETextCommit::OnEnter && !S.IsEmpty() ? TEXT("UiSelect") : TEXT("UiBack"));
					if (How == ETextCommit::OnEnter && !S.IsEmpty()) (Cur == RFirst ? Draft.First : Draft.Last) = S;
					// (the rebuild comes a frame later: not inside the text box's own commit)
					TWeakPtr<SGridironOverlay> W = StaticCastSharedRef<SGridironOverlay>(AsShared());
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([W](float) { if (TSharedPtr<SGridironOverlay> O = W.Pin()) O->ShowCreatePlayer(-1, nullptr); return false; }));
				});
			(Cur == RFirst ? FirstV : LastV) = Box;
		}
		Left->AddSlot().AutoHeight().Padding(0, 0, 0, 2)[ RowW(RFirst, TEXT("FIRST NAME"), FirstV, !bTyping) ];
		Left->AddSlot().AutoHeight().Padding(0, 0, 0, 2)[ RowW(RLast, TEXT("LAST NAME"), LastV, !bTyping) ];
		const bool bNumOk = tag::numberLegal(D.Pos, D.Number);
		Left->AddSlot().AutoHeight().Padding(0, 0, 0, 2)[ RowW(RNum, TEXT("NUMBER"), SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Val(FString::Printf(TEXT("#%d"), D.Number)) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[ Label(bNumOk ? TEXT("") : TEXT("unusual for the position"), 8, Faint, "Regular") ], true) ];
		Left->AddSlot().AutoHeight().Padding(0, 0, 0, 2)[ RowW(RAge, TEXT("AGE"), Val(FString::FromInt(D.Age)), true) ];
		Left->AddSlot().AutoHeight().Padding(0, 0, 0, 2)[ RowW(RHeight, TEXT("HEIGHT"), Val(Height(D.HeightIn)), true) ];
		Left->AddSlot().AutoHeight().Padding(0, 0, 0, 2)[ RowW(RWeight, TEXT("WEIGHT"), Val(FString::Printf(TEXT("%d LBS"), D.WeightLb)), true) ];
		Left->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[ Heading(TEXT("Look")) ];
		const int32 NF = FaceCount(), NH = HelmetCount(D.Pos);
		Left->AddSlot().AutoHeight().Padding(0, 0, 0, 2)[ RowW(RFace, TEXT("FACE"), Val(FString::Printf(TEXT("%d of %d  ·  skin tone %d"), (D.Face % NF) + 1, NF, FaceTone(D.Face))), true) ];
		Left->AddSlot().AutoHeight().Padding(0, 0, 0, 2)[ RowW(RHelmet, TEXT("HELMET"), Val(FString::Printf(TEXT("Style %d of %d"), (D.Helmet % NH) + 1, NH)), true) ];
		if (Rows.Contains(RRelease))
		{
			Left->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[ Heading(FString::Printf(TEXT("%s is full: release to make room"), *Str(Tm.abbr))) ];
			Left->AddSlot().AutoHeight().Padding(0, 0, 0, 2)[ RowW(RRelease, TEXT("RELEASE"),
				Val(Rel ? FString::Printf(TEXT("%s #%d %s  (%d)"), *Pos(Rel->pos), Rel->number, *Short(*Rel).ToUpper(), Rel->ovr) : FString(TEXT("-")), Warn), true) ];
		}
		Left->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[ Heading(TEXT("Finish")) ];
		auto Action = [&](int32 Id, const FString& S, const FLinearColor& C)
		{
			const bool bSel = Id == Cur;
			const int32 Idx = Rows.Find(Id);
			return Click(SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(bSel ? C : C * FLinearColor(1, 1, 1, 0.18f)).Padding(FMargin(12, 8)).HAlign(HAlign_Center)
				[ Label(S, 12, bSel ? Ink : C) ], [this, Idx, Rebuild] { if (Idx >= 0) { FormRow = Idx; Rebuild(); } });
		};
		TSharedRef<SHorizontalBox> Acts = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)[ Action(RCreate, D.Editing ? TEXT("SAVE CHANGES") : TEXT("CREATE PLAYER"), Good) ];
		if (D.Editing) Acts->AddSlot().FillWidth(1).Padding(0, 0, 4, 0)[ Action(RDelete, TEXT("DELETE"), Warn) ];
		Acts->AddSlot().FillWidth(1)[ Action(RCancel, TEXT("CANCEL"), Dim) ];
		Left->AddSlot().AutoHeight()[ Acts ];

		// ---- the middle: every rating (the position's key ones first)
		TSharedRef<SVerticalBox> Mid = SNew(SVerticalBox);
		TArray<int32> RRows;
		for (int32 Id : Rows)
			if (Id >= RRating) RRows.Add(Id);
		const int32 CurR = RRows.Find(Cur);
		const int32 Window = 26;
		const int32 RFirst_ = FMath::Clamp((CurR >= 0 ? CurR : 0) - Window / 2, 0, FMath::Max(0, RRows.Num() - Window));
		FString LastGroup;
		for (int32 k = RFirst_; k < RRows.Num() && k < RFirst_ + Window; ++k)
		{
			const tag::RatingDef& Def = Defs[static_cast<size_t>(RRows[k] - RRating)];
			const bool bKey = k < static_cast<int32>(KeyR.size());
			const FString Group = bKey ? FString::Printf(TEXT("Key ratings  ·  %s"), *Pos(D.Pos)) : FString(UTF8_TO_TCHAR(Def.group));
			if (Group != LastGroup)
			{
				LastGroup = Group;
				Mid->AddSlot().AutoHeight().Padding(0, k == RFirst_ ? 0 : 6, 0, 2)[ Heading(Group) ];
			}
			const bool bSel = RRows[k] == Cur;
			const double V = D.R.*(Def.m);
			const int32 Idx = Rows.Find(RRows[k]);
			Mid->AddSlot().AutoHeight().Padding(0, 0, 0, 1)
			[
				Click(RosterUI::Frame(bSel, true,
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 3, 0, 3)[ SNew(SBox).WidthOverride(40)[ Label(ANSI_TO_TCHAR(Def.abbr), 9, bKey ? Gold : Faint) ] ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(170)[ Label(UTF8_TO_TCHAR(Def.name), 10, bSel ? White90 : Dim, "Regular") ] ]
					+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ Bar(V, 220.f, 7.f) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 10, 0)[ SNew(SBox).WidthOverride(30).HAlign(HAlign_Right)[ Label(FString::FromInt(FMath::RoundToInt(V)), 12, OvrColor(FMath::RoundToInt(V))) ] ]),
					[this, Idx, Rebuild] { if (Idx >= 0) { FormRow = Idx; Rebuild(); } })
			];
		}
		if (RRows.Num() > Window)
			Mid->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[ Label(FString::Printf(TEXT("%d - %d of %d ratings"), RFirst_ + 1, FMath::Min(RFirst_ + Window, RRows.Num()), RRows.Num()), 9, Faint) ];

		// ---- the right: his card, live
		int32 Better = 0, Of = 1;
		const tag::StarGrade Gr = Project(Dir, D.Pos, Ovr, D.Editing, Better, Of);
		// where he would be on the depth chart: the men at his spot rated above him
		const tag::DSlot BS = BaseSlot(D.Pos);
		int32 Above = 0;
		const tag::Player* Ahead = nullptr;
		for (tag::Player* P : tag::chartOrder(Tm, BS))
			if (P != D.Editing && P->pos == D.Pos && P->ovr > Ovr && (!Rel || P != Rel))
			{
				if (!Ahead || P->ovr < Ahead->ovr) Ahead = P;
				++Above;
			}
		const int32 Starters = tag::dslotInfo(BS).starters;
		const FString Fit = Above < Starters
			? FString::Printf(TEXT("Starts at %s%s for %s"), ANSI_TO_TCHAR(tag::dslotInfo(BS).abbr), Starters > 1 ? *FString::Printf(TEXT(" (%d)"), Above + 1) : TEXT(""), *Str(Tm.abbr))
			: FString::Printf(TEXT("%s%d on the %s chart, behind #%d %s (%d)"), ANSI_TO_TCHAR(tag::dslotInfo(BS).abbr), Above + 1, *Str(Tm.abbr), Ahead ? Ahead->number : 0,
				Ahead ? *Short(*Ahead) : TEXT(""), Ahead ? Ahead->ovr : 0);
		const FLinearColor Sec = HexCol(Tm.colors.secondary);
		const FLinearColor JerseyBg = HexCol(Tm.colors.primary);
		const FLinearColor JerseyInk = (JerseyBg.GetLuminance() - Sec.GetLuminance()) * (JerseyBg.GetLuminance() - Sec.GetLuminance()) < 0.01f
			? (JerseyBg.GetLuminance() > 0.4f ? Ink : FLinearColor::White) : Sec;
		TSharedRef<SWidget> Jersey = SNew(SBox).WidthOverride(200).HeightOverride(214)
			[
				SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Sec).Padding(4)
				[
					SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(JerseyBg).Padding(FMargin(6, 10))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ Clip(D.Last.ToUpper(), 13, JerseyInk) ]
						+ SVerticalBox::Slot().FillHeight(1).HAlign(HAlign_Center).VAlign(VAlign_Center)[ Label(FString::FromInt(D.Number), 84, JerseyInk) ]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[ Logo(Tm) ? StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(34).HeightOverride(34)[ SNew(SImage).Image(Logo(Tm)) ]) : Label(Str(Tm.abbr), 10, JerseyInk) ]
					]
				]
			];
		// the man himself, turning (the flat jersey when there is no capture)
		const FSlateBrush* Shot = CapPreview::Update(Dir, D, Tm, Cur != RFace);
		TSharedRef<SWidget> Look = Shot
			? StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(280).HeightOverride(350)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[ SNew(SImage).Image(Shot) ]
					+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Left).Padding(8)
					[ SNew(SBox).WidthOverride(36).HeightOverride(36)[ Logo(Tm) ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Logo(Tm))) : SNullWidget::NullWidget ] ]
				])
			: Jersey;
		TSharedRef<SVerticalBox> KeyBars = SNew(SVerticalBox);
		for (const tag::RatingDef* K : KeyR)
		{
			const double V = D.R.*(K->m);
			KeyBars->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(46)[ Label(ANSI_TO_TCHAR(K->abbr), 9, Gold) ] ]
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ Bar(V, 300.f, 7.f) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[ SNew(SBox).WidthOverride(26).HAlign(HAlign_Right)[ Label(FString::FromInt(FMath::RoundToInt(V)), 11, OvrColor(FMath::RoundToInt(V))) ] ]
			];
		}
		const FString GradeS = Gr == tag::StarGrade::None ? FString(TEXT("NO GRADE")) : FString(UTF8_TO_TCHAR(tag::starGradeName(Gr))).ToUpper();
		TSharedRef<SWidget> CardW = SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(CardHi).Padding(0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ Fill(Pri, 3.f) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16, 12, 16, 8))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[ Look ]
					+ SHorizontalBox::Slot().FillWidth(1).Padding(16, 0, 0, 0)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ Label(FString::Printf(TEXT("#%d  ·  %s  ·  %s"), D.Number, *Pos(D.Pos), *Str(Tm.abbr)), 10, Dim) ]
						+ SVerticalBox::Slot().AutoHeight()[ Clip(D.First, 13, Dim, "Regular") ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, -3, 0, 0)[ Clip(D.Last.ToUpper(), 26, White90) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)[ Label(FString::Printf(TEXT("%s  ·  %d LBS  ·  AGE %d"), *Height(D.HeightIn), D.WeightLb, D.Age), 9, Faint) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ Label(FString::FromInt(Ovr), 46, OvrColor(Ovr)) ]
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, -8, 0, 0)[ Label(TEXT("OVR"), 9, Faint) ]
							]
							+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(16, 0, 0, 0)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()[ Label(UTF8_TO_TCHAR(Ar.name), 13, Gold) ]
								+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(9)).ColorAndOpacity(Dim).AutoWrapText(true).Text(Tx(UTF8_TO_TCHAR(Ar.blurb))) ]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth()[ Chip(GradeS, Gr == tag::StarGrade::None ? FLinearColor(1, 1, 1, 0.08f) : GradeColor(Gr), Gr == tag::StarGrade::None ? Dim : Ink, 8) ]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
									[ Label(FString::Printf(TEXT("%d of %d %ss rated higher"), Better, Of, *Pos(D.Pos)), 8, Faint, "Regular") ]
								]
							]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16, 4, 16, 6))[ Label(Fit, 10, Above < Starters ? Good : Dim) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16, 2, 16, 4))[ Heading(TEXT("Key ratings  ·  what his OVR is made of")) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16, 0, 16, 14))[ KeyBars ]
			];

		TSharedRef<SWidget> Hints = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::DUpDown, TEXT("↑↓"), TEXT("Row")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::DLeftRight, TEXT("←→"), TEXT("Change")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::L1, TEXT("Q"), TEXT("")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::R1, TEXT("E"), TEXT("By 5")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::L2, TEXT("Z"), TEXT("")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::R2, TEXT("C"), TEXT("Section")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Cross, TEXT("Enter"), Cur == RFirst || Cur == RLast ? TEXT("Type the name") : TEXT("Select")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Square, TEXT("R"), TEXT("Roll a profile")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Triangle, TEXT("T"), TEXT("Archetype's ratings")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::None, bPad ? TEXT("OPTIONS") : TEXT("F"), D.Editing ? TEXT("Save") : TEXT("Create")) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Circle, TEXT("Esc"), TEXT("Cancel")) ];

		PageP = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[ Label(D.Editing ? TEXT("EDIT PLAYER") : TEXT("CREATE A PLAYER"), 30, FLinearColor::White) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(14, 0, 0, 5)[ Label(FString::Printf(TEXT("%s  ·  %s"), *Pos(D.Pos), UTF8_TO_TCHAR(Ar.name)).ToUpper(), 13, Pri) ]
				+ SHorizontalBox::Slot().FillWidth(1)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(0, 0, 0, 5)[ Label(FString::Printf(TEXT("OVR %d"), Ovr), 14, OvrColor(Ovr)) ]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(540)[ SNew(TagTheme::SPanel).Fill(Card).Radius(6.f).Padding(FMargin(12, 10))[ Left ] ] ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(12, 0, 0, 0)[ SNew(SBox).WidthOverride(600)[ SNew(TagTheme::SPanel).Fill(Card).Radius(6.f).Padding(FMargin(12, 10))[ Mid ] ] ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(12, 0, 0, 0)[ SNew(SBox).WidthOverride(700)[ SNew(TagTheme::SPanel).Fill(Card).Radius(6.f).Padding(FMargin(12, 10))[ CardW ] ] ]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)
			[ SNew(SBox).HeightOverride(18)[ Label(Msg.IsEmpty() ? FString(TEXT("The archetype and talent set his ratings; change any of them after. His OVR is his position's formula over them.")) : Msg, 11, Msg.IsEmpty() ? Faint : MsgColor) ] ]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)[ TagTheme::HintBar(Hints) ];
		Wash = Pri;

		OnKey = [this, Dir, Rebuild](const FKey& K) -> bool
		{
			if (bTyping) return false;  // (the text box has the keys)
			const FNav N(K);
			FDraft& Dr = Draft;
			tag::Team& T = *Dir->League[Dr.TeamIdx];
			const TArray<int32> Rs = FormRows(Dir);
			FormRow = FMath::Clamp(FormRow, 0, Rs.Num() - 1);
			const int32 Id = Rs[FormRow];
			// the cursor clicks; a value stepped ticks, up or down
			if (N.bUp || N.bDown || N.bPrevT || N.bNextT) ATagAudio::Ui(TEXT("UiMove"));
			else if (N.Moves()) ATagAudio::Ui(TEXT("UiValue"), 1.f, (N.bRight || N.bNext) ? 1.06f : 0.94f);
			auto Save = [&]() -> bool
			{
				if (Dr.First.IsEmpty() || Dr.Last.IsEmpty()) { ATagAudio::Ui(TEXT("UiError")); Say(TEXT("He needs a first and a last name"), Warn); return false; }
				tag::Team& To = *Dir->League[Dr.TeamIdx];
				tag::Player* Rel = nullptr;
				if (NeedsRelease(Dir, Dr))
				{
					for (auto& P : To.roster)
						if (P->id == Dr.ReleaseId) Rel = P.get();
					if (!Rel) { ATagAudio::Ui(TEXT("UiError")); Say(TEXT("The team is full: pick a man to release"), Warn); return false; }
				}
				tag::Player* P = Dr.Editing;
				tag::Team* From = P ? CapStore::TeamOf(Dir, P) : nullptr;
				if (Rel)
				{
					CapStore::Remove(To, Rel, true);
					SaveCuts(Dir);
				}
				std::unique_ptr<tag::Player> NewP;
				if (!P)
				{
					NewP = std::make_unique<tag::Player>();
					NewP->id = CapStore::NextId++;
					P = NewP.get();
				}
				const bool bPosChanged = P->pos != Dr.Pos;
				if (bPosChanged && From) tag::chartRemovePlayer(*From, *P);
				P->first = Std(Dr.First);
				P->last = Std(Dr.Last);
				P->pos = Dr.Pos;
				P->age = Dr.Age;
				P->heightIn = Dr.HeightIn;
				P->weightLb = Dr.WeightLb;
				P->r = Dr.R;
				P->ovr = tag::computeOvr(Dr.Pos, Dr.R);
				P->archetype = Dr.Arch;
				P->face = Dr.Face % FaceCount();
				P->helmet = Dr.Helmet % HelmetCount(Dr.Pos);
				P->created = true;
				if (NewP)
				{
					NewP->number = tag::numberFree(To, Dr.Number) ? Dr.Number : tag::freeNumber(To, Dr.Pos, Dr.Number);
					CapStore::Sign(To, std::move(NewP));
				}
				else if (From && From != &To)
				{
					P->number = Dr.Number;
					CapStore::Move(*From, To, P);
				}
				else
				{
					P->number = tag::numberFree(To, Dr.Number, P) ? Dr.Number : tag::freeNumber(To, Dr.Pos, Dr.Number);
					if (bPosChanged) tag::chartAddPlayer(To, *P);
				}
				SaveCreated(Dir);
				Regrade(Dir);
				ATagAudio::Ui(TEXT("UiSave"));
				const tag::DSlot BS = BaseSlot(P->pos);
				int32 Depth = 1;
				for (tag::Player* Q : tag::chartOrder(To, BS))
				{
					if (Q == P) break;
					++Depth;
				}
				Say(FString::Printf(TEXT("%s %s #%d (%d OVR) %s %s: %s%d on the depth chart%s"), *Pos(P->pos), *Full(*P), P->number, P->ovr,
					Dr.Editing ? TEXT("saved with") : TEXT("signed with"), *Str(To.abbr), ANSI_TO_TCHAR(tag::dslotInfo(BS).abbr), Depth,
					Rel ? *FString::Printf(TEXT("  ·  %s released"), *Short(*Rel)) : TEXT("")), Good);
				UE_LOG(LogTemp, Display, TEXT("TAG create-a-player: %s"), *CapStore::PlayerLine(To, *P));
				Mode = EMode::Hub;
				TArray<FHubRow> Hr = HubRows(Dir);
				for (int32 i = 0; i < Hr.Num(); ++i)
					if (Hr[i].P == P) HubRow = i;
				return true;
			};
			if (N.bUp || N.bDown) { FormRow = FMath::Clamp(FormRow + (N.bDown ? 1 : -1), 0, Rs.Num() - 1); Rebuild(); return true; }
			if (N.bPrevT || N.bNextT)
			{
				// the sections: profile, look, ratings, finish
				auto Sec = [](int32 R) { return R >= RRating ? 2 : R == RCreate || R == RDelete || R == RCancel ? 3 : R >= RFace ? 1 : 0; };
				const int32 Want = FMath::Clamp(Sec(Id) + (N.bNextT ? 1 : -1), 0, 3);
				for (int32 i = 0; i < Rs.Num(); ++i)
					if (Sec(Rs[i]) == Want) { FormRow = i; break; }
				Rebuild();
				return true;
			}
			if (N.bLeft || N.bRight || N.bPrev || N.bNext)
			{
				const int32 Dir1 = (N.bRight || N.bNext) ? 1 : -1;
				const bool bBig = N.bPrev || N.bNext;
				const int32 Step = Dir1 * (bBig ? 5 : 1);
				const int32 NT = static_cast<int32>(Dir->League.size());
				switch (Id)
				{
				case RTeam:
					Dr.TeamIdx = (Dr.TeamIdx + Dir1 + NT) % NT;
					Dr.ReleaseId = -1;
					if (!tag::numberFree(*Dir->League[Dr.TeamIdx], Dr.Number, Dr.Editing)) Dr.Number = tag::freeNumber(*Dir->League[Dr.TeamIdx], Dr.Pos, Dr.Number);
					break;
				case RPos:
				{
					const int32 NP = static_cast<int32>(tag::Pos::P) + 1;
					Dr.Pos = static_cast<tag::Pos>((static_cast<int32>(Dr.Pos) + Dir1 + NP) % NP);
					Dr.Arch = 0;
					Rate(Dr);
					Frame(Dr);
					Dr.ReleaseId = -1;
					if (!tag::numberLegal(Dr.Pos, Dr.Number) || !tag::numberFree(T, Dr.Number, Dr.Editing)) Dr.Number = tag::freeNumber(T, Dr.Pos, -1);
					break;
				}
				case RArch:
				{
					const int32 NA = static_cast<int32>(tag::capArchetypes(Dr.Pos).size());
					Dr.Arch = (Dr.Arch + Dir1 + NA) % NA;
					Rate(Dr);
					Frame(Dr);
					break;
				}
				case RTalent:
					Dr.Talent = FMath::Clamp(Dr.Talent + Dir1, 0, NTalents - 1);
					Rate(Dr);
					break;
				case RFirst:
				case RLast:
				{
					const std::vector<std::string>& Pool = Id == RFirst ? tag::firstNamePool() : tag::lastNamePool();
					FString& Name = Id == RFirst ? Dr.First : Dr.Last;
					// the pool in alphabetical order, stepping from the name (or where it would be)
					std::vector<std::string> Sorted = Pool;
					std::sort(Sorted.begin(), Sorted.end());
					int32 I = NameIndex(Sorted, Name);
					if (I < 0)
					{
						I = 0;
						while (I < static_cast<int32>(Sorted.size()) && Str(Sorted[static_cast<size_t>(I)]) < Name) ++I;
						if (Dir1 < 0) --I;
						else --I;
					}
					Name = PickName(Sorted, I + Step);
					break;
				}
				case RNum:
				{
					int32 Nn = Dr.Number;
					for (int32 Tries = 0; Tries < 100; ++Tries)
					{
						Nn = (Nn + (bBig ? Dir1 * 10 : Dir1) + 100) % 100;
						if (tag::numberFree(T, Nn, Dr.Editing)) break;
					}
					Dr.Number = Nn;
					break;
				}
				case RAge: Dr.Age = FMath::Clamp(Dr.Age + Step, 20, 40); break;
				case RHeight: Dr.HeightIn = FMath::Clamp(Dr.HeightIn + Dir1, 64, 82); break;
				case RWeight: Dr.WeightLb = FMath::Clamp(Dr.WeightLb + Dir1 * (bBig ? 10 : 2), 160, 380); break;
				case RFace: Dr.Face = (Dr.Face + Step + FaceCount() * 10) % FaceCount(); break;
				case RHelmet: Dr.Helmet = (Dr.Helmet + Dir1 + HelmetCount(Dr.Pos) * 10) % HelmetCount(Dr.Pos); break;
				case RRelease:
				{
					const std::vector<tag::Player*> C = ReleaseCands(T, Dr.Pos);
					int32 I = 0;
					for (int32 i = 0; i < static_cast<int32>(C.size()); ++i)
						if (C[static_cast<size_t>(i)]->id == Dr.ReleaseId) I = i;
					if (!C.empty()) Dr.ReleaseId = C[static_cast<size_t>((I + Dir1 + static_cast<int32>(C.size())) % static_cast<int32>(C.size()))]->id;
					break;
				}
				default:
					if (Id >= RRating)
					{
						const tag::RatingDef& Def = tag::ratingDefs()[static_cast<size_t>(Id - RRating)];
						Dr.R.*(Def.m) = FMath::Clamp(FMath::RoundToDouble(Dr.R.*(Def.m)) + Step, 25.0, 99.0);
					}
					break;
				}
				Rebuild();
				return true;
			}
			if (N.bOk)
			{
				if (Id == RFirst || Id == RLast)
				{
					// the keyboard: type it (the text box takes the keys until Enter / Esc)
					ATagAudio::Ui(TEXT("UiTab"));
					bTyping = true;
					Rebuild();
					PendingFocus = 1;  // (the text box: the focusable after the screen's root)
					return true;
				}
				if (Id == RCreate || (Id >= RRating && false)) { if (Save()) Rebuild(); else Rebuild(); return true; }
				if (Id == RDelete && Dr.Editing)
				{
					if (tag::Team* From = CapStore::TeamOf(Dir, Dr.Editing))
					{
						const FString Who = Full(*Dr.Editing);
						Retire(*From, Dr.Editing);
						SaveCreated(Dir);
						Regrade(Dir);
						Say(FString::Printf(TEXT("%s deleted"), *Who), Dim);
					}
					ATagAudio::Ui(TEXT("UiDelete"));
					Mode = EMode::Hub;
					Rebuild();
					return true;
				}
				if (Id == RCancel) { ATagAudio::Ui(TEXT("UiBack")); Mode = EMode::Hub; Msg.Reset(); Rebuild(); return true; }
				FormRow = FMath::Clamp(FormRow + 1, 0, Rs.Num() - 1);
				Rebuild();
				return true;
			}
			if (N.bStart) { Save(); Rebuild(); return true; }
			if (N.bSquare)
			{
				ATagAudio::Ui(TEXT("UiRandom"));
				Roll(Dr, T);
				Say(TEXT("A new profile: name, number, face, age"), Gold);
				Rebuild();
				return true;
			}
			if (N.bTriangle)
			{
				ATagAudio::Ui(TEXT("UiTab"));
				Rate(Dr);
				Say(FString::Printf(TEXT("The %s ratings at %s"), UTF8_TO_TCHAR(tag::capArchetypes(Dr.Pos)[static_cast<size_t>(Dr.Arch)].name), Talents[Dr.Talent].Name), Gold);
				Rebuild();
				return true;
			}
			if (N.bBack)
			{
				ATagAudio::Ui(TEXT("UiBack"));
				Mode = EMode::Hub;
				Msg.Reset();
				Rebuild();
				return true;
			}
			return false;
		};
	}

	TSharedRef<SWidget> Root = SNew(SKeyRoot).OnKey(OnKey)
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0.01f, 0.012f, 0.02f, 0.92f)) ]
		+ SOverlay::Slot().VAlign(VAlign_Top)[ SNew(SBox).HeightOverride(6)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Wash) ] ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[ SNew(SBox).Padding(FMargin(16, 12))[ PageP.ToSharedRef() ] ]
	];
	CallRoot = Root;  // (the screen draws its own selection)
	ShowModal(Root, true);
	if (bTyping) PendingFocus = 1;
}

// ------------------------------------------------------------------ -TagRosterShots: the scripted check
// From the main menu: the depth chart (pick a man up and carry him, a corner onto FS, the groups, best at the slot,
// a team), then Create-a-Player (a new WR on the team: position, archetype, talent, a rating; create), the depth chart
// with him on it; a picture with the UI each step, nothing saved, then the game exits.
void SGridironOverlay::StartRosterShots()
{
	TWeakPtr<SGridironOverlay> Weak = StaticCastSharedRef<SGridironOverlay>(AsShared());
	// Screen: 0 keys only, 1 open the depth chart, 2 open create-a-player, 3 type Text (characters, then Enter) into the
	// focused text box
	struct FStep { int32 Screen; TArray<FKey> Keys; const TCHAR* Name; const TCHAR* Text = nullptr; };
	static TArray<FStep> Steps = {
		{1, {}, TEXT("depth_qb")},
		{0, {EKeys::Right}, TEXT("depth_chart_col")},
		{0, {EKeys::Enter, EKeys::Down}, TEXT("depth_carry")},
		{0, {EKeys::Enter}, TEXT("depth_dropped")},
		{0, {EKeys::Left, EKeys::E, EKeys::Down, EKeys::Down, EKeys::Down, EKeys::Down, EKeys::Down, EKeys::Down, EKeys::Down}, TEXT("depth_fs")},
		{0, {EKeys::Right, EKeys::R}, TEXT("depth_fs_all")},
		{0, {EKeys::Enter, EKeys::Up, EKeys::Up, EKeys::Up}, TEXT("depth_fs_corner")},
		{0, {EKeys::Enter, EKeys::Left}, TEXT("depth_fs_set")},
		{0, {EKeys::E}, TEXT("depth_special")},
		{0, {EKeys::E, EKeys::Right, EKeys::T}, TEXT("depth_situational_best")},
		{0, {EKeys::Left, EKeys::Down, EKeys::Down}, TEXT("depth_situational_swr")},
		{0, {EKeys::C}, TEXT("depth_next_team")},
		{2, {}, TEXT("cap_hub")},
		{0, {EKeys::Enter}, TEXT("cap_form")},
		{0, {EKeys::Down, EKeys::Right, EKeys::Right, EKeys::Right}, TEXT("cap_wr")},
		{0, {EKeys::Down, EKeys::Right, EKeys::Down, EKeys::Right, EKeys::Right}, TEXT("cap_elite_physical")},
		{0, {}, TEXT("cap_turntable")},
		{0, {EKeys::C, EKeys::C, EKeys::Right, EKeys::Right, EKeys::E, EKeys::Down, EKeys::E}, TEXT("cap_ratings")},
		{0, {EKeys::Z}, TEXT("cap_face_row")},
		{0, {}, TEXT("cap_face")},
		{0, {EKeys::Z, EKeys::Down, EKeys::Down, EKeys::Down, EKeys::Down, EKeys::Enter}, TEXT("cap_typing")},
		{3, {}, TEXT("cap_typed"), TEXT("Freddie")},
		{0, {EKeys::F}, TEXT("cap_created")},
		{0, {EKeys::R}, TEXT("cap_depth_chart")},
		{0, {EKeys::Escape, EKeys::Escape}, TEXT("cap_back_hub")},
	};
	static int32 I = -2;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float) -> bool
	{
		const TSharedPtr<SGridironOverlay> O = Weak.Pin();
		if (!O.IsValid()) return false;
		if (++I < 0) return true;
		if (I >= Steps.Num())
		{
			UE_LOG(LogTemp, Display, TEXT("TAG roster shots done"));
			FPlatformMisc::RequestExit(false);
			return false;
		}
		const FStep& S = Steps[I];
		int32 ShotTeam = 0; // (-TagHome=<league index>: another team's chart and kit)
		FParse::Value(FCommandLine::Get(), TEXT("TagHome="), ShotTeam);
		if (S.Screen == 1) O->ShowDepthChart(ShotTeam, [W = Weak] { if (auto P = W.Pin()) P->ShowMainMenu(); });
		if (S.Screen == 2) O->ShowCreatePlayer(ShotTeam, [W = Weak] { if (auto P = W.Pin()) P->ShowMainMenu(); });
		if (S.Screen == 3 && S.Text && FSlateApplication::IsInitialized())
		{
			// as the keyboard would: the characters to the focused widget, then Enter
			FSlateApplication& App = FSlateApplication::Get();
			for (const TCHAR* C = S.Text; *C; ++C) App.ProcessKeyCharEvent(FCharacterEvent(*C, FModifierKeysState(), 0, false));
			App.ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 13));
			App.ProcessKeyUpEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 13));
		}
		// (the keys go to the screen as it is now: each rebuild makes a new root)
		for (const FKey& K : S.Keys)
			if (const TSharedPtr<SWidget> R = O->CallRoot) StaticCastSharedPtr<SKeyRoot>(R)->Press(K);
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_roster_%02d_%s.png"), I, S.Name), true, false);
		UE_LOG(LogTemp, Display, TEXT("TAG roster shot %d %s"), I, S.Name);
		return true;
	}), 1.5f);
}
