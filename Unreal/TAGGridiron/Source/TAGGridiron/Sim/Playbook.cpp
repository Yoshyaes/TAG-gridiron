// Port of src/sim/playbook.ts, then expanded: the formation library, the concepts fitted to it, and the team books
#include "Playbook.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>

namespace tag {

namespace {

Align alOL(double dy) {
  Align a;
  a.kind = AlignKind::OL;
  a.dy = dy;
  return a;
}
Align alInline(char side, bool off = false) {
  Align a;
  a.kind = AlignKind::Inline;
  a.side = side;
  a.off = off;
  return a;
}
Align alWide(char side, bool off = false) {
  Align a;
  a.kind = AlignKind::Wide;
  a.side = side;
  a.off = off;
  return a;
}
Align alSlot(char side, int n = 0) {
  Align a;
  a.kind = AlignKind::Slot;
  a.side = side;
  a.n = n;
  return a;
}
Align alBack(double x, double dy) {
  Align a;
  a.kind = AlignKind::Back;
  a.x = x;
  a.dy = dy;
  return a;
}

std::vector<FormationSlot> withOL(std::vector<FormationSlot> rest) {
  std::vector<FormationSlot> out = {
      {"LT", {Pos::OT}, alOL(2.7)},
      {"LG", {Pos::OG}, alOL(1.35)},
      {"C", {Pos::C}, alOL(0)},
      {"RG", {Pos::OG}, alOL(-1.35)},
      {"RT", {Pos::OT}, alOL(-2.7)},
  };
  for (auto& s : rest) out.push_back(std::move(s));
  return out;
}

using Routes = std::map<std::string, std::string>;
using Strs = std::vector<std::string>;

OffPlay P(const char* id, const char* name, const char* formation, PlayType type, Routes routes, Strs reads, Strs tags,
          double dropDepth = 2) {
  OffPlay p;
  p.id = id;
  p.name = name;
  p.formation = formation;
  p.type = type;
  p.routes = std::move(routes);
  p.reads = std::move(reads);
  p.tags = std::move(tags);
  // the default drop is 2 yd from the gun (setting up ~7 deep); from under center the quarterback drops 5 (between a
  // three and a five step), as in the real game. A play's own depth stands.
  const Formation* f = formationById(p.formation);
  const bool underCenter = f && f->qbDepth <= 1.5;
  p.dropDepth = (underCenter && dropDepth == 2) ? 5 : dropDepth;
  return p;
}

OffPlay R(const char* id, const char* name, const char* formation, RunScheme run, Strs tags) {
  OffPlay p;
  p.id = id;
  p.name = name;
  p.formation = formation;
  p.type = PlayType::Run;
  p.routes = {{"X", "block"}, {"Z", "block"}, {"SLOT", "block"}, {"SLOT2", "block"}, {"TE", "block"}, {"TE2", "block"}, {"TE3", "block"}, {"FB", "block"}, {"RB2", "block"}};
  p.run = std::move(run);
  p.tags = std::move(tags);
  return p;
}

/** Inline-literal plays from the TS (explicit fields; dropDepth optional). */
OffPlay Lit(const char* id, const char* name, const char* formation, PlayType type, Routes routes, Strs reads, Strs tags,
            std::optional<double> dropDepth) {
  OffPlay p;
  p.id = id;
  p.name = name;
  p.formation = formation;
  p.type = type;
  p.routes = std::move(routes);
  p.reads = std::move(reads);
  p.tags = std::move(tags);
  p.dropDepth = dropDepth;
  return p;
}

/** The original sets' lines for the play-call screen (the newer ones carry their own). */
std::vector<Formation> withDescs(std::vector<Formation> v) {
  static const std::map<std::string, std::string> d = {
      {"gun_trips", "Three receivers to the tight end side, the back beside the quarterback"},
      {"gun_doubles", "Two receivers each side, the tight end flexed as a slot"},
      {"gun_empty", "Five out, nobody in the backfield: the defense has to show its coverage"},
      {"pistol", "The back behind the quarterback: runs either way off the same look"},
      {"singleback", "Two tight ends and one back under center: the base run and play-action set"},
      {"iform", "Fullback and tailback stacked behind the quarterback: downhill runs and play action"},
      {"heavy", "Two tight ends, two backs: short yardage and the goal line"},
      {"gun_bunch", "Three receivers bunched tight to one side: free releases and rubs"},
      {"gun_trey", "Trips with the tight end detached as the inside slot"},
      {"gun_spread", "Four receivers two by two, the back beside the quarterback"},
      {"gun_tightflex", "The tight end attached, a slot to the other side"},
      {"gun_ace", "Two tight ends attached from the gun"},
      {"gun_split", "Two backs beside the quarterback, the tight end attached"},
      {"pistol_ace", "Two tight ends with the pistol back"},
      {"pistol_trips", "Trips to the tight end with the back behind the quarterback"},
      {"sb_doubles", "Two receivers each side under center"},
      {"sb_trips", "Trips with the tight end detached, under center"},
      {"sb_bunch", "A bunch to one side, under center"},
      {"sb_wing", "The tight end, a wing off his hip and the flanker inside"},
      {"i_twin", "The I with both receivers to one side"},
      {"i_tight", "The I with two tight ends: twenty-two personnel"},
      {"i_slot", "The I with three receivers: twenty personnel"},
      {"strong_i", "The fullback offset to the tight end"},
      {"weak_i", "The fullback offset away from the tight end"},
      {"splitback", "Two backs split behind the quarterback"},
  };
  for (Formation& f : v) {
    auto it = d.find(f.id);
    if (f.desc.empty() && it != d.end()) f.desc = it->second;
  }
  return v;
}

RunScheme RS(double aimDy, RunStyle style, double handoffDelay, const char* carrier) {
  RunScheme r;
  r.aimDy = aimDy;
  r.style = style;
  r.handoffDelay = handoffDelay;
  r.carrier = carrier;
  return r;
}

}  // namespace

const std::vector<Formation>& FORMATIONS() {
  static const std::vector<Formation> f = withDescs({
      {"gun_trips", "Gun Trips TE", "11", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"RB", {Pos::RB}, alBack(-5, 1.2)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"SLOT", {Pos::WR}, alSlot('R')},
           {"TE", {Pos::TE}, alInline('R')},
       })},
      {"gun_doubles", "Gun Doubles", "11", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"RB", {Pos::RB}, alBack(-5, -1.2)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"SLOT", {Pos::WR}, alSlot('L')},
           {"TE", {Pos::TE}, alSlot('R')},
       })},
      {"gun_empty", "Gun Empty", "10", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"SLOT", {Pos::WR}, alSlot('L')},
           {"SLOT2", {Pos::WR}, alSlot('R', 2)},
           {"RB", {Pos::RB}, alSlot('R')},
       })},
      {"pistol", "Pistol Strong", "11", 4,
       withOL({
           {"QB", {Pos::QB}, alBack(-4, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"SLOT", {Pos::WR}, alSlot('L')},
           {"TE", {Pos::TE}, alInline('R')},
       })},
      {"singleback", "Singleback Ace", "12", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alInline('L', true)},
       })},
      {"iform", "I-Form Pro", "21", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"FB", {Pos::FB}, alBack(-4.5, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
       })},
      {"heavy", "Goal Line Heavy", "22", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"FB", {Pos::FB}, alBack(-4, 0)},
           {"RB", {Pos::RB}, alBack(-6.5, 0)},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alInline('L', true)},
           {"X", {Pos::WR}, alWide('L')},
       })},
      // ---- the expanded library (every team's book draws its formations from here)
      // shotgun
      {"gun_bunch", "Gun Bunch", "11", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"RB", {Pos::RB}, alBack(-5, 1.2)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alBack(-1.5, -9.2)},     // the bunch: outside man off the ball,
           {"SLOT", {Pos::WR}, alBack(-0.6, -8.2)},  // the point man on it,
           {"TE", {Pos::TE}, alBack(-1.5, -7.2)},    // the inside man off it
       }), "Shotgun"},
      {"gun_trey", "Gun Trey Open", "11", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"RB", {Pos::RB}, alBack(-5, 1.2)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"SLOT", {Pos::WR}, alSlot('R')},
           {"TE", {Pos::TE}, alSlot('R', 2)},
       }), "Shotgun"},
      {"gun_spread", "Gun Spread", "10", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"RB", {Pos::RB}, alBack(-5, -1.2)},
           {"X", {Pos::WR}, alWide('L')},
           {"SLOT", {Pos::WR}, alSlot('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"SLOT2", {Pos::WR}, alSlot('R')},
       }), "Shotgun"},
      {"gun_tightflex", "Gun Tight Doubles", "11", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"RB", {Pos::RB}, alBack(-5, 1.2)},
           {"X", {Pos::WR}, alWide('L')},
           {"SLOT", {Pos::WR}, alSlot('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
       }), "Shotgun"},
      {"gun_ace", "Gun Ace", "12", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"RB", {Pos::RB}, alBack(-5, 1.2)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alInline('L', true)},
       }), "Shotgun"},
      {"gun_split", "Gun Split Backs", "21", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"RB", {Pos::RB}, alBack(-5, -1.4)},
           {"FB", {Pos::FB, Pos::RB}, alBack(-5, 1.4)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
       }), "Shotgun"},
      // pistol
      {"pistol_ace", "Pistol Ace", "12", 4,
       withOL({
           {"QB", {Pos::QB}, alBack(-4, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alInline('L', true)},
       }), "Pistol"},
      {"pistol_trips", "Pistol Trips", "11", 4,
       withOL({
           {"QB", {Pos::QB}, alBack(-4, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"SLOT", {Pos::WR}, alSlot('R')},
           {"TE", {Pos::TE}, alInline('R')},
       }), "Pistol"},
      // singleback
      {"sb_doubles", "Singleback Doubles", "11", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"SLOT", {Pos::WR}, alSlot('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
       }), "Singleback"},
      {"sb_trips", "Singleback Trey Open", "11", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"SLOT", {Pos::WR}, alSlot('R')},
           {"TE", {Pos::TE}, alSlot('R', 2)},
       }), "Singleback"},
      {"sb_bunch", "Singleback Bunch", "11", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alBack(-1.5, -9.2)},
           {"SLOT", {Pos::WR}, alBack(-0.6, -8.2)},
           {"TE", {Pos::TE}, alBack(-1.5, -7.2)},
       }), "Singleback"},
      {"sb_wing", "Singleback Wing Trio", "12", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alBack(-1.3, -5.3)},  // the wing, off the end man's hip
           {"Z", {Pos::WR}, alBack(-0.6, -8.6)},
       }), "Singleback"},
      // I-formation
      {"i_twin", "I-Form Twin WR", "21", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"FB", {Pos::FB}, alBack(-4.5, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alSlot('R')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('L')},
       }), "I-Form"},
      {"i_tight", "I-Form Tight Pair", "22", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"FB", {Pos::FB}, alBack(-4.5, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alInline('L', true)},
       }), "I-Form"},
      {"i_slot", "I-Form Slot", "20", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"FB", {Pos::FB}, alBack(-4.5, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"SLOT", {Pos::WR}, alSlot('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
       }), "I-Form"},
      {"strong_i", "Strong I", "21", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"FB", {Pos::FB}, alBack(-4.5, -1.8)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
       }), "Strong I"},
      {"weak_i", "Weak I", "21", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"FB", {Pos::FB}, alBack(-4.5, 1.8)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
       }), "Weak I"},
      {"splitback", "Split Backs", "21", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"FB", {Pos::FB, Pos::RB}, alBack(-5, 1.6)},
           {"RB", {Pos::RB}, alBack(-5, -1.6)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
       }), "Split Backs"},
      // ---- 2026-11-22 expansion: the rest of a modern book's sets (personnel 10 / 11 / 12 / 13 / 20 / 21 / 23 / 31)
      // shotgun
      {"gun_quads", "Gun Quads", "10", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"RB", {Pos::RB}, alBack(-5, 1.2)},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"X", {Pos::WR}, alSlot('R', 3)},
           {"SLOT", {Pos::WR}, alSlot('R')},
           {"SLOT2", {Pos::WR}, alSlot('R', 2)},
       }), "Shotgun", "Four receivers to one side, nobody outside the left tackle: the defense has to declare its rotation"},
      {"gun_empty_trey", "Gun Empty Trey", "11", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"RB", {Pos::RB}, alSlot('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"SLOT", {Pos::WR}, alSlot('R')},
           {"TE", {Pos::TE}, alSlot('R', 2)},
       }), "Shotgun", "Empty: the back split out to the two-man side, the tight end the third man of the trips"},
      {"gun_empty_bunch", "Gun Empty Bunch", "10", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"RB", {Pos::RB}, alSlot('L')},
           {"Z", {Pos::WR}, alBack(-1.5, -9.2)},
           {"SLOT", {Pos::WR}, alBack(-0.6, -8.2)},
           {"SLOT2", {Pos::WR}, alBack(-1.5, -7.2)},
       }), "Shotgun", "Empty with a three-man bunch: rubs and pick routes against man"},
      {"gun_snugs", "Gun Tight Slots", "11", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"RB", {Pos::RB}, alBack(-5, -1.2)},
           {"X", {Pos::WR}, alBack(-0.6, 8.0)},
           {"SLOT", {Pos::WR}, alBack(-1.4, 5.6)},
           {"TE", {Pos::TE}, alInline('R')},
           {"Z", {Pos::WR}, alBack(-1.3, -7.4)},
       }), "Shotgun", "Everyone in tight: crack blocks for the run, crossers and corners with room outside"},
      {"gun_wing_trips", "Gun Trips Wing", "12", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"RB", {Pos::RB}, alBack(-5, 1.2)},
           {"X", {Pos::WR}, alWide('L')},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alBack(-1.3, -5.3)},
           {"Z", {Pos::WR}, alWide('R', true)},
       }), "Shotgun", "Two tight ends to the trips side, one on the line and one on the wing"},
      {"gun_ace_twins", "Gun Ace Twins", "12", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"RB", {Pos::RB}, alBack(-5, -1.2)},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alInline('L')},
           {"Z", {Pos::WR}, alWide('L', true)},
           {"X", {Pos::WR}, alSlot('L')},
       }), "Shotgun", "Both tight ends attached, the two receivers twins to the left"},
      {"gun_heavy", "Gun Y-Trips Heavy", "13", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"RB", {Pos::RB}, alBack(-5, 1.2)},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alInline('L')},
           {"TE3", {Pos::TE}, alBack(-1.3, -5.3)},
           {"X", {Pos::WR}, alWide('L')},
       }), "Shotgun", "Three tight ends from the gun: the run box of a goal line set, the passing alignment of the gun"},
      {"gun_split_slot", "Gun Split Slot", "20", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"RB", {Pos::RB}, alBack(-5, -1.4)},
           {"FB", {Pos::RB, Pos::FB}, alBack(-5, 1.4)},
           {"X", {Pos::WR}, alWide('L')},
           {"SLOT", {Pos::WR}, alSlot('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
       }), "Shotgun", "Two backs beside the quarterback and three receivers: the read option and the RPO set"},
      {"gun_dbl_stack", "Gun Doubles Stack", "11", 5,
       withOL({
           {"QB", {Pos::QB}, alBack(-5, 0)},
           {"RB", {Pos::RB}, alBack(-5, 1.2)},
           {"X", {Pos::WR}, alBack(-0.6, 11.0)},
           {"SLOT", {Pos::WR}, alBack(-2.6, 11.0)},
           {"Z", {Pos::WR}, alBack(-0.6, -11.0)},
           {"TE", {Pos::TE}, alBack(-2.6, -11.0)},
       }), "Shotgun", "Stacked pairs on both sides: free releases against press, natural rubs"},
      {"wildcat", "Wildcat", "21", 5,
       withOL({
           {"QB", {Pos::RB}, alBack(-5, 0)},  // (a back takes the direct snap)
           {"RB", {Pos::RB, Pos::WR}, alBack(-5, 2.2)},
           {"X", {Pos::WR}, alWide('L')},
           {"SLOT", {Pos::WR, Pos::QB}, alSlot('L')},
           {"TE", {Pos::TE}, alInline('R')},
           {"Z", {Pos::WR}, alWide('R', true)},
       }), "Wildcat", "The back takes the direct snap: an extra blocker, no handoff to defend"},
      // pistol
      {"pistol_bunch", "Pistol Bunch", "11", 4,
       withOL({
           {"QB", {Pos::QB}, alBack(-4, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alBack(-1.5, -9.2)},
           {"SLOT", {Pos::WR}, alBack(-0.6, -8.2)},
           {"TE", {Pos::TE}, alBack(-1.5, -7.2)},
       }), "Pistol", "A bunch to the right with the back behind the quarterback: the run goes either way"},
      {"pistol_wing", "Pistol Wing", "12", 4,
       withOL({
           {"QB", {Pos::QB}, alBack(-4, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alBack(-1.3, -5.3)},
           {"Z", {Pos::WR}, alWide('R', true)},
       }), "Pistol", "Tight end and wing to the right: split zone, counter and boots off them"},
      {"pistol_strong", "Pistol Strong Back", "21", 4,
       withOL({
           {"QB", {Pos::QB}, alBack(-4, 0)},
           {"FB", {Pos::FB, Pos::RB}, alBack(-4, -1.9)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
       }), "Pistol", "A fullback offset beside the quarterback: lead and kick-out blocks from the pistol"},
      {"pistol_jumbo", "Pistol Jumbo", "13", 4,
       withOL({
           {"QB", {Pos::QB}, alBack(-4, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alInline('L')},
           {"TE3", {Pos::TE}, alBack(-1.3, -5.3)},
           {"X", {Pos::WR}, alWide('L')},
       }), "Pistol", "Three tight ends and the back in the pistol: downhill runs and play action"},
      // singleback
      {"sb_pair_twins", "Singleback Ace Pair Twins", "12", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alInline('L')},
           {"Z", {Pos::WR}, alWide('L', true)},
           {"X", {Pos::WR}, alSlot('L')},
       }), "Singleback", "Two tight ends attached, the receivers twins to one side"},
      {"sb_jumbo", "Singleback Jumbo", "13", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alInline('L')},
           {"TE3", {Pos::TE}, alBack(-1.3, -5.3)},
           {"X", {Pos::WR}, alWide('L')},
       }), "Singleback", "Three tight ends, one wing: the run game's heavy set"},
      {"sb_tight_slots", "Singleback Tight Slots", "11", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"SLOT", {Pos::WR}, alBack(-1.3, 5.6)},
           {"TE", {Pos::TE}, alInline('R')},
           {"Z", {Pos::WR}, alBack(-1.3, -7.5)},
       }), "Singleback", "Receivers tight to the formation: cracks on the run, crossers and outs with room"},
      {"sb_trips_te", "Singleback Trips TE", "11", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"TE", {Pos::TE}, alInline('R')},
           {"SLOT", {Pos::WR}, alSlot('R')},
           {"Z", {Pos::WR}, alWide('R', true)},
       }), "Singleback", "Trips to the tight end from under center"},
      {"sb_y_flex", "Singleback Y-Flex", "12", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"TE2", {Pos::TE}, alSlot('L', 2)},
           {"TE", {Pos::TE}, alInline('R')},
           {"Z", {Pos::WR}, alWide('R', true)},
       }), "Singleback", "The second tight end flexed out as a slot: a linebacker or a nickel has to go with him"},
      // I-formation and the three-back sets
      {"i_close", "I-Form Close", "21", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"FB", {Pos::FB}, alBack(-4.5, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"TE", {Pos::TE}, alInline('R')},
           {"Z", {Pos::WR}, alBack(-1.3, -7.0)},
       }), "I-Form", "The flanker in a close split off the tight end: a crack block on the edge, a bigger box"},
      {"strong_close", "Strong Close", "21", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"FB", {Pos::FB}, alBack(-4.5, -1.8)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"TE", {Pos::TE}, alInline('R')},
           {"Z", {Pos::WR}, alBack(-1.3, -7.0)},
       }), "Strong I", "The fullback offset to the tight end and the flanker in close: the power side loaded"},
      {"power_i", "Power I", "31", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"FB", {Pos::FB}, alBack(-4.5, 0)},
           {"RB2", {Pos::FB, Pos::RB}, alBack(-4.5, -1.9)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"TE", {Pos::TE}, alInline('R')},
           {"X", {Pos::WR}, alWide('L')},
       }), "I-Form", "The I with a second lead back offset to the strength: two lead blocks for the tailback"},
      {"wishbone", "Wishbone", "31", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"FB", {Pos::FB}, alBack(-4.0, 0)},
           {"RB", {Pos::RB}, alBack(-6.0, 1.8)},
           {"RB2", {Pos::RB, Pos::FB}, alBack(-6.0, -1.8)},
           {"TE", {Pos::TE}, alInline('R')},
           {"X", {Pos::WR}, alWide('L')},
       }), "Wishbone", "Three backs in the wishbone: the dive, the lead and the pitch all a step from the ball"},
      {"full_house", "Full House", "31", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"RB", {Pos::RB}, alBack(-5.0, 2.3)},
           {"FB", {Pos::FB}, alBack(-5.0, 0)},
           {"RB2", {Pos::RB, Pos::FB}, alBack(-5.0, -2.3)},
           {"TE", {Pos::TE}, alInline('R')},
           {"X", {Pos::WR}, alWide('L')},
       }), "Full House", "Three backs abreast behind the quarterback: power both ways, the defense cannot key the strength"},
      // goal line
      {"gl_jumbo", "Goal Line Jumbo", "23", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1.2, 0)},
           {"FB", {Pos::FB}, alBack(-4, 0)},
           {"RB", {Pos::RB}, alBack(-6.5, 0)},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alInline('L')},
           {"TE3", {Pos::TE}, alBack(-1.3, -5.3)},
       }), "Goal Line", "Three tight ends and two backs: eleven men inside the tackle box"},
  });
  return f;
}

