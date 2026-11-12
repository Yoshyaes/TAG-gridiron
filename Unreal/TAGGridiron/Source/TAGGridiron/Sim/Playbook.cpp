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
  p.routes = {{"X", "block"}, {"Z", "block"}, {"SLOT", "block"}, {"SLOT2", "block"}, {"TE", "block"}, {"TE2", "block"}, {"FB", "block"}};
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
  static const std::vector<Formation> f = {
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
           {"QB", {Pos::QB}, alBack(-1, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alInline('L', true)},
       })},
      {"iform", "I-Form Pro", "21", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1, 0)},
           {"FB", {Pos::FB}, alBack(-4.5, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
       })},
      {"heavy", "Goal Line Heavy", "22", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1, 0)},
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
           {"QB", {Pos::QB}, alBack(-1, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"SLOT", {Pos::WR}, alSlot('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
       }), "Singleback"},
      {"sb_trips", "Singleback Trey Open", "11", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"SLOT", {Pos::WR}, alSlot('R')},
           {"TE", {Pos::TE}, alSlot('R', 2)},
       }), "Singleback"},
      {"sb_bunch", "Singleback Bunch", "11", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alBack(-1.5, -9.2)},
           {"SLOT", {Pos::WR}, alBack(-0.6, -8.2)},
           {"TE", {Pos::TE}, alBack(-1.5, -7.2)},
       }), "Singleback"},
      {"sb_wing", "Singleback Wing Trio", "12", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alBack(-1.3, -5.3)},  // the wing, off the end man's hip
           {"Z", {Pos::WR}, alBack(-0.6, -8.6)},
       }), "Singleback"},
      // I-formation
      {"i_twin", "I-Form Twin WR", "21", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1, 0)},
           {"FB", {Pos::FB}, alBack(-4.5, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alSlot('R')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('L')},
       }), "I-Form"},
      {"i_tight", "I-Form Tight Pair", "22", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1, 0)},
           {"FB", {Pos::FB}, alBack(-4.5, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"TE", {Pos::TE}, alInline('R')},
           {"TE2", {Pos::TE}, alInline('L', true)},
       }), "I-Form"},
      {"i_slot", "I-Form Slot", "20", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1, 0)},
           {"FB", {Pos::FB}, alBack(-4.5, 0)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"SLOT", {Pos::WR}, alSlot('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
       }), "I-Form"},
      {"strong_i", "Strong I", "21", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1, 0)},
           {"FB", {Pos::FB}, alBack(-4.5, -1.8)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
       }), "Strong I"},
      {"weak_i", "Weak I", "21", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1, 0)},
           {"FB", {Pos::FB}, alBack(-4.5, 1.8)},
           {"RB", {Pos::RB}, alBack(-7, 0)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
       }), "Weak I"},
      {"splitback", "Split Backs", "21", 1,
       withOL({
           {"QB", {Pos::QB}, alBack(-1, 0)},
           {"FB", {Pos::FB, Pos::RB}, alBack(-5, 1.6)},
           {"RB", {Pos::RB}, alBack(-5, -1.6)},
           {"X", {Pos::WR}, alWide('L')},
           {"Z", {Pos::WR}, alWide('R', true)},
           {"TE", {Pos::TE}, alInline('R')},
       }), "Split Backs"},
  };
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
      {"swing", {{{-1, 4}, {1, 10}}, false, false}},
      {"angle", {{{3, 3}, {6, -3}}, false, false}},
      {"checkdown", {{{2, 2}, {5, 3}}, true, false}},
      {"screen", {{{-2, 3}, {-2.5, 4}}, true, false}},
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
  };
  return r;
}

