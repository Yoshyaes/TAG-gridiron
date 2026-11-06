// Three.js world: stadium, field, players, ball, overlays, cameras, weather.
import * as THREE from 'three';
import type { Team, Player } from '../sim/types';
import { FIELD_W } from '../sim/playbook';
import type { PlaySim, Ent, Weather } from '../sim/play';
import { makeFieldTexture, makeCrowdTexture } from './field';
import { PlayerModel, Uniform, shadowGeo } from './playerModel';

export type CamMode = 'broadcast' | 'sideline' | 'skycam' | 'player';
const HALF_W = FIELD_W / 2;

/** Convert offense-frame coordinates to world (x along field 0..100, z across). */
export function toWorld(x: number, y: number, dir: number): THREE.Vector3 {
  return dir > 0 ? new THREE.Vector3(x, 0, HALF_W - y) : new THREE.Vector3(100 - x, 0, y - HALF_W);
}

export class World {
  renderer: THREE.WebGLRenderer;
  scene = new THREE.Scene();
  camera: THREE.PerspectiveCamera;
  camMode: CamMode = 'broadcast';
  private models = new Map<number, PlayerModel>();
  private shadows = new Map<number, THREE.Mesh>();
  private active = new Set<number>();
  private ball: THREE.Group;
  private losLine: THREE.Mesh;
  private fdLine: THREE.Mesh;
  private camPos = new THREE.Vector3(50, 30, 40);
  private camLook = new THREE.Vector3(50, 0, 0);
  private weatherFx: THREE.Points | THREE.LineSegments | null = null;
  private prev = new Map<Ent, { x: number; y: number; face: number }>();
  private routeLines = new THREE.Group();
  private uniforms = new Map<number, Uniform>();
  private homeTeamId = -1;
  night: boolean;
  shake = 0;
  private fieldMesh: THREE.Mesh | null = null;
  private crowdMats: THREE.MeshStandardMaterial[] = [];
  crowdHype = 0;

  constructor(canvas: HTMLCanvasElement, night: boolean) {
    this.night = night;
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(Math.min(2, window.devicePixelRatio));
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = night ? 1.05 : 1.0;
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.camera = new THREE.PerspectiveCamera(42, 1, 0.1, 1500);
    this.resize();
    window.addEventListener('resize', () => this.resize());
    // ball
    this.ball = new THREE.Group();
    const leather = new THREE.Mesh(new THREE.SphereGeometry(0.1, 20, 14), new THREE.MeshStandardMaterial({ color: '#6b3a1e', roughness: 0.55 }));
    leather.scale.set(1.6, 1, 1);
    leather.castShadow = true;
    this.ball.add(leather);
    const laces = new THREE.Mesh(new THREE.BoxGeometry(0.09, 0.012, 0.02), new THREE.MeshStandardMaterial({ color: '#ffffff' }));
    laces.position.y = 0.1;
    this.ball.add(laces);
    this.scene.add(this.ball);
    // lines
    const lineGeo = new THREE.PlaneGeometry(0.22, FIELD_W - 0.6);
    this.losLine = new THREE.Mesh(lineGeo, new THREE.MeshBasicMaterial({ color: '#2f7bff', transparent: true, opacity: 0.85, depthWrite: false }));
    this.fdLine = new THREE.Mesh(lineGeo, new THREE.MeshBasicMaterial({ color: '#ffd400', transparent: true, opacity: 0.9, depthWrite: false }));
    for (const l of [this.losLine, this.fdLine]) {
      l.rotation.x = -Math.PI / 2;
      l.position.y = 0.02;
      l.renderOrder = 2;
      this.scene.add(l);
    }
    this.scene.add(this.routeLines);
  }

  resize() {
    const w = window.innerWidth;
    const h = window.innerHeight;
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
  }

