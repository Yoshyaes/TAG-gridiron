// Verification-only: C++ twin of scenario_dump.ts. Build with RefMathFI.h forced-included (see verify_vs_ts.bat).
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <cstring>
#include <string>
#include "League.h"
#include "PlaySim.h"
using namespace tag;
static std::string hx(double v){ unsigned long long u; std::memcpy(&u,&v,8); char b[32]; std::snprintf(b,32,"%llx",u); return b; }
static std::string id(const Player* p){ return p ? std::to_string(p->id) : "undefined"; }
static const char* B(bool b){ return b ? "true" : "false"; }
int main(){
  _setmode(_fileno(stdout), _O_BINARY);  // LF line endings, like node
  League league = createLeague(2027);
  Rng rng(5);
  const std::vector<double> ys = {FIELD_W / 2 - 3.08, FIELD_W / 2, FIELD_W / 2 + 3.08};
  const std::vector<Precip> precs = {Precip::None, Precip::Rain, Precip::Snow};
  bool first = true;
  for (const OffPlay& op : OFF_PLAYS()) for (const DefCall& dc : DEF_PLAYS()) for (int mode = 0; mode < 3; mode++) {
    PlayConfig c; c.off = league[1].get(); c.def = league[2].get(); c.offPlay = &op; c.defCall = &dc;
    c.los = rng.range(5,95); c.ballY = rng.pick(ys); c.flip = rng.chance(0.5);
    c.weather.windX = rng.range(-5,5); c.weather.windY = rng.range(-5,5); c.weather.precip = rng.pick(precs); c.weather.tempF = 40;
    c.worldDir = 1; c.rng = rng.fork();
    if (mode == 1) c.userSide = Side::Off; else if (mode == 2) c.userSide = Side::Def;
    c.down = 3; c.toGo = 6;
    PlaySim sim(c);
    int k = 0;
    while (sim.phase == SimPhase::Live && k < 2400) {
      UserInput& I = sim.input;
      if (mode == 1) {
        I.move = k < 30 ? V2{0,0} : k < 90 ? V2{-0.3,0.5} : V2{1,0.2};
        I.sprint = k >= 90; I.lob = (k % 2) == 0;
        if (k == 75) I.throwTo = "X";
        if (k == 100 || k == 160) I.juke = true;
        if (k == 140) I.switchReq = true;
      } else if (mode == 2 && sim.user) {
        Ent* u = sim.user; double dx = sim.ball.pos.x - u->pos.x, dy = sim.ball.pos.y - u->pos.y; double l = jsHypot(dx,dy); if (l == 0) l = 1;
        I.move = V2{dx/l, dy/l}; I.sprint = true; I.dive = k % 20 == 0;
        if (k == 50 || k == 120) I.switchReq = true;
      }
      sim.step(); k++;
    }
    if (sim.phase == SimPhase::Live) sim.endPlay("tackle");
    const PlayResult& r = *sim.result;
    std::string line = op.id + " " + dc.id + " " + std::to_string(mode) + " " + sim.endReason + " " + playKindName(r.kind) + " " + hx(r.endX) + " " + std::to_string(r.yards) + " " + B(r.complete) + " " + B(r.interception) + " " + B(r.turnover) + " " + (r.td ? sideName(*r.td) : "null") + " " + hx(r.duration) + " ";
    for (size_t i = 0; i < r.tacklers.size(); i++) { if (i) line += ","; line += std::to_string(r.tacklers[i]->id); }
    line += " " + id(r.passer) + " " + id(r.rusher) + " " + id(r.receiver) + " " + id(r.target) + " " + std::to_string(r.airYards) + " " + std::to_string(r.yac) + " " + std::to_string(r.brokenTackles) + " " + B(r.thrownAway) + " " + B(r.pressured) + " " + hx(r.timeToThrow) + " " + (r.targetSpotY ? hx(*r.targetSpotY) : "u") + " ev:";
    for (size_t i = 0; i < r.events.size(); i++) { if (i) line += ","; line += r.events[i].type + "@" + std::to_string(jsRoundI(r.events[i].t * 60)); }
    line += std::string(" user:") + (sim.user ? sim.user->role : "-");
    for (const Ent* e : sim.ents) line += " " + hx(e->pos.x) + "/" + hx(e->pos.y) + "/" + hx(e->face) + "/" + animName(e->anim) + "/" + entStateName(e->state);
    line += std::string(" ball:") + ballModeName(sim.ball.mode) + "/" + hx(sim.ball.pos.x) + "/" + hx(sim.ball.pos.y) + "/" + hx(sim.ball.pos.z);
    if (!first) std::printf("\n"); first = false;
    std::printf("%s", line.c_str());
  }
  std::printf("\n");
}