namespace {
std::vector<OffPlay> generatedPlays(const std::vector<OffPlay>& stock);
std::vector<DefCall> generatedCalls(const std::vector<DefCall>& stock);
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
    std::vector<OffPlay> g = generatedPlays(v);
    v.insert(v.end(), std::make_move_iterator(g.begin()), std::make_move_iterator(g.end()));
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
  switch (f) {
    case Front::Base: return base;
    case Front::Nickel: return nickel;
    case Front::Dime: return dime;
    case Front::Goalline: return goalline;
    case Front::Base34: return base34;
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
const char* frontName(Front f) {
  switch (f) {
    case Front::Base: return "base";
    case Front::Nickel: return "nickel";
    case Front::Dime: return "dime";
    case Front::Goalline: return "goalline";
    case Front::Base34: return "base34";
  }
  return "?";
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
      case AlignKind::Slot: lat = sg * (a.n == 2 ? 5.3 : 9.7); onLine = false; break;
      case AlignKind::Inline: lat = sg * 4.05; onLine = !a.off; break;
      case AlignKind::Back: lat = a.dy; onLine = a.x > -1; break;
      case AlignKind::OL: break;
    }
    const bool backfield = a.kind == AlignKind::Back && a.x <= -2.5 && std::fabs(a.dy) < 2.5;
    if (backfield) {
      const bool tail = s.role == "RB" && !pl.has("B");
      pl.role[tail ? "B" : "F"] = s.role;
      if (tail) pl.backDepth = -a.x;
      continue;
    }
    if (a.kind == AlignKind::Inline) pl.inlineRoles.insert(s.role);
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
};

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
  // RS Run and Shoot
  static const std::vector<Concept> c = {
      // quick game
      {"slants", "Slants", PT::Pass, {{"S1", "slant"}, {"S2", "slant"}, {"W1", "slant"}, {"S3/W2", "flat"}, {"B", "checkdown"}}, {1, 0, 2, 3, 4}, {"short", "quick"}, 1, "GL"},
      {"slantflat", "Slant Flat", PT::Pass, {{"S1", "slant"}, {"S2", "flat"}, {"W1", "slant"}, {"W2", "flat"}, {"S3", "seam"}, {"B", "checkdown"}}, {0, 1, 2, 3, 5}, {"short", "quick"}, 1, "WC PS PR ZR GL"},
      {"stick", "Stick", PT::Pass, {{"S2", "stick"}, {"S3/F", "flat"}, {"S1", "go"}, {"W1", "slant"}, {"W2", "stick"}, {"B", "checkdown"}}, {0, 1, 3, 4, 5}, {"short", "quick"}, 1, "WC SP AR PS RS GL"},
      {"spacing", "Spacing", PT::Pass, {{"S2", "stick"}, {"S1", "hitch"}, {"S3", "quickout"}, {"W1", "hitch"}, {"W2", "quickout"}, {"B", "flat"}}, {0, 1, 2, 3, 4, 5}, {"short", "quick"}, 1, "WC AR RS SP"},
      {"snag", "Snag", PT::Pass, {{"S1", "snag"}, {"S2", "corner"}, {"S3/F", "flat"}, {"W1", "slant"}, {"W2", "drag"}}, {0, 2, 1, 3, 4}, {"short"}, 2, "WC SP PS ZR"},
      {"bubble", "Slot Bubble", PT::Pass, {{"S2", "bubble"}, {"S1", "hitch"}, {"W1", "slant"}, {"W2", "stick"}, {"S3", "seam"}, {"B", "checkdown"}}, {0, 2, 1, 3, 5}, {"short", "quick"}, 1, "SP AR RS", 2, true},
      {"texas", "Texas", PT::Pass, {{"B", "angle"}, {"S2", "curl"}, {"S1", "go"}, {"W1", "dig"}, {"W2/S3", "flat"}}, {0, 1, 3, 4}, {"short"}, 2, "WC PS PR ZR GL"},
      {"whip", "Whip Flat", PT::Pass, {{"S2", "whip"}, {"S3/F", "flat"}, {"S1", "go"}, {"W1", "slant"}, {"W2", "hitch"}}, {0, 1, 3, 4}, {"short", "quick"}, 1, "WC SP RS"},
      // intermediate
      {"curlflat", "Curl Flat", PT::Pass, {{"S1", "curl"}, {"S2", "flat"}, {"W1", "curl"}, {"S3", "seam"}, {"W2", "arrow"}, {"B", "checkdown"}}, {0, 1, 2, 3, 4, 5}, {"mid"}, 2, "GL"},
      {"curls", "All Curls", PT::Pass, {{"S1", "curl"}, {"S2", "curl"}, {"W1", "curl"}, {"S3", "curl"}, {"W2", "curl"}, {"B", "flat"}}, {1, 0, 2, 3, 4, 5}, {"mid"}, 2, "WC PR RS"},
      {"smash", "Smash", PT::Pass, {{"S1", "hitch"}, {"S2", "corner"}, {"W1", "hitch"}, {"W2", "corner"}, {"S3", "seam"}, {"B", "checkdown"}}, {1, 0, 3, 2, 4, 5}, {"mid"}, 2, ""},
      {"flood", "Flood", PT::Pass, {{"S2", "sail"}, {"S3/F", "flat"}, {"S1", "go"}, {"W1", "post"}, {"W2", "drag"}}, {0, 1, 3, 4}, {"mid"}, 2, "WC PS ZR SP VT GL"},
      {"mesh", "Mesh", PT::Pass, {{"S2", "drag"}, {"W2/W1", "shallow"}, {"S1", "corner"}, {"W1", "dig"}, {"B", "swing"}, {"S3", "seam"}}, {0, 1, 4, 2, 5}, {"short"}, 2, "AR SP WC RS"},
      {"drive", "Drive", PT::Pass, {{"S2", "shallow"}, {"S1", "dig"}, {"W1", "post"}, {"W2", "curl"}, {"S3/F", "flat"}}, {0, 1, 4, 2}, {"mid"}, 2, "WC ZR AR PS"},
      {"ycross", "Y Cross", PT::Pass, {{"S3/S2", "cross"}, {"S1", "post"}, {"W1", "curl"}, {"W2", "shallow"}, {"S2", "flat"}, {"B", "flat"}}, {0, 2, 3, 4, 5}, {"mid", "long"}, 3, "AR SP WC PS"},
      {"dagger", "Dagger", PT::Pass, {{"S1", "dig"}, {"S2", "seam"}, {"W1", "curl"}, {"W2", "shallow"}, {"S3", "flat"}, {"B", "checkdown"}}, {0, 2, 3, 4, 5}, {"mid"}, 3, "SP VT PS AR"},
      {"levels", "Levels", PT::Pass, {{"S2", "in"}, {"S1", "dig"}, {"S3", "seam"}, {"W1", "go"}, {"W2", "drag"}, {"B", "checkdown"}}, {0, 1, 4, 5}, {"mid"}, 2, "WC SP RS PS"},
      {"comebacks", "Comebacks", PT::Pass, {{"S1", "comeback"}, {"W1", "comeback"}, {"S2", "seam"}, {"W2", "drag"}, {"S3/F", "flat"}}, {0, 1, 2, 4}, {"mid", "long"}, 3, "VT PR PS RS"},
      {"dagout", "Deep Outs", PT::Pass, {{"S1", "deepout"}, {"S2", "seam"}, {"W1", "deepout"}, {"W2", "shallow"}, {"B", "checkdown"}}, {0, 2, 3, 4}, {"mid", "long"}, 3, "VT RS PS"},
      {"slotfade", "Slot Fade", PT::Pass, {{"S2", "fade"}, {"S1", "slant"}, {"W1", "slant"}, {"W2", "flat"}, {"B", "checkdown"}}, {0, 1, 2, 3}, {"short"}, 1, "SP AR RS VT GL", 2},
      // shots
      {"4verts", "Four Verticals", PT::Pass, {{"S1", "go"}, {"S2", "seam"}, {"W1", "go"}, {"W2/S3", "seam"}, {"B", "checkdown"}}, {1, 3, 0, 2, 4}, {"deep"}, 3, "SP AR VT RS PS"},
      {"hbwheel", "Verticals HB Wheel", PT::Pass, {{"S2", "seam"}, {"B", "wheel"}, {"S1", "go"}, {"W1", "go"}, {"W2", "seam"}}, {0, 1, 3, 2}, {"deep"}, 3, "SP VT AR"},
      {"mills", "Mills", PT::Pass, {{"S1", "post"}, {"S2", "dig"}, {"W1", "go"}, {"W2", "drag"}, {"B", "checkdown"}}, {0, 1, 3, 4}, {"deep"}, 3, "VT PS ZR SP"},
      {"postwheel", "Post Wheel", PT::Pass, {{"S1", "post"}, {"S2", "wheel"}, {"W1", "out"}, {"W2", "dig"}, {"B", "checkdown"}}, {0, 1, 3, 2, 4}, {"deep"}, 3, "VT SP PS"},
      {"scissors", "Scissors", PT::Pass, {{"S1", "post"}, {"S2", "corner"}, {"W1", "dig"}, {"W2", "drag"}, {"B", "checkdown"}}, {1, 0, 2, 4}, {"deep"}, 3, "VT PS PR ZR"},
      {"doublemove", "Double Moves", PT::Pass, {{"S1", "outnup"}, {"W1", "slantgo"}, {"S2", "hitch"}, {"W2", "drag"}, {"B", "checkdown"}}, {0, 1, 2, 3, 4}, {"deep"}, 3, "VT RS PR"},
      {"skinny", "Skinny Posts", PT::Pass, {{"S1", "skinny"}, {"W1", "skinny"}, {"S2", "out"}, {"W2", "curl"}, {"B", "checkdown"}}, {0, 1, 2, 3, 4}, {"deep"}, 3, "VT AR SP"},
      // play action (the back fakes and stays in)
      {"pa_boot", "PA Boot Over", PT::PlayAction, {{"S1", "cross"}, {"S2", "flat"}, {"W1", "post"}, {"W2", "corner"}, {"F", "flat"}, {"B", "block"}}, {1, 0, 3, 2, 4}, {"mid", "playaction"}, 0, "WC PS PR ZR GL"},
      {"pa_shot", "PA Deep Post", PT::PlayAction, {{"W1", "post"}, {"S1", "dig"}, {"S2", "seam"}, {"W2", "go"}, {"B", "block"}}, {0, 1, 2, 3}, {"deep", "playaction"}, 0, "PR PS VT ZR"},
      {"pa_cross", "PA Crossers", PT::PlayAction, {{"S1", "cross"}, {"W1", "post"}, {"S2", "flat"}, {"F", "flat"}, {"W2", "drag"}, {"B", "block"}}, {0, 1, 2, 3, 4}, {"mid", "playaction"}, 0, "WC ZR PS PR"},
      {"pa_yankee", "PA Yankee", PT::PlayAction, {{"S1", "post"}, {"W1", "deepcross"}, {"S2", "flat"}, {"F", "flat"}, {"B", "block"}}, {0, 1, 2, 3}, {"deep", "playaction"}, 0, "VT ZR PS PR"},
      // screens
      {"hbscreen", "HB Screen", PT::Screen, {{"B", "screen"}, {"S1", "go"}, {"S2", "in"}, {"W1", "go"}, {"W2", "in"}}, {0}, {"screen", "short"}, 0, ""},
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

/** Fits a concept to a formation; false if it doesn't fit (a route with nobody to run it, too few to one side...). */
bool fitConcept(const Formation& f, const Places& pl, const Concept& c, OffPlay& out) {
  if (pl.nStrong < c.minStrong) return false;
  const bool needB = c.type == PlayType::PlayAction || c.type == PlayType::Screen;
  if (needB && !pl.has("B")) return false;
  std::set<std::string> used;
  std::vector<std::string> entryRole(c.e.size());
  Routes routes;
  for (size_t i = 0; i < c.e.size(); i++) {
    for (const std::string& alt : splitAlts(c.e[i].places)) {
      if (!pl.has(alt) || used.count(alt)) continue;
      used.insert(alt);
      entryRole[i] = pl.role.at(alt);
      routes[entryRole[i]] = c.e[i].route;
      break;
    }
    if (i == 0 && entryRole[0].empty()) return false;  // the concept's first route is its point
  }
  if (c.firstWide && pl.inlineRoles.count(entryRole[0])) return false;
  // a tailback seven deep (under center, the pistol) checks his block and releases to a spot past the line (his
  // gun checkdown and flat would be caught behind it); only the concepts built on him run his own routes. A
  // fullback the concept leaves out checks and releases to the flat past the line.
  const std::string cid = c.id;
  const bool backConcept = cid == "texas" || cid == "hbwheel" || cid == "hbscreen";
  if (pl.backDepth >= 6 && !backConcept && pl.has("B")) {
    auto it = routes.find(pl.role.at("B"));
    if (it != routes.end() && (it->second == "checkdown" || it->second == "swing")) it->second = "release";
    else if (it != routes.end() && it->second == "flat") it->second = "releaseflat";
  }
  // anyone the concept left out: wide men clear deep, attached tight ends stay in, backs check down
  for (const auto& kv : pl.role) {
    if (used.count(kv.first)) continue;
    const std::string& r = kv.second;
    std::string rt;
    if (kv.first == "B") rt = c.type == PlayType::PlayAction ? "block" : pl.backDepth >= 6 ? "release" : "checkdown";
    else if (kv.first == "F") rt = "releaseflat";
    else if (pl.inlineRoles.count(r)) rt = "block";
    else rt = kv.first == "S1" || kv.first == "W1" ? "go" : "seam";
    routes[r] = rt;
  }
  Strs reads;
  auto addRead = [&](const std::string& r) {
    if (r.empty() || routes[r] == "block" || std::find(reads.begin(), reads.end(), r) != reads.end()) return;
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
  const bool underCenter = f.qbDepth <= 1.5;
  if (c.type == PlayType::PlayAction) out.dropDepth = underCenter ? 6.0 : 3.0;
  else if (c.type == PlayType::Screen) out.dropDepth = 4.0;
  else out.dropDepth = (underCenter && c.drop == 2) ? 5.0 : c.drop;
  return true;
}

struct RunConcept {
  const char* id;
  const char* name;
  RunStyle style;
  double aim;           // yards from the center, + = to the formation's strength
  const char* carrier;  // "B" (the tailback), "F" (the fullback), "QB"
  int from;             // 0 any, 1 under center only, 2 gun / pistol only
  bool needFB, needTE;
  Strs tags;
  const char* styles;
};

const std::vector<RunConcept>& RUNS() {
  using RSt = RunStyle;
  static const std::vector<RunConcept> r = {
      {"iz", "Inside Zone", RSt::Zone, 0.7, "B", 0, false, false, {"run"}, ""},
      {"izw", "Inside Zone Weak", RSt::Zone, -0.7, "B", 0, false, false, {"run"}, "ZR PS SP WC"},
      {"oz", "Outside Zone", RSt::Stretch, 4.5, "B", 0, false, false, {"run"}, "ZR SP WC PS"},
      {"ozw", "Outside Zone Weak", RSt::Stretch, -4.5, "B", 0, false, false, {"run"}, "ZR WC"},
      {"power", "Power", RSt::Gap, 2.0, "B", 0, false, false, {"run", "shortyard"}, "PR PS WC VT ZR SP"},
      {"counter", "Counter", RSt::Gap, -2.6, "B", 0, false, false, {"run"}, "PR PS SP VT RS"},
      {"dive", "HB Dive", RSt::Gap, -0.7, "B", 1, false, false, {"run", "shortyard"}, "PR PS WC VT"},
      {"iso", "Iso", RSt::Gap, -0.7, "B", 1, true, false, {"run", "shortyard"}, "PR PS WC VT"},
      {"toss", "HB Toss", RSt::Stretch, 6.0, "B", 1, false, false, {"run"}, "PR PS WC VT ZR"},
      {"stretch", "HB Stretch", RSt::Stretch, 5.0, "B", 1, false, false, {"run"}, "ZR PS WC"},
      {"draw", "HB Draw", RSt::Draw, -0.6, "B", 2, false, false, {"run", "long"}, "SP AR RS VT PS WC"},
      {"duo", "Duo", RSt::Gap, 0.7, "B", 0, false, true, {"run", "shortyard"}, "PR PS ZR"},
      {"fbdive", "FB Dive", RSt::Gap, 0.4, "F", 1, true, false, {"run", "shortyard"}, "PR PS"},
      {"sneak", "QB Sneak", RSt::Sneak, 0.6, "QB", 1, false, true, {"run", "shortyard"}, "GL"},
  };
  return r;
}

bool fitRun(const Formation& f, const Places& pl, const RunConcept& rc, const std::vector<OffPlay>& have, OffPlay& out) {
  const bool underCenter = f.qbDepth <= 1.5;
  if (rc.from == 1 && !underCenter) return false;
  if (rc.from == 2 && underCenter) return false;
  const bool hasFB = pl.has("F") && pl.role.at("F") == "FB";
  if (rc.needFB && !hasFB) return false;
  if (rc.needTE && pl.inlineRoles.empty()) return false;
  std::string carrier = rc.carrier;
  if (carrier == "B" || carrier == "F") {
    if (!pl.has(carrier)) return false;
    carrier = pl.role.at(carrier);
  }
  if (!pl.has("B")) return false;  // an empty set hands off to nobody
  // a QB sneak is a short-yardage call: the heavy sets only
  if (rc.style == RunStyle::Sneak && f.personnel != "22") return false;
  const double aim = rc.aim * pl.strong;
  // the formation's own stock run of the same kind stands in for it
  for (const OffPlay& o : have)
    if (o.formation == f.id && o.run && o.run->style == rc.style && o.run->carrier == carrier &&
        (o.run->aimDy > 0) == (aim > 0) && std::fabs(std::fabs(o.run->aimDy) - std::fabs(aim)) < 1.0)
      return false;
  RunScheme run;
  run.aimDy = aim;
  run.style = rc.style;
  run.carrier = carrier;
  const double qd = f.qbDepth;
  switch (rc.style) {
    case RunStyle::Zone: run.handoffDelay = qd >= 4.5 ? 0.65 : qd >= 3 ? 0.75 : 0.85; break;
    case RunStyle::Gap: run.handoffDelay = carrier == "FB" ? 0.6 : qd >= 4.5 ? 0.75 : qd >= 3 ? 0.8 : (rc.aim < -1 ? 1.0 : 0.9); break;
    case RunStyle::Stretch: run.handoffDelay = std::string(rc.id) == "toss" ? 0.55 : qd >= 4.5 ? 0.7 : qd >= 3 ? 0.75 : 0.8; break;
    case RunStyle::Draw: run.handoffDelay = 0.9; break;
    case RunStyle::Sneak: run.handoffDelay = 0; break;
  }
  out = OffPlay();
  out.id = f.id + "__" + rc.id;
  out.name = (std::string(rc.id) == "power" && hasFB) ? "Power O" : rc.name;
  out.formation = f.id;
  out.type = PlayType::Run;
  out.routes = {{"X", "block"}, {"Z", "block"}, {"SLOT", "block"}, {"SLOT2", "block"}, {"TE", "block"}, {"TE2", "block"}, {"FB", "block"}};
  out.run = run;
  out.tags = rc.tags;
  if (f.personnel == "22") out.tags.push_back("goalline");
  out.styles = rc.styles;
  return true;
}

std::vector<OffPlay> generatedPlays(const std::vector<OffPlay>& stock) {
  std::vector<OffPlay> out;
  for (const Formation& f : FORMATIONS()) {
    const Places pl = placesOf(f);
    const bool goalSet = f.personnel == "22";
    auto haveName = [&](const std::string& n) {
      for (const OffPlay& o : stock)
        if (o.formation == f.id && o.name == n) return true;
      for (const OffPlay& o : out)
        if (o.formation == f.id && o.name == n) return true;
      return false;
    };
    for (const Concept& c : CONCEPTS()) {
      if (goalSet && !fromGoalLine(c.styles)) continue;  // the goal line set keeps to its short game
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
      {"odd", "3-4 Odd", Front::Base34, FrontAlign::Normal},
      {"odd_over", "3-4 Over", Front::Base34, FrontAlign::Over},
      {"bear", "3-4 Bear", Front::Base34, FrontAlign::Bear},
      {"nk", "Nickel Normal", Front::Nickel, FrontAlign::Normal},
      {"nk_wide9", "Nickel Wide 9", Front::Nickel, FrontAlign::Wide9},
      {"dime", "Dime Normal", Front::Dime, FrontAlign::Normal},
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
};

/** The calls of a front (role names are the front's own). */
std::vector<CallTpl> callsFor(Front f) {
  using Cv = Coverage;
  switch (f) {
    case Front::Base:
      return {
          {"c1", "Cover 1", Cv::C1, {}, {"base"}},
          {"c2", "Cover 2 Zone", Cv::C2, {}, {"base"}},
          {"c2t", "Tampa 2", Cv::C2T, {}, {"base", "pass"}},
          {"c2m", "2 Man Under", Cv::C2M, {}, {"pass", "long"}},
          {"c3", "Cover 3 Sky", Cv::C3, {}, {"run", "base"}},
          {"c4", "Cover 4 Quarters", Cv::C4, {}, {"base", "pass"}},
          {"c6", "Cover 6", Cv::C6, {}, {"base", "pass"}},
          {"mike", "Mike Blitz", Cv::C1, {"MLB"}, {"blitz", "run"}},
          {"will", "Will Blitz", Cv::C1, {"WLB"}, {"blitz"}},
          {"sam3", "Sam Fire 3", Cv::C3Fire, {"SLB"}, {"blitz", "run"}},
          {"ss3", "Strong Fire", Cv::C3Fire, {"SS"}, {"blitz"}},
          {"dbla", "Double A Gap", Cv::C1, {"MLB", "WLB"}, {"blitz", "shortyard"}},
          {"c0", "Cover 0 Blitz", Cv::C0, {"MLB", "WLB"}, {"blitz", "shortyard"}},
      };
    case Front::Base34:
      return {
          {"c1", "Cover 1", Cv::C1, {"LOLB"}, {"base"}},
          {"c2", "Cover 2 Zone", Cv::C2, {"LOLB"}, {"base"}},
          {"c2m", "2 Man Under", Cv::C2M, {"LOLB"}, {"pass", "long"}},
          {"c3", "Cover 3 Sky", Cv::C3, {"LOLB"}, {"run", "base"}},
          {"c4", "Cover 4 Quarters", Cv::C4, {"LOLB"}, {"base", "pass"}},
          {"c6", "Cover 6", Cv::C6, {"LOLB"}, {"base", "pass"}},
          {"drop8", "Drop 8", Cv::C3, {}, {"long", "prevent"}},
          {"olb", "OLB Fire", Cv::C3Fire, {"LOLB", "ROLB"}, {"blitz"}},
          {"will", "Will Fire", Cv::C3Fire, {"LOLB", "WLB"}, {"blitz", "run"}},
          {"inside", "Inside Blitz", Cv::C1, {"LOLB", "MLB"}, {"blitz", "run"}},
          {"dbla", "Double A Gap", Cv::C1, {"LOLB", "WLB", "MLB"}, {"blitz", "shortyard"}},
          {"c0", "Cover 0 Blitz", Cv::C0, {"LOLB", "ROLB", "MLB"}, {"blitz", "shortyard"}},
      };
    case Front::Nickel:
      return {
          {"c1", "Cover 1", Cv::C1, {}, {"pass"}},
          {"c2", "Cover 2 Zone", Cv::C2, {}, {"pass"}},
          {"c2t", "Tampa 2", Cv::C2T, {}, {"pass"}},
          {"c2m", "2 Man Under", Cv::C2M, {}, {"pass", "long"}},
          {"c3", "Cover 3 Sky", Cv::C3, {}, {"pass"}},
          {"c4", "Cover 4 Quarters", Cv::C4, {}, {"pass"}},
          {"c6", "Cover 6", Cv::C6, {}, {"pass"}},
          {"nb", "Nickel Blitz", Cv::C3Fire, {"NB"}, {"blitz", "pass"}},
          {"mike", "Mike Blitz", Cv::C1, {"MLB"}, {"blitz"}},
          {"will3", "Will Fire", Cv::C3Fire, {"WLB"}, {"blitz"}},
          {"ss", "Safety Blitz", Cv::C1, {"SS"}, {"blitz", "pass"}},
          {"dbla", "Double A Gap", Cv::C1, {"MLB", "WLB"}, {"blitz", "shortyard"}},
          {"c0", "Cover 0 Blitz", Cv::C0, {"WLB", "MLB"}, {"blitz", "pass", "shortyard"}},
      };
    case Front::Dime:
      return {
          {"c1", "Cover 1", Cv::C1, {}, {"long", "pass"}},
          {"c2", "Cover 2 Zone", Cv::C2, {}, {"long", "prevent"}},
          {"c2m", "2 Man Under", Cv::C2M, {}, {"long", "pass"}},
          {"c3", "Cover 3 Sky", Cv::C3, {}, {"long", "pass"}},
          {"c4", "Cover 4 Quarters", Cv::C4, {}, {"long", "prevent"}},
          {"c6", "Cover 6", Cv::C6, {}, {"long", "pass"}},
          {"db", "Dime Blitz", Cv::C3Fire, {"DB"}, {"blitz", "pass"}},
          {"mike", "Mike Blitz", Cv::C1, {"MLB"}, {"blitz", "pass"}},
          {"nb", "Nickel Blitz", Cv::C1, {"NB"}, {"blitz", "pass"}},
      };
    case Front::Goalline:
      return {
          {"c3", "Goal Line Cover 3", Cv::C3, {}, {"goalline", "shortyard", "run"}},
          {"c2", "Goal Line Cover 2", Cv::C2, {}, {"goalline", "shortyard"}},
          {"jam", "Goal Line Jam", Cv::C0, {"SLB", "WLB", "MLB"}, {"goalline", "shortyard", "run", "blitz"}},
      };
  }
  return {};
}

bool sameCall(const DefCall& a, const DefCall& b) {
  if (a.front != b.front || a.coverage != b.coverage || a.blitz.size() != b.blitz.size()) return false;
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
      // the call's name carries its set where the set is not the front's normal look
      d.name = s.align == FrontAlign::Normal && s.front != Front::Base34 ? std::string(frontFamily(s.front)) + " " + t.name
                                                                         : std::string(s.name) + " " + t.name;
      if (s.front == Front::Goalline) d.name = t.name;
      bool dup = false;
      if (s.align == FrontAlign::Normal)
        for (const DefCall& o : stock) dup = dup || sameCall(o, d);
      if (!dup) out.push_back(std::move(d));
    }
  }
  return out;
}

// ---------------- schemes and books
struct OffScheme {
  const char* name;
  const char* code;
  std::vector<const char*> formations;
};
const std::vector<OffScheme>& OFF_SCHEME_DEFS() {
  static const std::vector<OffScheme> s = {
      {"West Coast", "WC", {"gun_doubles", "gun_trips", "gun_bunch", "gun_tightflex", "singleback", "sb_doubles", "sb_bunch", "sb_wing", "iform", "i_twin", "strong_i", "weak_i", "splitback", "heavy"}},
      {"Spread", "SP", {"gun_doubles", "gun_trips", "gun_trey", "gun_bunch", "gun_spread", "gun_tightflex", "gun_empty", "gun_split", "pistol", "pistol_trips", "sb_doubles", "heavy"}},
      {"Air Raid", "AR", {"gun_spread", "gun_doubles", "gun_trey", "gun_trips", "gun_bunch", "gun_tightflex", "gun_empty", "gun_split", "pistol", "pistol_trips", "sb_doubles", "heavy"}},
      {"Pro Style", "PS", {"singleback", "sb_doubles", "sb_trips", "sb_wing", "iform", "i_tight", "i_twin", "strong_i", "weak_i", "gun_doubles", "gun_trips", "gun_ace", "gun_bunch", "pistol", "splitback", "heavy"}},
      {"Power Run", "PR", {"iform", "i_tight", "i_twin", "strong_i", "weak_i", "splitback", "singleback", "sb_wing", "sb_doubles", "pistol", "pistol_ace", "gun_ace", "gun_doubles", "heavy"}},
      {"Zone Run", "ZR", {"singleback", "sb_doubles", "sb_bunch", "sb_wing", "sb_trips", "iform", "i_twin", "weak_i", "pistol", "pistol_ace", "gun_doubles", "gun_tightflex", "gun_bunch", "heavy"}},
      {"Vertical", "VT", {"singleback", "sb_doubles", "sb_trips", "iform", "i_slot", "strong_i", "gun_doubles", "gun_trips", "gun_spread", "gun_trey", "gun_empty", "pistol", "heavy"}},
      {"Run and Shoot", "RS", {"gun_spread", "gun_doubles", "gun_trey", "gun_empty", "gun_trips", "gun_bunch", "pistol", "pistol_trips", "sb_doubles", "sb_trips", "i_slot", "heavy"}},
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
      {"4-3 Cover 3", Front::Base, {"base", "over", "under", "nk", "nk_wide9", "dime", "gl"}},
      {"Tampa 2", Front::Base, {"base", "over", "under", "nk", "dime", "gl"}},
      {"4-3 Quarters", Front::Base, {"base", "over", "wide9", "nk", "nk_wide9", "dime", "gl"}},
      {"3-4 Multiple", Front::Base34, {"odd", "odd_over", "bear", "nk", "dime", "gl"}},
      {"3-4 Pressure", Front::Base34, {"odd", "bear", "nk", "nk_wide9", "dime", "gl"}},
  };
  return s;
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
  for (const char* fid : sc.formations) {
    if (!formationById(fid)) continue;
    std::vector<const OffPlay*> in;
    for (const OffPlay& p : OFF_PLAYS())
      if (p.formation == fid && p.type != PlayType::Kneel && p.type != PlayType::Spike && (p.stock || styleIn(p.styles.c_str(), sc.code)))
        in.push_back(&p);
    // a team's own book: about one generated play in eight left out (never a stock play, never below eight a set)
    if (vary) {
      std::vector<const OffPlay*> kept;
      size_t left = in.size();
      for (const OffPlay* p : in) {
        const bool drop = !p->stock && left > 8 && fnv(id + "/" + p->id) % 8 == 0;
        if (drop) left--;
        else kept.push_back(p);
      }
      in = std::move(kept);
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
        const bool drop = !stockCall && left > 7 && fnv(id + "/" + d->id) % 9 == 0;
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
  int dl = front == Front::Base34 ? 3 : 4;
  return dl + static_cast<int>(blitz.size());
}

std::string formationFamily(const Formation& f) {
  if (!f.family.empty()) return f.family;
  const std::string& id = f.id;
  if (id.rfind("gun", 0) == 0) return "Shotgun";
  if (id.rfind("pistol", 0) == 0) return "Pistol";
  if (id == "singleback" || id.rfind("sb_", 0) == 0) return "Singleback";
  if (id == "iform" || id.rfind("i_", 0) == 0) return "I-Form";
  if (id == "heavy") return "Goal Line";
  return "Other";
}

const char* frontFamily(Front f) {
  switch (f) {
    case Front::Base: return "4-3";
    case Front::Base34: return "3-4";
    case Front::Nickel: return "Nickel";
    case Front::Dime: return "Dime";
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