  /** Build stadium for a matchup. */
  build(home: Team, away: Team, weather: Weather) {
    this.homeTeamId = home.id;
    const sky = this.night ? '#0a0f1e' : weather.precip !== 'none' ? '#8a929c' : '#9cc7ec';
    this.scene.background = new THREE.Color(sky);
    this.scene.fog = new THREE.Fog(sky, weather.precip === 'none' ? 260 : 120, weather.precip === 'none' ? 700 : 340);
    // lights
    const hemi = new THREE.HemisphereLight(this.night ? '#8fa3c7' : '#e8f2ff', '#2b3a1e', this.night ? 0.55 : 0.9);
    this.scene.add(hemi);
    const sun = new THREE.DirectionalLight(this.night ? '#fff6e5' : '#fff3d6', this.night ? 2.3 : 2.6);
    sun.position.set(this.night ? 50 : 20, 120, this.night ? 30 : 60);
    sun.target.position.set(50, 0, 0);
    sun.castShadow = true;
    sun.shadow.mapSize.set(4096, 4096);
    const sc = sun.shadow.camera;
    sc.left = -80;
    sc.right = 80;
    sc.top = 50;
    sc.bottom = -50;
    sc.near = 10;
    sc.far = 300;
    sun.shadow.bias = -0.0004;
    this.scene.add(sun);
    this.scene.add(sun.target);
    if (this.night) {
      for (const [x, z] of [[-5, -45], [105, -45], [-5, 45], [105, 45]]) {
        const l = new THREE.SpotLight('#fff4dd', 2600, 260, 0.75, 0.6, 1.6);
        l.position.set(x, 70, z);
        l.target.position.set(50, 0, 0);
        this.scene.add(l);
        this.scene.add(l.target);
      }
    }
    // field
    const ft = makeFieldTexture(home, away, weather.precip);
    const field = new THREE.Mesh(new THREE.PlaneGeometry(120, FIELD_W), new THREE.MeshStandardMaterial({ map: ft, roughness: weather.precip === 'rain' ? 0.45 : 0.92 }));
    field.rotation.x = -Math.PI / 2;
    field.position.set(50, 0, 0);
    field.receiveShadow = true;
    this.scene.add(field);
    this.fieldMesh = field;
    // surround turf + track
    const surround = new THREE.Mesh(new THREE.PlaneGeometry(170, 100), new THREE.MeshStandardMaterial({ color: weather.precip === 'snow' ? '#c9ced4' : '#24461f', roughness: 1 }));
    surround.rotation.x = -Math.PI / 2;
    surround.position.set(50, -0.02, 0);
    surround.receiveShadow = true;
    this.scene.add(surround);
    // stands
    const crowd = makeCrowdTexture(home);
    const standMat = (rep: number) => {
      const t = crowd.clone();
      t.needsUpdate = true;
      t.repeat.set(rep, 6);
      const m = new THREE.MeshStandardMaterial({ map: t, roughness: 1 });
      this.crowdMats.push(m);
      return m;
    };
    const wallMat = new THREE.MeshStandardMaterial({ color: home.colors.primary, roughness: 0.6 });
    const mkStand = (len: number, x: number, z: number, rotY: number) => {
      const g = new THREE.Group();
      const deck = new THREE.Mesh(new THREE.PlaneGeometry(len, 46), standMat(len / 30));
      deck.position.set(0, 18, -16);
      deck.rotation.x = -0.72;
      g.add(deck);
      const wall = new THREE.Mesh(new THREE.BoxGeometry(len, 2.2, 0.5), wallMat);
      wall.position.set(0, 1.1, 0);
      g.add(wall);
      const upper = new THREE.Mesh(new THREE.PlaneGeometry(len, 30), standMat(len / 30));
      upper.position.set(0, 46, -42);
      upper.rotation.x = -0.9;
      g.add(upper);
      const roof = new THREE.Mesh(new THREE.BoxGeometry(len, 1.5, 22), new THREE.MeshStandardMaterial({ color: '#3a3f4a', roughness: 0.8 }));
      roof.position.set(0, 60, -50);
      g.add(roof);
      g.position.set(x, 0, z);
      g.rotation.y = rotY;
      this.scene.add(g);
    };
    mkStand(150, 50, -HALF_W - 9, 0);
    mkStand(150, 50, HALF_W + 9, Math.PI);
    mkStand(80, -26, 0, Math.PI / 2);
    mkStand(80, 126, 0, -Math.PI / 2);
    // goal posts
    const postMat = new THREE.MeshStandardMaterial({ color: '#f2d21b', roughness: 0.4, metalness: 0.3 });
    for (const gx of [-10, 110]) {
      const g = new THREE.Group();
      const base = new THREE.Mesh(new THREE.CylinderGeometry(0.12, 0.12, 3.3), postMat);
      base.position.y = 1.65;
      g.add(base);
      const arm = new THREE.Mesh(new THREE.CylinderGeometry(0.08, 0.08, 2), postMat);
      arm.rotation.z = Math.PI / 2;
      arm.position.set(gx < 0 ? 1 : -1, 3.3, 0);
      g.add(arm);
      const cross = new THREE.Mesh(new THREE.CylinderGeometry(0.07, 0.07, 6.17), postMat);
      cross.rotation.x = Math.PI / 2;
      cross.position.set(0, 3.33, 0);
      const cg = new THREE.Group();
      cg.add(cross);
      for (const z of [-3.083, 3.083]) {
        const up = new THREE.Mesh(new THREE.CylinderGeometry(0.06, 0.06, 11.7), postMat);
        up.position.set(0, 3.33 + 5.85, z);
        cg.add(up);
      }
      cg.position.x = gx < 0 ? 2 : -2;
      g.add(cg);
      g.position.set(gx, 0, 0);
      g.traverse((o) => (o.castShadow = true));
      this.scene.add(g);
    }
    // pylons
    const pylonMat = new THREE.MeshStandardMaterial({ color: '#ff6a00', roughness: 0.5 });
    for (const x of [0, 100, -10, 110])
      for (const z of [-HALF_W, HALF_W]) {
        const p = new THREE.Mesh(new THREE.BoxGeometry(0.12, 0.5, 0.12), pylonMat);
        p.position.set(x, 0.25, z);
        this.scene.add(p);
      }
    // light towers
    const towerMat = new THREE.MeshStandardMaterial({ color: '#55585f', roughness: 0.7 });
    const lampMat = new THREE.MeshStandardMaterial({ color: '#ffffff', emissive: '#fff5d8', emissiveIntensity: this.night ? 3 : 0.6 });
    for (const [x, z] of [[-12, -62], [112, -62], [-12, 62], [112, 62]]) {
      const pole = new THREE.Mesh(new THREE.CylinderGeometry(0.6, 0.9, 70), towerMat);
      pole.position.set(x, 35, z);
      this.scene.add(pole);
      const panel = new THREE.Mesh(new THREE.BoxGeometry(14, 7, 1), lampMat);
      panel.position.set(x, 72, z);
      panel.lookAt(50, 0, 0);
      this.scene.add(panel);
    }
    // weather particles
    if (weather.precip !== 'none') this.makeWeather(weather);
    // uniforms: home in color, away in white
    const mk = (t: Team, homeSide: boolean): Uniform =>
      homeSide
        ? { jersey: t.colors.primary, pants: t.colors.secondary === '#FFFFFF' ? '#e8e8e8' : t.colors.secondary, helmet: t.colors.primary, trim: t.colors.secondary, numberColor: t.colors.accent }
        : { jersey: '#f2f2f2', pants: '#e6e6e6', helmet: t.colors.primary, trim: t.colors.primary, numberColor: t.colors.primary };
    for (const p of home.roster) this.uniforms.set(p.id, mk(home, true));
    for (const p of away.roster) this.uniforms.set(p.id, mk(away, false));
  }