const std::map<std::string, RouteDef>& ROUTES() {
  static const std::map<std::string, RouteDef> r = {
      {"go", {{{6, 0.5}, {45, 2}}, false, false}},
      {"fade", {{{4, 1.5}, {40, 4}}, false, false}},
      {"seam", {{{8, -0.5}, {45, -1.5}}, false, false}},
      {"slant", {{{2.5, 0}, {10, -7}}, false, false}},
      {"quickout", {{{5, 0}, {4.5, 8}}, false, false}},
      {"out", {{{11, 0}, {10.5, 10}}, false, false}},
      {"in", {{{6, 0}, {6, -14}}, false, false}},
      {"dig", {{{13, 0}, {13, -18}}, false, false}},
      {"curl", {{{12, 0}, {10.5, -1.2}}, true, false}},
      {"hitch", {{{6, 0}, {5, -0.5}}, true, false}},
      {"comeback", {{{16, 0}, {14, 3}}, true, false}},
      {"post", {{{12, 0}, {38, -12}}, false, false}},
      {"corner", {{{11, 0}, {26, 10}}, false, false}},
      {"flat", {{{1.5, 3}, {4, 12}}, false, false}},
      {"arrow", {{{4, 6}, {6, 14}}, false, false}},
      {"wheel", {{{1.5, 4}, {5, 9}, {40, 10}}, false, false}},
      {"drag", {{{2.5, -1}, {5, -25}}, false, false}},
      {"shallow", {{{2, -1}, {4, -22}}, false, false}},
      {"cross", {{{10, -2}, {16, -24}}, false, false}},
      {"sail", {{{10, 0}, {18, 10}}, false, false}},
      {"stick", {{{6, 0}, {5.5, -0.5}}, true, false}},
      {"swing", {{{0, 4}, {2, 10}}, false, false}},  // (bowed out level with his alignment, not back: caught at -7 a quarter of the time, NFL -4)
      {"angle", {{{3, 3}, {6, -3}}, false, false}},
      {"checkdown", {{{2, 2}, {5, 3}}, true, false}},
      {"screen", {{{2.0, 3}, {2.5, 4.5}}, true, false}},  // (slips out and up toward the line: caught 2-3 yd behind it as on Sundays - level with a 5 yd alignment it was caught at -6.6 and gained 2.9 yd an attempt against the NFL's ~5.5)
      {"block", {{}, false, true}},
      // the expanded tree
      {"snag", {{{5, 0}, {6, -2.5}}, true, false}},                  // spot: six yards, turn in and sit
      {"outnup", {{{5, 0}, {5.5, 3}, {32, 4}}, false, false}},       // out and up
      {"slantgo", {{{3, 0}, {5, -2.5}, {32, -1}}, false, false}},    // sluggo
      {"bubble", {{{-1.2, 2.5}, {-0.8, 6}, {1, 9}}, false, false}},  // slot bubble
      {"deepcross", {{{12, -2}, {18, -28}}, false, false}},
      {"skinny", {{{10, 0}, {35, -5}}, false, false}},               // skinny post
      {"deepout", {{{14, 0}, {13.5, 9}}, false, false}},             // 14-yard out
      {"whip", {{{4, -2}, {4.5, 3}}, true, false}},                  // fake in, break out and sit
      // a deep tailback's check-release (under center, the pistol): to a spot just past the line
      {"release", {{{3.5, 2}, {8.2, 3}}, true, false}},
      {"releaseflat", {{{3.5, 4}, {7.8, 11}}, false, false}},
      // ---- 2026-11-22: the rest of the tree a modern book's concepts need
      {"flag", {{{7, 0}, {17, 9}}, false, false}},                       // 7-step corner off a short stem (smash's "7")
      {"postcorner", {{{10, 0}, {14, -3}, {27, 8}}, false, false}},     // shake: nod to the post, break to the corner
      {"china", {{{4, 0}, {6, -3}, {15, 6}}, false, false}},            // slant stem, break back out to the corner
      {"zig", {{{5, 0}, {6.5, -3}, {6.5, 5}}, false, false}},            // zig-out: in, plant, out (beats inside leverage)
      {"jerk", {{{3, -1}, {5, -6}, {5.5, -2.5}}, true, false}},          // across, plant and back out: sits vs zone
      {"speedout", {{{4, 0}, {4.5, 6}, {4.5, 12}}, false, false}},       // rounded out at 4-5
      {"hitchgo", {{{6, 0}, {5.5, -0.5}, {9, 0.5}, {34, 1.5}}, false, false}},  // hitch and go (stick nod)
      {"under", {{{6, -1}, {7.5, -20}}, false, false}},                 // the 7-yard drag under the linebackers
      {"sit", {{{5, -0.5}, {5.5, -1.5}}, true, false}},                 // a tight end's sit over the ball
      {"bench", {{{11, 0}, {11, 12}}, false, false}},                    // the slot's out to the sideline
      {"glance", {{{5, 0}, {14, -5}}, false, false}},                    // the RPO's quick skinny post
      {"deeppost", {{{16, 0}, {38, -10}}, false, false}},                // a deep post off a dig stem (mills' post)
      {"bigdig", {{{17, 0}, {17, -16}}, false, false}},                  // the deep in
      {"circle", {{{1.5, 3}, {4, 3.5}, {5.5, 0.5}, {5.5, -2}}, true, false}},  // the back's circle under the linebackers
      {"rail", {{{1, 4}, {7, 6.5}, {36, 7.5}}, false, false}},           // a back or slot up the sideline
      // screens: the receiver's tunnel (a step up, back inside behind the line), the slip (shows the block, slips out)
      {"tunnel", {{{0.8, 0}, {-0.8, -3.0}, {-1.2, -6.5}}, true, false}},
      {"slip", {{{0.6, 1.5}, {1.2, 4.5}}, true, false}},
      // a man in jet motion: on across behind the line to the flat (the jet fake) / into a swing
      {"jetflat", {{{0.8, 5}, {3.5, 12}}, false, false}},
      // a receiver's stalk block on a screen to his side (PlaySim: he run-blocks the man over him)
      {"stalk", {{}, false, true}},
  };
  return r;
}

namespace {
std::vector<OffPlay> generatedPlays(const std::vector<OffPlay>& stock);
std::vector<DefCall> generatedCalls(const std::vector<DefCall>& stock);
void fillPlayInfo(std::vector<OffPlay>& v);
void fillCallInfo(std::vector<DefCall>& v);
}  // namespace

const std::vector<OffPlay>& OFF_PLAYS() {
  using PT = PlayType;
  using RSt = RunStyle;
  // the hand-written ("stock") plays first, with their ids, then every concept the generator fits to the library
  static const std::vector<OffPlay> plays = [] {
    std::vector<OffPlay> v = {
      // ---- Gun Trips
      P("trips_flood", "Flood", "gun_trips", PT::Pass, {{"X", "post"}, {"Z", "go"}, {"SLOT", "sail"}, {"TE", "flat"}, {"RB", "checkdown"}}, {"SLOT", "TE", "X", "RB"}, {"mid"}),
      P("trips_stick", "Stick", "gun_trips", PT::Pass, {{"X", "slant"}, {"Z", "go"}, {"SLOT", "stick"}, {"TE", "flat"}, {"RB", "checkdown"}}, {"SLOT", "TE", "X", "RB"}, {"short", "quick"}, 1),
      P("trips_4verts", "Four Verticals", "gun_trips", PT::Pass, {{"X", "go"}, {"Z", "go"}, {"SLOT", "seam"}, {"TE", "seam"}, {"RB", "checkdown"}}, {"SLOT", "TE", "X", "Z", "RB"}, {"deep"}, 3),
      P("trips_dagger", "Dagger", "gun_trips", PT::Pass, {{"X", "curl"}, {"Z", "dig"}, {"SLOT", "seam"}, {"TE", "shallow"}, {"RB", "checkdown"}}, {"Z", "X", "TE", "RB"}, {"mid"}, 3),
      R("trips_iz", "Inside Zone", "gun_trips", RS(-0.7, RSt::Zone, 0.65, "RB"), {"run"}),
      R("trips_draw", "HB Draw", "gun_trips", RS(0.6, RSt::Draw, 0.9, "RB"), {"run", "long"}),
      Lit("trips_screen", "HB Screen", "gun_trips", PT::Screen, {{"X", "go"}, {"Z", "go"}, {"SLOT", "in"}, {"TE", "block"}, {"RB", "screen"}}, {"RB"}, {"screen", "short"}, 4.0),
      // ---- Gun Doubles
      P("dbl_mesh", "Mesh", "gun_doubles", PT::Pass, {{"X", "corner"}, {"Z", "dig"}, {"SLOT", "drag"}, {"TE", "shallow"}, {"RB", "swing"}}, {"SLOT", "TE", "RB", "Z"}, {"short"}),
      P("dbl_smash", "Smash", "gun_doubles", PT::Pass, {{"X", "hitch"}, {"Z", "hitch"}, {"SLOT", "corner"}, {"TE", "corner"}, {"RB", "checkdown"}}, {"SLOT", "X", "TE", "Z", "RB"}, {"mid"}),
      P("dbl_slants", "Slant Flat", "gun_doubles", PT::Pass, {{"X", "slant"}, {"Z", "slant"}, {"SLOT", "flat"}, {"TE", "flat"}, {"RB", "checkdown"}}, {"X", "SLOT", "Z", "TE", "RB"}, {"short", "quick"}, 1),
      P("dbl_curls", "Curl Flat", "gun_doubles", PT::Pass, {{"X", "curl"}, {"Z", "curl"}, {"SLOT", "arrow"}, {"TE", "flat"}, {"RB", "checkdown"}}, {"X", "SLOT", "Z", "TE", "RB"}, {"mid"}),
      P("dbl_postwheel", "Post Wheel", "gun_doubles", PT::Pass, {{"X", "post"}, {"Z", "out"}, {"SLOT", "wheel"}, {"TE", "dig"}, {"RB", "checkdown"}}, {"X", "SLOT", "TE", "Z", "RB"}, {"deep"}, 3),
      P("dbl_spacing", "Spacing", "gun_doubles", PT::Pass, {{"X", "hitch"}, {"Z", "quickout"}, {"SLOT", "stick"}, {"TE", "hitch"}, {"RB", "flat"}}, {"SLOT", "TE", "X", "Z", "RB"}, {"short", "quick"}, 1),
      R("dbl_oz", "Outside Zone", "gun_doubles", RS(4.5, RSt::Stretch, 0.7, "RB"), {"run"}),
      R("dbl_iz", "Inside Zone", "gun_doubles", RS(0.7, RSt::Zone, 0.65, "RB"), {"run"}),
      // ---- Empty
      P("empty_quick", "Quick Outs", "gun_empty", PT::Pass, {{"X", "quickout"}, {"Z", "quickout"}, {"SLOT", "stick"}, {"SLOT2", "slant"}, {"RB", "hitch"}}, {"SLOT", "X", "Z", "SLOT2", "RB"}, {"short", "quick"}, 1),
      P("empty_ycross", "Y Cross", "gun_empty", PT::Pass, {{"X", "post"}, {"Z", "curl"}, {"SLOT", "cross"}, {"SLOT2", "shallow"}, {"RB", "flat"}}, {"SLOT", "Z", "SLOT2", "RB"}, {"mid", "long"}, 3),
      P("empty_verts", "Spread Verts", "gun_empty", PT::Pass, {{"X", "go"}, {"Z", "comeback"}, {"SLOT", "seam"}, {"SLOT2", "seam"}, {"RB", "hitch"}}, {"SLOT", "SLOT2", "X", "Z", "RB"}, {"deep", "long"}, 3),
      // ---- Pistol
      R("pistol_iz", "Inside Zone", "pistol", RS(-0.7, RSt::Zone, 0.75, "RB"), {"run"}),
      R("pistol_power", "Power", "pistol", RS(2.0, RSt::Gap, 0.8, "RB"), {"run", "shortyard"}),
      Lit("pistol_pa", "PA Boot", "pistol", PT::PlayAction, {{"X", "post"}, {"Z", "cross"}, {"SLOT", "corner"}, {"TE", "flat"}, {"RB", "block"}}, {"TE", "Z", "SLOT", "X"}, {"mid", "playaction"}, 3.0),
      P("pistol_slants", "Double Slants", "pistol", PT::Pass, {{"X", "slant"}, {"Z", "go"}, {"SLOT", "slant"}, {"TE", "flat"}, {"RB", "checkdown"}}, {"SLOT", "X", "TE", "RB"}, {"short", "quick"}, 1),
      // ---- Singleback
      R("ace_iz", "Inside Zone", "singleback", RS(-0.7, RSt::Zone, 0.85, "RB"), {"run"}),
      R("ace_counter", "Counter", "singleback", RS(2.6, RSt::Gap, 1.0, "RB"), {"run"}),
      R("ace_stretch", "Stretch", "singleback", RS(-5, RSt::Stretch, 0.8, "RB"), {"run"}),
      Lit("ace_pa", "PA Shot", "singleback", PT::PlayAction, {{"X", "post"}, {"Z", "dig"}, {"TE", "seam"}, {"TE2", "block"}, {"RB", "block"}}, {"X", "Z", "TE"}, {"deep", "playaction"}, 6.0),
      P("ace_curl", "Curl Flat", "singleback", PT::Pass, {{"X", "curl"}, {"Z", "curl"}, {"TE", "flat"}, {"TE2", "flat"}, {"RB", "checkdown"}}, {"X", "TE", "Z", "TE2", "RB"}, {"mid"}, 5),
      // ---- I-form
      R("i_power", "Power O", "iform", RS(-2.0, RSt::Gap, 0.95, "RB"), {"run", "shortyard"}),
      R("i_iso", "Iso", "iform", RS(0.7, RSt::Gap, 0.9, "RB"), {"run", "shortyard"}),
      R("i_toss", "Toss Crack", "iform", RS(-6, RSt::Stretch, 0.55, "RB"), {"run"}),
      Lit("i_pa", "PA Crossers", "iform", PT::PlayAction, {{"X", "post"}, {"Z", "cross"}, {"TE", "flat"}, {"FB", "flat"}, {"RB", "block"}}, {"Z", "X", "TE", "FB"}, {"mid", "playaction"}, 6.0),
      // ---- Goal line
      R("gl_dive", "HB Dive", "heavy", RS(-0.7, RSt::Gap, 0.75, "RB"), {"run", "shortyard", "goalline"}),
      R("gl_sneak", "QB Sneak", "heavy", RS(0.6, RSt::Sneak, 0, "QB"), {"run", "shortyard", "goalline"}),
      Lit("gl_pa", "PA TE Flat", "heavy", PT::PlayAction, {{"TE", "flat"}, {"TE2", "corner"}, {"X", "fade"}, {"FB", "flat"}, {"RB", "block"}}, {"TE", "TE2", "FB", "X"}, {"goalline", "playaction"}, 4.0),
      // ---- clock
      Lit("kneel", "QB Kneel", "singleback", PT::Kneel, {}, {}, {"clock"}, std::nullopt),
      Lit("spike", "Spike", "gun_doubles", PT::Spike, {}, {}, {"clock"}, std::nullopt),
    };
    for (OffPlay& p : v) p.stock = true;
    // the stock runs block as their schemes are drawn (the generator's jobs: Playbook RUNS())
    for (OffPlay& p : v) {
      if (!p.run) continue;
      if (p.name == "Power O" || p.name == "Power") {
        p.run->jobs = {{"BG", "wrap"}, {"H", "kick"}};
        p.run->down = true;
      } else if (p.name == "Counter") {
        p.name = "Counter GT";
        p.run->jobs = {{"BG", "kick"}, {"BT", "wrap"}};
        p.run->down = true;
      } else if (p.name == "Iso") {
        p.run->jobs = {{"FB", "lead"}};
      } else if (p.name == "Toss Crack") {
        p.run->jobs = {{"PG", "lead"}};
        p.run->down = true;
      }
    }
    std::vector<OffPlay> g = generatedPlays(v);
    v.insert(v.end(), std::make_move_iterator(g.begin()), std::make_move_iterator(g.end()));
    fillPlayInfo(v);
    return v;
  }();
  return plays;
}

