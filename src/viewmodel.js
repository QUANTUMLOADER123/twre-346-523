// First-person weapon + arms, rendered in a separate pass (own camera / FOV)
import * as THREE from 'three';
import { buildGun, applySkin, limbBetween } from './models.js';
import { makeFlashTexture } from './textures.js';

const V = (x, y, z) => new THREE.Vector3(x, y, z);

const LAYOUT = {
  rifle: { pos: V(7, -6.8, -14), rh: V(0, -2.6, 1.6), re: V(5, -9, 10), rs: V(10, -16, 22), lh: V(0, -0.2, -11), le: V(-5, -8, -3), ls: V(-12, -16, 10) },
  sniper: { pos: V(7, -7.2, -14), rh: V(0, -2.8, 2.6), re: V(5, -9, 11), rs: V(10, -16, 22), lh: V(0, -0.6, -9), le: V(-5, -8, -1), ls: V(-12, -16, 12) },
  pistol: { pos: V(5, -5.2, -14), rh: V(0.2, -2.6, 1.2), re: V(4, -8, 9), rs: V(8, -14, 20), lh: V(-1.0, -2.8, 0.6), le: V(-5, -8, 7), ls: V(-10, -14, 18) },
  knife: { pos: V(6.5, -6, -12), rh: V(0, 0, 1.4), re: V(4, -6, 9), rs: V(9, -14, 20), lh: null },
};
const KIND = { ak47: 'rifle', m4a4: 'rifle', awp: 'sniper', ssg08: 'sniper', deagle: 'pistol', knife: 'knife' };

export class Viewmodel {
  constructor(cfg) {
    this.cfg = cfg;
    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(54, innerWidth / innerHeight, 0.5, 300);
    this.scene.add(new THREE.HemisphereLight(0xdfe8ff, 0x3a3025, 1.6));
    const d = new THREE.DirectionalLight(0xffffff, 2.2); d.position.set(-3, 6, 4); this.scene.add(d);
    this.light = new THREE.PointLight(0xffb060, 0, 60); this.light.position.set(6, -4, -30); this.scene.add(this.light);
    this.root = new THREE.Group(); this.scene.add(this.root);
    this.items = {};
    this.sleeve = new THREE.MeshStandardMaterial({ color: 0x2f405a, roughness: 0.9 });
    this.glove = new THREE.MeshStandardMaterial({ color: 0x1c1c1c, roughness: 0.6 });
    this.flashTex = makeFlashTexture();
    this.current = null; this.id = null;
    this.t = 0; this.drawT = -10; this.kickT = -10; this.kickAmt = 0; this.reloadT = -10; this.reloadDur = 1; this.stab = false; this.slashFlip = false;
    this.swayX = 0; this.swayY = 0; this.bobPhase = 0; this.bobAmt = 0; this.landKick = 0;
    this.knifeModel = cfg.skins.knifeModel;
  }

  build(id) {
    const kind = KIND[id];
    const L = LAYOUT[kind];
    const holder = new THREE.Group();
    const gun = buildGun(id, { knife: this.cfg.skins.knifeModel });
    gun.traverse(o => { if (o.isMesh) { o.castShadow = false; } });
    holder.add(gun);
    // arms
    const mkArm = (hand, elbow, shoulder) => {
      const up = limbBetween(this.sleeve, 2.6, 2.3); up.userData.set(shoulder, elbow); gun.add(up);
      const fo = limbBetween(this.sleeve, 2.3, 1.8); fo.userData.set(elbow, hand); gun.add(fo);
      const g = new THREE.Mesh(new THREE.BoxGeometry(2.6, 2.8, 3.4), this.glove); g.position.copy(hand); gun.add(g);
      const cuff = limbBetween(this.glove, 1.95, 1.95); cuff.userData.set(hand.clone().lerp(elbow, 0.18), hand); gun.add(cuff);
    };
    mkArm(L.rh, L.re, L.rs);
    if (L.lh) mkArm(L.lh, L.le, L.ls);
    // muzzle flash
    const fm = new THREE.SpriteMaterial({ map: this.flashTex, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending });
    const flash = new THREE.Sprite(fm); flash.scale.set(9, 9, 9); flash.visible = false;
    flash.position.copy(gun.userData.muzzle.position);
    gun.add(flash);
    holder.userData = { gun, flash, kind, L };
    return holder;
  }

  setWeapon(id, force = false) {
    if (force) { for (const k of Object.keys(this.items)) { this.root.remove(this.items[k]); } this.items = {}; this.knifeModel = this.cfg.skins.knifeModel; }
    if (this.id === id && !force) return;
    if (this.current) this.root.remove(this.current);
    if (!this.items[id]) this.items[id] = this.build(id);
    this.current = this.items[id];
    this.root.add(this.current);
    this.id = id; this.drawT = this.t;
    this.reloadT = -10;
    applySkin(this.current.userData.gun, this.cfg.skins[id], this.cfg.skins.color);
  }
  applySkin(id, skin, color) {
    if (this.items[id]) applySkin(this.items[id].userData.gun, skin, color);
  }
  fire(w, stab) {
    this.kickT = this.t; this.kickAmt = w.kick || 1; this.stab = !!stab; this.slashFlip = !this.slashFlip;
    if (!w.melee && this.current) { this.current.userData.flash.visible = true; this.current.userData.flash.material.rotation = Math.random() * 6; this.flashUntil = this.t + 0.045; this.light.intensity = 40; }
  }
  reload(dur) { this.reloadT = this.t; this.reloadDur = dur; }

