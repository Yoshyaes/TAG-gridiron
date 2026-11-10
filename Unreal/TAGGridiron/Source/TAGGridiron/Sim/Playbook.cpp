// Port of src/sim/playbook.ts
#include "Playbook.h"

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
  const bool underCenter = p.formation == "singleback" || p.formation == "iform";
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
  };
  return r;
}

const std::vector<OffPlay>& OFF_PLAYS() {
  using PT = PlayType;
  using RSt = RunStyle;
  static const std::vector<OffPlay> plays = {
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
  return plays;
}

const std::vector<DefCall>& DEF_PLAYS() {
  using F = Front;
  using Cv = Coverage;
  static const std::vector<DefCall> d = {
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
  }
  return "?";
}

}  // namespace tag