const std::vector<DefCall>& DEF_PLAYS() {
  using F = Front;
  using Cv = Coverage;
  static const std::vector<DefCall> d = [] {
    std::vector<DefCall> v = {
      {"base_c3", "4-3 Cover 3", F::Base, Cv::C3, {}, {"run", "base"}},
      {"base_c2", "4-3 Cover 2", F::Base, Cv::C2, {}, {"base"}},
      {"base_c1", "4-3 Cover 1 Lurk", F::Base, Cv::C1, {}, {"run", "base"}},
      {"base_mike", "4-3 Mike Blitz", F::Base, Cv::C1, {"MLB"}, {"blitz", "run"}},
      {"base_fire", "4-3 Fire Zone", F::Base, Cv::C3Fire, {"WLB"}, {"blitz"}},
      {"nk_c3", "Nickel Cover 3", F::Nickel, Cv::C3, {}, {"pass"}},
      {"nk_c4", "Nickel Quarters", F::Nickel, Cv::C4, {}, {"pass"}},
      {"nk_c2m", "Nickel 2-Man", F::Nickel, Cv::C2M, {}, {"pass", "long"}},
      {"nk_c1", "Nickel Cover 1", F::Nickel, Cv::C1, {}, {"pass"}},
      {"nk_nbblitz", "Nickel Corner Blitz", F::Nickel, Cv::C3Fire, {"NB"}, {"blitz", "pass"}},
      {"nk_c0", "Nickel Cover 0", F::Nickel, Cv::C0, {"WLB", "MLB"}, {"blitz", "pass", "shortyard"}},
      {"dime_c4", "Dime Quarters", F::Dime, Cv::C4, {}, {"long", "prevent"}},
      {"dime_c2", "Dime Cover 2", F::Dime, Cv::C2, {}, {"long", "prevent"}},
      {"gl_c0", "Goal Line Cover 0", F::Goalline, Cv::C0, {"SLB", "WLB"}, {"goalline", "shortyard", "run"}},
      {"gl_c1", "Goal Line Cover 1", F::Goalline, Cv::C1, {}, {"goalline", "shortyard", "run"}},
    };
    std::vector<DefCall> g = generatedCalls(v);
    v.insert(v.end(), std::make_move_iterator(g.begin()), std::make_move_iterator(g.end()));
    fillCallInfo(v);
    return v;
  }();
  return d;
}

const std::vector<FrontSlot>& FRONT_PERSONNEL(Front f) {
  static const std::vector<FrontSlot> base = {
      {"LE", {Pos::DE}}, {"LDT", {Pos::DT}}, {"RDT", {Pos::DT}}, {"RE", {Pos::DE}},
      {"WLB", {Pos::LB}}, {"MLB", {Pos::LB}}, {"SLB", {Pos::LB}},
      {"LCB", {Pos::CB}}, {"RCB", {Pos::CB}}, {"FS", {Pos::S}}, {"SS", {Pos::S}},
  };
  static const std::vector<FrontSlot> nickel = {
      {"LE", {Pos::DE}}, {"LDT", {Pos::DT}}, {"RDT", {Pos::DT}}, {"RE", {Pos::DE}},
      {"WLB", {Pos::LB}}, {"MLB", {Pos::LB}}, {"NB", {Pos::CB}},
      {"LCB", {Pos::CB}}, {"RCB", {Pos::CB}}, {"FS", {Pos::S}}, {"SS", {Pos::S}},
  };
  static const std::vector<FrontSlot> dime = {
      {"LE", {Pos::DE}}, {"LDT", {Pos::DT}}, {"RDT", {Pos::DT}}, {"RE", {Pos::DE}},
      {"MLB", {Pos::LB}}, {"NB", {Pos::CB}}, {"DB", {Pos::CB, Pos::S}},
      {"LCB", {Pos::CB}}, {"RCB", {Pos::CB}}, {"FS", {Pos::S}}, {"SS", {Pos::S}},
  };
  // the 3-4: three down (the nose in the LDT slot), two outside backers on the edges, two inside
  static const std::vector<FrontSlot> base34 = {
      {"LE", {Pos::DE, Pos::DT}}, {"LDT", {Pos::DT}}, {"RE", {Pos::DE, Pos::DT}},
      {"LOLB", {Pos::LB, Pos::DE}}, {"WLB", {Pos::LB}}, {"MLB", {Pos::LB}}, {"ROLB", {Pos::LB, Pos::DE}},
      {"LCB", {Pos::CB}}, {"RCB", {Pos::CB}}, {"FS", {Pos::S}}, {"SS", {Pos::S}},
  };
  static const std::vector<FrontSlot> goalline = {
      {"LE", {Pos::DE}}, {"LDT", {Pos::DT}}, {"RDT", {Pos::DT}}, {"RE", {Pos::DE}},
      {"WLB", {Pos::LB}}, {"MLB", {Pos::LB}}, {"SLB", {Pos::LB}},
      {"LCB", {Pos::CB}}, {"RCB", {Pos::CB}}, {"FS", {Pos::S}}, {"SS", {Pos::S}},
  };
  // the sub packages of the odd and multiple fronts: the 3-3-5 (three down, three stacked backers), the 2-4-5 (two
  // down, two edge backers standing up, two inside), the 3-2-6
  static const std::vector<FrontSlot> nickel33 = {
      {"LE", {Pos::DE, Pos::DT}}, {"LDT", {Pos::DT}}, {"RE", {Pos::DE, Pos::DT}},
      {"WLB", {Pos::LB}}, {"MLB", {Pos::LB}}, {"SLB", {Pos::LB, Pos::DE}}, {"NB", {Pos::CB}},
      {"LCB", {Pos::CB}}, {"RCB", {Pos::CB}}, {"FS", {Pos::S}}, {"SS", {Pos::S}},
  };
  static const std::vector<FrontSlot> nickel24 = {
      {"LDT", {Pos::DT, Pos::DE}}, {"RDT", {Pos::DT, Pos::DE}},
      {"LOLB", {Pos::LB, Pos::DE}}, {"WLB", {Pos::LB}}, {"MLB", {Pos::LB}}, {"ROLB", {Pos::LB, Pos::DE}}, {"NB", {Pos::CB}},
      {"LCB", {Pos::CB}}, {"RCB", {Pos::CB}}, {"FS", {Pos::S}}, {"SS", {Pos::S}},
  };
  static const std::vector<FrontSlot> dime32 = {
      {"LE", {Pos::DE, Pos::DT}}, {"LDT", {Pos::DT}}, {"RE", {Pos::DE, Pos::DT}},
      {"WLB", {Pos::LB}}, {"MLB", {Pos::LB}}, {"NB", {Pos::CB}}, {"DB", {Pos::CB, Pos::S}},
      {"LCB", {Pos::CB}}, {"RCB", {Pos::CB}}, {"FS", {Pos::S}}, {"SS", {Pos::S}},
  };
  switch (f) {
    case Front::Base: return base;
    case Front::Nickel: return nickel;
    case Front::Dime: return dime;
    case Front::Goalline: return goalline;
    case Front::Base34: return base34;
    case Front::Nickel33: return nickel33;
    case Front::Nickel24: return nickel24;
    case Front::Dime32: return dime32;
  }
  return base;
}

const OffPlay* playById(const std::string& id) {
  for (const auto& p : OFF_PLAYS())
    if (p.id == id) return &p;
  return nullptr;
}
const Formation* formationById(const std::string& id) {
  for (const auto& f : FORMATIONS())
    if (f.id == id) return &f;
  return nullptr;
}
const DefCall* defById(const std::string& id) {
  for (const auto& d : DEF_PLAYS())
    if (d.id == id) return &d;
  return nullptr;
}
const RouteDef* routeByName(const std::string& name) {
  const auto& r = ROUTES();
  auto it = r.find(name);
  return it == r.end() ? nullptr : &it->second;
}

const char* playTypeName(PlayType t) {
  switch (t) {
    case PlayType::Run: return "run";
    case PlayType::Pass: return "pass";
    case PlayType::PlayAction: return "playaction";
    case PlayType::Screen: return "screen";
    case PlayType::Kneel: return "kneel";
    case PlayType::Spike: return "spike";
  }
  return "?";
}
const char* playCatName(PlayCat c) {
  switch (c) {
    case PlayCat::Run: return "Run";
    case PlayCat::Pass: return "Pass";
    case PlayCat::PlayAction: return "PA";
    case PlayCat::Screen: return "Screen";
    case PlayCat::RPO: return "RPO";
    case PlayCat::Trick: return "Trick";
    case PlayCat::Kneel: return "Kneel";
    case PlayCat::Spike: return "Spike";
  }
  return "?";
}
const char* frontName(Front f) {
  switch (f) {
    case Front::Base: return "base";
    case Front::Nickel: return "nickel";
    case Front::Dime: return "dime";
    case Front::Goalline: return "goalline";
    case Front::Base34: return "base34";
    case Front::Nickel33: return "nickel33";
    case Front::Nickel24: return "nickel24";
    case Front::Dime32: return "dime32";
  }
  return "?";
}
const char* coverTechName(CoverTech t) {
  switch (t) {
    case CoverTech::None: return "";
    case CoverTech::Robber: return "robber";
    case CoverTech::Spy: return "spy";
    case CoverTech::Buzz: return "buzz";
    case CoverTech::Cloud: return "cloud";
    case CoverTech::Invert: return "invert";
    case CoverTech::Prevent: return "prevent";
    case CoverTech::Bracket: return "bracket";
  }
  return "";
}
int frontLinemen(Front f) {
  switch (f) {
    case Front::Base34:
    case Front::Nickel33:
    case Front::Dime32: return 3;
    case Front::Nickel24: return 2;
    default: return 4;
  }
}
const char* coverageName(Coverage c) {
  switch (c) {
    case Coverage::C0: return "c0";
    case Coverage::C1: return "c1";
    case Coverage::C2: return "c2";
    case Coverage::C3: return "c3";
    case Coverage::C4: return "c4";
    case Coverage::C2M: return "c2m";
    case Coverage::C3Fire: return "c3fire";
    case Coverage::C2T: return "c2t";
    case Coverage::C6: return "c6";
  }
  return "?";
}


