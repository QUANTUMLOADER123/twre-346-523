// Procedural 3D models: player characters (bone hierarchy + hitboxes) and weapons.
import * as THREE from 'three';
import { makeFadeTexture, makeCamoTexture } from './textures.js';

const D2R = Math.PI / 180;
const V = (x, y, z) => new THREE.Vector3(x, y, z);

// ------------------------------------------------------------------ helpers
const geoCache = new Map();
function boxGeo(w, h, d) {
  const k = `b${w}|${h}|${d}`;
  if (!geoCache.has(k)) geoCache.set(k, new THREE.BoxGeometry(w, h, d));
  return geoCache.get(k);
}
function cylGeo(r1, r2, h, seg = 10) {
  const k = `c${r1}|${r2}|${h}|${seg}`;
  if (!geoCache.has(k)) geoCache.set(k, new THREE.CylinderGeometry(r1, r2, h, seg));
  return geoCache.get(k);
}
function sphGeo(r, ws = 12, hs = 8, phiLen = Math.PI * 2, thetaLen = Math.PI) {
  const k = `s${r}|${ws}|${hs}|${thetaLen}`;
  if (!geoCache.has(k)) geoCache.set(k, new THREE.SphereGeometry(r, ws, hs, 0, phiLen, 0, thetaLen));
  return geoCache.get(k);
}
function addBox(parent, w, h, d, mat, x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0) {
  const m = new THREE.Mesh(boxGeo(w, h, d), mat);
  m.position.set(x, y, z); m.rotation.set(rx, ry, rz);
  m.castShadow = true; m.receiveShadow = true;
  parent.add(m); return m;
}
function addCylZ(parent, r, len, mat, x, y, z, seg = 10, r2) {
  const m = new THREE.Mesh(cylGeo(r, r2 ?? r, len, seg), mat);
  m.rotation.x = Math.PI / 2; m.position.set(x, y, z);
  m.castShadow = true; parent.add(m); return m;
}
function addCylY(parent, r, len, mat, x, y, z, seg = 10, r2) {
  const m = new THREE.Mesh(cylGeo(r, r2 ?? r, len, seg), mat);
  m.position.set(x, y, z); m.castShadow = true; m.receiveShadow = true; parent.add(m); return m;
}
const std = (color, rough = 0.8, metal = 0.0, extra = {}) => new THREE.MeshStandardMaterial({ color, roughness: rough, metalness: metal, ...extra });

// ------------------------------------------------------------------ characters
const TEAM_STYLE = {
  T: { shirt: 0x7d6b4f, pants: 0x4f4a3a, vest: 0x3a3a2e, skin: 0xc49a78, mask: 0x1c1c1c, boots: 0x2a2018, gloves: 0x252525, helmet: null, accent: 0x8a2a1a },
  CT: { shirt: 0x2f405a, pants: 0x283246, vest: 0x1b2532, skin: 0xd0a888, mask: 0x1a1a1a, boots: 0x161616, gloves: 0x1a1a1a, helmet: 0x1f2a36, accent: 0x3a6ea5 },
};