  update(dt, st) {
    this.t += dt;
    const c = this.cfg.visuals;
    // FOV conversion (CS viewmodel_fov is horizontal @4:3)
    const vfov = 2 * Math.atan(Math.tan(c.vmFov * Math.PI / 360) * 0.75) * 180 / Math.PI;
    if (this.camera.fov !== vfov) { this.camera.fov = vfov; this.camera.updateProjectionMatrix(); }
    if (!this.current) return;
    const ud = this.current.userData, L = ud.L;
    if (this.flashUntil && this.t > this.flashUntil) { ud.flash.visible = false; this.light.intensity = 0; }
    // sway
    this.swayX += (-st.mouseDX * 0.012 - this.swayX) * Math.min(1, dt * 10);
    this.swayY += (st.mouseDY * 0.012 - this.swayY) * Math.min(1, dt * 10);
    this.swayX = THREE.MathUtils.clamp(this.swayX, -1.2, 1.2); this.swayY = THREE.MathUtils.clamp(this.swayY, -1.2, 1.2);
    // bob
    const sp = st.onGround ? Math.min(1, st.speed / 250) : 0;
    this.bobAmt += (sp - this.bobAmt) * Math.min(1, dt * 8);
    this.bobPhase += dt * (6 + st.speed / 40);
    const bx = Math.sin(this.bobPhase) * 0.45 * this.bobAmt, by = -Math.abs(Math.cos(this.bobPhase)) * 0.4 * this.bobAmt;
    this.landKick *= Math.exp(-dt * 8);
    const pos = L.pos.clone();
    pos.x += c.vmX + bx + this.swayX; pos.y += c.vmY + by - this.swayY - this.landKick + (st.ducked ? 0.3 : 0); pos.z += c.vmZ;
    const rot = new THREE.Euler(0, 0, 0, 'YXZ');
    // air: lower the gun a bit
    if (!st.onGround) { pos.y -= 0.6; rot.x -= 0.03; }
    // draw
    const drawU = Math.min(1, (this.t - this.drawT) / 0.35);
    const de = 1 - Math.pow(1 - drawU, 3);
    pos.y -= (1 - de) * 10; rot.x -= (1 - de) * 0.9;
    // kick
    const kt = this.t - this.kickT;
    if (ud.kind === 'knife') {
      if (kt < 0.4) {
        const u = kt / 0.4, s = Math.sin(u * Math.PI);
        if (this.stab) { pos.z -= s * 8; rot.x += s * 0.3; }
        else { rot.y += s * (this.slashFlip ? 1 : -1) * 0.9; rot.z += s * (this.slashFlip ? 1 : -1) * 0.6; pos.x -= s * 4 * (this.slashFlip ? 1 : -1); }
      }
    } else if (kt < 0.25) {
      const e = Math.exp(-kt * 22) * this.kickAmt;
      pos.z += e * 1.8; rot.x += e * 0.07; pos.y += e * 0.2;
    }
    // reload
    const ru = (this.t - this.reloadT) / this.reloadDur;
    const mag = ud.gun.userData.mag;
    if (ru >= 0 && ru < 1) {
      const s = Math.sin(ru * Math.PI);
      rot.z += s * 0.45; rot.x += s * 0.25; pos.y -= s * 2; pos.x -= s * 1.5;
      let md = 0;
      if (ru > 0.15 && ru < 0.45) md = (ru - 0.15) / 0.3; else if (ru >= 0.45 && ru < 0.7) md = 1 - (ru - 0.45) / 0.25;
      if (mag) mag.position.y = -md * 12;
    } else if (mag) mag.position.y = 0;
    // inspect
    if (st.inspectT > 0) {
      const iu = (st.time - st.inspectT) / 3.0;
      if (iu >= 0 && iu < 1) {
        const s = Math.sin(iu * Math.PI);
        rot.y += s * 0.9 * Math.sin(iu * Math.PI * 2 + 0.5); rot.z += s * 0.6; pos.x -= s * 3; pos.y += s * 1.5;
        if (ud.kind === 'knife') { rot.x += Math.sin(iu * Math.PI * 4) * s * 1.2; }
      }
    }
    this.current.position.copy(pos);
    this.current.rotation.copy(rot);
    this.root.visible = st.visible;
  }

  render(renderer) {
    if (!this.root.visible || !this.current) return;
    renderer.clearDepth();
    renderer.render(this.scene, this.camera);
  }
  resize() { this.camera.aspect = innerWidth / innerHeight; this.camera.updateProjectionMatrix(); }
}