// ================================================================== the expanded playbooks
// Concepts are written once against the receivers' places in a formation (the strong side's #1, #2, #3 from the
// outside in, the weak side's, the backs), and fitted to every formation in the library that can run them. A scheme
// (West Coast, Spread, ...) is a list of formations and the concepts it runs; a team's book is its scheme's, with a
// few plays of its own left out, so no two books are the same.
namespace {

bool isOLRole(const std::string& r) { return r == "LT" || r == "LG" || r == "C" || r == "RG" || r == "RT"; }

/** A formation's eligible men by their place: "S1".."S4" (strong side, outside in), "W1".."W3" (weak side), "B" (the
 *  tailback in the backfield), "F" (the fullback / second back in the backfield). */
struct Places {
  std::map<std::string, std::string> role;  // place -> role
  std::set<std::string> inlineRoles;        // tight ends attached to the line
  double backDepth = 0;                     // how deep the tailback stands (yd behind the line)
  double strong = -1;                       // +1 strong to the left (+y), -1 to the right
  bool hRole = false;                       // a tight end off the line near the formation (a wing, an H-back)
  int nStrong = 0, nWeak = 0;
  bool has(const std::string& place) const { return role.count(place) > 0; }
};

Places placesOf(const Formation& f) {
  struct M {
    std::string role;
    double lat;
    bool onLine;
  };
  Places pl;
  std::vector<M> recv;
  (void)f;
  for (const FormationSlot& s : f.slots) {
    if (isOLRole(s.role) || s.role == "QB") continue;
    const Align& a = s.align;
    const double sg = a.side == 'L' ? 1.0 : -1.0;
    double lat = 0;
    bool onLine = true;
    switch (a.kind) {
      case AlignKind::Wide: lat = sg * (FIELD_W / 2 - 9); onLine = !a.off; break;
      case AlignKind::Slot: lat = sg * (a.n == 2 ? 5.3 : a.n == 3 ? 13.0 : 9.7); onLine = false; break;
      case AlignKind::Inline: lat = sg * 4.05; onLine = !a.off; break;
      case AlignKind::Back: lat = a.dy; onLine = a.x > -1; break;
      case AlignKind::OL: break;
    }
    const bool backfield = a.kind == AlignKind::Back && a.x <= -2.5 && std::fabs(a.dy) < 2.5;
    if (backfield) {
      const bool tail = s.role == "RB" && !pl.has("B");
      // (a third back - the wishbone, the full house, the power I - is "F2")
      pl.role[tail ? "B" : pl.has("F") ? "F2" : "F"] = s.role;
      if (tail) pl.backDepth = -a.x;
      continue;
    }
    if (a.kind == AlignKind::Inline) pl.inlineRoles.insert(s.role);
    if (a.kind == AlignKind::Back && s.role.rfind("TE", 0) == 0 && std::fabs(a.dy) < 7) pl.hRole = true;
    recv.push_back(M{s.role, lat, onLine});
  }
  int nL = 0, nR = 0;
  for (const M& m : recv) (m.lat > 0 ? nL : nR)++;
  if (nL != nR) pl.strong = nL > nR ? 1 : -1;
  else {
    pl.strong = -1;
    for (const M& m : recv)
      if (pl.inlineRoles.count(m.role)) pl.strong = m.lat > 0 ? 1 : -1;
  }
  for (int side = 0; side < 2; side++) {
    const double want = side == 0 ? pl.strong : -pl.strong;
    std::vector<M> v;
    for (const M& m : recv)
      if ((m.lat > 0 ? 1.0 : -1.0) == want) v.push_back(m);
    std::stable_sort(v.begin(), v.end(), [](const M& a, const M& b) {
      if (std::fabs(std::fabs(a.lat) - std::fabs(b.lat)) > 0.05) return std::fabs(a.lat) > std::fabs(b.lat);
      return a.onLine && !b.onLine;
    });
    for (size_t i = 0; i < v.size(); i++) pl.role[std::string(side == 0 ? "S" : "W") + std::to_string(i + 1)] = v[i].role;
    (side == 0 ? pl.nStrong : pl.nWeak) = static_cast<int>(v.size());
  }
  return pl;
}

/** One route in a concept: the places that can run it, first free one wins ("S3/S2": the #3, else the #2). */
struct CEntry {
  const char* places;
  const char* route;
};
struct Concept {
  const char* id;
  const char* name;
  PlayType type;
  std::vector<CEntry> e;
  std::vector<int> reads;  // indices into e, the progression
  Strs tags;
  double drop;             // 1 quick, 2 standard, 3 deep (PA and screens set their own)
  const char* styles;      // scheme codes, "" = every scheme; "GL" = also from the goal line set
  int minStrong = 0;       // receivers needed to the strong side
  bool firstWide = false;  // the first entry must be a detached receiver (bubbles)
  const char* desc = "";
  bool maxPro = false;       // the backs and the attached tight ends stay in to block
  bool firstInline = false;  // the first entry must be an attached tight end (the TE screen)
  const char* jet = nullptr; // a place sent across in jet motion before the snap (his route runs on from there)
};

Concept Cn(const char* id, const char* name, PlayType ty, std::vector<CEntry> e, std::vector<int> reads, Strs tags, double drop,
           const char* styles, const char* desc, int minStrong = 0, bool firstWide = false) {
  Concept c{id, name, ty, std::move(e), std::move(reads), std::move(tags), drop, styles};
  c.minStrong = minStrong;
  c.firstWide = firstWide;
  c.desc = desc;
  return c;
}
Concept maxPro(Concept c) {
  c.maxPro = true;
  return c;
}
Concept firstInline(Concept c) {
  c.firstInline = true;
  return c;
}
Concept jetFrom(Concept c, const char* place) {
  c.jet = place;
  return c;
}

std::vector<std::string> splitAlts(const char* s) {
  std::vector<std::string> out;
  std::string cur;
  for (const char* c = s; *c; c++) {
    if (*c == '/') {
      out.push_back(cur);
      cur.clear();
    } else cur += *c;
  }
  out.push_back(cur);
  return out;
}

const std::vector<Concept>& CONCEPTS() {
  using PT = PlayType;
  // scheme codes: WC West Coast, SP Spread, AR Air Raid, PS Pro Style, PR Power Run, ZR Zone Run, VT Vertical,
  // RS Run and Shoot. Route depths are the receiver's break (Playbook ROUTES()): quick game 5-6, intermediate 10-14,
  // shots 15+ (coaching usage: the NFL's intermediate breaks sit at 12-15 yd).
  static const std::vector<Concept> c = {
      // ---- quick game (three-step / catch-and-throw)
      Cn("slants", "Slants", PT::Pass, {{"S1", "slant"}, {"S2", "slant"}, {"W1", "slant"}, {"S3/W2", "flat"}, {"B", "checkdown"}}, {1, 0, 2, 3, 4}, {"short", "quick"}, 1, "GL",
         "Slants across the board: throw the first one inside his man's leverage"),
      Cn("slantflat", "Slant Flat", PT::Pass, {{"S1", "slant"}, {"S2", "flat"}, {"W1", "slant"}, {"W2", "flat"}, {"S3", "seam"}, {"B", "checkdown"}}, {0, 1, 2, 3, 5}, {"short", "quick"}, 1, "WC PS PR ZR GL",
         "Slant-flat high-low on the flat defender: he sinks, hit the flat; he widens, the slant"),
      Cn("stick", "Stick", PT::Pass, {{"S2", "stick"}, {"S3/F", "flat"}, {"S1", "go"}, {"W1", "slant"}, {"W2", "stick"}, {"B", "checkdown"}}, {0, 1, 3, 4, 5}, {"short", "quick"}, 1, "WC SP AR PS RS GL",
         "The stick at six yards and the flat under it: a horizontal stretch of the curl-flat defender"),
      Cn("spacing", "Spacing", PT::Pass, {{"S2", "stick"}, {"S1", "hitch"}, {"S3", "quickout"}, {"W1", "hitch"}, {"W2", "quickout"}, {"B", "flat"}}, {0, 1, 2, 3, 4, 5}, {"short", "quick"}, 1, "WC AR RS SP",
         "Five receivers spread at five to six yards: one of the underneath zones has two men in it"),
      Cn("snag", "Snag", PT::Pass, {{"S1", "snag"}, {"S2", "corner"}, {"S3/F", "flat"}, {"W1", "slant"}, {"W2", "drag"}}, {0, 2, 1, 3, 4}, {"short"}, 2, "WC SP PS ZR",
         "Snag triangle: the spot, the corner over it and the flat - a high-low and an in-out at once"),
      Cn("bubble", "Slot Bubble", PT::Pass, {{"S2", "bubble"}, {"S1", "hitch"}, {"W1", "slant"}, {"W2", "stick"}, {"S3", "seam"}, {"B", "checkdown"}}, {0, 2, 1, 3, 5}, {"short", "quick"}, 1, "SP AR RS",
         "The slot's bubble with a hitch outside it: get the ball to the numbers' side", 2, true),
      Cn("texas", "Texas", PT::Pass, {{"B", "angle"}, {"S2", "curl"}, {"S1", "go"}, {"W1", "dig"}, {"W2/S3", "flat"}}, {0, 1, 3, 4}, {"short"}, 2, "WC PS PR ZR GL",
         "The back's angle route inside the linebacker who widens with the curl"),
      Cn("whip", "Whip Flat", PT::Pass, {{"S2", "whip"}, {"S3/F", "flat"}, {"S1", "go"}, {"W1", "slant"}, {"W2", "hitch"}}, {0, 1, 3, 4}, {"short", "quick"}, 1, "WC SP RS",
         "Whip: fake in, break out under the flat man - a man-beater"),
      Cn("quickouts", "Quick Outs", PT::Pass, {{"S1", "quickout"}, {"S2", "stick"}, {"W1", "quickout"}, {"W2", "slant"}, {"S3/F", "flat"}, {"B", "checkdown"}}, {0, 2, 1, 3, 5}, {"short", "quick"}, 1, "WC AR RS SP",
         "Five-yard outs outside, a stick inside: throw away from soft corners"),
      Cn("hitchseam", "Hitch Seams", PT::Pass, {{"S1", "hitch"}, {"S2", "seam"}, {"W1", "hitch"}, {"W2/S3", "seam"}, {"B", "checkdown"}}, {0, 2, 1, 3, 4}, {"short", "quick"}, 1, "AR SP RS VT",
         "Hitches outside, seams inside: the seams split a two-high shell, the hitches beat off coverage"),
      Cn("zigout", "Zig Out", PT::Pass, {{"S2", "zig"}, {"S1", "go"}, {"S3/F", "flat"}, {"W1", "slant"}, {"W2", "drag"}, {"B", "checkdown"}}, {0, 2, 3, 4, 5}, {"short"}, 2, "WC PS SP RS",
         "The slot zigs in and breaks back out: beats a man defender sitting on the inside"),
      Cn("jerk", "Jerk", PT::Pass, {{"S3/S2", "jerk"}, {"S1", "go"}, {"S2", "flat"}, {"W1", "curl"}, {"W2", "drag"}, {"B", "checkdown"}}, {0, 3, 2, 4, 5}, {"short"}, 2, "WC PS PR RS",
         "The inside man runs across, plants and comes back out: sits against zone, separates against man"),
      Cn("fadeout", "Fade Out", PT::Pass, {{"S1", "fade"}, {"S2", "quickout"}, {"W1", "fade"}, {"W2", "quickout"}, {"B", "checkdown"}}, {1, 3, 0, 2, 4}, {"short", "quick"}, 1, "AR SP VT GL",
         "Fades take the corners deep, the quick outs sit in the hole under them (Cover 2's corner has to choose)"),
      Cn("sticknod", "Stick Nod", PT::Pass, {{"S2", "hitchgo"}, {"S3/F", "flat"}, {"S1", "hitch"}, {"W1", "post"}, {"W2", "drag"}, {"B", "checkdown"}}, {0, 1, 2, 3, 5}, {"deep"}, 3, "VT PS RS SP",
         "The stick shown, then up the seam past the defender who jumped it"),
      // ---- intermediate (five-step / gun two-step)
      Cn("curlflat", "Curl Flat", PT::Pass, {{"S1", "curl"}, {"S2", "flat"}, {"W1", "curl"}, {"S3", "seam"}, {"W2", "arrow"}, {"B", "checkdown"}}, {0, 1, 2, 3, 4, 5}, {"mid"}, 2, "GL",
         "Curl at twelve, the flat under it: a high-low on the flat defender"),
      Cn("curls", "All Curls", PT::Pass, {{"S1", "curl"}, {"S2", "curl"}, {"W1", "curl"}, {"S3", "curl"}, {"W2", "curl"}, {"B", "flat"}}, {1, 0, 2, 3, 4, 5}, {"mid"}, 2, "WC PR RS",
         "Every receiver curls at twelve in the windows between the underneath zones"),
      Cn("smash", "Smash", PT::Pass, {{"S1", "hitch"}, {"S2", "corner"}, {"W1", "hitch"}, {"W2", "corner"}, {"S3", "seam"}, {"B", "checkdown"}}, {1, 0, 3, 2, 4, 5}, {"mid"}, 2, "",
         "Smash: the hitch under, the corner over the squatting corner - Cover 2's beater"),
      Cn("flood", "Flood", PT::Pass, {{"S2", "sail"}, {"S3/F", "flat"}, {"S1", "go"}, {"W1", "post"}, {"W2", "drag"}}, {0, 1, 3, 4}, {"mid"}, 2, "WC PS ZR SP VT GL",
         "Three levels to one side - the go, the sail at fifteen, the flat: Cover 3's flat and curl defenders outnumbered"),
      Cn("mesh", "Mesh", PT::Pass, {{"S2", "drag"}, {"W2/W1", "shallow"}, {"S1", "corner"}, {"W1", "dig"}, {"B", "swing"}, {"S3", "seam"}}, {0, 1, 4, 2, 5}, {"short"}, 2, "AR SP WC RS",
         "Two crossers rub under the linebackers: man defenders run into the traffic, zone hands them off"),
      Cn("drive", "Drive", PT::Pass, {{"S2", "shallow"}, {"S1", "dig"}, {"W1", "post"}, {"W2", "curl"}, {"S3/F", "flat"}}, {0, 1, 4, 2}, {"mid"}, 2, "WC ZR AR PS",
         "The shallow under the hook defender and the dig over him: a high-low on one linebacker"),
      Cn("ycross", "Y Cross", PT::Pass, {{"S3/S2", "cross"}, {"S1", "post"}, {"W1", "curl"}, {"W2", "shallow"}, {"S2", "flat"}, {"B", "flat"}}, {0, 2, 3, 4, 5}, {"mid", "long"}, 3, "AR SP WC PS",
         "The inside man's deep cross behind the linebackers, the post clearing the safety"),
      Cn("dagger", "Dagger", PT::Pass, {{"S1", "dig"}, {"S2", "seam"}, {"W1", "curl"}, {"W2", "shallow"}, {"S3", "flat"}, {"B", "checkdown"}}, {0, 2, 3, 4, 5}, {"mid"}, 3, "SP VT PS AR",
         "The slot's seam pulls the safety, the outside man's dig comes in under him"),
      Cn("levels", "Levels", PT::Pass, {{"S2", "in"}, {"S1", "dig"}, {"S3", "seam"}, {"W1", "go"}, {"W2", "drag"}, {"B", "checkdown"}}, {0, 1, 4, 5}, {"mid"}, 2, "WC SP RS PS",
         "Two in-breakers at six and thirteen: read the hook defender's level"),
      Cn("comebacks", "Comebacks", PT::Pass, {{"S1", "comeback"}, {"W1", "comeback"}, {"S2", "seam"}, {"W2", "drag"}, {"S3/F", "flat"}}, {0, 1, 2, 4}, {"mid", "long"}, 3, "VT PR PS RS",
         "Comebacks at sixteen against corners bailing deep"),
      Cn("dagout", "Deep Outs", PT::Pass, {{"S1", "deepout"}, {"S2", "seam"}, {"W1", "deepout"}, {"W2", "shallow"}, {"B", "checkdown"}}, {0, 2, 3, 4}, {"mid", "long"}, 3, "VT RS PS",
         "Fourteen-yard outs with the seam holding the safety"),
      Cn("slotfade", "Slot Fade", PT::Pass, {{"S2", "fade"}, {"S1", "slant"}, {"W1", "slant"}, {"W2", "flat"}, {"B", "checkdown"}}, {0, 1, 2, 3}, {"short"}, 1, "SP AR RS VT GL",
         "The slot fades over the outside slant: the corner can't take both", 2),
      Cn("shallow", "Shallow Cross", PT::Pass, {{"S2", "shallow"}, {"W1", "dig"}, {"S1", "go"}, {"W2", "curl"}, {"S3/F", "flat"}, {"B", "checkdown"}}, {0, 1, 4, 5}, {"short"}, 2, "WC AR PS ZR SP",
         "The shallow at two yards and the dig at thirteen over it: high-low the middle linebacker"),
      Cn("china", "China", PT::Pass, {{"S2", "china"}, {"S1", "in"}, {"W1", "curl"}, {"W2", "drag"}, {"B", "checkdown"}, {"S3", "seam"}}, {0, 1, 2, 4}, {"mid"}, 2, "WC PS VT",
         "The slot's slant breaks back to the corner behind the outside man's in: the flat defender jumps the in"),
      Cn("shake", "Shake", PT::Pass, {{"S2", "postcorner"}, {"S1", "dig"}, {"W1", "post"}, {"W2", "drag"}, {"S3/F", "flat"}, {"B", "checkdown"}}, {0, 1, 4, 2}, {"deep"}, 3, "PS VT AR SP",
         "Post-corner from the slot: the nod inside turns the safety, the break goes to the sideline"),
      Cn("hilo", "Hi-Lo Crossers", PT::Pass, {{"S2", "under"}, {"W2/W1", "shallow"}, {"S1", "go"}, {"W1", "post"}, {"B", "checkdown"}}, {1, 0, 3, 4}, {"short"}, 2, "WC SP AR RS",
         "Crossers at two and seven yards from opposite sides: one of them comes open past the hook defender"),
      Cn("bench", "Bench", PT::Pass, {{"S2", "bench"}, {"S1", "go"}, {"S3/F", "flat"}, {"W1", "dig"}, {"W2", "drag"}, {"B", "checkdown"}}, {0, 2, 3, 5}, {"mid"}, 2, "SP AR VT PS",
         "The slot runs to the sideline at eleven under the vertical: the out into the hole of a Cover 3"),
      Cn("divide", "Divide", PT::Pass, {{"S2", "seam"}, {"W2/S3", "seam"}, {"S1", "curl"}, {"W1", "curl"}, {"B", "checkdown"}}, {0, 1, 2, 3, 4}, {"mid"}, 3, "AR SP VT",
         "Two seams split the single safety; the outside men curl under the bailing corners"),
      Cn("deepover", "Deep Over", PT::Pass, {{"W1", "deepcross"}, {"S1", "go"}, {"S2", "dig"}, {"W2", "drag"}, {"S3/F", "flat"}, {"B", "checkdown"}}, {0, 2, 4, 5}, {"deep"}, 3, "PS VT ZR WC",
         "The backside receiver's deep over route across the field behind the linebackers"),
      Cn("dblpost", "Double Posts", PT::Pass, {{"S1", "post"}, {"S2", "deeppost"}, {"W1", "dig"}, {"W2", "drag"}, {"B", "checkdown"}}, {1, 0, 2, 3}, {"deep"}, 3, "VT PS PR",
         "Two posts at different depths: the middle safety can't stay over both"),
      Cn("meshwheel", "Mesh Wheel", PT::Pass, {{"S2", "drag"}, {"W2/W1", "shallow"}, {"S1", "corner"}, {"W1", "dig"}, {"B", "wheel"}, {"S3", "seam"}}, {0, 1, 4, 2}, {"short"}, 2, "AR SP RS",
         "Mesh with the back wheeling up the sideline behind the linebacker chasing the crossers"),
      Cn("meshspot", "Mesh Spot", PT::Pass, {{"S2", "drag"}, {"W2/W1", "shallow"}, {"S1", "snag"}, {"S3/F", "flat"}, {"B", "checkdown"}}, {0, 1, 2, 3, 4}, {"short"}, 2, "AR SP",
         "The mesh crossers under, the spot route sitting over them at six"),
      Cn("wheeldig", "Dig Wheel", PT::Pass, {{"S2", "wheel"}, {"S1", "dig"}, {"W1", "post"}, {"W2", "drag"}, {"B", "checkdown"}}, {0, 1, 2, 3}, {"deep"}, 3, "VT SP PS RS",
         "The slot wheels outside the outside man's dig: the corner can't play both"),
      Cn("ycorner", "Y Corner", PT::Pass, {{"S3/S2", "corner"}, {"S1", "dig"}, {"F/S2", "flat"}, {"W1", "post"}, {"W2", "drag"}, {"B", "checkdown"}}, {0, 2, 1, 3}, {"mid"}, 2, "PS WC PR ZR",
         "The inside man's corner over the flat: a two-man high-low on the flat defender"),
      Cn("circle", "HB Circle", PT::Pass, {{"B", "circle"}, {"S1", "go"}, {"S2", "out"}, {"W1", "dig"}, {"W2", "seam"}}, {0, 3, 2}, {"short"}, 2, "WC PS AR",
         "The back's circle route under the linebackers while the receivers clear them out"),
      // ---- shots
      Cn("4verts", "Four Verticals", PT::Pass, {{"S1", "go"}, {"S2", "seam"}, {"W1", "go"}, {"W2/S3", "seam"}, {"B", "checkdown"}}, {1, 3, 0, 2, 4}, {"deep"}, 3, "SP AR VT RS PS",
         "Four verticals: more deep routes than the defense has deep defenders"),
      Cn("hbwheel", "Verticals HB Wheel", PT::Pass, {{"S2", "seam"}, {"B", "wheel"}, {"S1", "go"}, {"W1", "go"}, {"W2", "seam"}}, {0, 1, 3, 2}, {"deep"}, 3, "SP VT AR",
         "Verticals with the back's wheel up the sideline under them"),
      Cn("mills", "Mills", PT::Pass, {{"S1", "post"}, {"S2", "dig"}, {"W1", "go"}, {"W2", "drag"}, {"B", "checkdown"}}, {0, 1, 3, 4}, {"deep"}, 3, "VT PS ZR SP",
         "Post over dig: the dig holds the safety down, the post goes over him"),
      Cn("postwheel", "Post Wheel", PT::Pass, {{"S1", "post"}, {"S2", "wheel"}, {"W1", "out"}, {"W2", "dig"}, {"B", "checkdown"}}, {0, 1, 3, 2, 4}, {"deep"}, 3, "VT SP PS",
         "The outside post takes the corner inside, the slot wheels up the sideline behind him"),
      Cn("scissors", "Scissors", PT::Pass, {{"S1", "post"}, {"S2", "corner"}, {"W1", "dig"}, {"W2", "drag"}, {"B", "checkdown"}}, {1, 0, 2, 4}, {"deep"}, 3, "VT PS PR ZR",
         "Post and corner crossing: quarters' safety and corner each have to pass one off"),
      Cn("doublemove", "Double Moves", PT::Pass, {{"S1", "outnup"}, {"W1", "slantgo"}, {"S2", "hitch"}, {"W2", "drag"}, {"B", "checkdown"}}, {0, 1, 2, 3, 4}, {"deep"}, 3, "VT RS PR",
         "Out-and-up and sluggo outside: double moves against corners squatting on the short game"),
      Cn("skinny", "Skinny Posts", PT::Pass, {{"S1", "skinny"}, {"W1", "skinny"}, {"S2", "out"}, {"W2", "curl"}, {"B", "checkdown"}}, {0, 1, 2, 3, 4}, {"deep"}, 3, "VT AR SP",
         "Skinny posts outside into the void between the corner and the safety"),
      maxPro(Cn("mpshot", "Max Protect Shot", PT::Pass, {{"S1", "post"}, {"W1", "comeback"}, {"S2", "dig"}}, {2, 0, 1}, {"deep"}, 3, "VT PS PR ZR",
         "Seven or eight in to block, the post over a dig and a comeback: time for the long ball against the blitz")),
      // ---- play action (the back fakes and stays in)
      Cn("pa_boot", "PA Boot Over", PT::PlayAction, {{"S1", "cross"}, {"S2", "flat"}, {"W1", "post"}, {"W2", "corner"}, {"F", "flat"}, {"B", "block"}}, {1, 0, 3, 2, 4}, {"mid", "playaction"}, 0, "WC PS PR ZR GL",
         "Zone fake one way, the quarterback boots the other: the flat, the crosser coming across, the post"),
      Cn("pa_shot", "PA Deep Post", PT::PlayAction, {{"W1", "post"}, {"S1", "dig"}, {"S2", "seam"}, {"W2", "go"}, {"B", "block"}}, {0, 1, 2, 3}, {"deep", "playaction"}, 0, "PR PS VT ZR",
         "The run fake brings the safety down; the post goes over his head"),
      Cn("pa_cross", "PA Crossers", PT::PlayAction, {{"S1", "cross"}, {"W1", "post"}, {"S2", "flat"}, {"F", "flat"}, {"W2", "drag"}, {"B", "block"}}, {0, 1, 2, 3, 4}, {"mid", "playaction"}, 0, "WC ZR PS PR",
         "Play action crossers: the linebackers step up for the fake and the cross goes behind them"),
      Cn("pa_yankee", "PA Yankee", PT::PlayAction, {{"S1", "post"}, {"W1", "deepcross"}, {"S2", "flat"}, {"F", "flat"}, {"B", "block"}}, {0, 1, 2, 3}, {"deep", "playaction"}, 0, "VT ZR PS PR",
         "Yankee: the post and the deep over from the other side, two-man shot off the run fake"),
      Cn("pa_leak", "PA Leak", PT::PlayAction, {{"F/S3", "cross"}, {"S1", "post"}, {"W1", "go"}, {"S2", "flat"}, {"B", "block"}}, {0, 1, 2, 3}, {"mid", "playaction"}, 0, "PS PR ZR WC",
         "The tight end or fullback shows his block on the fake, then leaks across behind the flow"),
      Cn("pa_flood", "PA Flood", PT::PlayAction, {{"S2", "sail"}, {"S3/F", "flat"}, {"S1", "go"}, {"W1", "post"}, {"B", "block"}}, {0, 1, 3}, {"mid", "playaction"}, 0, "WC PS ZR PR",
         "Flood off the run fake: go, sail and flat against a defense stepping up"),
      Cn("pa_mills", "PA Mills", PT::PlayAction, {{"S1", "deeppost"}, {"S2", "dig"}, {"W1", "go"}, {"W2", "drag"}, {"B", "block"}}, {0, 1, 2, 3}, {"deep", "playaction"}, 0, "VT PS ZR PR",
         "Post over the dig behind a run fake: the safety bites, the post is behind him"),
      Cn("pa_dagger", "PA Dagger", PT::PlayAction, {{"S1", "dig"}, {"S2", "seam"}, {"W1", "curl"}, {"W2", "drag"}, {"B", "block"}}, {1, 0, 2, 3}, {"mid", "playaction"}, 0, "SP VT PS",
         "Dagger off play action: the seam and the dig behind linebackers filling for the run"),
      Cn("pa_slants", "PA Slants", PT::PlayAction, {{"S1", "slant"}, {"W1", "slant"}, {"S2", "flat"}, {"W2", "slant"}, {"B", "block"}}, {0, 1, 2, 3}, {"short", "playaction"}, 1, "WC PR ZR PS",
         "A quick run fake and slants into the space the linebackers vacate"),
      Cn("pa_comeback", "PA Comebacks", PT::PlayAction, {{"S1", "comeback"}, {"W1", "comeback"}, {"S2", "seam"}, {"B", "block"}}, {0, 1, 2}, {"mid", "playaction"}, 0, "VT PS PR",
         "Comebacks off the fake against corners bailing for the shot"),
      maxPro(Cn("pa_maxpost", "PA Max Protect Post", PT::PlayAction, {{"W1", "post"}, {"S1", "dig"}}, {0, 1}, {"deep", "playaction"}, 0, "VT PR PS ZR",
         "Everyone in on the fake, two receivers out: the post over the dig, the shot play")),
      Cn("pa_bootsail", "PA Boot Sail", PT::PlayAction, {{"S2", "sail"}, {"S3/F", "flat"}, {"S1", "go"}, {"W1", "cross"}, {"B", "block"}}, {0, 1, 3, 2}, {"mid", "playaction"}, 0, "WC PS ZR PR",
         "The bootleg with the sail and the flat on the side he rolls to, the backside crosser coming to him"),
      jetFrom(Cn("pa_jet", "PA Jet Fake", PT::PlayAction, {{"S1", "jetflat"}, {"W1", "dig"}, {"S2", "post"}, {"W2", "drag"}, {"B", "block"}}, {0, 1, 2, 3}, {"mid", "playaction"}, 0, "SP ZR WC PS",
         "Jet motion across, the handoff faked to him, then to the back: he runs on to the flat, the dig and post behind"), "S1"),
      // ---- screens
      Cn("hbscreen", "HB Screen", PT::Screen, {{"B", "screen"}, {"S1", "go"}, {"S2", "in"}, {"W1", "go"}, {"W2", "in"}}, {0}, {"screen", "short"}, 0, "",
         "The line lets the rush by and releases in front of the back"),
      Cn("tunnel", "WR Tunnel Screen", PT::Screen, {{"S1", "tunnel"}, {"S2", "stalk"}, {"S3", "stalk"}, {"W1", "go"}, {"W2", "slant"}}, {0}, {"screen", "short", "quick"}, 0, "SP AR RS WC VT",
         "The outside man steps up and comes back inside behind the line; the line releases to lead him", 2, true),
      Cn("bubblescr", "Bubble Screen", PT::Screen, {{"S2", "bubble"}, {"S1", "stalk"}, {"S3", "stalk"}, {"W1", "go"}, {"W2", "slant"}, {"B", "checkdown"}}, {0}, {"screen", "short", "quick"}, 0, "SP AR RS ZR",
         "The slot bubbles out behind the outside receiver's stalk block: a long handoff", 2, true),
      firstInline(Cn("tescreen", "TE Screen", PT::Screen, {{"S2/S3/W2/W1", "slip"}, {"S1", "go"}, {"W1", "go"}, {"W2", "in"}, {"B", "checkdown"}}, {0}, {"screen", "short"}, 0, "WC PS ZR PR",
         "The tight end shows his block, then slips out behind the rush to the flat")),
      Cn("hbslip", "HB Slip Screen", PT::Screen, {{"B", "slip"}, {"S1", "go"}, {"S2", "in"}, {"W1", "go"}, {"W2", "in"}}, {0}, {"screen", "short"}, 0, "SP WC AR PS",
         "The back checks his block and slips out under the rush"),
  };
  return c;
}

/** "GL" in a concept's styles lets the goal line set run it; it says nothing about the schemes. */
bool fromGoalLine(const char* styles) { return styles && std::strstr(styles, "GL") != nullptr; }
/** Whether a scheme (its code) runs a concept with these styles: no codes but "GL" = every scheme. */
bool styleIn(const char* styles, const char* code) {
  std::string s = styles ? styles : "";
  for (size_t at; (at = s.find("GL")) != std::string::npos;) s.erase(at, 2);
  if (s.find_first_not_of(' ') == std::string::npos) return true;
  return s.find(code) != std::string::npos;
}

bool isGoalSet(const Formation& f) { return f.personnel == "22" || f.personnel == "23"; }
bool isWildcat(const Formation& f) { return f.family == "Wildcat"; }
bool threeBacks(const Formation& f) { return f.personnel == "31"; }

/** Fits a concept to a formation; false if it doesn't fit (a route with nobody to run it, too few to one side...). */
bool fitConcept(const Formation& f, const Places& pl, const Concept& c, OffPlay& out) {
  if (pl.nStrong < c.minStrong) return false;
  const bool needB = c.type == PlayType::PlayAction || (c.type == PlayType::Screen && std::string(c.e[0].places) == "B");
  if (needB && !pl.has("B")) return false;
  if (c.type == PlayType::PlayAction && !pl.has("B")) return false;
  std::set<std::string> used;
  std::vector<std::string> entryRole(c.e.size());
  Routes routes;
  for (size_t i = 0; i < c.e.size(); i++) {
    for (const std::string& alt : splitAlts(c.e[i].places)) {
      if (!pl.has(alt) || used.count(alt)) continue;
      // (an attached tight end does not run a screen's stalk or a tunnel; the TE screen is his alone)
      if (c.firstInline && i == 0 && !pl.inlineRoles.count(pl.role.at(alt))) continue;
      used.insert(alt);
      entryRole[i] = pl.role.at(alt);
      routes[entryRole[i]] = c.e[i].route;
      break;
    }
    if (i == 0 && entryRole[0].empty()) return false;  // the concept's first route is its point
  }
  if (c.firstWide && pl.inlineRoles.count(entryRole[0])) return false;
  if (c.firstInline && !pl.inlineRoles.count(entryRole[0])) return false;
  // a tailback seven deep (under center, the pistol) checks his block and releases to a spot past the line (his
  // gun checkdown and flat would be caught behind it); only the concepts built on him run his own routes. A
  // fullback the concept leaves out checks and releases to the flat past the line.
  const std::string cid = c.id;
  const bool backConcept = cid == "texas" || cid == "hbwheel" || cid == "hbscreen" || cid == "meshwheel" || cid == "circle" || cid == "hbslip";
  if (pl.backDepth >= 6 && !backConcept && pl.has("B")) {
    auto it = routes.find(pl.role.at("B"));
    if (it != routes.end() && (it->second == "checkdown" || it->second == "swing")) it->second = "release";
    else if (it != routes.end() && it->second == "flat") it->second = "releaseflat";
  }
  // anyone the concept left out: wide men clear deep, attached tight ends stay in, backs check down (max protect: the
  // backs and tight ends all stay in)
  for (const auto& kv : pl.role) {
    if (used.count(kv.first)) continue;
    const std::string& r = kv.second;
    std::string rt;
    const bool back = kv.first == "B" || kv.first == "F" || kv.first == "F2";
    if (c.maxPro && (back || pl.inlineRoles.count(r) || r.rfind("TE", 0) == 0)) rt = "block";
    else if (kv.first == "B") rt = c.type == PlayType::PlayAction ? "block" : pl.backDepth >= 6 ? "release" : "checkdown";
    else if (kv.first == "F") rt = "releaseflat";
    else if (kv.first == "F2") rt = "block";
    else if (pl.inlineRoles.count(r)) rt = "block";
    else rt = kv.first == "S1" || kv.first == "W1" ? "go" : "seam";
    routes[r] = rt;
  }
  if (c.maxPro)
    for (const auto& kv : pl.role)
      if ((kv.first == "B" || kv.first == "F" || kv.first == "F2") && used.count(kv.first) == 0) routes[kv.second] = "block";
  auto blocks = [&](const std::string& rt) {
    const RouteDef* rd = routeByName(rt);
    return !rd || rd->block;
  };
  Strs reads;
  auto addRead = [&](const std::string& r) {
    if (r.empty() || blocks(routes[r]) || std::find(reads.begin(), reads.end(), r) != reads.end()) return;
    reads.push_back(r);
  };
  for (int i : c.reads)
    if (i >= 0 && i < static_cast<int>(entryRole.size())) addRead(entryRole[i]);
  for (const auto& kv : routes) addRead(kv.first);
  if (reads.size() < (c.type == PlayType::Screen ? 1u : 2u)) return false;
  if (c.type == PlayType::Screen) reads.resize(1);
  out = OffPlay();
  out.id = f.id + "__" + c.id;
  out.name = c.name;
  out.formation = f.id;
  out.type = c.type;
  out.routes = std::move(routes);
  out.reads = std::move(reads);
  out.tags = c.tags;
  out.styles = c.styles;
  out.conceptName = c.name;
  out.desc = c.desc;
  if (c.jet) {
    // the jet man: a detached receiver on the strong side, across toward the weak side before the snap
    if (!pl.has(c.jet)) return false;
    const std::string jr = pl.role.at(c.jet);
    if (pl.inlineRoles.count(jr)) return false;
    PlayMotion mo;
    mo.role = jr;
    mo.dy = -pl.strong;
    mo.jet = true;
    out.motion = mo;
  }
  const bool underCenter = f.qbDepth <= 1.5;
  // (play action's drop behind the fake: the full one, or a quick game's - the slants off a quick fake - when the concept
  // gives its own)
  if (c.type == PlayType::PlayAction) out.dropDepth = c.drop > 0 ? (underCenter ? c.drop + 1.5 : c.drop) : underCenter ? 6.0 : 3.0;
  else if (c.type == PlayType::Screen) out.dropDepth = c.e[0].route == std::string("tunnel") || c.e[0].route == std::string("bubble") ? 1.0 : 4.0;
  else out.dropDepth = (underCenter && c.drop == 2) ? 5.0 : c.drop;
  return true;
}

struct RunConcept {
  const char* id;
  const char* name;
  RunStyle style;
  double aim;           // yards from the center, + = to the formation's strength
  const char* carrier;  // "B" (the tailback), "F" (the fullback), "QB", or a receiver's place (a jet man)
  int from;             // 0 any, 1 under center only, 2 gun / pistol only, 3 gun only
  bool needFB, needTE;
  Strs tags;
  const char* styles;
  const char* desc = "";
  std::vector<BlockJob> jobs;
  bool down = false;
  const char* read = "";
  const char* rpoPlace = nullptr;
  const char* rpoRoute = nullptr;
  const char* jet = nullptr;      // the jet man's place (the carrier, or with fakeJet a decoy)
  bool fakeJet = false;
  const char* reverse = nullptr;  // the reverse man's place
  bool needH = false;             // an H-back (a tight end off the line) or a fullback
  bool wildcat = false;           // the wildcat set's own runs (and only they run from it)
};

RunConcept Rn(const char* id, const char* name, RunStyle st, double aim, const char* carrier, int from, bool needFB, bool needTE, Strs tags,
              const char* styles, const char* desc, std::vector<BlockJob> jobs = {}, bool down = false) {
  RunConcept r{id, name, st, aim, carrier, from, needFB, needTE, std::move(tags), styles};
  r.desc = desc;
  r.jobs = std::move(jobs);
  r.down = down;
  return r;
}
RunConcept withRead(RunConcept r, const char* read) {
  r.read = read;
  return r;
}
RunConcept rpo(RunConcept r, const char* place, const char* route) {
  r.read = "rpo";
  r.rpoPlace = place;
  r.rpoRoute = route;
  return r;
}
RunConcept jetOf(RunConcept r, const char* place, bool fake) {
  r.jet = place;
  r.fakeJet = fake;
  return r;
}
RunConcept needH(RunConcept r) {
  r.needH = true;
  return r;
}
RunConcept wildcatRun(RunConcept r) {
  r.wildcat = true;
  return r;
}
RunConcept reverseBy(RunConcept r, const char* place) {
  r.reverse = place;
  return r;
}

const std::vector<RunConcept>& RUNS() {
  using RSt = RunStyle;
  // jobs: PG / BG / PT / BT play-side and back-side linemen, H the H-back (off-line tight end or fullback)
  static const std::vector<RunConcept> r = {
      Rn("iz", "Inside Zone", RSt::Zone, 0.7, "B", 0, false, false, {"run"}, "", "Zone steps to the play side, the back reads the first down lineman past the center: bang, bend or bounce"),
      Rn("izw", "Inside Zone Weak", RSt::Zone, -0.7, "B", 0, false, false, {"run"}, "ZR PS SP WC", "Inside zone away from the tight end"),
      Rn("oz", "Outside Zone", RSt::Stretch, 4.5, "B", 0, false, false, {"run"}, "ZR SP WC PS", "Reach blocks along the line; the back presses the edge and cuts up where it opens"),
      Rn("ozw", "Outside Zone Weak", RSt::Stretch, -4.5, "B", 0, false, false, {"run"}, "ZR WC", "Outside zone to the open side"),
      Rn("power", "Power", RSt::Gap, 2.0, "B", 0, false, false, {"run", "shortyard"}, "PR PS WC VT ZR SP",
         "The back-side guard pulls up through the hole for the linebacker, the H kicks out the end; the line blocks the men in front of it",
         {{"BG", "wrap"}, {"H", "kick"}}, true),
      Rn("counter", "Counter GT", RSt::Gap, -2.6, "B", 0, false, false, {"run"}, "PR PS SP VT RS",
         "The back steps one way and comes back: the back-side guard kicks out the end, the tackle wraps through for the linebacker",
         {{"BG", "kick"}, {"BT", "wrap"}}, true),
      Rn("dive", "HB Dive", RSt::Gap, -0.7, "B", 1, false, false, {"run", "shortyard"}, "PR PS WC VT", "Quick-hitting dive straight downhill at the guard"),
      Rn("iso", "Iso", RSt::Gap, -0.7, "B", 1, true, false, {"run", "shortyard"}, "PR PS WC VT", "The fullback isolates the middle linebacker in the hole",
         {{"FB", "lead"}}),
      Rn("toss", "HB Toss", RSt::Stretch, 6.0, "B", 1, false, false, {"run"}, "PR PS WC VT ZR", "The pitch to the back already running for the edge"),
      Rn("stretch", "HB Stretch", RSt::Stretch, 5.0, "B", 1, false, false, {"run"}, "ZR PS WC", "Stretch to the edge behind reach blocks"),
      Rn("draw", "HB Draw", RSt::Draw, -0.6, "B", 2, false, false, {"run", "long"}, "SP AR RS VT PS WC", "Show pass, let the rush up the field, hand it under them"),
      Rn("duo", "Duo", RSt::Gap, 0.7, "B", 0, false, true, {"run", "shortyard"}, "PR PS ZR", "Man blocking straight ahead, doubles on the down linemen: the back reads the linebacker"),
      Rn("fbdive", "FB Dive", RSt::Gap, 0.4, "F", 1, true, false, {"run", "shortyard"}, "PR PS", "The fullback hits the A gap before the line has moved"),
      Rn("sneak", "QB Sneak", RSt::Sneak, 0.6, "QB", 1, false, true, {"run", "shortyard"}, "GL", "The quarterback follows the center for the yard"),
      Rn("trap", "Trap", RSt::Gap, 0.8, "B", 0, false, false, {"run"}, "PR PS WC VT ZR",
         "The play-side tackle is let through on purpose and trapped from the side by the back-side guard; the back hits right behind it",
         {{"BG", "trap"}}, true),
      needH(Rn("wham", "Wham", RSt::Gap, 0.6, "B", 0, false, false, {"run"}, "PR PS ZR WC",
         "The H-back comes across and traps the defensive tackle the line released", {{"H", "trap"}}, true)),
      Rn("tosscrack", "Toss Crack", RSt::Stretch, 6.5, "B", 1, false, false, {"run"}, "PR PS VT ZR",
         "The toss with the receivers cracking inside and the play-side guard pulling around for the corner", {{"PG", "lead"}}),
      Rn("pinpull", "Pin Pull Sweep", RSt::Stretch, 5.5, "B", 1, false, false, {"run"}, "ZR WC PS SP",
         "Covered linemen pin their men inside, the uncovered guard pulls around the edge to lead", {{"PG", "lead"}}),
      Rn("buck", "Buck Sweep", RSt::Stretch, 5.0, "B", 1, false, false, {"run"}, "PR WC PS",
         "Both guards pull: the play-side guard kicks out the force player, the back-side guard turns up for the linebacker",
         {{"PG", "kick"}, {"BG", "wrap"}}, true),
      Rn("dart", "Dart", RSt::Gap, -0.9, "B", 0, false, false, {"run"}, "SP ZR PS RS",
         "Zone blocking with the back-side tackle pulling through the hole for the linebacker", {{"BT", "wrap"}}, true),
      needH(Rn("splitzone", "Split Zone", RSt::Zone, 0.7, "B", 0, false, false, {"run"}, "ZR SP PS WC",
         "Inside zone with the H-back crossing the formation to kick out the back-side end", {{"H", "arc"}})),
      Rn("lead", "HB Lead", RSt::Gap, 2.2, "B", 1, true, false, {"run", "shortyard"}, "PR PS VT WC",
         "The fullback leads off tackle for the linebacker, the line base-blocking the men over it", {{"FB", "lead"}}),
      Rn("qbdraw", "QB Draw", RSt::Draw, 0.5, "QB", 3, false, false, {"run", "long"}, "SP AR RS",
         "The quarterback shows his drop, the rush goes by, he runs it up the middle behind the back's block", {{"RB", "lead"}}),
      Rn("qbpower", "QB Power", RSt::Gap, 2.0, "QB", 3, false, false, {"run", "shortyard"}, "SP PS RS",
         "Power with the quarterback carrying: the back kicks out the end, the back-side guard wraps - an extra blocker at the point",
         {{"BG", "wrap"}, {"RB", "kick"}}, true),
      Rn("qbcounter", "QB Counter", RSt::Gap, -2.6, "QB", 3, false, false, {"run"}, "SP RS AR",
         "Counter GT for the quarterback: the back fakes one way, guard and tackle pull the other",
         {{"BG", "kick"}, {"BT", "wrap"}, {"RB", "lead"}}, true),
      withRead(Rn("zoneread", "Zone Read", RSt::Zone, 0.7, "B", 2, false, false, {"run"}, "SP ZR RS AR",
         "Inside zone with the back-side end left unblocked: he crashes on the back, the quarterback keeps it around him"), "end"),
      jetOf(Rn("jetsweep", "Jet Sweep", RSt::Stretch, 8.0, "W1/W2", 0, false, false, {"run"}, "SP ZR WC PS RS",
         "The receiver in jet motion takes it at full speed across the formation, the line reaching to the play side"), "W1/W2", false),
      jetOf(Rn("izjet", "Inside Zone Jet Fake", RSt::Zone, -0.7, "B", 2, false, false, {"run"}, "SP ZR WC",
         "Jet motion across to hold the back side, the ball given to the back on inside zone the other way"), "S1/S2", true),
      reverseBy(Rn("reverse", "WR Reverse", RSt::Stretch, 5.0, "B", 0, false, false, {"run", "trick"}, "SP WC RS VT ZR",
         "The back starts the sweep, the receiver coming back the other way takes it from him: the pursuit is going the wrong way"), "S1"),
      wildcatRun(Rn("wc_power", "Wildcat Power", RSt::Gap, 2.0, "QB", 2, false, false, {"run", "shortyard"}, "",
         "Power from the direct snap: the back-side guard wraps and the offset back leads", {{"BG", "wrap"}, {"RB", "lead"}}, true)),
      wildcatRun(Rn("wc_zone", "Wildcat Zone", RSt::Zone, -0.7, "QB", 2, false, false, {"run"}, "",
         "Inside zone on the direct snap: every man blocked, the runner reads it himself")),
      wildcatRun(Rn("wc_sweep", "Wildcat Sweep", RSt::Stretch, 6.0, "B", 2, false, false, {"run"}, "",
         "The direct snap and the handoff to the back sweeping behind it")),
      wildcatRun(Rn("wc_counter", "Wildcat Counter", RSt::Gap, -2.6, "QB", 2, false, false, {"run"}, "",
         "Counter GT on the direct snap: the guard kicks out, the tackle wraps", {{"BG", "kick"}, {"BT", "wrap"}}, true)),
      wildcatRun(Rn("wc_trap", "Wildcat Trap", RSt::Gap, 0.8, "QB", 2, false, false, {"run"}, "",
         "The direct snap and the trap behind the back-side guard", {{"BG", "trap"}}, true)),
      wildcatRun(jetOf(Rn("wc_jet", "Wildcat Jet Sweep", RSt::Stretch, 8.0, "W1/W2", 2, false, false, {"run"}, "",
         "The receiver in jet motion takes it from the direct-snap back at full speed"), "W1/W2", false)),
      // the RPOs: a run called with a quick route the quarterback throws when the conflict defender plays the run
      rpo(Rn("rpo_bubble", "Zone Bubble RPO", RSt::Zone, -0.7, "B", 2, false, false, {"run", "rpo"}, "SP AR ZR RS",
         "Inside zone with the slot's bubble: the quarterback throws it if the overhang defender steps into the box"), "S2", "bubble"),
      rpo(Rn("rpo_slant", "Zone Slant RPO", RSt::Zone, 0.7, "B", 2, false, false, {"run", "rpo"}, "SP ZR WC",
         "Inside zone with the back-side slant: thrown behind a linebacker who triggers on the run"), "W1", "slant"),
      rpo(Rn("rpo_glance", "Glance RPO", RSt::Zone, -0.7, "B", 2, false, false, {"run", "rpo"}, "SP AR VT",
         "Inside zone with the outside receiver's glance behind a safety who comes down for the run"), "S1", "glance"),
      rpo(Rn("rpo_stick", "Stick RPO", RSt::Zone, 0.7, "B", 2, false, false, {"run", "rpo"}, "SP WC RS",
         "Inside zone with the slot's stick in the curl window"), "S2", "stick"),
      rpo(Rn("rpo_pop", "Power Pop RPO", RSt::Gap, 2.0, "B", 2, false, false, {"run", "rpo"}, "SP PS ZR",
         "Power with the inside receiver popping up the seam behind the linebacker filling for the pull", {{"BG", "wrap"}}, true), "S3/S2", "seam"),
      rpo(Rn("rpo_hitch", "Zone Hitch RPO", RSt::Zone, -0.7, "B", 2, false, false, {"run", "rpo"}, "SP AR RS",
         "Inside zone with the outside hitch against an off corner"), "S1", "hitch"),
  };
  return r;
}

bool fitRun(const Formation& f, const Places& pl, const RunConcept& rc, const std::vector<OffPlay>& have, OffPlay& out) {
  const bool underCenter = f.qbDepth <= 1.5;
  if (rc.from == 1 && !underCenter) return false;
  if (rc.from == 2 && underCenter) return false;
  if (rc.from == 3 && f.qbDepth < 4.5) return false;
  if (rc.wildcat != isWildcat(f)) return false;
  const bool hasFB = pl.has("F") && pl.role.at("F") == "FB";
  if (rc.needFB && !hasFB) return false;
  if (rc.needTE && pl.inlineRoles.empty()) return false;
  if (rc.needH && !pl.hRole && !hasFB) return false;
  // (split zone's arc is a tight end's, from the wing or the H: a fullback crossing in the I runs through the back's path)
  if (std::string(rc.id) == "splitzone" && !pl.hRole) return false;
  std::string carrier = rc.carrier;
  std::string jetRole;
  if (rc.jet) {
    // the jet man: the first of the places there, detached; he crosses from his side to the strong side (a decoy jet
    // from the strong side across to the weak)
    for (const std::string& alt : splitAlts(rc.jet))
      if (pl.has(alt) && !pl.inlineRoles.count(pl.role.at(alt)) && (alt[0] == 'S' || alt[0] == 'W')) {
        jetRole = pl.role.at(alt);
        break;
      }
    if (jetRole.empty()) return false;
    if (!rc.fakeJet) carrier = jetRole;
  }
  if (carrier == "B" || carrier == "F") {
    if (!pl.has(carrier)) return false;
    carrier = pl.role.at(carrier);
  }
  if (!pl.has("B")) return false;  // an empty set hands off to nobody
  // a QB sneak is a short-yardage call: the heavy sets only
  if (rc.style == RunStyle::Sneak && !isGoalSet(f)) return false;
  // three-back sets keep to downhill football: the gap runs (no zone or stretch path from three abreast)
  if (threeBacks(f) && rc.style != RunStyle::Gap) return false;
  const double aim = rc.aim * pl.strong;
  // (and an offset back carries to his own side: across, his path runs through the fullback in front of him)
  if (threeBacks(f))
    for (const FormationSlot& fs : f.slots)
      if (fs.role == carrier && fs.align.kind == AlignKind::Back && std::fabs(fs.align.dy) > 1.0 && (fs.align.dy > 0) != (aim > 0)) return false;
  // the formation's own stock run of the same kind stands in for it
  for (const OffPlay& o : have)
    if (o.formation == f.id && o.run && o.run->style == rc.style && o.run->carrier == carrier &&
        std::string(rc.read).empty() && !rc.jet && !rc.reverse && (o.run->aimDy > 0) == (aim > 0) && std::fabs(std::fabs(o.run->aimDy) - std::fabs(aim)) < 1.0)
      return false;
  RunScheme run;
  run.aimDy = aim;
  run.style = rc.style;
  run.carrier = carrier;
  run.jobs = rc.jobs;
  run.down = rc.down;
  run.read = rc.read;
  run.jet = rc.jet && !rc.fakeJet;
  const double qd = f.qbDepth;
  switch (rc.style) {
    case RunStyle::Zone: run.handoffDelay = qd >= 4.5 ? 0.65 : qd >= 3 ? 0.75 : 0.85; break;
    case RunStyle::Gap: run.handoffDelay = carrier == "FB" ? 0.6 : qd >= 4.5 ? 0.75 : qd >= 3 ? 0.8 : (rc.aim < -1 ? 1.0 : 0.9); break;
    case RunStyle::Stretch: run.handoffDelay = std::string(rc.id) == "toss" ? 0.55 : qd >= 4.5 ? 0.7 : qd >= 3 ? 0.75 : 0.8; break;
    case RunStyle::Draw: run.handoffDelay = 0.9; break;
    case RunStyle::Sneak: run.handoffDelay = 0; break;
  }
  if (carrier == "QB") run.handoffDelay = 0;
  out = OffPlay();
  out.id = f.id + "__" + rc.id;
  out.name = (std::string(rc.id) == "power" && hasFB) ? "Power O" : rc.name;
  out.formation = f.id;
  out.type = PlayType::Run;
  out.routes = {{"X", "block"}, {"Z", "block"}, {"SLOT", "block"}, {"SLOT2", "block"}, {"TE", "block"}, {"TE2", "block"}, {"TE3", "block"},
                {"FB", "block"}, {"RB2", "block"}};
  if (rc.rpoPlace) {
    std::string rr;
    for (const std::string& alt : splitAlts(rc.rpoPlace))
      if (pl.has(alt) && pl.role.at(alt) != carrier && (alt[0] == 'S' || alt[0] == 'W')) {
        rr = pl.role.at(alt);
        break;
      }
    if (rr.empty()) return false;
    // (a bubble or a hitch from an attached tight end is not an RPO anyone runs)
    if (pl.inlineRoles.count(rr) && std::string(rc.rpoRoute) != "seam") return false;
    out.routes[rr] = rc.rpoRoute;
    out.reads = {rr};
    run.rpo = rr;
  }
  // (a reverse wants the backfield clear: a fullback in front of the tailback's sweep path held up the first exchange)
  if ((rc.reverse || std::string(rc.id) == "buck") && hasFB) return false;  // (buck: both guards pulling past a fullback too)
  if (rc.reverse) {
    std::string rv;
    for (const std::string& alt : splitAlts(rc.reverse))
      if (pl.has(alt) && !pl.inlineRoles.count(pl.role.at(alt))) {
        rv = pl.role.at(alt);
        break;
      }
    if (rv.empty()) return false;
    out.reverse = rv;
  }
  if (!jetRole.empty()) {
    PlayMotion mo;
    mo.role = jetRole;
    mo.dy = rc.fakeJet ? -pl.strong : pl.strong;
    // (a carrying jet man from the weak side runs to the strength: his place must be on the weak side)
    if (!rc.fakeJet) {
      bool weak = false;
      for (const auto& kv : pl.role)
        if (kv.second == jetRole && kv.first[0] == 'W') weak = true;
      if (!weak) return false;
    }
    mo.jet = true;
    out.motion = mo;
  }
  out.run = run;
  out.tags = rc.tags;
  if (isGoalSet(f)) out.tags.push_back("goalline");
  out.styles = rc.styles;
  out.conceptName = rc.name;
  out.desc = rc.desc;
  return true;
}

std::vector<OffPlay> generatedPlays(const std::vector<OffPlay>& stock) {
  std::vector<OffPlay> out;
  for (const Formation& f : FORMATIONS()) {
    const Places pl = placesOf(f);
    const bool goalSet = isGoalSet(f);
    auto haveName = [&](const std::string& n) {
      for (const OffPlay& o : stock)
        if (o.formation == f.id && o.name == n) return true;
      for (const OffPlay& o : out)
        if (o.formation == f.id && o.name == n) return true;
      return false;
    };
    if (!isWildcat(f))
      for (const Concept& c : CONCEPTS()) {
        if (goalSet && !fromGoalLine(c.styles)) continue;  // the goal line set keeps to its short game
        // the three-back sets throw off play action and their quick game only
        if (threeBacks(f) && c.type != PlayType::PlayAction && !fromGoalLine(c.styles)) continue;
        if (haveName(c.name)) continue;
        OffPlay p;
        if (fitConcept(f, pl, c, p)) out.push_back(std::move(p));
      }
    for (const RunConcept& rc : RUNS()) {
      if (haveName(rc.name)) continue;
      OffPlay p;
      if (fitRun(f, pl, rc, stock, p)) out.push_back(std::move(p));
    }
  }
  return out;
}

// ---------------- defense
struct SetDef {
  const char* id;
  const char* name;
  Front front;
  FrontAlign align;
};

/** The defensive sets. The stock calls are each front's "Normal" set. */
const std::vector<SetDef>& SETS() {
  static const std::vector<SetDef> s = {
      {"base", "4-3 Normal", Front::Base, FrontAlign::Normal},
      {"over", "4-3 Over", Front::Base, FrontAlign::Over},
      {"under", "4-3 Under", Front::Base, FrontAlign::Under},
      {"wide9", "4-3 Wide 9", Front::Base, FrontAlign::Wide9},
      {"bear43", "4-3 Bear", Front::Base, FrontAlign::Bear},
      {"odd", "3-4 Odd", Front::Base34, FrontAlign::Normal},
      {"odd_over", "3-4 Over", Front::Base34, FrontAlign::Over},
      {"bear", "3-4 Bear", Front::Base34, FrontAlign::Bear},
      {"okie", "3-4 Okie", Front::Base34, FrontAlign::Okie},
      {"tite", "3-4 Tite", Front::Base34, FrontAlign::Tite},
      {"nk", "Nickel Normal", Front::Nickel, FrontAlign::Normal},
      {"nk_wide9", "Nickel Wide 9", Front::Nickel, FrontAlign::Wide9},
      {"nk_over", "Nickel Over", Front::Nickel, FrontAlign::Over},
      {"nk_under", "Nickel Under", Front::Nickel, FrontAlign::Under},
      {"nk33", "Nickel 3-3-5", Front::Nickel33, FrontAlign::Normal},
      {"nk33_tite", "Nickel 3-3-5 Tite", Front::Nickel33, FrontAlign::Tite},
      {"nk24", "Nickel 2-4-5", Front::Nickel24, FrontAlign::Normal},
      {"dime", "Dime Normal", Front::Dime, FrontAlign::Normal},
      {"dime_wide9", "Dime Wide 9", Front::Dime, FrontAlign::Wide9},
      {"dime32", "Dime 3-2-6", Front::Dime32, FrontAlign::Normal},
      {"gl", "Goal Line", Front::Goalline, FrontAlign::Normal},
  };
  return s;
}

struct CallTpl {
  const char* id;
  const char* name;
  Coverage cov;
  Strs blitz;
  Strs tags;
  Strs drop;
  CoverTech tech = CoverTech::None;
  bool rotate = false;
  bool mug = false;
};
CallTpl Tpl(const char* id, const char* name, Coverage cov, Strs blitz, Strs tags, Strs drop = {}, CoverTech tech = CoverTech::None, bool rotate = false,
          bool mug = false) {
  CallTpl c{id, name, cov, std::move(blitz), std::move(tags), std::move(drop)};
  c.tech = tech;
  c.rotate = rotate;
  c.mug = mug;
  return c;
}

/** The men a front sends on every snap besides its down linemen (a 3-4's edge backer, the 2-4-5's two edges). */
Strs baseRush(Front f) {
  switch (f) {
    case Front::Base34: return {"LOLB"};
    case Front::Nickel24: return {"LOLB", "ROLB"};
    case Front::Nickel33: return {"SLB"};
    case Front::Dime32: return {"WLB"};
    default: return {};
  }
}

/** The coverage shells every front plays (its base rush in front of them). */
std::vector<CallTpl> shellsFor(Front f) {
  using Cv = Coverage;
  using Tq = CoverTech;
  const Strs b = baseRush(f);
  const bool dime = f == Front::Dime || f == Front::Dime32;
  const bool sub = f == Front::Nickel || f == Front::Nickel33 || f == Front::Nickel24 || dime;
  const Strs base = dime ? Strs{"long", "pass"} : sub ? Strs{"pass"} : Strs{"base"};
  auto tg = [&](Strs extra) {
    Strs t = base;
    for (auto& x : extra) t.push_back(x);
    return t;
  };
  std::vector<CallTpl> v = {
      Tpl("c1", "Cover 1", Cv::C1, b, tg({})),
      Tpl("c1r", "Cover 1 Robber", Cv::C1, b, tg({"run"}), {}, Tq::Robber),
      Tpl("c1s", "Cover 1 Spy", Cv::C1, b, tg({}), {}, Tq::Spy),
      Tpl("c1d", "Cover 1 Disguise", Cv::C1, b, tg({}), {}, Tq::None, true),
      Tpl("c2", "Cover 2 Zone", Cv::C2, b, tg({})),
      Tpl("c2m", "2 Man Under", Cv::C2M, b, tg({"long"})),
      Tpl("c2i", "Cover 2 Invert", Cv::C2, b, tg({}), {}, Tq::Invert),
      Tpl("c3", "Cover 3 Sky", Cv::C3, b, tg({"run"})),
      Tpl("c3b", "Cover 3 Buzz", Cv::C3, b, tg({"run"}), {}, Tq::Buzz),
      Tpl("c3c", "Cover 3 Cloud", Cv::C3, b, tg({}), {}, Tq::Cloud),
      Tpl("c3r", "Cover 3 Rotate", Cv::C3, b, tg({}), {}, Tq::None, true),
      Tpl("c4", "Cover 4 Quarters", Cv::C4, b, tg({"pass"})),
      Tpl("c4d", "Cover 4 Disguise", Cv::C4, b, tg({"pass"}), {}, Tq::None, true),
      Tpl("c6", "Cover 6", Cv::C6, b, tg({"pass"})),
  };
  if (!dime && f != Front::Nickel24) v.push_back(Tpl("c2t", "Tampa 2", Cv::C2T, b, tg({"pass"})));
  if (sub) v.push_back(Tpl("c1b", "Cover 1 Bracket", Cv::C1, b, tg({}), {}, Tq::Bracket));
  if (dime) {
    v.push_back(Tpl("prev", "Prevent", Cv::C4, b, {"long", "prevent"}, {}, Tq::Prevent));
    v.push_back(Tpl("prev3", "Prevent 3 Deep", Cv::C3, b, {"long", "prevent"}, {}, Tq::Prevent));
  }
  return v;
}

/** The pressures of a front (role names are the front's own; drop = down linemen who drop into coverage). */
std::vector<CallTpl> pressuresFor(Front f) {
  using Cv = Coverage;
  const Strs bl = {"blitz"};
  switch (f) {
    case Front::Base:
      return {
          Tpl("mike", "Mike Blitz", Cv::C1, {"MLB"}, {"blitz", "run"}),
          Tpl("will", "Will Blitz", Cv::C1, {"WLB"}, bl),
          Tpl("sam3", "Sam Fire 3", Cv::C3Fire, {"SLB"}, {"blitz", "run"}),
          Tpl("ss3", "Strong Fire", Cv::C3Fire, {"SS"}, bl),
          Tpl("dbla", "Double A Gap", Cv::C1, {"MLB", "WLB"}, {"blitz", "shortyard"}),
          Tpl("c0", "Cover 0 Blitz", Cv::C0, {"MLB", "WLB"}, {"blitz", "shortyard"}),
          Tpl("firesm", "Sam Mike Fire Zone", Cv::C3Fire, {"SLB", "MLB"}, bl, {"RE"}),
          Tpl("firewm", "Will Mike Fire Zone", Cv::C3Fire, {"WLB", "MLB"}, bl, {"LE"}),
          Tpl("tdrop", "Tackle Drop Fire", Cv::C3Fire, {"MLB", "WLB"}, bl, {"LDT"}),
          Tpl("creep", "Mike Creeper", Cv::C3, {"MLB"}, {"pass"}, {"RDT"}),
          Tpl("screep", "Safety Creeper", Cv::C3, {"SS"}, {"pass"}, {"RE"}),
          Tpl("cbfire", "Corner Fire", Cv::C3Fire, {"LCB"}, bl),
          Tpl("ssblitz", "Safety Blitz", Cv::C1, {"SS"}, bl),
          Tpl("overload", "Strong Overload", Cv::C0, {"SLB", "SS"}, {"blitz", "shortyard"}),
          Tpl("mugdbl", "Mug Double A", Cv::C1, {"MLB", "WLB"}, {"blitz", "shortyard"}, {}, CoverTech::None, false, true),
          Tpl("mugbluff", "Mug Bluff", Cv::C3, {}, {"pass"}, {}, CoverTech::None, false, true),
          Tpl("mugwill", "Mug Will", Cv::C1, {"WLB"}, bl, {}, CoverTech::None, false, true),
      };
    case Front::Goalline:
      return {
          Tpl("c3", "Goal Line Cover 3", Cv::C3, {}, {"goalline", "shortyard", "run"}),
          Tpl("c2", "Goal Line Cover 2", Cv::C2, {}, {"goalline", "shortyard"}),
          Tpl("jam", "Goal Line Jam", Cv::C0, {"SLB", "WLB", "MLB"}, {"goalline", "shortyard", "run", "blitz"}),
          Tpl("fire", "Goal Line Fire", Cv::C3Fire, {"MLB"}, {"goalline", "shortyard", "run", "blitz"}),
          Tpl("c2m", "Goal Line 2 Man", Cv::C2M, {}, {"goalline", "shortyard"}),
          Tpl("c1r", "Goal Line Robber", Cv::C1, {}, {"goalline", "shortyard", "run"}, {}, CoverTech::Robber),
      };
    case Front::Base34:
      return {
          Tpl("drop8", "Drop 8", Cv::C3, {}, {"long", "prevent"}),
          Tpl("olb", "OLB Fire", Cv::C3Fire, {"LOLB", "ROLB"}, bl),
          Tpl("will", "Will Fire", Cv::C3Fire, {"LOLB", "WLB"}, {"blitz", "run"}),
          Tpl("inside", "Inside Blitz", Cv::C1, {"LOLB", "MLB"}, {"blitz", "run"}),
          Tpl("dbla", "Double A Gap", Cv::C1, {"LOLB", "WLB", "MLB"}, {"blitz", "shortyard"}),
          Tpl("c0", "Cover 0 Blitz", Cv::C0, {"LOLB", "ROLB", "MLB"}, {"blitz", "shortyard"}),
          Tpl("firezone", "Field Fire Zone", Cv::C3Fire, {"LOLB", "ROLB", "WLB"}, bl, {"LE"}),
          Tpl("nose", "Nose Drop Fire", Cv::C3Fire, {"LOLB", "ROLB", "MLB"}, bl, {"LDT"}),
          Tpl("sim", "Mike Sim Pressure", Cv::C3, {"LOLB", "MLB"}, {"pass"}, {"RE"}),
          Tpl("creep", "Will Creeper", Cv::C3, {"LOLB", "WLB"}, {"pass"}, {"LE"}),
          Tpl("ssfire", "Strong Fire", Cv::C3Fire, {"LOLB", "SS"}, bl),
          Tpl("cbfire", "Corner Fire", Cv::C3Fire, {"LOLB", "LCB"}, bl),
          Tpl("mugdbl", "Mug Double A", Cv::C0, {"LOLB", "WLB", "MLB"}, {"blitz", "shortyard"}, {}, CoverTech::None, false, true),
          Tpl("mugbluff", "Mug Bluff", Cv::C3, {"LOLB"}, {"pass"}, {}, CoverTech::None, false, true),
          Tpl("mugwill", "Mug Will", Cv::C1, {"LOLB", "WLB"}, bl, {}, CoverTech::None, false, true),
      };
    case Front::Nickel:
      return {
          Tpl("nb", "Nickel Blitz", Cv::C3Fire, {"NB"}, {"blitz", "pass"}),
          Tpl("mike", "Mike Blitz", Cv::C1, {"MLB"}, bl),
          Tpl("will3", "Will Fire", Cv::C3Fire, {"WLB"}, bl),
          Tpl("ss", "Safety Blitz", Cv::C1, {"SS"}, {"blitz", "pass"}),
          Tpl("dbla", "Double A Gap", Cv::C1, {"MLB", "WLB"}, {"blitz", "shortyard"}),
          Tpl("c0", "Cover 0 Blitz", Cv::C0, {"WLB", "MLB"}, {"blitz", "pass", "shortyard"}),
          Tpl("firenb", "Nickel Fire Zone", Cv::C3Fire, {"NB", "MLB"}, bl, {"LE"}),
          Tpl("creep", "Mike Creeper", Cv::C3, {"MLB"}, {"pass"}, {"RDT"}),
          Tpl("nbsim", "Nickel Sim", Cv::C3, {"NB"}, {"pass"}, {"RE"}),
          Tpl("cbfire", "Corner Fire", Cv::C3Fire, {"LCB"}, bl),
          Tpl("overload", "Nickel Overload", Cv::C0, {"NB", "SS"}, {"blitz", "pass"}),
          Tpl("mugdbl", "Mug Double A", Cv::C1, {"MLB", "WLB"}, {"blitz", "shortyard"}, {}, CoverTech::None, false, true),
          Tpl("mugbluff", "Mug Bluff", Cv::C3, {}, {"pass"}, {}, CoverTech::None, false, true),
          Tpl("mugsim", "Mug Sim", Cv::C3, {"MLB"}, {"pass"}, {"LDT"}, CoverTech::None, false, true),
      };
    case Front::Nickel33:
      return {
          Tpl("mike", "Mike Blitz", Cv::C1, {"SLB", "MLB"}, bl),
          Tpl("will", "Will Fire", Cv::C3Fire, {"SLB", "WLB"}, bl),
          Tpl("nb", "Nickel Blitz", Cv::C3Fire, {"SLB", "NB"}, {"blitz", "pass"}),
          Tpl("stack", "Stack Fire Zone", Cv::C3Fire, {"SLB", "MLB", "WLB"}, bl, {"LE"}),
          Tpl("drop8", "Drop 8", Cv::C3, {}, {"long", "prevent"}),
          Tpl("c0", "Cover 0 Blitz", Cv::C0, {"SLB", "MLB", "WLB"}, {"blitz", "shortyard"}),
          Tpl("creep", "Mike Creeper", Cv::C3, {"SLB", "MLB"}, {"pass"}, {"LDT"}),
          Tpl("ssfire", "Strong Fire", Cv::C3Fire, {"SLB", "SS"}, bl),
          Tpl("mugbluff", "Mug Bluff", Cv::C3, {"SLB"}, {"pass"}, {}, CoverTech::None, false, true),
          Tpl("mugdbl", "Mug Double A", Cv::C0, {"SLB", "MLB", "WLB"}, {"blitz", "shortyard"}, {}, CoverTech::None, false, true),
      };
    case Front::Nickel24:
      return {
          Tpl("mike", "Mike Blitz", Cv::C1, {"LOLB", "ROLB", "MLB"}, bl),
          Tpl("will", "Will Fire", Cv::C3Fire, {"LOLB", "ROLB", "WLB"}, bl),
          Tpl("nb", "Nickel Blitz", Cv::C3Fire, {"LOLB", "ROLB", "NB"}, {"blitz", "pass"}),
          Tpl("edgedrop", "Edge Drop Fire", Cv::C3Fire, {"LOLB", "MLB", "WLB"}, bl),
          Tpl("sim", "Sim Pressure", Cv::C3, {"LOLB", "MLB"}, {"pass"}),
          Tpl("dbla", "Double A Gap", Cv::C0, {"LOLB", "ROLB", "MLB", "WLB"}, {"blitz", "shortyard"}),
          Tpl("cbfire", "Corner Fire", Cv::C3Fire, {"LOLB", "ROLB", "LCB"}, bl),
          Tpl("mugbluff", "Mug Bluff", Cv::C3, {"LOLB", "ROLB"}, {"pass"}, {}, CoverTech::None, false, true),
          Tpl("mugsim", "Mug Sim", Cv::C3, {"LOLB", "MLB"}, {"pass"}, {}, CoverTech::None, false, true),
      };
    case Front::Dime:
      return {
          Tpl("db", "Dime Blitz", Cv::C3Fire, {"DB"}, {"blitz", "pass"}),
          Tpl("mike", "Mike Blitz", Cv::C1, {"MLB"}, {"blitz", "pass"}),
          Tpl("nb", "Nickel Blitz", Cv::C1, {"NB"}, {"blitz", "pass"}),
          Tpl("dbfire", "Dime Fire Zone", Cv::C3Fire, {"DB", "MLB"}, bl, {"LE"}),
          Tpl("creep", "Dime Creeper", Cv::C3, {"DB"}, {"pass", "long"}, {"RDT"}),
          Tpl("c0", "Cover 0 Blitz", Cv::C0, {"MLB", "NB"}, {"blitz", "pass"}),
          Tpl("cbfire", "Corner Fire", Cv::C3Fire, {"LCB"}, bl),
          Tpl("ssfire", "Safety Fire", Cv::C3Fire, {"SS"}, bl),
      };
    case Front::Dime32:
      return {
          Tpl("mike", "Mike Blitz", Cv::C1, {"WLB", "MLB"}, {"blitz", "pass"}),
          Tpl("db", "Dime Blitz", Cv::C3Fire, {"WLB", "DB"}, {"blitz", "pass"}),
          Tpl("drop8", "Drop 8", Cv::C3, {}, {"long", "prevent"}),
          Tpl("creep", "Dime Creeper", Cv::C3, {"WLB", "NB"}, {"pass", "long"}, {"LE"}),
          Tpl("c0", "Cover 0 Blitz", Cv::C0, {"WLB", "MLB", "NB"}, {"blitz", "pass"}),
      };
  }
  return {};
}

std::vector<CallTpl> callsFor(Front f) {
  if (f == Front::Goalline) return pressuresFor(f);
  std::vector<CallTpl> v = shellsFor(f);
  for (CallTpl& p : pressuresFor(f)) v.push_back(std::move(p));
  return v;
}

bool sameCall(const DefCall& a, const DefCall& b) {
  if (a.front != b.front || a.coverage != b.coverage || a.blitz.size() != b.blitz.size() || a.tech != b.tech || a.rotate != b.rotate ||
      a.mug != b.mug || a.drop.size() != b.drop.size())
    return false;
  for (const std::string& r : a.blitz)
    if (std::find(b.blitz.begin(), b.blitz.end(), r) == b.blitz.end()) return false;
  return true;
}

std::vector<DefCall> generatedCalls(const std::vector<DefCall>& stock) {
  std::vector<DefCall> out;
  for (const SetDef& s : SETS()) {
    for (const CallTpl& t : callsFor(s.front)) {
      DefCall d;
      d.id = std::string(s.id) + "__" + t.id;
      d.front = s.front;
      d.coverage = t.cov;
      d.blitz = t.blitz;
      d.tags = t.tags;
      d.set = s.name;
      d.align = s.align;
      d.drop = t.drop;
      d.tech = t.tech;
      d.rotate = t.rotate;
      d.mug = t.mug;
      // the call's name carries its set where the set is not the front's normal look
      const bool plainSet = s.align == FrontAlign::Normal && (s.front == Front::Base || s.front == Front::Nickel || s.front == Front::Dime);
      d.name = plainSet ? std::string(frontFamily(s.front)) + " " + t.name : std::string(s.name) + " " + t.name;
      if (s.front == Front::Goalline) d.name = t.name;
      bool dup = false;
      if (s.align == FrontAlign::Normal)
        for (const DefCall& o : stock) dup = dup || sameCall(o, d);
      if (!dup) out.push_back(std::move(d));
    }
  }
  return out;
}

const char* coverDesc(const DefCall& d) {
  switch (d.tech) {
    case CoverTech::Robber: return "Man coverage, the free safety deep and the strong safety robbing the crossers at nine yards";
    case CoverTech::Spy: return "Man coverage with the Mike spying the quarterback: no free scramble lanes";
    case CoverTech::Buzz: return "Three deep; the strong safety buzzes to the hook, the outside backer runs to the flat";
    case CoverTech::Cloud: return "Three deep with the strong corner squatting in the flat and the safety rotating over him";
    case CoverTech::Invert: return "Two deep played by the corners; the safeties come down to the flats";
    case CoverTech::Prevent: return "Everyone deep and soft: give up the short throw, nothing over the top";
    case CoverTech::Bracket: return "Man coverage with the strong safety doubling the best receiver to the strength";
    case CoverTech::None: break;
  }
  switch (d.coverage) {
    case Coverage::C0: return "All-out man, nobody deep: everyone else is coming";
    case Coverage::C1: return d.rotate ? "Shows two high, rotates to man free at the snap" : "Man across the board with a free safety in the middle";
    case Coverage::C2: return "Two deep halves, five underneath; the corners squat on the flats";
    case Coverage::C3: return d.rotate ? "Shows two high, spins to three deep at the snap" : "Three deep thirds, four underneath; the strong safety rolls down";
    case Coverage::C4: return d.rotate ? "Shows a single safety, bails to quarters at the snap" : "Four deep quarters, the safeties reading the slots: strong against the run and the deep ball";
    case Coverage::C2M: return "Two deep safeties over man coverage underneath, trailing technique";
    case Coverage::C3Fire: return "Five rush with three deep and three under behind it";
    case Coverage::C2T: return "Tampa 2: two deep halves with the Mike running the deep middle hole";
    case Coverage::C6: return "Quarter-quarter-half: quarters to one side, Cover 2 to the other";
  }
  return "";
}

std::string coverLabelOf(const DefCall& d) {
  static const char* names[] = {"Cover 0", "Cover 1", "Cover 2", "Cover 3", "Cover 4", "2 Man", "Cover 3 Fire", "Tampa 2", "Cover 6"};
  std::string s = names[static_cast<int>(d.coverage)];
  switch (d.tech) {
    case CoverTech::Robber: s += " Robber"; break;
    case CoverTech::Spy: s += " Spy"; break;
    case CoverTech::Buzz: s += " Buzz"; break;
    case CoverTech::Cloud: s += " Cloud"; break;
    case CoverTech::Invert: s += " Invert"; break;
    case CoverTech::Prevent: s += " Prevent"; break;
    case CoverTech::Bracket: s += " Bracket"; break;
    case CoverTech::None: if (d.coverage == Coverage::C3 && !d.rotate) s += " Sky"; break;
  }
  if (d.rotate) s += " (Rotate)";
  return s;
}

std::string pressureOf(const DefCall& d) {
  if (d.tech == CoverTech::Prevent) return "Prevent";
  if (d.mug) return "Mug";
  const Strs b = baseRush(d.front);
  bool extra = false, db = false;
  for (const std::string& r : d.blitz) {
    if (std::find(b.begin(), b.end(), r) != b.end()) continue;
    extra = true;
    db = db || r == "LCB" || r == "RCB" || r == "NB" || r == "DB" || r == "SS" || r == "FS";
  }
  // (a front's own edge rushers dropping - the 2-4-5's edge drop, a 3-4 dropping its outside backer - is a pressure too)
  for (const std::string& r : b)
    if (std::find(d.blitz.begin(), d.blitz.end(), r) == d.blitz.end()) extra = true;
  if (!extra && d.drop.empty()) return d.rushers() <= 3 ? "Drop 8" : "Base";
  if (db) return "DB Blitz";
  if (d.rushers() <= 4) return "Sim Pressure";
  const bool man = d.coverage == Coverage::C0 || d.coverage == Coverage::C1 || d.coverage == Coverage::C2M;
  return man ? "Man Blitz" : "Zone Blitz";
}

// ---------------- schemes and books
struct OffScheme {
  const char* name;
  const char* code;
  std::vector<const char*> formations;
};
const std::vector<OffScheme>& OFF_SCHEME_DEFS() {
  // each scheme's sets in screen order (about 30 a book, as a team's game plan carries)
  static const std::vector<OffScheme> s = {
      {"West Coast", "WC", {"gun_doubles", "gun_trips", "gun_bunch", "gun_tightflex", "gun_trey", "gun_wing_trips", "gun_snugs", "gun_split", "gun_dbl_stack",
                            "pistol", "pistol_wing", "singleback", "sb_doubles", "sb_bunch", "sb_wing", "sb_tight_slots", "sb_trips_te", "sb_y_flex", "sb_pair_twins",
                            "iform", "i_twin", "i_close", "i_tight", "strong_i", "weak_i", "splitback", "power_i", "heavy", "gl_jumbo"}},
      {"Spread", "SP", {"gun_doubles", "gun_trips", "gun_trey", "gun_bunch", "gun_spread", "gun_tightflex", "gun_empty", "gun_empty_trey", "gun_empty_bunch",
                        "gun_quads", "gun_snugs", "gun_split", "gun_split_slot", "gun_dbl_stack", "gun_wing_trips", "gun_ace", "wildcat", "pistol", "pistol_trips",
                        "pistol_bunch", "pistol_wing", "pistol_strong", "sb_doubles", "sb_trips", "sb_tight_slots", "heavy", "gl_jumbo"}},
      {"Air Raid", "AR", {"gun_spread", "gun_doubles", "gun_trey", "gun_trips", "gun_bunch", "gun_tightflex", "gun_empty", "gun_empty_trey", "gun_empty_bunch",
                          "gun_quads", "gun_dbl_stack", "gun_snugs", "gun_split", "gun_split_slot", "gun_wing_trips", "gun_ace", "pistol", "pistol_trips",
                          "pistol_bunch", "sb_doubles", "sb_trips", "sb_bunch", "heavy", "gl_jumbo"}},
      {"Pro Style", "PS", {"singleback", "sb_doubles", "sb_trips", "sb_bunch", "sb_wing", "sb_jumbo", "sb_tight_slots", "sb_trips_te", "sb_y_flex", "sb_pair_twins",
                           "iform", "i_tight", "i_twin", "i_close", "strong_i", "strong_close", "weak_i", "power_i", "gun_doubles", "gun_trips", "gun_ace",
                           "gun_bunch", "gun_wing_trips", "gun_empty_trey", "gun_heavy", "pistol", "pistol_wing", "pistol_jumbo", "splitback", "heavy", "gl_jumbo"}},
      {"Power Run", "PR", {"iform", "i_tight", "i_twin", "i_close", "strong_i", "strong_close", "weak_i", "power_i", "wishbone", "full_house", "splitback",
                           "singleback", "sb_wing", "sb_jumbo", "sb_doubles", "sb_pair_twins", "sb_y_flex", "pistol", "pistol_ace", "pistol_strong",
                           "pistol_jumbo", "pistol_wing", "gun_ace", "gun_heavy", "gun_doubles", "gun_wing_trips", "wildcat", "heavy", "gl_jumbo"}},
      {"Zone Run", "ZR", {"singleback", "sb_doubles", "sb_bunch", "sb_wing", "sb_trips", "sb_tight_slots", "sb_jumbo", "sb_y_flex", "sb_pair_twins", "iform",
                          "i_twin", "i_close", "weak_i", "pistol", "pistol_ace", "pistol_wing", "pistol_bunch", "pistol_jumbo", "gun_doubles", "gun_tightflex",
                          "gun_bunch", "gun_trips", "gun_snugs", "gun_wing_trips", "gun_split_slot", "gun_heavy", "heavy", "gl_jumbo"}},
      {"Vertical", "VT", {"singleback", "sb_doubles", "sb_trips", "sb_wing", "sb_y_flex", "iform", "i_slot", "i_close", "strong_i", "power_i", "gun_doubles",
                          "gun_trips", "gun_spread", "gun_trey", "gun_empty", "gun_empty_trey", "gun_quads", "gun_dbl_stack", "gun_bunch", "gun_ace",
                          "pistol", "pistol_trips", "pistol_wing", "splitback", "heavy", "gl_jumbo"}},
      {"Run and Shoot", "RS", {"gun_spread", "gun_doubles", "gun_trey", "gun_empty", "gun_empty_bunch", "gun_quads", "gun_trips", "gun_bunch", "gun_dbl_stack",
                               "gun_snugs", "gun_split_slot", "gun_tightflex", "pistol", "pistol_trips", "pistol_bunch", "sb_doubles", "sb_trips", "sb_tight_slots",
                               "i_slot", "wildcat", "heavy", "gl_jumbo"}},
  };
  return s;
}

struct DefScheme {
  const char* name;
  Front base;
  std::vector<const char*> sets;  // SETS ids
};
const std::vector<DefScheme>& DEF_SCHEME_DEFS() {
  static const std::vector<DefScheme> s = {
      {"4-3 Cover 3", Front::Base, {"base", "over", "under", "wide9", "nk", "nk_over", "nk_wide9", "nk33", "dime", "dime_wide9", "gl"}},
      {"Tampa 2", Front::Base, {"base", "over", "under", "bear43", "nk", "nk_over", "nk_under", "dime", "dime32", "gl"}},
      {"4-3 Quarters", Front::Base, {"base", "over", "wide9", "bear43", "nk", "nk_wide9", "nk_under", "nk24", "dime", "gl"}},
      {"3-4 Multiple", Front::Base34, {"odd", "odd_over", "okie", "tite", "bear", "nk", "nk24", "nk33", "dime", "dime32", "gl"}},
      {"3-4 Pressure", Front::Base34, {"odd", "bear", "okie", "tite", "nk", "nk_wide9", "nk24", "nk33", "nk33_tite", "dime", "gl"}},
  };
  return s;
}

std::string roleWords(const std::string& r) {
  if (r == "MLB") return "the Mike";
  if (r == "WLB") return "the Will";
  if (r == "SLB") return "the Sam";
  if (r == "LOLB" || r == "ROLB") return "an edge backer";
  if (r == "NB") return "the nickel";
  if (r == "DB") return "the dime back";
  if (r == "SS") return "the strong safety";
  if (r == "FS") return "the free safety";
  if (r == "LCB" || r == "RCB") return "a corner";
  if (r == "LE" || r == "RE") return "an end";
  return "a tackle";
}

void fillPlayInfo(std::vector<OffPlay>& v) {
  std::map<std::string, std::string> desc;
  for (const Concept& c : CONCEPTS()) desc[c.name] = c.desc;
  for (const RunConcept& r : RUNS()) desc[r.name] = r.desc;
  desc["Power O"] = desc["Power"];
  static const std::map<std::string, std::string> stock = {
      {"Spread Verts", "Verticals from empty with the comeback outside: five deep threats"},
      {"PA Boot", "Run fake one way, the quarterback boots out the other: flat, crosser and post"},
      {"Double Slants", "Two slants to one side: the inside one clears the hook defender"},
      {"Stretch", "Stretch to the edge behind reach blocks"},
      {"PA Shot", "Play action with two tight ends kept in: the post and the dig deep"},
      {"PA TE Flat", "Goal line play action: the tight end leaks to the flat behind the fake"},
      {"QB Kneel", "The quarterback takes a knee"},
      {"Spike", "The ball spiked to stop the clock"},
  };
  for (OffPlay& p : v) {
    const Formation* f = formationById(p.formation);
    p.personnel = f ? f->personnel : "";
    switch (p.type) {
      case PlayType::Run: p.cat = p.run && !p.run->rpo.empty() ? PlayCat::RPO : !p.reverse.empty() ? PlayCat::Trick : PlayCat::Run; break;
      case PlayType::Pass: p.cat = PlayCat::Pass; break;
      case PlayType::PlayAction: p.cat = PlayCat::PlayAction; break;
      case PlayType::Screen: p.cat = PlayCat::Screen; break;
      case PlayType::Kneel: p.cat = PlayCat::Kneel; break;
      case PlayType::Spike: p.cat = PlayCat::Spike; break;
    }
    if (p.hasTag("trick")) p.cat = PlayCat::Trick;
    if (p.conceptName.empty()) p.conceptName = p.name;
    if (p.desc.empty()) {
      auto it = desc.find(p.name);
      auto st = stock.find(p.name);
      p.desc = it != desc.end() ? it->second : st != stock.end() ? st->second : std::string();
    }
  }
}

void fillCallInfo(std::vector<DefCall>& v) {
  for (DefCall& d : v) {
    d.frontLabel = defSet(d);
    d.coverLabel = coverLabelOf(d);
    d.pressure = pressureOf(d);
    std::string s = coverDesc(d);
    if (d.pressure != "Base" && d.pressure != "Prevent" && d.pressure != "Drop 8") {
      const Strs b = baseRush(d.front);
      std::string who;
      for (const std::string& r : d.blitz) {
        if (std::find(b.begin(), b.end(), r) != b.end()) continue;
        who += (who.empty() ? "" : " and ") + roleWords(r);
      }
      s += ". " + d.pressure + (who.empty() ? "" : ": " + who + " coming");
      if (!d.drop.empty()) s += ", " + roleWords(d.drop[0]) + " dropping into coverage";
      if (d.mug) s += (who.empty() ? ": " : ", ") + std::string("the backers walked up into the A gaps");
    } else if (d.pressure == "Drop 8") s += ". Three rush, eight in coverage";
    s += " (" + std::to_string(d.rushers()) + " rush)";
    d.desc = s;
  }
}

const OffScheme& offSchemeDef(const std::string& name) {
  for (const OffScheme& s : OFF_SCHEME_DEFS())
    if (name == s.name) return s;
  return OFF_SCHEME_DEFS()[0];
}
const DefScheme& defSchemeDef(const std::string& name) {
  for (const DefScheme& s : DEF_SCHEME_DEFS())
    if (name == s.name) return s;
  return DEF_SCHEME_DEFS()[0];
}

uint32_t fnv(const std::string& s, uint32_t h = 2166136261u) {
  for (unsigned char c : s) {
    h ^= c;
    h *= 16777619u;
  }
  return h;
}

OffBook buildOffBook(const OffScheme& sc, const std::string& id, const std::string& name, bool vary) {
  OffBook b;
  b.id = id;
  b.name = name;
  b.scheme = sc.name;
  // a team's book is a game plan, not the whole library: per set the scheme's plays a set carries in a real book (a
  // dozen to sixteen - runs, RPOs, dropbacks, play action, screens in proportion to the set), each concept spread
  // across the sets (the one the book has least of goes in first), the team's own pick of the rest by a hash of its id
  std::map<std::string, int> used;
  for (const char* fid : sc.formations) {
    const Formation* fm = formationById(fid);
    if (!fm) continue;
    std::vector<const OffPlay*> in;
    for (const OffPlay& p : OFF_PLAYS())
      if (p.formation == fid && p.type != PlayType::Kneel && p.type != PlayType::Spike && (p.stock || styleIn(p.styles.c_str(), sc.code)))
        in.push_back(&p);
    if (vary && !isGoalSet(*fm) && !isWildcat(*fm)) {
      // quotas: run, RPO, pass, play action, screen, trick
      const bool under = fm->qbDepth <= 1.5, gun = fm->qbDepth >= 4.5;
      int q[6] = {5, 0, 4, 3, 1, 1};
      if (gun) q[0] = 3, q[1] = 2, q[2] = 7, q[3] = 1, q[4] = 2;
      else if (!under) q[0] = 4, q[1] = 1, q[2] = 5, q[3] = 2, q[4] = 1;
      if (threeBacks(*fm)) q[0] = 6, q[2] = 1, q[3] = 3, q[4] = 0;
      // (each team's own: a play more or less here and there)
      const uint32_t h = fnv(id + "#" + fid);
      q[2] += static_cast<int>(h % 3) - 1;
      q[0] += static_cast<int>((h / 3) % 2);
      auto group = [](const OffPlay* p) {
        switch (p->cat) {
          case PlayCat::Run: return 0;
          case PlayCat::RPO: return 1;
          case PlayCat::Pass: return 2;
          case PlayCat::PlayAction: return 3;
          case PlayCat::Screen: return 4;
          default: return 5;
        }
      };
      std::vector<const OffPlay*> kept;
      for (int g = 0; g < 6; g++) {
        std::vector<const OffPlay*> c;
        for (const OffPlay* p : in)
          if (group(p) == g) c.push_back(p);
        std::stable_sort(c.begin(), c.end(), [&](const OffPlay* a, const OffPlay* b2) {
          if (a->stock != b2->stock) return a->stock;  // (the stock plays stay)
          const int ua = used[a->conceptName], ub = used[b2->conceptName];
          if (ua != ub) return ua < ub;
          const bool ea = !a->styles.empty() && a->styles.find(sc.code) != std::string::npos;
          const bool eb = !b2->styles.empty() && b2->styles.find(sc.code) != std::string::npos;
          if (ea != eb) return ea;  // (the scheme's own concepts before the ones every book has)
          return fnv(id + "/" + a->id) < fnv(id + "/" + b2->id);
        });
        for (size_t i = 0; i < c.size() && static_cast<int>(i) < q[g]; i++) {
          kept.push_back(c[i]);
          used[c[i]->conceptName]++;
        }
      }
      // a set short of plays of one kind (no play action in an Air Raid book) fills its quota with its others
      int total = 0;
      for (int g = 0; g < 6; g++) total += q[g];
      std::vector<const OffPlay*> rest;
      for (const OffPlay* p : in)
        if (std::find(kept.begin(), kept.end(), p) == kept.end() && group(p) != 5) rest.push_back(p);
      std::stable_sort(rest.begin(), rest.end(), [&](const OffPlay* a, const OffPlay* b2) {
        const int ua = used[a->conceptName], ub = used[b2->conceptName];
        if (ua != ub) return ua < ub;
        return fnv(id + "/" + a->id) < fnv(id + "/" + b2->id);
      });
      for (size_t i = 0; i < rest.size() && static_cast<int>(kept.size()) < total; i++) {
        kept.push_back(rest[i]);
        used[rest[i]->conceptName]++;
      }
      // in the library's order
      std::vector<const OffPlay*> ordered;
      for (const OffPlay* p : in)
        if (std::find(kept.begin(), kept.end(), p) != kept.end()) ordered.push_back(p);
      in = std::move(ordered);
    }
    if (in.empty()) continue;
    b.formations.push_back(fid);
    b.plays.insert(b.plays.end(), in.begin(), in.end());
  }
  return b;
}

DefBook buildDefBook(const DefScheme& sc, const std::string& id, const std::string& name, bool vary) {
  DefBook b;
  b.id = id;
  b.name = name;
  b.scheme = sc.name;
  b.base = sc.base;
  for (const char* sid : sc.sets) {
    const SetDef* sd = nullptr;
    for (const SetDef& s : SETS())
      if (std::string(s.id) == sid) sd = &s;
    if (!sd) continue;
    std::vector<const DefCall*> in;
    for (const DefCall& d : DEF_PLAYS())
      if (defSet(d) == sd->name) in.push_back(&d);
    if (vary) {
      std::vector<const DefCall*> kept;
      size_t left = in.size();
      for (const DefCall* d : in) {
        const bool stockCall = d->set.empty();
        const bool drop = !stockCall && left > 7 && fnv(id + "/" + d->id) % 14 == 0;
        if (drop) left--;
        else kept.push_back(d);
      }
      in = std::move(kept);
    }
    if (in.empty()) continue;
    b.sets.push_back(sd->name);
    b.calls.insert(b.calls.end(), in.begin(), in.end());
  }
  return b;
}

}  // namespace

