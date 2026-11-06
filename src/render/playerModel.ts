// Procedural football player: helmet + facemask, shoulder pads, jersey numbers, animated limbs.
import * as THREE from 'three';

export interface Uniform {
  jersey: string;
  pants: string;
  helmet: string;
  trim: string;
  numberColor: string;
}

const numberTexCache = new Map<string, THREE.CanvasTexture>();
function numberTexture(num: number, u: Uniform): THREE.CanvasTexture {
  const key = `${num}|${u.jersey}|${u.numberColor}|${u.trim}`;
  let t = numberTexCache.get(key);
  if (t) return t;
  const c = document.createElement('canvas');
  c.width = 128;
  c.height = 128;
  const g = c.getContext('2d')!;
  g.fillStyle = u.jersey;
  g.fillRect(0, 0, 128, 128);
  g.font = '900 86px "Arial Black", Arial, sans-serif';
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  g.lineWidth = 8;
  g.strokeStyle = u.trim;
  g.strokeText(String(num), 64, 70);
  g.fillStyle = u.numberColor;
  g.fillText(String(num), 64, 70);
  t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  numberTexCache.set(key, t);
  return t;
}

const matCache = new Map<string, THREE.MeshStandardMaterial>();
function mat(color: string, rough = 0.6, metal = 0): THREE.MeshStandardMaterial {
  const k = `${color}|${rough}|${metal}`;
  let m = matCache.get(k);
  if (!m) {
    m = new THREE.MeshStandardMaterial({ color, roughness: rough, metalness: metal });
    matCache.set(k, m);
  }
  return m;
}

const SKIN = ['#8d5524', '#c68642', '#e0ac69', '#f1c27d', '#5c3a1e', '#a0662f'];

// shared geometries (yards). A 6'2" player ~ 2.05 yd tall.
const G = {
  torso: new THREE.CapsuleGeometry(0.2, 0.42, 6, 12),
  pads: new THREE.BoxGeometry(0.62, 0.2, 0.36),
  helmet: new THREE.SphereGeometry(0.155, 20, 16),
  mask: new THREE.TorusGeometry(0.12, 0.016, 6, 16, Math.PI),
  thigh: new THREE.CapsuleGeometry(0.085, 0.3, 4, 8),
  shin: new THREE.CapsuleGeometry(0.07, 0.3, 4, 8),
  upperArm: new THREE.CapsuleGeometry(0.065, 0.24, 4, 8),
  foreArm: new THREE.CapsuleGeometry(0.055, 0.22, 4, 8),
  shoe: new THREE.BoxGeometry(0.11, 0.07, 0.24),
  numPlane: new THREE.PlaneGeometry(0.3, 0.3),
  shadow: new THREE.CircleGeometry(0.45, 16),
};

export class PlayerModel {
  root = new THREE.Group();
  body = new THREE.Group(); // leans
  hipL = new THREE.Group();
  hipR = new THREE.Group();
  kneeL = new THREE.Group();
  kneeR = new THREE.Group();
  shL = new THREE.Group();
  shR = new THREE.Group();
  elL = new THREE.Group();
  elR = new THREE.Group();
  head = new THREE.Group();
  phase = Math.random() * 6;
  scale = 1;
  marker: THREE.Mesh;
  label: THREE.Sprite | null = null;

