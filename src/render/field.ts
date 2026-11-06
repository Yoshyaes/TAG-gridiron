// Procedural field texture: mowed stripes, yard lines, hashes, numbers, end zones. 1 world unit = 1 yard.
import * as THREE from 'three';
import type { Team } from '../sim/types';
import { FIELD_W, HASH_L, HASH_R } from '../sim/playbook';

const PX = 40; // pixels per yard

export function makeFieldTexture(home: Team, away: Team, weather: 'none' | 'rain' | 'snow'): THREE.CanvasTexture {
  const W = 120 * PX;
  const H = Math.round(FIELD_W * PX);
  const c = document.createElement('canvas');
  c.width = W;
  c.height = H;
  const g = c.getContext('2d')!;
  // grass with mowing stripes (5-yard bands) and subtle noise
  const base = weather === 'snow' ? ['#4f7a4a', '#4a7445'] : ['#2f6b2c', '#2a6127'];
  for (let i = 0; i < 24; i++) {
    g.fillStyle = base[i % 2];
    g.fillRect(i * 5 * PX, 0, 5 * PX, H);
  }
  const img = g.getImageData(0, 0, W, H);
  for (let i = 0; i < img.data.length; i += 4) {
    const n = (Math.random() - 0.5) * 14;
    img.data[i] += n * 0.6;
    img.data[i + 1] += n;
    img.data[i + 2] += n * 0.4;
  }
  g.putImageData(img, 0, 0);
  // end zones
  const ez = (x0: number, t: Team, flip: boolean) => {
    g.fillStyle = t.colors.primary;
    g.globalAlpha = 0.92;
    g.fillRect(x0 * PX, 0, 10 * PX, H);
    g.globalAlpha = 1;
    g.save();
    g.translate((x0 + 5) * PX, H / 2);
    g.rotate(flip ? Math.PI / 2 : -Math.PI / 2);
    g.fillStyle = t.colors.accent === '#FFFFFF' || t.colors.accent === '#ffffff' ? '#ffffff' : t.colors.accent;
    g.strokeStyle = t.colors.secondary;
    g.lineWidth = PX * 0.25;
    g.font = `900 ${PX * 5.2}px "Segoe UI", Arial, sans-serif`;
    g.textAlign = 'center';
    g.textBaseline = 'middle';
    g.strokeText(t.name.toUpperCase(), 0, 0);
    g.fillText(t.name.toUpperCase(), 0, 0);
    g.restore();
  };
  ez(0, away, false);
  ez(110, home, true);
  g.strokeStyle = '#f4f4f0';
  g.fillStyle = '#f4f4f0';
  // sidelines / end lines
  g.lineWidth = PX * 0.33;
  g.strokeRect(PX * 0.17, PX * 0.17, W - PX * 0.33, H - PX * 0.33);
  // yard lines
  for (let yd = 0; yd <= 100; yd++) {
    const x = (yd + 10) * PX;
    if (yd % 5 === 0) {
      g.lineWidth = yd === 0 || yd === 100 ? PX * 0.33 : PX * 0.13;
      g.beginPath();
      g.moveTo(x, PX * 0.3);
      g.lineTo(x, H - PX * 0.3);
      g.stroke();
    } else {
      // hash ticks at sideline and inbounds hashes
      g.lineWidth = PX * 0.1;
      const tick = (y: number, len: number) => {
        g.beginPath();
        g.moveTo(x, y);
        g.lineTo(x, y + len);
        g.stroke();
      };
      tick(PX * 0.33, PX * 0.67);
      tick(H - PX, PX * 0.67);
      tick((FIELD_W - HASH_L) * PX - PX * 0.33, PX * 0.67);
      tick((FIELD_W - HASH_R) * PX - PX * 0.33, PX * 0.67);
    }
  }
  // two-point conversion lines
  for (const yd of [2, 98]) {
    g.fillRect((yd + 10) * PX - PX * 0.06, H / 2 - PX * 0.5, PX * 0.12, PX);
  }
  // numbers (12 yards from sideline, 6 ft tall)
  g.font = `700 ${PX * 2}px "Segoe UI", Arial, sans-serif`;
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  for (let yd = 10; yd <= 90; yd += 10) {
    const n = String(yd <= 50 ? yd : 100 - yd);
    const x = (yd + 10) * PX;
    for (const [y, rot] of [[H - 12 * PX + PX, 0], [12 * PX - PX, Math.PI]] as [number, number][]) {
      g.save();
      g.translate(x, y);
      g.rotate(rot);
      g.fillText(n.split('').join(' '), 0, 0);
      // direction arrows toward nearest goal
      if (yd !== 50) {
        const dir = (yd < 50 ? -1 : 1) * (rot ? -1 : 1);
        g.beginPath();
        const ax = dir * PX * 2.2;
        g.moveTo(ax + dir * PX * 0.45, 0);
        g.lineTo(ax, -PX * 0.3);
        g.lineTo(ax, PX * 0.3);
        g.fill();
      }
      g.restore();
    }
  }
  // midfield emblem (original TAG mark)
  g.save();
  g.translate(60 * PX, H / 2);
  g.globalAlpha = 0.85;
  g.fillStyle = home.colors.primary;
  g.beginPath();
  for (let i = 0; i < 6; i++) {
    const a = (i / 6) * Math.PI * 2 + Math.PI / 6;
    g.lineTo(Math.cos(a) * PX * 5, Math.sin(a) * PX * 5);
  }
  g.closePath();
  g.fill();
  g.lineWidth = PX * 0.35;
  g.strokeStyle = home.colors.secondary;
  g.stroke();
  g.fillStyle = '#ffffff';
  g.font = `900 ${PX * 3}px "Segoe UI", Arial, sans-serif`;
  g.fillText(home.abbr, 0, 0);
  g.restore();
  g.globalAlpha = 1;
  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 16;
  tex.generateMipmaps = true;
  tex.minFilter = THREE.LinearMipmapLinearFilter;
  return tex;
}

/** Crowd texture: dense speckle of team colors. */
export function makeCrowdTexture(home: Team): THREE.CanvasTexture {
  const c = document.createElement('canvas');
  c.width = 1024;
  c.height = 256;
  const g = c.getContext('2d')!;
  g.fillStyle = '#1c1c22';
  g.fillRect(0, 0, c.width, c.height);
  const palette = [home.colors.primary, home.colors.secondary, '#d9d9d9', '#3a3a44', '#8a6f5a', '#c2a38a', '#2b2b30', home.colors.primary];
  for (let i = 0; i < 26000; i++) {
    g.fillStyle = palette[Math.floor(Math.random() * palette.length)];
    const x = Math.random() * c.width;
    const y = Math.random() * c.height;
    g.fillRect(x, y, 2 + Math.random() * 2, 3 + Math.random() * 2);
  }
  for (let r = 0; r < c.height; r += 8) {
    g.fillStyle = 'rgba(0,0,0,0.25)';
    g.fillRect(0, r, c.width, 1.5);
  }
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  t.wrapS = THREE.RepeatWrapping;
  t.wrapT = THREE.RepeatWrapping;
  return t;
}