int DefCall::rushers() const {
  return frontLinemen(front) - static_cast<int>(drop.size()) + static_cast<int>(blitz.size());
}

std::string formationFamily(const Formation& f) {
  if (!f.family.empty()) return f.family;
  const std::string& id = f.id;
  if (id.rfind("gun", 0) == 0) return "Shotgun";
  if (id.rfind("pistol", 0) == 0) return "Pistol";
  if (id == "singleback" || id.rfind("sb_", 0) == 0) return "Singleback";
  if (id == "iform" || id.rfind("i_", 0) == 0) return "I-Form";
  if (id == "heavy" || id.rfind("gl_", 0) == 0) return "Goal Line";
  return "Other";
}

const char* frontFamily(Front f) {
  switch (f) {
    case Front::Base: return "4-3";
    case Front::Base34: return "3-4";
    case Front::Nickel:
    case Front::Nickel33:
    case Front::Nickel24: return "Nickel";
    case Front::Dime:
    case Front::Dime32: return "Dime";
    case Front::Goalline: return "Goal Line";
  }
  return "?";
}

std::string defSet(const DefCall& d) {
  if (!d.set.empty()) return d.set;
  switch (d.front) {
    case Front::Base: return "4-3 Normal";
    case Front::Base34: return "3-4 Odd";
    case Front::Nickel: return "Nickel Normal";
    case Front::Dime: return "Dime Normal";
    case Front::Goalline: return "Goal Line";
    case Front::Nickel33: return "Nickel 3-3-5";
    case Front::Nickel24: return "Nickel 2-4-5";
    case Front::Dime32: return "Dime 3-2-6";
  }
  return "?";
}