  constructor(num: number, u: Uniform, heightIn: number, weightLb: number, skinIdx: number) {
    const s = heightIn / 74;
    const w = Math.min(1.35, Math.max(0.85, 0.75 + weightLb / 900));
    this.scale = s;
    const jersey = mat(u.jersey, 0.75);
    const pants = mat(u.pants, 0.7);
    const helmetM = mat(u.helmet, 0.25, 0.35);
    const skin = mat(SKIN[skinIdx % SKIN.length], 0.8);
    const sock = mat(u.trim, 0.8);
    const shoeM = mat('#111111', 0.5);
    const maskM = mat('#d8d8d8', 0.3, 0.6);

    this.root.add(this.body);
    this.body.position.y = 1.0 * s;
    // torso
    const torso = new THREE.Mesh(G.torso, jersey);
    torso.scale.set(w, s, w * 0.8);
    torso.position.y = 0.32 * s;
    torso.castShadow = true;
    this.body.add(torso);
    const pads = new THREE.Mesh(G.pads, jersey);
    pads.scale.set(w, 1, w);
    pads.position.y = 0.66 * s;
    pads.castShadow = true;
    this.body.add(pads);
    // numbers front/back + shoulder numbers
    const nt = numberTexture(num, u);
    const nm = new THREE.MeshStandardMaterial({ map: nt, roughness: 0.75 });
    const back = new THREE.Mesh(G.numPlane, nm);
    back.position.set(0, 0.42 * s, -0.17 * w - 0.005);
    back.rotation.y = Math.PI;
    this.body.add(back);
    const front = new THREE.Mesh(G.numPlane, nm);
    front.scale.setScalar(0.75);
    front.position.set(0, 0.42 * s, 0.17 * w + 0.005);
    this.body.add(front);
    // head
    this.head.position.y = 0.92 * s;
    this.body.add(this.head);
    const helmet = new THREE.Mesh(G.helmet, helmetM);
    helmet.scale.set(1, 1.05, 1.12);
    helmet.castShadow = true;
    this.head.add(helmet);
    const stripe = new THREE.Mesh(new THREE.BoxGeometry(0.04, 0.02, 0.34), mat(u.trim, 0.4));
    stripe.position.y = 0.155;
    this.head.add(stripe);
    const mask = new THREE.Mesh(G.mask, maskM);
    mask.rotation.set(0, 0, Math.PI);
    mask.position.set(0, -0.03, 0.13);
    mask.rotation.x = -0.35;
    this.head.add(mask);
    const mask2 = mask.clone();
    mask2.position.y = -0.07;
    mask2.scale.setScalar(0.85);
    this.head.add(mask2);
    // legs
    const leg = (hip: THREE.Group, knee: THREE.Group, x: number) => {
      hip.position.set(x * w, 0, 0);
      this.body.add(hip);
      const th = new THREE.Mesh(G.thigh, pants);
      th.position.y = -0.24 * s;
      th.scale.set(w, s, w);
      th.castShadow = true;
      hip.add(th);
      knee.position.y = -0.47 * s;
      hip.add(knee);
      const sh = new THREE.Mesh(G.shin, sock);
      sh.position.y = -0.22 * s;
      sh.scale.set(1, s, 1);
      sh.castShadow = true;
      knee.add(sh);
      const shoe = new THREE.Mesh(G.shoe, shoeM);
      shoe.position.set(0, -0.46 * s, 0.05);
      knee.add(shoe);
    };
    leg(this.hipL, this.kneeL, 0.11);
    leg(this.hipR, this.kneeR, -0.11);
    // arms
    const arm = (sh: THREE.Group, el: THREE.Group, x: number) => {
      sh.position.set(x * w, 0.62 * s, 0);
      this.body.add(sh);
      const ua = new THREE.Mesh(G.upperArm, jersey);
      ua.position.y = -0.17;
      ua.castShadow = true;
      sh.add(ua);
      el.position.y = -0.34;
      sh.add(el);
      const fa = new THREE.Mesh(G.foreArm, skin);
      fa.position.y = -0.15;
      fa.castShadow = true;
      el.add(fa);
    };
    arm(this.shL, this.elL, 0.33);
    arm(this.shR, this.elR, -0.33);
    // selection ring
    this.marker = new THREE.Mesh(new THREE.RingGeometry(0.55, 0.72, 32), new THREE.MeshBasicMaterial({ color: '#ffd400', transparent: true, opacity: 0.9, depthWrite: false }));
    this.marker.rotation.x = -Math.PI / 2;
    this.marker.position.y = 0.03;
    this.marker.visible = false;
    this.root.add(this.marker);
  }

  setLabel(text: string | null) {
    if (this.label) {
      this.root.remove(this.label);
      this.label = null;
    }
    if (!text) return;
    const c = document.createElement('canvas');
    c.width = 64;
    c.height = 64;
    const g = c.getContext('2d')!;
    g.fillStyle = '#ffd400';
    g.beginPath();
    g.arc(32, 32, 28, 0, Math.PI * 2);
    g.fill();
    g.fillStyle = '#111';
    g.font = '900 38px Arial';
    g.textAlign = 'center';
    g.textBaseline = 'middle';
    g.fillText(text, 32, 34);
    const sp = new THREE.Sprite(new THREE.SpriteMaterial({ map: new THREE.CanvasTexture(c), depthTest: false }));
    sp.scale.set(0.9, 0.9, 1);
    sp.position.y = 2.9;
    sp.renderOrder = 10;
    this.label = sp;
    this.root.add(sp);
  }

