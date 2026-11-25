// The game's side of the recently-used store (Game/TagRecent.h): bound to GameUserSettings.ini [TAG.Recent] when the
// module loads, and -TagRecentTest, its self-check.
#include "Game/TagRecent.h"

#include "CoreMinimal.h"
#include "Game/TagResult.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"

namespace
{
	const TCHAR* const kRecentSection = TEXT("TAG.Recent");

	/** A category as a config key: no '=', ';', brackets or spaces. */
	FString RecentKey(const std::string& Category)
	{
		FString K = UTF8_TO_TCHAR(Category.c_str());
		for (TCHAR& C : K)
			if (C == TEXT('=') || C == TEXT(';') || C == TEXT('[') || C == TEXT(']') || FChar::IsWhitespace(C)) C = TEXT('_');
		return K.IsEmpty() ? FString(TEXT("_")) : K;
	}

	bool RecentReset() { static const bool b = FParse::Param(FCommandLine::Get(), TEXT("TagRecentReset")); return b; }
	bool RecentNoSave() { static const bool b = FParse::Param(FCommandLine::Get(), TEXT("TagRecentNoSave")); return b; }

	/** Binds the store to the saved config when the game's module is loaded (GConfig is read when a category is first
	 *  used, never here). */
	struct FTagRecentBinder
	{
		FTagRecentBinder()
		{
			tag::Recent& R = tag::Recent::get();
			R.load = [](const std::string& Category, std::string& Out)
			{
				if (!GConfig || RecentReset()) return false;
				FString V;
				if (!GConfig->GetString(kRecentSection, *RecentKey(Category), V, GGameUserSettingsIni)) return false;
				Out = TCHAR_TO_UTF8(*V);
				return true;
			};
			R.store = [](const std::string& Category, const std::string& Value)
			{
				if (!GConfig || RecentNoSave()) return;
				if (Value.empty()) GConfig->RemoveKey(kRecentSection, *RecentKey(Category), GGameUserSettingsIni); // (forgotten: no empty key left behind)
				else GConfig->SetString(kRecentSection, *RecentKey(Category), UTF8_TO_TCHAR(Value.c_str()), GGameUserSettingsIni);
			};
			R.commit = []()
			{
				if (GConfig && !RecentNoSave()) GConfig->Flush(false, GGameUserSettingsIni);
			};
		}
	};
	FTagRecentBinder GTagRecentBinder;
}

