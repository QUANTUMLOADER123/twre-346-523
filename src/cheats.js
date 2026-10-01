// The "cheat" layer: aimbot (rage/legit), triggerbot, anti-aim/desync, fake lag, movement helpers.
import * as THREE from 'three';
import { anglesTo, wrap180, clamp, angleVectors, angleDist, D2R, R2D, rayCapsule } from './util.js';
import { HITGROUP_MULT, weaponInaccuracy } from './weapons.js';
import { predictOnGroundNextTick } from './movement.js';

const GROUP_OF = { head: ['head'], chest: ['chest'], stomach: ['stomach'], legs: ['leg'] };

// ------------------------------------------------------------ helpers
function enemies(game) { return game.actors.filter(a => a.alive && a !== game.player && a.team !== game.player.team); }

function hitboxPoint(h) { return new THREE.Vector3().addVectors(h.wa, h.wb).multiplyScalar(0.5); }

function isVisiblePoint(game, eye, p) { return game.world.visible(eye, p); }

// ------------------------------------------------------------ rage aimbot
export function rageAimbot(game, cfg) {
  const p = game.player, r = cfg.rage;
  if (!r.enabled || !p.alive) return null;
  const w = p.weapon;
  if (w.melee) return null;
  const eye = p.eye(new THREE.Vector3());
  const groups = [];
  if (r.hitboxes.head) groups.push('head');
  if (r.hitboxes.chest) groups.push('chest');
  if (r.hitboxes.stomach) groups.push('stomach');
  if (r.hitboxes.legs) groups.push('leg');
  if (!groups.length) return null;
  let best = null, bestScore = Infinity;
  const spread = cfg.misc.nospread ? 0 : weaponInaccuracy(w, p.ws, p);
  const hcOf = (hb, dist) => {
    const ang = Math.atan2(hb.r * 0.9, dist) * R2D;
    return spread <= 0.0001 ? 100 : clamp((ang / spread) * (ang / spread) * 100, 0, 100);
  };
  for (const e of enemies(game)) {
    const dist = eye.distanceTo(e.origin);
    // walk hitboxes in priority order; take the first one that is visible, does enough damage and
    // satisfies hitchance. If none satisfies hitchance keep the best candidate (aim, but don't shoot).
    let pick = null;
    for (const g of groups) {
      const hb = e.model.hitboxes.find(h => h.group === g);
      if (!hb) continue;
      const pt = hitboxPoint(hb);
      if (r.visibleOnly && !isVisiblePoint(game, eye, pt)) continue;
      const dmg = w.damage * HITGROUP_MULT[g] * w.armorPen * Math.pow(w.rangeMod, dist / 500);
      if (dmg < Math.min(r.mindmg, e.health)) continue;
      const hc = hcOf(hb, dist);
      const cand = { target: e, point: pt, group: g, hb, dist, hitchance: hc };
      if (!pick || (pick.hitchance < r.hitchance && hc > pick.hitchance)) pick = cand;
      if (hc >= r.hitchance) break;
    }
    if (!pick) continue;
    const a = anglesTo(eye, pick.point);
    const fovd = angleDist(a.yaw, a.pitch, p.yaw, p.pitch);
    if (fovd > r.fov) continue;
    const score = (r.target === 'Distance' ? dist : r.target === 'Health' ? e.health * 1000 + fovd : fovd) + (pick.hitchance >= r.hitchance ? 0 : 1e6);
    if (score < bestScore) { bestScore = score; best = { ...pick, angles: a }; }
  }
  if (!best) return null;
  return best;
}

// ------------------------------------------------------------ legit aimbot
export function legitAimbot(game, cfg, attackHeld) {
  const p = game.player, l = cfg.legit;
  if (!l.aimbot || !p.alive || p.weapon.melee) return;
  if (l.activation === 'On fire' && !attackHeld) return;
  const eye = p.eye(new THREE.Vector3());
  const punchP = cfg.misc.norecoil ? 0 : p.ws.punchP * 2, punchY = cfg.misc.norecoil ? 0 : p.ws.punchY * 2;
  let best = null, bd = Infinity;
  for (const e of enemies(game)) {
    let cands;
    if (l.hitbox === 'Head') cands = [e.model.hitboxes[0]];
    else if (l.hitbox === 'Chest') cands = [e.model.hitboxes[2]];
    else cands = [e.model.hitboxes[0], e.model.hitboxes[2], e.model.hitboxes[3]];
    for (const hb of cands) {
      const pt = hitboxPoint(hb);
      if (l.visibleOnly && !isVisiblePoint(game, eye, pt)) continue;
      const a = anglesTo(eye, pt);
      // compensate recoil if RCS is on so we aim the bullets, not the crosshair
      const ty = a.yaw - (l.rcs ? punchY * l.rcsX / 100 : 0), tp = a.pitch + (l.rcs ? punchP * l.rcsY / 100 : 0);
      const d = angleDist(ty, tp, p.yaw, p.pitch);
      if (d < l.fov && d < bd) { bd = d; best = { yaw: ty, pitch: tp }; }
    }
  }
  if (!best) return;
  const k = 1 / Math.max(1, l.smooth);
  p.yaw = wrap180(p.yaw + wrap180(best.yaw - p.yaw) * k);
  p.pitch = clamp(p.pitch + (best.pitch - p.pitch) * k, -89, 89);
}