export function createCharacter(team, opts = {}) {
  const st = TEAM_STYLE[team] || TEAM_STYLE.T;
  const mats = {
    shirt: std(st.shirt, 0.9), pants: std(st.pants, 0.9), vest: std(st.vest, 0.75), skin: std(st.skin, 0.7),
    mask: std(st.mask, 0.95), boots: std(st.boots, 0.6), gloves: std(st.gloves, 0.7), accent: std(st.accent, 0.6),
    helmet: std(st.helmet || 0x222222, 0.5, 0.2), lens: std(0x223344, 0.1, 0.8, { emissive: 0x050a10 }),
  };
  const root = new THREE.Group(); root.rotation.order = 'YXZ';
  const B = {};
  const mk = (name, parent, x, y, z) => { const o = new THREE.Group(); o.name = name; o.position.set(x, y, z); o.rotation.order = 'YXZ'; parent.add(o); B[name] = o; return o; };

  const pelvis = mk('pelvis', root, 0, 36, 0);
  addBox(pelvis, 13, 7, 8, mats.pants, 0, 0, 0);
  addBox(pelvis, 13.6, 1.6, 8.6, mats.boots, 0, 3, 0); // belt
  for (const side of [-1, 1]) {
    const thigh = mk(side < 0 ? 'thighL' : 'thighR', pelvis, side * 3.6, -2.5, 0);
    addBox(thigh, 5.6, 17, 6.2, mats.pants, 0, -8.5, 0);
    addBox(thigh, 4, 4, 1.5, mats.vest, side * 0.4, -8, -3.4); // pocket
    const shin = mk(side < 0 ? 'shinL' : 'shinR', thigh, 0, -17, 0);
    addBox(shin, 5, 14, 5.4, mats.pants, 0, -7, 0);
    addBox(shin, 5.4, 3, 6, mats.vest, 0, -1, -0.3); // knee pad
    addBox(shin, 5.4, 4.5, 9, mats.boots, 0, -15.5, -1.6); // boot
  }
  const spine = mk('spine', pelvis, 0, 3, 0);
  addBox(spine, 13, 11, 7.5, mats.shirt, 0, 5.5, 0);
  const chest = mk('chest', spine, 0, 10, 0);
  addBox(chest, 15, 11, 8.5, mats.shirt, 0, 5, 0);
  addBox(chest, 16, 13, 10, mats.vest, 0, 3.5, 0); // vest
  for (const px of [-4.5, 0, 4.5]) addBox(chest, 3.6, 4.2, 2, mats.vest, px, 0.5, -5.6); // mag pouches
  addBox(chest, 4, 2.5, 0.5, mats.accent, -4, 7.5, -5.2); // patch
  const neck = mk('neck', chest, 0, 10.5, 0);
  addCylY(neck, 2.4, 3, mats.skin, 0, 1, 0, 8);
  const head = mk('head', neck, 0, 2.5, 0);
  addBox(head, 8, 9, 8.6, team === 'T' ? mats.mask : mats.skin, 0, 4.6, 0);
  if (team === 'T') {
    addBox(head, 6.6, 1.6, 0.6, mats.skin, 0, 5.8, -4.4); // eye slit
    addBox(head, 1.3, 0.8, 0.4, std(0x111111), -1.6, 5.8, -4.8);
    addBox(head, 1.3, 0.8, 0.4, std(0x111111), 1.6, 5.8, -4.8);
  } else {
    const helm = new THREE.Mesh(sphGeo(5.6, 14, 8, Math.PI * 2, Math.PI / 2), mats.helmet);
    helm.position.set(0, 7, 0.2); helm.scale.set(1, 0.85, 1.08); helm.castShadow = true; head.add(helm);
    addBox(head, 7.4, 2.2, 1.2, mats.mask, 0, 6.4, -4.4); // goggles strap
    addBox(head, 2.6, 1.8, 0.8, mats.lens, -1.7, 6.4, -5);
    addBox(head, 2.6, 1.8, 0.8, mats.lens, 1.7, 6.4, -5);
    addBox(head, 6, 3, 0.8, mats.mask, 0, 2.6, -4.4); // face mask
  }
  // arms
  for (const side of [-1, 1]) {
    const sh = mk(side < 0 ? 'upperArmL' : 'upperArmR', chest, side * 9, 8, 0);
    addBox(sh, 5, 5, 5, mats.vest, 0, -1, 0); // shoulder pad
    addBox(sh, 4.4, 11, 4.4, mats.shirt, 0, -5.5, 0);
    const fa = mk(side < 0 ? 'foreArmL' : 'foreArmR', sh, 0, -11, 0);
    addBox(fa, 4, 10, 4, mats.shirt, 0, -5, 0);
    addBox(fa, 3.8, 3.6, 4.2, mats.gloves, 0, -11, 0);
  }
  // weapon mount
  const gunMount = mk('gunMount', chest, 2.5, 3, -8);

  // collect meshes, add chams twins
  const meshes = [];
  root.traverse(o => { if (o.isMesh) meshes.push(o); });
  const twins = [];
  const hiddenMat = new THREE.MeshBasicMaterial({ color: 0xff00ff, transparent: true, opacity: 0.85, depthFunc: THREE.GreaterDepth, depthWrite: false });
  for (const m of meshes) {
    m.userData.baseMat = m.material;
    const t = new THREE.Mesh(m.geometry, hiddenMat);
    t.position.copy(m.position); t.rotation.copy(m.rotation); t.scale.copy(m.scale);
    t.visible = false; t.renderOrder = 5; t.castShadow = false;
    m.parent.add(t); twins.push(t);
  }

  const hitboxes = [
    { bone: B.head, a: V(0, 4.8, -0.4), b: V(0, 4.8, -0.4), r: 5.0, group: 'head' },
    { bone: B.neck, a: V(0, 0, 0), b: V(0, 2.5, 0), r: 2.8, group: 'chest' },
    { bone: B.chest, a: V(0, 0.5, 0), b: V(0, 8, 0), r: 7.6, group: 'chest' },
    { bone: B.spine, a: V(0, 1, 0), b: V(0, 9, 0), r: 7.0, group: 'stomach' },
    { bone: B.pelvis, a: V(-3, 0, 0), b: V(3, 0, 0), r: 5.6, group: 'stomach' },
    { bone: B.upperArmL, a: V(0, 0, 0), b: V(0, -11, 0), r: 2.7, group: 'arm' },
    { bone: B.upperArmR, a: V(0, 0, 0), b: V(0, -11, 0), r: 2.7, group: 'arm' },
    { bone: B.foreArmL, a: V(0, 0, 0), b: V(0, -11, 0), r: 2.3, group: 'arm' },
    { bone: B.foreArmR, a: V(0, 0, 0), b: V(0, -11, 0), r: 2.3, group: 'arm' },
    { bone: B.thighL, a: V(0, 0, 0), b: V(0, -17, 0), r: 3.6, group: 'leg' },
    { bone: B.thighR, a: V(0, 0, 0), b: V(0, -17, 0), r: 3.6, group: 'leg' },
    { bone: B.shinL, a: V(0, 0, 0), b: V(0, -16, 0), r: 3.0, group: 'leg' },
    { bone: B.shinR, a: V(0, 0, 0), b: V(0, -16, 0), r: 3.0, group: 'leg' },
  ].map(h => ({ ...h, wa: new THREE.Vector3(), wb: new THREE.Vector3() }));

  const skeletonPairs = [
    ['head', 'neck'], ['neck', 'chest'], ['chest', 'spine'], ['spine', 'pelvis'],
    ['chest', 'upperArmL'], ['upperArmL', 'foreArmL'], ['chest', 'upperArmR'], ['upperArmR', 'foreArmR'],
    ['pelvis', 'thighL'], ['thighL', 'shinL'], ['pelvis', 'thighR'], ['thighR', 'shinR'],
  ];

  const model = {
    root, bones: B, meshes, twins, hiddenMat, hitboxes, skeletonPairs, mats, gun: null, gunId: null, team,
    chamsState: '',
    setGun(id) {
      if (this.gunId === id) return;
      if (this.gun) gunMount.remove(this.gun);
      this.gun = buildGun(id, { world: true });
      this.gun.traverse(o => { if (o.isMesh) o.castShadow = true; });
      gunMount.add(this.gun);
      this.gunId = id;
      // gun meshes take part in chams
      this.gunMeshes = [];
      this.gun.traverse(o => { if (o.isMesh) { o.userData.baseMat = o.material; this.gunMeshes.push(o); } });
      this.chamsState = '';
    },
    setPose(p) { applyPose(this, p); },
    updateHitboxes() {
      root.updateMatrixWorld(true);
      for (const h of hitboxes) { h.wa.copy(h.a).applyMatrix4(h.bone.matrixWorld); h.wb.copy(h.b).applyMatrix4(h.bone.matrixWorld); }
    },
    headPos(out = new THREE.Vector3()) { return out.copy(hitboxes[0].wa); },
    bonePos(name, out = new THREE.Vector3()) { return out.setFromMatrixPosition(B[name].matrixWorld); },
    setChams(visMat, hidMat, hidColor) {
      const key = (visMat ? visMat.uuid : '-') + (hidMat ? hidMat.uuid : '-');
      if (key !== this.chamsState) {
        this.chamsState = key;
        for (const m of meshes) m.material = visMat || m.userData.baseMat;
        if (this.gunMeshes) for (const m of this.gunMeshes) m.material = visMat || m.userData.baseMat;
        for (const t of twins) { t.visible = !!hidMat; if (hidMat) t.material = hidMat; }
      }
    },
    setGhost(mat) {
      for (const m of meshes) { m.material = mat; m.castShadow = false; }
      if (this.gunMeshes) for (const m of this.gunMeshes) { m.material = mat; m.castShadow = false; }
    },
  };
  return model;
}