namespace TagRecentTest
{
	/** -TagRecentTest: the store's rules on throwaway categories (nothing of the player's history is read or kept: the
	 *  test's categories are forgotten at the end), its saved form read back, and the order the salt gives. */
	void Run()
	{
		int32 Pass = 0, Fail = 0;
		auto Check = [&Pass, &Fail](bool bOk, const FString& What)
		{
			(bOk ? Pass : Fail)++;
			UE_LOG(LogTemp, Display, TEXT("TAG recenttest %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
		};
		tag::Recent& R = tag::Recent::get();
		const std::string Cat = "test.recent.a", Cat2 = "test.recent.b";
		R.forget(Cat);
		R.forget(Cat2);
		const std::vector<std::string> Opts = {"one", "two", "three", "four"};
		// never used: the salt orders them, the same salt the same way, and some other salt another way
		const int P1 = R.leastRecent(Cat, Opts, 7), P1b = R.leastRecent(Cat, Opts, 7);
		Check(P1 >= 0 && P1 == P1b, FString::Printf(TEXT("with no history the same salt picks the same (%d, %d)"), P1, P1b));
		bool bOther = false;
		for (uint32 S = 0; S < 16 && !bOther; ++S) bOther = R.leastRecent(Cat, Opts, S) != P1;
		Check(bOther, TEXT("with no history another salt can pick another"));
		// used in turn: the least recent comes round, never a repeat until all have had a turn
		TArray<FString> Seq;
		TSet<FString> Seen;
		bool bRepeat = false;
		for (int32 k = 0; k < 4; ++k)
		{
			const std::string V = R.take(Cat, Opts, 7);
			const FString F = UTF8_TO_TCHAR(V.c_str());
			bRepeat = bRepeat || Seen.Contains(F);
			Seen.Add(F);
			Seq.Add(F);
		}
		Check(!bRepeat && Seen.Num() == 4, FString::Printf(TEXT("four takes give the four variants (%s)"), *FString::Join(Seq, TEXT(" "))));
		const std::string Fifth = R.take(Cat, Opts, 7);
		Check(UTF8_TO_TCHAR(Fifth.c_str()) == Seq[0], FString::Printf(TEXT("the fifth take is the first again (%s)"), UTF8_TO_TCHAR(Fifth.c_str())));
		Check(R.age(Cat, Fifth) == 0 && R.age(Cat, TCHAR_TO_UTF8(*Seq[1])) == 3 && R.age(Cat, "never") == -1,
			FString::Printf(TEXT("ages: last 0 (%d), oldest 3 (%d), unknown -1 (%d)"), R.age(Cat, Fifth), R.age(Cat, TCHAR_TO_UTF8(*Seq[1])), R.age(Cat, "never")));
		// a variant outside the candidates does not stop the least recent of them being picked
		R.use(Cat, "elsewhere");
		Check(R.leastRecent(Cat, Opts, 7) == static_cast<int>(std::find(Opts.begin(), Opts.end(), std::string(TCHAR_TO_UTF8(*Seq[1]))) - Opts.begin()),
			TEXT("an unrelated use leaves the order of the candidates as it was"));
		// '|' in a variant is kept out of the saved form
		R.use(Cat2, "a|b");
		Check(R.age(Cat2, "a/b") == 0, TEXT("a '|' in a variant is stored as '/'"));
		// the cap: kKeep distinct variants, the oldest dropped
		for (int32 k = 0; k < tag::Recent::kKeep + 5; ++k) R.use(Cat2, "v" + std::to_string(k));
		Check(static_cast<int32>(R.history(Cat2).size()) == tag::Recent::kKeep && R.age(Cat2, "v0") == -1 && R.age(Cat2, "v4") == -1 &&
			R.age(Cat2, "v5") == tag::Recent::kKeep - 1, FString::Printf(TEXT("kept to %d per category, the oldest dropped"), tag::Recent::kKeep));
		// the saved form read back: a store loaded from what was written gives the same order
		{
			std::map<std::string, std::string> Disk;
			tag::Recent Mem;
			Mem.store = [&Disk](const std::string& C, const std::string& V) { Disk[C] = V; };
			for (const char* V : {"x", "y", "z", "y"}) Mem.use("k", V);
			tag::Recent Back;
			Back.load = [&Disk](const std::string& C, std::string& Out) { auto It = Disk.find(C); if (It == Disk.end()) return false; Out = It->second; return true; };
			const std::vector<std::string> H = Back.history("k");
			Check(H.size() == 3 && H[0] == "x" && H[1] == "z" && H[2] == "y" && Disk["k"] == "x|z|y",
				FString::Printf(TEXT("saved as \"%s\" and read back in the same order"), UTF8_TO_TCHAR(Disk["k"].c_str())));
		}
		// the game's own binding: GameUserSettings.ini [TAG.Recent] (only when this run may write)
		if (GConfig && !FParse::Param(FCommandLine::Get(), TEXT("TagRecentNoSave")))
		{
			R.use(Cat, "ini-check");
			FString V;
			GConfig->GetString(kRecentSection, *RecentKey(Cat), V, GGameUserSettingsIni);
			Check(V.EndsWith(TEXT("|ini-check")), FString::Printf(TEXT("written to [TAG.Recent] %s = %s"), *RecentKey(Cat), *V));
		}
		R.forget(Cat);
		R.forget(Cat2);
		R.flush();
		TagResult::Finish(TEXT("TagRecentTest"), Pass, Fail);
	}
}