std::vector<const OffPlay*> OffBook::playsIn(const std::string& formation) const {
  std::vector<const OffPlay*> out;
  for (const OffPlay* p : plays)
    if (p->formation == formation) out.push_back(p);
  return out;
}
bool OffBook::has(const OffPlay* p) const { return std::find(plays.begin(), plays.end(), p) != plays.end(); }
std::vector<const DefCall*> DefBook::callsIn(const std::string& set) const {
  std::vector<const DefCall*> out;
  for (const DefCall* d : calls)
    if (defSet(*d) == set) out.push_back(d);
  return out;
}
bool DefBook::has(const DefCall* d) const { return std::find(calls.begin(), calls.end(), d) != calls.end(); }

const std::vector<std::string>& OFF_SCHEMES() {
  static const std::vector<std::string> v = [] {
    std::vector<std::string> o;
    for (const OffScheme& s : OFF_SCHEME_DEFS()) o.push_back(s.name);
    return o;
  }();
  return v;
}
const std::vector<std::string>& DEF_SCHEMES() {
  static const std::vector<std::string> v = [] {
    std::vector<std::string> o;
    for (const DefScheme& s : DEF_SCHEME_DEFS()) o.push_back(s.name);
    return o;
  }();
  return v;
}

namespace {
std::mutex& bookMutex() {
  static std::mutex m;
  return m;
}
}  // namespace