function wrap180(a) { a = ((a + 180) % 360 + 360) % 360 - 180; return a; }

function applyPose(model, p) {
  const B = model.bones;
  const duck = p.duck || 0;
  model.root.rotation.set(0, (p.bodyYaw || 0) * D2R, 0);
  const twist = THREE.MathUtils.clamp(wrap180((p.aimYaw ?? p.bodyYaw) - (p.bodyYaw || 0)), -85, 85);
  const pitch = THREE.MathUtils.clamp(p.pitch || 0, -89, 89);
  B.pelvis.position.y = 36 - duck * 16;
  B.spine.rotation.set(-pitch * 0.4 * D2R + duck * 0.15, twist * 0.6 * D2R, 0);
  B.chest.rotation.set(-pitch * 0.15 * D2R, 0, 0);
  B.neck.rotation.set(0, twist * 0.4 * D2R, 0);
  B.head.rotation.set(-pitch * 0.45 * D2R, 0, 0);

  // legs
  const ph = p.phase || 0;
  const fwd = THREE.MathUtils.clamp((p.speedFwd || 0) / 250, -1, 1);
  const side = THREE.MathUtils.clamp((p.speedSide || 0) / 250, -1, 1);
  const amp = 0.55;
  const s = Math.sin(ph);
  if (p.air) {
    B.thighL.rotation.set(0.7, 0, 0); B.thighR.rotation.set(0.35, 0, 0);
    B.shinL.rotation.set(-1.1, 0, 0); B.shinR.rotation.set(-0.7, 0, 0);
  } else {
    B.thighL.rotation.set(duck * 1.3 + s * amp * fwd, 0, s * amp * side * 0.6);
    B.thighR.rotation.set(duck * 1.3 - s * amp * fwd, 0, -s * amp * side * 0.6);
    B.shinL.rotation.set(-duck * 2.3 - Math.max(0, -s) * Math.abs(fwd) * 0.9, 0, 0);
    B.shinR.rotation.set(-duck * 2.3 - Math.max(0, s) * Math.abs(fwd) * 0.9, 0, 0);
  }
  // arms holding the weapon (angles solved so the hands reach grip / handguard)
  const pistol = p.pistol;
  if (pistol) {
    B.upperArmR.rotation.set(1.23, 0, -0.52); B.foreArmR.rotation.set(0.3, 0, 0);
    B.upperArmL.rotation.set(1.23, 0, 0.52); B.foreArmL.rotation.set(0.3, 0, 0);
  } else {
    B.upperArmR.rotation.set(0.2, 0, 0.2); B.foreArmR.rotation.set(2.1, 0, -0.96);
    B.upperArmL.rotation.set(1.29, 0, 0.52); B.foreArmL.rotation.set(0.1, 0, 0);
  }
  B.gunMount.position.set(pistol ? 0 : 2.5, pistol ? 4.5 : 3, pistol ? -17 : -8);
  // gun follows pitch a bit more
  B.gunMount.rotation.set(0, 0, 0);
  // death fall
  if (p.dead) {
    const t = Math.min(1, p.dead);
    model.root.rotation.x = t * Math.PI / 2 * (p.deathDir || 1);
    B.pelvis.position.y = 36 - duck * 16 * (1 - t) - t * 8;
  }
}

