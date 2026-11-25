// What was used lately, remembered from game to game: which variant of something (a camera angle, a shot, a line of
// commentary) each category used, most recent last, so the next pick can be the one used least recently - game ten does
// not open the way game nine did. Plain C++ (like the Sim and the booth core), header only: the booth includes it as it
// is and the headless BoothScript tool links nothing new. The game binds it to the saved config (Game/TagRecent.cpp:
// GameUserSettings.ini, section [TAG.Recent], one key per category, the variants joined by '|'); BoothScript keeps it
// in memory.
//
// Usage:
//
//   tag::Recent& R = tag::Recent::get();
//   // the line least recently said of these (never said first; ties broken by the salt, e.g. the game's seed, so two
//   // games with no history yet still differ, and one game with the same history always picks the same)
//   const std::vector<std::string> Opts = {"td_a", "td_b", "td_c"};
//   const int i = R.leastRecent("booth.td", Opts, salt);
//   R.use("booth.td", Opts[i]);          // (or: const std::string Pick = R.take("booth.td", Opts, salt);)
//   R.age("booth.td", "td_b");           // other variants used since td_b was (0: it was the last; -1: never)
//   R.flush();                           // write it out now (the game also does at the end of a match and on exit)
//
// Categories are free strings ("shot.capper", "replay.first.pass", "booth.td"): keep them stable, they are the saved
// keys. A variant id is any string without '|' (one is replaced by '/'). Each category keeps its last kKeep distinct
// variants. Thread safe (one lock). Nothing here is random: it never touches the sim's numbers.
// -TagRecentReset: the game starts with no history (nothing is read); -TagRecentNoSave: nothing is written (tests).
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace tag {

class Recent {
 public:
  /** The one store (game and booth share it). */
  static Recent& get() {
    static Recent R;
    return R;
  }

  /** Distinct variants kept per category. */
  static constexpr int kKeep = 64;

  /** variant of category was used now: it becomes the most recent. */
  void use(const std::string& category, const std::string& variant) {
    std::lock_guard<std::mutex> L(m_);
    Cat& C = cat(category);
    const std::string V = clean(variant);
    auto It = std::find(C.order.begin(), C.order.end(), V);
    if (It != C.order.end()) C.order.erase(It);
    C.order.push_back(V);
    while (static_cast<int>(C.order.size()) > kKeep) C.order.erase(C.order.begin());
    ++C.uses;
    if (store) store(category, joined(C));
  }

  /** How many other variants of category have been used since variant was last: 0 it was the last one, -1 never (or
   *  forgotten past kKeep). */
  int age(const std::string& category, const std::string& variant) {
    std::lock_guard<std::mutex> L(m_);
    return ageLocked(cat(category), clean(variant));
  }

  /** The index into candidates of the one used least recently: never used before any used one; among the never used,
   *  the order salt gives (a stable hash: the same salt, the same pick). -1 when candidates is empty. */
  int leastRecent(const std::string& category, const std::vector<std::string>& candidates, uint32_t salt = 0) {
    const std::vector<int> O = order(category, candidates, salt);
    return O.empty() ? -1 : O.front();
  }

  /** candidates' indexes, least recently used first (as leastRecent ranks them). */
  std::vector<int> order(const std::string& category, const std::vector<std::string>& candidates, uint32_t salt = 0) {
    std::lock_guard<std::mutex> L(m_);
    Cat& C = cat(category);
    struct Key { int idx; int age; uint32_t tie; };
    std::vector<Key> Ks;
    Ks.reserve(candidates.size());
    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
      const std::string V = clean(candidates[i]);
      const int A = ageLocked(C, V);
      Ks.push_back({i, A < 0 ? 1 << 30 : A, hash(V, salt)});
    }
    // the oldest first (never used = oldest of all); equal ages (only the never used can tie) by the salted hash
    std::stable_sort(Ks.begin(), Ks.end(), [](const Key& X, const Key& Y) { return X.age != Y.age ? X.age > Y.age : X.tie < Y.tie; });
    std::vector<int> Out;
    Out.reserve(Ks.size());
    for (const Key& K : Ks) Out.push_back(K.idx);
    return Out;
  }

  /** leastRecent and use in one: the variant picked (empty when candidates is). */
  std::string take(const std::string& category, const std::vector<std::string>& candidates, uint32_t salt = 0) {
    const int i = leastRecent(category, candidates, salt);
    if (i < 0) return std::string();
    use(category, candidates[i]);
    return candidates[i];
  }

  /** The category's variants, the least recent first (at most kKeep). */
  std::vector<std::string> history(const std::string& category) {
    std::lock_guard<std::mutex> L(m_);
    return cat(category).order;
  }

  /** Uses of category since the store started (this run only: not saved). */
  int usesThisRun(const std::string& category) {
    std::lock_guard<std::mutex> L(m_);
    return cat(category).uses;
  }

  /** Drop a category's history (saved as empty). */
  void forget(const std::string& category) {
    std::lock_guard<std::mutex> L(m_);
    Cat& C = cat(category);
    C.order.clear();
    if (store) store(category, std::string());
  }

  /** Write what has changed to the saved config now. */
  void flush() {
    std::function<void()> F;
    {
      std::lock_guard<std::mutex> L(m_);
      F = commit;
    }
    if (F) F();
  }

  /** The order salt hashes with (exposed for callers that want the same tie order elsewhere). */
  static uint32_t hash(const std::string& s, uint32_t salt) {
    uint32_t h = 2166136261u ^ (salt * 2654435761u);
    for (unsigned char c : s) {
      h ^= c;
      h *= 16777619u;
    }
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return h;
  }

  // ---- persistence (Game/TagRecent.cpp binds these in the game; unset: memory only)
  /** A category's saved value ("a|b|c", the most recent last), read once on its first use; false: none saved. */
  std::function<bool(const std::string& category, std::string& out)> load;
  /** A category's new value, after every change. */
  std::function<void(const std::string& category, const std::string& value)> store;
  /** Make the stored values durable (the config written to disk). */
  std::function<void()> commit;

 private:
  struct Cat {
    std::vector<std::string> order;  // the least recent first
    int uses = 0;
  };
  std::map<std::string, Cat> cats_;
  std::mutex m_;

  static std::string clean(const std::string& v) {
    std::string s = v;
    std::replace(s.begin(), s.end(), '|', '/');
    return s;
  }

  static std::string joined(const Cat& C) {
    std::string s;
    for (size_t i = 0; i < C.order.size(); ++i) {
      if (i) s += '|';
      s += C.order[i];
    }
    return s;
  }

  static int ageLocked(const Cat& C, const std::string& v) {
    for (int i = static_cast<int>(C.order.size()) - 1; i >= 0; --i)
      if (C.order[i] == v) return static_cast<int>(C.order.size()) - 1 - i;
    return -1;
  }

  Cat& cat(const std::string& category) {
    auto It = cats_.find(category);
    if (It != cats_.end()) return It->second;
    Cat& C = cats_[category];
    std::string Saved;
    if (load && load(category, Saved) && !Saved.empty()) {
      size_t a = 0;
      while (a <= Saved.size()) {
        const size_t b = Saved.find('|', a);
        const std::string V = Saved.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (!V.empty() && std::find(C.order.begin(), C.order.end(), V) == C.order.end()) C.order.push_back(V);
        if (b == std::string::npos) break;
        a = b + 1;
      }
      while (static_cast<int>(C.order.size()) > kKeep) C.order.erase(C.order.begin());
    }
    return C;
  }
};

}  // namespace tag