  private makeWeather(w: Weather) {
    const n = w.precip === 'rain' ? 9000 : 6000;
    const pos = new Float32Array(n * (w.precip === 'rain' ? 6 : 3));
    for (let i = 0; i < n; i++) {
      const x = Math.random() * 180 - 40;
      const y = Math.random() * 60;
      const z = Math.random() * 120 - 60;
      if (w.precip === 'rain') {
        pos.set([x, y, z, x + 0.05, y - 0.8, z], i * 6);
      } else pos.set([x, y, z], i * 3);
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    this.weatherFx = w.precip === 'rain'
      ? new THREE.LineSegments(g, new THREE.LineBasicMaterial({ color: '#b7c4d6', transparent: true, opacity: 0.45 }))
      : new THREE.Points(g, new THREE.PointsMaterial({ color: '#ffffff', size: 0.18, transparent: true, opacity: 0.9 }));
    this.weatherFx.userData = { kind: w.precip, wind: w };
    this.scene.add(this.weatherFx);
  }

  private updateWeather(dt: number) {
    const fx = this.weatherFx;
    if (!fx) return;
    const rain = fx.userData.kind === 'rain';
    const w: Weather = fx.userData.wind;
    const arr = (fx.geometry.getAttribute('position') as THREE.BufferAttribute).array as Float32Array;
    const stride = rain ? 6 : 3;
    const vy = rain ? 32 : 2.2;
    for (let i = 0; i < arr.length; i += stride) {
      const dx = w.windX * dt * (rain ? 0.6 : 1) + (rain ? 0 : Math.sin(arr[i + 1] + i) * 0.01);
      const dz = -w.windY * dt * (rain ? 0.6 : 1);
      for (let k = 0; k < stride; k += 3) {
        arr[i + k] += dx;
        arr[i + k + 1] -= vy * dt;
        arr[i + k + 2] += dz;
      }
      if (arr[i + 1] < 0) {
        const y = 55 + Math.random() * 5;
        const x = Math.random() * 180 - 40;
        const z = Math.random() * 120 - 60;
        arr[i] = x; arr[i + 1] = y; arr[i + 2] = z;
        if (rain) { arr[i + 3] = x + 0.05; arr[i + 4] = y - 0.8; arr[i + 5] = z; }
      }
    }
    fx.geometry.getAttribute('position').needsUpdate = true;
  }

  private model(p: Player): PlayerModel {
    let m = this.models.get(p.id);
    if (!m) {
      const u = this.uniforms.get(p.id)!;
      m = new PlayerModel(p.number, u, p.heightIn, p.weightLb, p.id * 7);
      m.root.traverse((o) => {
        if ((o as THREE.Mesh).isMesh) o.castShadow = true;
      });
      this.models.set(p.id, m);
      this.scene.add(m.root);
      const sh = new THREE.Mesh(shadowGeo, new THREE.MeshBasicMaterial({ color: '#000', transparent: true, opacity: 0.22, depthWrite: false }));
      sh.rotation.x = -Math.PI / 2;
      sh.position.y = 0.015;
      this.shadows.set(p.id, sh);
      this.scene.add(sh);
    }
    return m;
  }

  /** Make exactly this set of players visible. */
  showPlayers(ps: Player[]) {
    const ids = new Set(ps.map((p) => p.id));
    for (const p of ps) this.model(p);
    for (const [id, m] of this.models) {
      m.root.visible = ids.has(id);
      this.shadows.get(id)!.visible = ids.has(id);
    }
    this.active = ids;
  }

  capturePrev(sim: PlaySim) {
    for (const e of sim.ents) this.prev.set(e, { x: e.pos.x, y: e.pos.y, face: e.face });
  }

  setLines(los: number, firstDown: number | null, dir: number) {
    const l = toWorld(los, HALF_W, dir);
    this.losLine.position.x = l.x;
    this.losLine.visible = true;
    if (firstDown !== null && firstDown < 100) {
      this.fdLine.position.x = toWorld(firstDown, HALF_W, dir).x;
      this.fdLine.visible = true;
    } else this.fdLine.visible = false;
  }
  hideLines() {
    this.losLine.visible = false;
    this.fdLine.visible = false;
  }

  /** Draw route/assignment previews (pre-snap, user's play). */
  drawRoutes(sim: PlaySim | null, dir: number) {
    this.routeLines.clear();
    if (!sim) return;
    for (const e of sim.off) {
      if (e.state !== 'route' || !e.route.length) continue;
      const pts = [toWorld(e.pos.x, e.pos.y, dir), ...e.route.map((p) => toWorld(p.x, p.y, dir))];
      for (const p of pts) p.y = 0.05;
      const g = new THREE.BufferGeometry().setFromPoints(pts);
      const color = e.settle ? '#ffd400' : '#ffffff';
      this.routeLines.add(new THREE.Line(g, new THREE.LineBasicMaterial({ color, transparent: true, opacity: 0.85 })));
    }
  }

  /** Sync models from sim with interpolation alpha. */
  syncPlay(sim: PlaySim, dir: number, alpha: number, dt: number, labels: Map<Ent, string>) {
    for (const e of sim.ents) {
      const m = this.model(e.p);
      const pv = this.prev.get(e) ?? { x: e.pos.x, y: e.pos.y, face: e.face };
      const x = pv.x + (e.pos.x - pv.x) * alpha;
      const y = pv.y + (e.pos.y - pv.y) * alpha;
      const w = toWorld(x, y, dir);
      m.root.position.set(w.x, 0, w.z);
      const fx = Math.cos(e.face);
      const fy = Math.sin(e.face);
      const wx = dir > 0 ? fx : -fx;
      const wz = dir > 0 ? -fy : fy;
      const target = Math.atan2(wx, wz);
      let cur = m.root.rotation.y;
      let d = target - cur;
      while (d > Math.PI) d -= Math.PI * 2;
      while (d < -Math.PI) d += Math.PI * 2;
      cur += d * Math.min(1, dt * 14);
      m.root.rotation.y = cur;
      m.animate(dt, e.speed, e.vmax, e.anim, e.animT, e.side);
      m.marker.visible = e.userControlled;
      const lbl = labels.get(e) ?? null;
      if ((m.label ? m.label.userData.t : null) !== lbl) {
        m.setLabel(lbl);
        if (m.label) m.label.userData.t = lbl;
      }
      const sh = this.shadows.get(e.p.id)!;
      sh.position.set(w.x, 0.015, w.z);
    }
    // ball
    const b = sim.ball;
    const bw = toWorld(b.pos.x, b.pos.y, dir);
    this.ball.position.set(bw.x, b.pos.z, bw.z);
    if (b.mode === 'air' || b.mode === 'loose') {
      const vw = new THREE.Vector3(dir > 0 ? b.vel.x : -b.vel.x, b.vel.z, dir > 0 ? -b.vel.y : b.vel.y);
      if (vw.lengthSq() > 0.01) {
        const q = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(1, 0, 0), vw.normalize());
        this.ball.quaternion.copy(q);
        this.ball.rotateX(b.spin);
      }
    } else if (b.holder) {
      const m = this.models.get(b.holder.p.id);
      if (m) {
        const fwd = new THREE.Vector3(Math.sin(m.root.rotation.y), 0, Math.cos(m.root.rotation.y));
        const side = new THREE.Vector3(fwd.z, 0, -fwd.x);
        this.ball.position.copy(m.root.position).add(fwd.multiplyScalar(0.18)).add(side.multiplyScalar(-0.22));
        this.ball.position.y = b.holder.anim === 'down' ? 0.35 : 1.15;
        this.ball.rotation.set(0, m.root.rotation.y + Math.PI / 2, 0.5);
      }
    }
  }