// ------------------------------------------------------------------ weapons
const M = {
  metal: () => std(0x2a2a2c, 0.45, 0.7),
  metalLight: () => std(0x55575a, 0.4, 0.8),
  wood: () => std(0x7a4520, 0.65, 0.0),
  bakelite: () => std(0x7a3010, 0.5, 0.1),
  polymer: () => std(0x1b1b1d, 0.7, 0.1),
  green: () => std(0x4c5a3c, 0.7, 0.1),
  slate: () => std(0x39414e, 0.6, 0.3),
  silver: () => std(0xc4c4c8, 0.25, 0.95),
  blade: () => std(0xd8dade, 0.18, 1.0),
  lens: () => std(0x112244, 0.05, 0.9, { emissive: 0x0a1630 }),
};

export function buildGun(id, opts = {}) {
  const g = new THREE.Group();
  g.userData.skinMats = [];
  const muzzle = new THREE.Object3D();
  const mag = new THREE.Group(); g.add(mag); g.userData.mag = mag;
  const skin = (mat) => { g.userData.skinMats.push(mat); return mat; };
  if (id === 'ak47') {
    const metal = M.metal(), wood = skin(M.wood()), bake = M.bakelite();
    addBox(g, 2.0, 2.4, 11, metal, 0, 0.6, -2);
    addBox(g, 1.8, 0.5, 10.4, metal, 0, 2.0, -2);
    addCylZ(g, 0.32, 16, metal, 0, 1.0, -15.5);
    addCylZ(g, 0.5, 1.8, metal, 0, 1.0, -24);
    addCylZ(g, 0.42, 8.5, metal, 0, 2.0, -11.5);
    addBox(g, 2.3, 1.7, 7, wood, 0, 0.6, -11);
    addBox(g, 1.7, 0.9, 6, wood, 0, 2.05, -11.2);
    addBox(g, 0.4, 1.8, 0.6, metal, 0, 1.9, -22);
    addBox(g, 1.0, 0.6, 1.6, metal, 0, 2.4, -6.5);
    addBox(g, 1.7, 2.6, 9.5, wood, 0, -0.1, 7.8, -0.12);
    addBox(g, 1.8, 3.6, 0.7, metal, 0, -0.7, 12.5, -0.12);
    addBox(g, 1.4, 3.8, 1.9, wood, 0, -2.0, 1.2, 0.32);
    addBox(g, 0.3, 0.3, 2.6, metal, 0, -1.0, -1.4);
    addBox(mag, 1.25, 2.6, 2.3, bake, 0, -1.4, -4.6);
    addBox(mag, 1.25, 2.6, 2.3, bake, 0, -3.6, -5.2, -0.28);
    addBox(mag, 1.25, 2.6, 2.3, bake, 0, -5.7, -6.3, -0.5);
    muzzle.position.set(0, 1.0, -25.5);
  } else if (id === 'm4a4') {
    const blk = skin(M.polymer()), metal = M.metal();
    addBox(g, 1.9, 2.5, 10, blk, 0, 0.6, -2);
    addBox(g, 0.9, 0.5, 10, metal, 0, 2.1, -2);
    for (let i = 0; i < 8; i++) addBox(g, 1.0, 0.35, 0.5, metal, 0, 2.45, -6 + i * 1.1);
    addBox(g, 2.3, 2.3, 8.5, blk, 0, 0.8, -11.2);
    for (let i = 0; i < 6; i++) addBox(g, 2.4, 0.25, 0.6, metal, 0, 0.8, -8 - i * 1.3);
    addCylZ(g, 0.3, 8, metal, 0, 0.9, -19);
    addCylZ(g, 0.45, 1.8, metal, 0, 0.9, -23.6);
    addBox(g, 0.4, 2.2, 0.5, blk, 0, 2.4, -15);
    addCylZ(g, 0.65, 5, metal, 0, 1.0, 4.8);
    addBox(g, 1.6, 2.5, 5.5, blk, 0, 0.4, 8.6, -0.05);
    addBox(g, 1.3, 3.5, 1.8, blk, 0, -2.0, 1.2, 0.32);
    addBox(g, 0.3, 0.3, 2.6, metal, 0, -1.0, -1.4);
    addBox(mag, 1.15, 6.2, 2.3, metal, 0, -3.3, -4.8, -0.14);
    muzzle.position.set(0, 0.9, -24.6);
  } else if (id === 'awp' || id === 'ssg08') {
    const awp = id === 'awp';
    const body = skin(awp ? M.green() : M.slate()), metal = M.metal(), lens = M.lens();
    const s = awp ? 1 : 0.85;
    addBox(g, 2.2 * s, 2.6 * s, 14, body, 0, 0.3, -2);
    addCylZ(g, 0.45 * s, awp ? 26 : 22, metal, 0, 0.8, awp ? -22 : -20);
    addCylZ(g, 0.65 * s, 2.2, metal, 0, 0.8, awp ? -35.5 : -31.5);
    addCylZ(g, 1.0 * s, 12, metal, 0, 3.6 * s, -3, 12);
    addCylZ(g, 1.45 * s, 2.4, metal, 0, 3.6 * s, -9.8, 12, 1.0 * s);
    addCylZ(g, 1.25 * s, 2.0, metal, 0, 3.6 * s, 3.8, 12);
    const l = addCylZ(g, 1.3 * s, 0.2, lens, 0, 3.6 * s, -11.05, 12); l.castShadow = false;
    addBox(g, 0.8, 1.6, 1.0, metal, 0, 2.3 * s, -6); addBox(g, 0.8, 1.6, 1.0, metal, 0, 2.3 * s, 1);
    addBox(g, 2.2 * s, 4.2 * s, 11, body, 0, -0.6, 9.8);
    addBox(g, 1.6 * s, 1.1, 6, body, 0, 1.9, 9.3);
    addBox(g, 1.6 * s, 3.2, 1.9, body, 0, -2.4, 2.4, 0.25);
    addBox(mag, 1.4 * s, 2.6, 3, metal, 0, -1.8, -3);
    const bolt = addCylY(g, 0.25, 2.6, metal, 1.6, 1.2, 2.0); bolt.rotation.z = Math.PI / 2;
    muzzle.position.set(0, 0.8, awp ? -36.8 : -32.8);
  } else if (id === 'deagle') {
    const silver = skin(M.silver()), blk = M.polymer();
    addBox(g, 1.3, 1.7, 10, silver, 0, 1.4, -3.6);
    addBox(g, 1.2, 1.1, 6.2, silver, 0, 0.1, -2.4);
    addBox(g, 1.35, 4.4, 2.3, blk, 0, -2.1, 1.0, 0.25);
    addBox(g, 0.3, 0.3, 2.3, silver, 0, -0.9, -1.6);
    addBox(g, 0.45, 0.6, 0.6, silver, 0, 1.6, 1.6);
    addBox(g, 0.25, 0.45, 0.3, silver, 0, 2.45, -8.2);
    addBox(g, 0.9, 0.4, 0.6, blk, 0, 2.4, 0.8);
    addBox(mag, 1.0, 0.6, 1.8, blk, 0, -4.4, 1.6);
    muzzle.position.set(0, 1.5, -8.8);
  } else if (id === 'knife') {
    const knifeType = opts.knife || 'default';
    const blade = skin(M.blade()), blk = M.polymer();
    if (knifeType === 'karambit') {
      const ring = new THREE.Mesh(new THREE.TorusGeometry(1.1, 0.3, 8, 16), blade); ring.position.set(0, 0, 2.8); ring.rotation.y = Math.PI / 2; g.add(ring);
      addBox(g, 0.7, 1.2, 4, blk, 0, 0, 0.4);
      const sh = new THREE.Shape();
      sh.moveTo(0, 0.6); sh.quadraticCurveTo(-3.5, 1.4, -6.2, -1.8); sh.quadraticCurveTo(-3.2, -0.2, 0, -0.5); sh.lineTo(0, 0.6);
      const geo = new THREE.ExtrudeGeometry(sh, { depth: 0.16, bevelEnabled: true, bevelThickness: 0.05, bevelSize: 0.05, bevelSegments: 1 });
      const m = new THREE.Mesh(geo, blade); m.rotation.y = Math.PI / 2; m.position.set(-0.08, 0, -1.6); g.add(m);
      muzzle.position.set(0, -1.5, -7);
    } else {
      addCylZ(g, 0.55, 4.4, blk, 0, 0, 1.4, 8);
      addBox(g, 0.5, 2.2, 0.4, M.metal(), 0, 0, -0.9);
      const sh = new THREE.Shape();
      sh.moveTo(0, 0.55); sh.lineTo(-5.5, 0.45); sh.quadraticCurveTo(-7.6, 0.3, -8.0, -0.2); sh.quadraticCurveTo(-6.4, -0.75, -4.5, -0.7); sh.lineTo(0, -0.6); sh.lineTo(0, 0.55);
      const geo = new THREE.ExtrudeGeometry(sh, { depth: 0.16, bevelEnabled: true, bevelThickness: 0.05, bevelSize: 0.06, bevelSegments: 1 });
      const m = new THREE.Mesh(geo, blade); m.rotation.y = Math.PI / 2; m.position.set(-0.08, 0, -1.1); g.add(m);
      muzzle.position.set(0, 0, -9);
    }
  }
  g.add(muzzle); g.userData.muzzle = muzzle;
  g.traverse(o => { if (o.isMesh) o.receiveShadow = false; });
  return g;
}