// recoil control system (modifies view to counter punch)
export function rcs(game, cfg) {
  const p = game.player, l = cfg.legit;
  const st = game.rcsState || (game.rcsState = { p: 0, y: 0 });
  if (!l.rcs || cfg.misc.norecoil || !p.alive) { st.p = p.ws.punchP; st.y = p.ws.punchY; return; }
  const dp = p.ws.punchP - st.p, dy = p.ws.punchY - st.y;
  // only compensate while firing (punch growing), let it recover naturally otherwise
  if (game.time - p.ws.lastShot < 0.15) {
    p.pitch = clamp(p.pitch + dp * 2 * l.rcsY / 100, -89, 89);
    p.yaw = wrap180(p.yaw - dy * 2 * l.rcsX / 100);
  }
  st.p = p.ws.punchP; st.y = p.ws.punchY;
}

// ------------------------------------------------------------ triggerbot
export function triggerbot(game, cfg) {
  const p = game.player, l = cfg.legit;
  if (!l.trigger || !p.alive || p.weapon.melee) return false;
  const eye = p.eye(new THREE.Vector3());
  const yaw = p.yaw + (cfg.misc.norecoil ? 0 : p.ws.punchY * 2);
  const pitch = p.pitch - (cfg.misc.norecoil ? 0 : p.ws.punchP * 2);
  const dir = angleVectors(yaw, pitch);
  const hit = game.traceBullet(eye, dir, p, 8192);
  const ts = game.trigState || (game.trigState = { since: -1 });
  if (hit && hit.actor && (!l.triggerHeadOnly || hit.group === 'head')) {
    if (ts.since < 0) ts.since = game.time;
    if ((game.time - ts.since) * 1000 >= l.triggerDelay) return true;
  } else ts.since = -1;
  return false;
}

// ------------------------------------------------------------ anti-aim
export function computeAntiAim(game, cfg, shotThisTick) {
  const p = game.player, a = cfg.aa;
  const st = game.aaState || (game.aaState = { spin: 0, flip: false, manual: 0, desyncFlip: false });
  st.flip = !st.flip;
  const view = { yaw: p.yaw, pitch: p.pitch };
  if (!a.enabled || !p.alive || shotThisTick || (p.weapon.melee && game.input.mouse1)) {
    return { real: view.yaw, fake: view.yaw, pitch: view.pitch, aa: false };
  }
  let base = view.yaw;
  if (a.yawBase === 'At targets') {
    let best = null, bd = Infinity;
    for (const e of enemies(game)) { const d = e.origin.distanceTo(p.origin); if (d < bd) { bd = d; best = e; } }
    if (best) base = anglesTo(p.origin, best.origin).yaw;
  }
  let yaw = base + 180;
  switch (a.yaw) {
    case 'Backward': yaw = base + 180; break;
    case 'Forward': yaw = base; break;
    case 'Spin': st.spin = (st.spin + a.spinSpeed * 2.5) % 360; yaw = base + st.spin; break;
    case 'Jitter': yaw = base + 180 + (st.flip ? a.jitter : -a.jitter); break;
    case 'Sideways': yaw = base + (a.invert ? -90 : 90); break;
    case 'Random': yaw = base + Math.random() * 360; break;
  }
  if (st.manual === 1) yaw = base + 90; else if (st.manual === 2) yaw = base - 90; else if (st.manual === 3) yaw = base + 180;
  yaw += a.yawOffset;
  let pitch = view.pitch;
  switch (a.pitch) {
    case 'Down': pitch = 89; break;
    case 'Up': pitch = -89; break;
    case 'Zero': pitch = 0; break;
    case 'Jitter': pitch = st.flip ? 89 : -89; break;
    case 'Random': pitch = Math.random() * 178 - 89; break;
  }
  let side = a.invert ? -1 : 1;
  if (a.desync === 'Jitter') { if (game.tickCount % 3 === 0) st.desyncFlip = !st.desyncFlip; side *= st.desyncFlip ? -1 : 1; }
  const body = a.desync === 'Off' ? 0 : a.bodyYaw;
  const real = wrap180(yaw);
  const fake = wrap180(yaw + side * body * 2);
  return { real, fake, pitch, aa: true, side, body };
}