  /** Place a player directly (special-teams choreography). Coordinates in a team's offense frame. */
  pose(p: Player, x: number, y: number, dir: number, faceX: number, faceY: number, anim: string, speed: number, dt: number, animT = 0) {
    const m = this.model(p);
    m.root.visible = true;
    this.shadows.get(p.id)!.visible = true;
    const w = toWorld(x, y, dir);
    m.root.position.set(w.x, 0, w.z);
    const wx = dir > 0 ? faceX : -faceX;
    const wz = dir > 0 ? -faceY : faceY;
    if (Math.hypot(wx, wz) > 0.01) m.root.rotation.y = Math.atan2(wx, wz);
    m.animate(dt, speed, 9, anim, animT, 'off');
    m.marker.visible = false;
    if (m.label) m.setLabel(null);
    this.shadows.get(p.id)!.position.set(w.x, 0.015, w.z);
  }

  /** Cosmetic ball flight for kicks. */
  setBallFlight(fromX: number, toX: number, yy: number, h: number, t: number, dir: number) {
    const x = fromX + (toX - fromX) * t;
    const w = toWorld(x, yy, dir);
    this.ball.position.set(w.x, 0.3 + 4 * h * t * (1 - t), w.z);
    this.ball.rotation.z += 0.3;
  }

