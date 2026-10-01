// Visual effects: bullet holes, tracers, particles, muzzle flashes
import * as THREE from 'three';
import { makeDecalTexture, makeGlowTexture, makeFlashTexture } from './textures.js';

export class Effects {
  constructor(scene) {
    this.scene = scene;
    this.group = new THREE.Group(); scene.add(this.group);
    // decals
    this.decalMat = new THREE.MeshBasicMaterial({ map: makeDecalTexture(), transparent: true, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -4 });
    this.decalGeo = new THREE.PlaneGeometry(5, 5);
    this.decals = []; this.decalIdx = 0;
    for (let i = 0; i < 160; i++) {
      const m = new THREE.Mesh(this.decalGeo, this.decalMat); m.visible = false; this.group.add(m); this.decals.push(m);
    }
    // particles (sprites)
    this.sparkTex = makeGlowTexture('255,200,120');
    this.bloodTex = makeGlowTexture('170,10,10');
    this.dustTex = makeGlowTexture('200,180,150');
    this.particles = [];
    this.tracers = [];
    this.flashTex = makeFlashTexture();
    this.impactBoxes = [];
  }
  clear() {
    for (const d of this.decals) d.visible = false;
    for (const p of this.particles) this.group.remove(p.sprite);
    for (const t of this.tracers) this.group.remove(t.mesh);
    for (const b of this.impactBoxes) this.group.remove(b.mesh);
    this.particles = []; this.tracers = []; this.impactBoxes = [];
  }
  decal(pos, normal) {
    const m = this.decals[this.decalIdx++ % this.decals.length];
    m.visible = true;
    m.position.copy(pos).addScaledVector(normal, 0.15);
    m.lookAt(pos.x + normal.x, pos.y + normal.y, pos.z + normal.z);
    m.rotateZ(Math.random() * 6.28);
    const s = 0.7 + Math.random() * 0.5; m.scale.set(s, s, s);
  }
  burst(pos, normal, kind = 'spark', count = 6) {
    const tex = kind === 'blood' ? this.bloodTex : kind === 'dust' ? this.dustTex : this.sparkTex;
    for (let i = 0; i < count; i++) {
      const mat = new THREE.SpriteMaterial({ map: tex, transparent: true, depthWrite: false, blending: kind === 'spark' ? THREE.AdditiveBlending : THREE.NormalBlending });
      const s = new THREE.Sprite(mat);
      const size = kind === 'blood' ? 4 + Math.random() * 6 : kind === 'dust' ? 6 + Math.random() * 8 : 1.5 + Math.random() * 2;
      s.scale.set(size, size, size);
      s.position.copy(pos);
      const v = new THREE.Vector3((Math.random() - 0.5), (Math.random() - 0.2), (Math.random() - 0.5)).multiplyScalar(kind === 'spark' ? 300 : 90);
      if (normal) v.addScaledVector(normal, kind === 'spark' ? 150 : 60);
      this.group.add(s);
      this.particles.push({ sprite: s, vel: v, life: 0, max: kind === 'spark' ? 0.25 : 0.5 + Math.random() * 0.3, grav: kind === 'dust' ? -30 : 500, size });
    }
  }
  tracer(from, to, color = 0xffd27a, width = 0.5, life = 0.12, additive = true) {
    const d = new THREE.Vector3().subVectors(to, from);
    const len = d.length();
    if (len < 1) return;
    const geo = new THREE.CylinderGeometry(width, width, len, 5, 1, true);
    geo.translate(0, len / 2, 0);
    const mat = new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.9, depthWrite: false, blending: additive ? THREE.AdditiveBlending : THREE.NormalBlending });
    const m = new THREE.Mesh(geo, mat);
    m.position.copy(from);
    m.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), d.normalize());
    this.group.add(m);
    this.tracers.push({ mesh: m, life: 0, max: life });
  }
  impactBox(pos, color) {
    const m = new THREE.Mesh(new THREE.BoxGeometry(3, 3, 3), new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.6, depthTest: false }));
    m.position.copy(pos); m.renderOrder = 20;
    this.group.add(m); this.impactBoxes.push({ mesh: m, life: 0, max: 2.5 });
  }
  muzzleFlash(pos, scale = 10) {
    const mat = new THREE.SpriteMaterial({ map: this.flashTex, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending });
    const s = new THREE.Sprite(mat); s.position.copy(pos); s.scale.set(scale, scale, scale); mat.rotation = Math.random() * 6;
    this.group.add(s);
    this.particles.push({ sprite: s, vel: new THREE.Vector3(), life: 0, max: 0.05, grav: 0, size: scale, flash: true });
  }
  update(dt) {
    for (let i = this.particles.length - 1; i >= 0; i--) {
      const p = this.particles[i];
      p.life += dt;
      if (p.life >= p.max) { this.group.remove(p.sprite); p.sprite.material.dispose(); this.particles.splice(i, 1); continue; }
      p.vel.y -= p.grav * dt;
      p.sprite.position.addScaledVector(p.vel, dt);
      p.sprite.material.opacity = 1 - p.life / p.max;
    }
    for (let i = this.tracers.length - 1; i >= 0; i--) {
      const t = this.tracers[i]; t.life += dt;
      if (t.life >= t.max) { this.group.remove(t.mesh); t.mesh.geometry.dispose(); t.mesh.material.dispose(); this.tracers.splice(i, 1); continue; }
      t.mesh.material.opacity = 0.9 * (1 - t.life / t.max);
    }
    for (let i = this.impactBoxes.length - 1; i >= 0; i--) {
      const t = this.impactBoxes[i]; t.life += dt;
      if (t.life >= t.max) { this.group.remove(t.mesh); t.mesh.geometry.dispose(); t.mesh.material.dispose(); this.impactBoxes.splice(i, 1); continue; }
      t.mesh.material.opacity = 0.6 * (1 - t.life / t.max);
    }
  }
}