const OffBook& offBookFor(const Team& t) {
  // keyed by the team's identity and scheme; the books live as long as the program (stable addresses)
  static std::map<std::string, std::unique_ptr<OffBook>> books;
  const std::string key = t.abbr + "|" + t.offScheme;
  std::lock_guard<std::mutex> lock(bookMutex());
  auto it = books.find(key);
  if (it != books.end()) return *it->second;
  auto b = std::make_unique<OffBook>(buildOffBook(offSchemeDef(t.offScheme), t.abbr, t.city.empty() ? t.abbr : t.city + " " + t.name, true));
  return *books.emplace(key, std::move(b)).first->second;
}

const DefBook& defBookFor(const Team& t) {
  static std::map<std::string, std::unique_ptr<DefBook>> books;
  const std::string key = t.abbr + "|" + t.defScheme;
  std::lock_guard<std::mutex> lock(bookMutex());
  auto it = books.find(key);
  if (it != books.end()) return *it->second;
  auto b = std::make_unique<DefBook>(buildDefBook(defSchemeDef(t.defScheme), t.abbr, t.city.empty() ? t.abbr : t.city + " " + t.name, true));
  return *books.emplace(key, std::move(b)).first->second;
}

const OffBook& offSchemeBook(const std::string& scheme) {
  static std::map<std::string, std::unique_ptr<OffBook>> books;
  std::lock_guard<std::mutex> lock(bookMutex());
  auto it = books.find(scheme);
  if (it != books.end()) return *it->second;
  const OffScheme& sc = offSchemeDef(scheme);
  auto b = std::make_unique<OffBook>(buildOffBook(sc, sc.code, sc.name, false));
  return *books.emplace(scheme, std::move(b)).first->second;
}