  ballWorld(): THREE.Vector3 {
    return this.ball.position.clone();
  }

  updateCamera(dt: number, focus: THREE.Vector3, dir: number, userPos: THREE.Vector3 | null) {
    const t = new THREE.Vector3();
    const look = new THREE.Vector3();
    switch (this.camMode) {
      case 'broadcast':
        t.set(focus.x - dir * 17, 10.5, focus.z * 0.45);
        look.set(focus.x + dir * 7, 0, focus.z * 0.8);
        break;
      case 'sideline':
        t.set(focus.x - dir * 4, 24, HALF_W + 38);
        look.set(focus.x + dir * 2, 0, focus.z * 0.5);
        break;
      case 'skycam':
        t.set(focus.x - dir * 24, 34, focus.z * 0.3);
        look.set(focus.x + dir * 10, 0, focus.z * 0.6);
        break;
      case 'player': {
        const f = userPos ?? focus;
        t.set(f.x - dir * 7, 3.6, f.z);
        look.set(f.x + dir * 8, 1, f.z);
        break;
      }
    }
    const k = 1 - Math.exp(-dt * 3.2);
    this.camPos.lerp(t, k);
    this.camLook.lerp(look, k * 1.3);
    this.camera.position.copy(this.camPos);
    if (this.shake > 0) {
      this.camera.position.x += (Math.random() - 0.5) * this.shake;
      this.camera.position.y += (Math.random() - 0.5) * this.shake;
      this.shake *= Math.exp(-dt * 6);
      if (this.shake < 0.01) this.shake = 0;
    }
    this.camera.lookAt(this.camLook);
  }

  snapCamera() {
    this.camera.position.copy(this.camPos);
  }

  render(dt: number) {
    this.updateWeather(dt);
    // crowd sway: scroll texture a little; more when hyped
    this.crowdHype = Math.max(0, this.crowdHype - dt * 0.25);
    for (const m of this.crowdMats) {
      if (m.map) m.map.offset.y = Math.sin(performance.now() * 0.012) * 0.002 * (0.3 + this.crowdHype * 3);
    }
    this.renderer.render(this.scene, this.camera);
  }

  dispose() {
    this.renderer.dispose();
  }
}