  /** Pose the rig. speed in yd/s, anim state name, animT seconds in state. */
  animate(dt: number, speed: number, vmax: number, anim: string, animT: number, side: 'off' | 'def') {
    const b = this.body;
    const s = this.scale;
    // reset
    b.rotation.set(0, 0, 0);
    b.position.y = 1.0 * s;
    b.position.z = 0;
    this.head.rotation.set(0, 0, 0);
    const f = Math.min(1, speed / Math.max(1, vmax));
    let legAmp = 0.15 + f * 0.95;
    let armAmp = 0.1 + f * 0.9;
    this.phase += dt * (speed / 2.1) * Math.PI * 2 * 0.55 + dt * 0.5;
    const p = this.phase;
    let lean = f * 0.32;
    let hipL = Math.sin(p) * legAmp;
    let hipR = -Math.sin(p) * legAmp;
    let kneeL = Math.max(0, -Math.sin(p + 0.9)) * legAmp * 1.6 + 0.08;
    let kneeR = Math.max(0, Math.sin(p + 0.9)) * legAmp * 1.6 + 0.08;
    let shL = -Math.sin(p) * armAmp;
    let shR = Math.sin(p) * armAmp;
    let elL = -0.9 * f - 0.25;
    let elR = -0.9 * f - 0.25;
    let shLz = 0.12;
    let shRz = -0.12;
    let bob = Math.abs(Math.sin(p)) * 0.06 * f;
    switch (anim) {
      case 'idle':
        lean = side === 'def' ? 0.35 : 0.2; // ready stance
        hipL = hipR = -0.35;
        kneeL = kneeR = 0.6;
        shL = shR = 0.2;
        elL = elR = -0.6;
        b.position.y = 0.9 * s;
        bob = 0;
        break;
      case 'block':
        lean = 0.5;
        hipL = -0.6 + Math.sin(p * 1.5) * 0.15;
        hipR = -0.3 - Math.sin(p * 1.5) * 0.15;
        kneeL = kneeR = 0.7;
        shL = shR = -1.35;
        elL = elR = -0.25;
        b.position.y = 0.88 * s;
        break;
      case 'throw': {
        const t = Math.min(1, animT / 0.35);
        shR = t < 0.5 ? 2.4 * (t / 0.5) : 2.4 - 3.6 * ((t - 0.5) / 0.5);
        elR = -1.2 + t * 1.1;
        shL = -0.6;
        b.rotation.y = 0.5 - t * 0.9;
        lean = 0.1 + t * 0.2;
        break;
      }
      case 'catch':
        shL = shR = -2.6 + Math.min(1, animT * 3);
        elL = elR = -0.2;
        break;
      case 'tackle':
      case 'dive': {
        const t = Math.min(1, animT / 0.35);
        lean = 0.4 + t * 1.0;
        shL = shR = -2.6;
        elL = elR = -0.1;
        b.position.y = Math.max(0.35, 1.0 - t * 0.6) * s;
        hipL = hipR = 0.6 * t;
        break;
      }
      case 'down': {
        const t = Math.min(1, animT / 0.4);
        b.rotation.x = (Math.PI / 2) * 0.95 * t;
        b.position.y = (1.0 - t * 0.78) * s;
        hipL = 0.2;
        hipR = -0.1;
        kneeL = kneeR = 0.2;
        shL = -2.2;
        shR = -1.6;
        elL = elR = -0.2;
        lean = 0;
        bob = 0;
        break;
      }
      case 'kneel':
        hipL = -1.5;
        kneeL = 1.5;
        hipR = 0.2;
        kneeR = 1.6;
        b.position.y = 0.55 * s;
        lean = 0.1;
        bob = 0;
        break;
      case 'celebrate':
        shL = shR = -2.9 + Math.sin(animT * 9) * 0.25;
        shLz = 0.5;
        shRz = -0.5;
        elL = elR = 0;
        b.position.y = 1.0 * s + Math.abs(Math.sin(animT * 6)) * 0.25;
        lean = 0;
        break;
      case 'juke':
        b.rotation.z = Math.sin(Math.min(1, animT / 0.35) * Math.PI) * 0.4;
        break;
    }
    if (anim !== 'down') b.rotation.x = lean;
    b.position.y += bob;
    this.hipL.rotation.x = -hipL;
    this.hipR.rotation.x = -hipR;
    this.kneeL.rotation.x = kneeL;
    this.kneeR.rotation.x = kneeR;
    this.shL.rotation.x = -shL;
    this.shR.rotation.x = -shR;
    this.shL.rotation.z = shLz;
    this.shR.rotation.z = shRz;
    this.elL.rotation.x = elL;
    this.elR.rotation.x = elR;
  }
}

export const shadowGeo = G.shadow;