const DefBook& defSchemeBook(const std::string& scheme) {
  static std::map<std::string, std::unique_ptr<DefBook>> books;
  std::lock_guard<std::mutex> lock(bookMutex());
  auto it = books.find(scheme);
  if (it != books.end()) return *it->second;
  const DefScheme& sc = defSchemeDef(scheme);
  auto b = std::make_unique<DefBook>(buildDefBook(sc, sc.name, sc.name, false));
  return *books.emplace(scheme, std::move(b)).first->second;
}

double defSchemeLean(const std::string& scheme, const DefCall& d) {
  // how much a scheme leans on a call (the CPU's play caller multiplies its situational weight by it)
  const Coverage c = d.coverage;
  const bool pressure = d.rushers() > 4;
  double x = 1;
  if (scheme == "4-3 Cover 3") x = c == Coverage::C3 || c == Coverage::C3Fire ? 2.2 : c == Coverage::C1 ? 1.4 : c == Coverage::C2T ? 0.5 : 1;
  else if (scheme == "Tampa 2") x = (c == Coverage::C2T ? 2.8 : c == Coverage::C2 ? 2.0 : c == Coverage::C2M ? 1.3 : 0.8) * (pressure ? 0.7 : 1);
  else if (scheme == "4-3 Quarters") x = c == Coverage::C4 ? 2.4 : c == Coverage::C6 ? 2.0 : c == Coverage::C2T ? 0.4 : 1;
  else if (scheme == "3-4 Multiple") x = c == Coverage::C1 || c == Coverage::C3 || c == Coverage::C4 || c == Coverage::C6 ? 1.3 : 1;
  else if (scheme == "3-4 Pressure") x = (c == Coverage::C3Fire || c == Coverage::C1 || c == Coverage::C0 ? 1.6 : 1) * (pressure ? 1.3 : 1);
  return x;
}

}  // namespace tag