// ------------------------------------------------------------ fake lag
export function fakeLagShouldChoke(game, cfg, shotThisTick) {
  const f = cfg.fakelag, p = game.player, net = game.net;
  if (!f.enabled || !p.alive) return false;
  if (shotThisTick && f.unchokeOnShot) return false;
  const st = game.flState || (game.flState = { limit: f.limit });
  if (net.choked === 0) {
    st.limit = f.mode === 'Random' ? Math.max(1, Math.round(f.limit * (1 - Math.random() * f.variance / 100))) : f.limit;
  }
  const limit = Math.min(st.limit, 16);
  if (net.choked >= limit) return false;
  if (f.mode === 'Adaptive') {
    // choke until we've moved far enough to break lag compensation (64u)
    const moved = Math.hypot(p.origin.x - net.origin.x, p.origin.z - net.origin.z);
    if (moved > 64 && net.choked >= 2) return false;
  }
  return true;
}

// ------------------------------------------------------------ movement helpers
export function movementCheats(game, cfg, cmd, inputState) {
  const p = game.player, m = cfg.misc;
  if (!p.alive) return;
  // edge jump
  if (m.edgejump && p.onGround && !cmd.jump) {
    const fut = p.origin.clone().addScaledVector(p.velocity, 1 / 64);
    const fake = { origin: fut, velocity: new THREE.Vector3(p.velocity.x, -1, p.velocity.z), onGround: false, ducked: p.ducked };
    const ground = predictOnGroundNextTick(game.world, fake, 1 / 64);
    if (!ground && p.speed2d() > 50) { cmd.jump = true; cmd.autoJump = true; }
  }
  // bunnyhop: re-press jump exactly on the landing tick
  if (m.bhop && inputState.jumpHeld) {
    cmd.jump = p.onGround;
    cmd.autoJump = false;
    if (p.onGround) p.oldJump = false;
  }
  // autostrafe
  if (m.autostrafe !== 'Off' && !p.onGround && (inputState.jumpHeld || m.autostrafe === 'Rage')) {
    const sp = p.speed2d();
    if (m.autostrafe === 'Legit') {
      // strafe in the direction the mouse moves (like a human syncing A/D with the mouse)
      if (cmd.sidemove === 0) {
        if (inputState.mouseDX < -0.5) { cmd.sidemove = -450; cmd.forwardmove = 0; }
        else if (inputState.mouseDX > 0.5) { cmd.sidemove = 450; cmd.forwardmove = 0; }
      }
    } else if (sp > 15) {
      // directional / rage: optimal angle strafe towards WASD direction
      let wishOff = 0, any = false;
      const f = cmd.forwardmove, s = cmd.sidemove;
      if (f || s) { wishOff = Math.atan2(-s, f) * R2D; any = true; }
      const desired = p.yaw + (any ? wishOff : 0);
      const velYaw = Math.atan2(-p.velocity.x, -p.velocity.z) * R2D;
      const diff = wrap180(desired - velYaw);
      // ideal strafe angle: perpendicular minus a tiny lead, scaled by smoothness
      const ideal = clamp(Math.atan2(30, sp) * R2D, 0, 90);
      const smooth = m.strafeSmooth / 100;
      let side;
      const st = game.strafeState || (game.strafeState = { flip: false });
      if (Math.abs(diff) > ideal * 1.5) side = Math.sign(diff); else { st.flip = !st.flip; side = st.flip ? 1 : -1; }
      const moveYaw = velYaw + side * (45 + 45 * smooth);
      const rel = (moveYaw - p.yaw) * D2R;
      cmd.forwardmove = Math.cos(rel) * 450;
      cmd.sidemove = -Math.sin(rel) * 450;
    }
  }
  // quick stop when no keys pressed
  if (m.quickstop && p.onGround && !cmd.jump && cmd.forwardmove === 0 && cmd.sidemove === 0 && p.speed2d() > 20) quickStop(p, cmd);
}

export function quickStop(p, cmd) {
  const v = p.velocity; const sp = Math.hypot(v.x, v.z);
  if (sp < 20) { cmd.forwardmove = 0; cmd.sidemove = 0; return; }
  const velYaw = Math.atan2(-v.x, -v.z);
  const rel = velYaw + Math.PI - p.yaw * D2R;
  cmd.forwardmove = Math.cos(rel) * 450;
  cmd.sidemove = -Math.sin(rel) * 450;
}
