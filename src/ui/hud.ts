// In-game HUD: scorebug, play-by-play ticker, weather box, banners, hints.
import type { Game, PlayLog } from '../sim/game';
import { fmtClock } from '../sim/game';

const el = (html: string): HTMLElement => {
  const d = document.createElement('div');
  d.innerHTML = html.trim();
  return d.firstElementChild as HTMLElement;
};

export class Hud {
  root: HTMLElement;
  bug: HTMLElement;
  ticker: HTMLElement;
  wx: HTMLElement;
  hintEl: HTMLElement;
  keysEl: HTMLElement;
  private lastLogLen = 0;

  constructor(private ui: HTMLElement, private game: Game) {
    this.root = el(`<div style="position:absolute;inset:0;pointer-events:none"></div>`);
    ui.appendChild(this.root);
    this.bug = el(`<div class="bug"></div>`);
    this.ticker = el(`<div class="ticker"></div>`);
    this.wx = el(`<div class="wx"></div>`);
    this.hintEl = el(`<div class="hint" style="display:none"></div>`);
    this.keysEl = el(`<div class="keys" style="display:none"></div>`);
    for (const x of [this.bug, this.ticker, this.wx, this.hintEl, this.keysEl]) this.root.appendChild(x);
    this.renderWx();
  }

  renderWx() {
    const g = this.game;
    const w = g.weather;
    const mph = Math.round(Math.hypot(w.windX, w.windY) * 2.045);
    const arrowAng = Math.atan2(-w.windY, w.windX) * (180 / Math.PI);
    const icon = w.precip === 'rain' ? 'Rain' : w.precip === 'snow' ? 'Snow' : 'Clear';
    this.wx.innerHTML = `<b>${icon}</b> ${Math.round(w.tempF)}&deg;F<br>Wind <b>${mph} mph</b> <span style="display:inline-block;transform:rotate(${arrowAng}deg)">&#10140;</span>`;
  }

  update(flag = false, clockOffset = 0) {
    const g = this.game;
    const team = (k: 'home' | 'away') => {
      const t = g.teamOf(k);
      const to = [0, 1, 2].map((i) => `<i class="${i < g.timeouts[k] ? '' : 'used'}"></i>`).join('');
      return `<div class="tm ${g.poss === k && g.phase !== 'final' ? 'has' : ''}" style="background:linear-gradient(90deg, ${t.colors.primary}, ${t.colors.primary}cc)">
        <span class="ab">${t.abbr}</span><span class="poss"></span><span class="sc">${g.score[k]}</span><div class="to">${to}</div></div>`;
    };
    const q = g.q <= 4 ? ['1st', '2nd', '3rd', '4th'][g.q - 1] : 'OT';
    let dd = '';
    if (g.phase === 'final') dd = 'FINAL';
    else if (g.phase === 'kickoff') dd = 'KICKOFF';
    else if (g.phase === 'pat') dd = 'PAT';
    else dd = `${g.downText()} &middot; ${g.ydLine()}`;
    this.bug.innerHTML = `${team('away')}${team('home')}<div class="mid"><span class="clk">${fmtClock(Math.max(0, g.clock - clockOffset))}</span><span class="q">${q}</span></div><div class="dd ${flag ? 'flag' : ''}">${dd}</div>`;
    if (g.log.length !== this.lastLogLen) {
      this.lastLogLen = g.log.length;
      const last = g.log.slice(-4).reverse();
      this.ticker.innerHTML = last.map((l: PlayLog, i) => `<div class="${l.big ?? ''}" style="opacity:${1 - i * 0.18}">${l.desc}</div>`).join('');
    }
  }

  banner(text: string, sub = '', color = '#fff') {
    const b = el(`<div class="banner" style="color:${color}">${text}${sub ? `<small>${sub}</small>` : ''}</div>`);
    this.root.appendChild(b);
    setTimeout(() => b.remove(), 2300);
  }

  hint(html: string | null) {
    if (!html) {
      this.hintEl.style.display = 'none';
      return;
    }
    this.hintEl.style.display = '';
    this.hintEl.innerHTML = html;
  }

  keys(html: string | null) {
    if (!html) {
      this.keysEl.style.display = 'none';
      return;
    }
    this.keysEl.style.display = '';
    this.keysEl.innerHTML = html;
  }

  destroy() {
    this.root.remove();
  }
}