// ------------------------------------------------------------------ skins
let fadeTex = null, camoTex = null, asiTex = null;
export const SKINS = ['Default', 'Gold', 'Fade', 'Asiimov', 'Neon Rider', 'Forest Camo', 'Crimson', 'Chrome'];
export function applySkin(gun, skinName, color) {
  if (!gun) return;
  for (const mat of gun.userData.skinMats) {
    if (!mat.userData.orig) mat.userData.orig = { color: mat.color.getHex(), rough: mat.roughness, metal: mat.metalness, map: mat.map, emissive: mat.emissive.getHex(), ei: mat.emissiveIntensity };
    const o = mat.userData.orig;
    mat.color.setHex(o.color); mat.roughness = o.rough; mat.metalness = o.metal; mat.map = o.map; mat.emissive.setHex(o.emissive); mat.emissiveIntensity = o.ei;
    switch (skinName) {
      case 'Gold': mat.color.setHex(0xd4a017); mat.metalness = 1; mat.roughness = 0.22; break;
      case 'Fade': if (!fadeTex) fadeTex = makeFadeTexture(); mat.map = fadeTex; mat.color.setHex(0xffffff); mat.metalness = 0.7; mat.roughness = 0.25; break;
      case 'Asiimov': if (!asiTex) asiTex = makeCamoTexture(['#f2f2f2', '#f2f2f2', '#ff7b1c', '#151515']); mat.map = asiTex; mat.color.setHex(0xffffff); mat.roughness = 0.5; mat.metalness = 0.1; break;
      case 'Neon Rider': mat.color.setHex(0x16001e); mat.emissive.setHex(0xff2bd6); mat.emissiveIntensity = 0.55; mat.roughness = 0.35; break;
      case 'Forest Camo': if (!camoTex) camoTex = makeCamoTexture(['#3e4a2a', '#5b6b36', '#2a2f1c', '#7a7a50']); mat.map = camoTex; mat.color.setHex(0xffffff); mat.roughness = 0.8; mat.metalness = 0; break;
      case 'Crimson': mat.color.set(color || '#b0102a'); mat.roughness = 0.35; mat.metalness = 0.3; break;
      case 'Chrome': mat.color.setHex(0xffffff); mat.metalness = 1; mat.roughness = 0.05; break;
    }
    mat.needsUpdate = true;
  }
}

// helper: a cylinder mesh stretched between two points (used for viewmodel arms)
export function limbBetween(mat, r1, r2) {
  const geo = new THREE.CylinderGeometry(r2, r1, 1, 10, 1, false);
  geo.translate(0, 0.5, 0);
  const m = new THREE.Mesh(geo, mat);
  m.userData.set = (a, b) => {
    const d = new THREE.Vector3().subVectors(b, a);
    const len = d.length();
    m.position.copy(a);
    m.scale.set(1, len, 1);
    m.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), d.normalize());
  };
  return m;
}
