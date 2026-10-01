// Bot AI: navigation (A* over cell graph), target acquisition, aiming, shooting, strafing.
import * as THREE from 'three';
import { Actor } from './entity.js';
import { anglesTo, wrap180, clamp, angleVectors, D2R } from './util.js';
import { WEAPONS } from './weapons.js';
import { CS } from './maps.js';

export const DIFFICULTY = {
  Easy: { reaction: 0.65, turn: 220, error: 4.5, hs: 0.15, resolver: 0.0, burst: 3, rcs: 0.2, predict: 0 },
  Normal: { reaction: 0.42, turn: 420, error: 2.6, hs: 0.3, resolver: 0.15, burst: 5, rcs: 0.5, predict: 0 },
  Hard: { reaction: 0.26, turn: 700, error: 1.4, hs: 0.5, resolver: 0.35, burst: 7, rcs: 0.8, predict: 0.5 },
  Expert: { reaction: 0.16, turn: 1000, error: 0.7, hs: 0.7, resolver: 0.55, burst: 10, rcs: 0.95, predict: 1 },
};

const BOT_NAMES = ['Shrek', 'Dimon', 'Kolyan', 'Vovan', 'Sanya', 'Zheka', 'Tolik', 'Gena', 'Stas', 'Borya', 'Petya', 'Lyokha', 'Garik', 'Edik', 'Fedya', 'Grisha', 'Misha', 'Kostyan'];
let nameIdx = Math.floor(Math.random() * BOT_NAMES.length);

export class Bot extends Actor {
  constructor(team, difficulty, primary) {
    super({ name: 'BOT ' + BOT_NAMES[nameIdx++ % BOT_NAMES.length], team, isBot: true, primary });
    this.diff = DIFFICULTY[difficulty] || DIFFICULTY.Normal;
    this.ai = {
      path: [], pathIdx: 0, goal: null, repathAt: 0, stuckTime: 0, lastPos: new THREE.Vector3(),
      target: null, seenAt: -10, spottedAt: -10, lastKnown: null, lastKnownTime: -10,
      aimYaw: 0, aimPitch: 0, errYaw: 0, errPitch: 0, errAt: 0, aimHead: true,
      strafeDir: 1, strafeUntil: 0, burstLeft: 0, burstPauseUntil: 0, scanUntil: 0, missed: 0, resolve: false,
      nextScan: 0, jumpUntil: 0, lookYaw: 0,
    };
  }
}

// --------------------------------------------------------------- navigation
function nearestNode(nav, x, z, y) {
  const g = nav.grid;
  const c = Math.floor((x - nav.bounds.x0) / CS), r = Math.floor((z - nav.bounds.z0) / CS);
  let best = -1, bd = 1e9;
  for (let rad = 0; rad <= 3 && best < 0; rad++) {
    for (let dr = -rad; dr <= rad; dr++) for (let dc = -rad; dc <= rad; dc++) {
      if (Math.max(Math.abs(dr), Math.abs(dc)) !== rad) continue;
      const cc = c + dc, rr = r + dr;
      if (cc < 0 || rr < 0 || cc >= g.w || rr >= g.h) continue;
      const id = nav.nodeAt[rr * g.w + cc];
      if (id < 0) continue;
      const n = nav.nodes[id];
      const d = (n.x - x) ** 2 + (n.z - z) ** 2 + ((n.y - (y ?? n.y)) * 3) ** 2;
      if (d < bd) { bd = d; best = id; }
    }
  }
  return best;
}

function astar(nav, from, to) {
  if (from < 0 || to < 0) return null;
  const nodes = nav.nodes;
  const open = [from];
  const g = new Map([[from, 0]]), came = new Map();
  const f = new Map([[from, 0]]);
  const goal = nodes[to];
  const h = id => Math.hypot(nodes[id].x - goal.x, nodes[id].z - goal.z) / CS;
  const closed = new Set();
  let iter = 0;
  while (open.length && iter++ < 6000) {
    let bi = 0;
    for (let i = 1; i < open.length; i++) if (f.get(open[i]) < f.get(open[bi])) bi = i;
    const cur = open[bi]; open.splice(bi, 1);
    if (cur === to) {
      const path = [cur]; let c = cur;
      while (came.has(c)) { c = came.get(c); path.push(c); }
      return path.reverse();
    }
    closed.add(cur);
    for (const l of nodes[cur].links) {
      if (closed.has(l.to)) continue;
      const ng = g.get(cur) + l.cost;
      if (ng < (g.get(l.to) ?? Infinity)) {
        came.set(l.to, cur); g.set(l.to, ng); f.set(l.to, ng + h(l.to));
        if (!open.includes(l.to)) open.push(l.to);
      }
    }
  }
  return null;
}

function pickRoamGoal(game, bot) {
  const nav = game.nav;
  // prefer heading towards enemy spawn / random distant nodes
  for (let i = 0; i < 10; i++) {
    const n = nav.nodes[Math.floor(Math.random() * nav.nodes.length)];
    if (n.links.length < 3) continue;
    const d = Math.hypot(n.x - bot.origin.x, n.z - bot.origin.z);
    if (d > 500) return n.id;
  }
  return nav.nodes[Math.floor(Math.random() * nav.nodes.length)].id;
}

function setPath(game, bot, goalId) {
  const start = nearestNode(game.nav, bot.origin.x, bot.origin.z, bot.origin.y);
  const p = astar(game.nav, start, goalId);
  bot.ai.path = p || []; bot.ai.pathIdx = 0; bot.ai.goal = goalId;
}

// --------------------------------------------------------------- perception
function perceivedPoint(game, bot, target, group) {
  // what the bot "sees": for the local player this is the networked (fake-lagged / desynced) model
  const ai = bot.ai;
  const out = new THREE.Vector3();
  if (target === game.player) {
    const ghost = game.netModel;
    const useReal = ai.resolve;
    const src = useReal ? target.model : ghost;
    const idx = group === 'head' ? 0 : 2;
    out.copy(src.hitboxes[idx].wa).add(src.hitboxes[idx].wb).multiplyScalar(0.5);
    if (useReal) out.add(game.net.origin).sub(target.origin); // still at the networked position
    if (bot.diff.predict > 0 && game.net.chokeTime > 0) {
      out.addScaledVector(game.net.velocity, game.net.chokeTime * bot.diff.predict * 0.7);
    }
    return out;
  }
  const idx = group === 'head' ? 0 : 2;
  return out.copy(target.model.hitboxes[idx].wa).add(target.model.hitboxes[idx].wb).multiplyScalar(0.5);
}

function canSee(game, bot, target) {
  const eye = bot.eye(new THREE.Vector3());
  const pos = target === game.player ? game.net.origin : target.origin;
  const tgtH = new THREE.Vector3(pos.x, pos.y + (target.ducked ? 44 : 62), pos.z);
  if (game.world.visible(eye, tgtH)) return true;
  const tgtC = new THREE.Vector3(pos.x, pos.y + 36, pos.z);
  return game.world.visible(eye, tgtC);
}

// --------------------------------------------------------------- think
export function botThink(game, bot, dt) {
  const ai = bot.ai, now = game.time, d = bot.diff;
  const out = { forwardmove: 0, sidemove: 0, jump: false, duck: false, walk: false, attack: false, attack2: false, reload: false };

  // ---- target selection
  if (now >= ai.nextScan) {
    ai.nextScan = now + 0.1;
    let best = null, bd = Infinity;
    const eye = bot.eye(new THREE.Vector3());
    for (const e of game.actors) {
      if (!e.alive || e.team === bot.team || e === bot || now < (e.protectedUntil || 0)) continue;
      const pos = e === game.player ? game.net.origin : e.origin;
      const dist = eye.distanceTo(pos);
      if (dist > 4500) continue;
      // field of view (wide, plus "hearing" when close)
      const ang = anglesTo(eye, pos);
      const fovOk = Math.abs(wrap180(ang.yaw - ai.aimYaw)) < 80 || dist < 300 || e === ai.target;
      if (!fovOk) continue;
      if (!canSee(game, bot, e)) continue;
      if (dist < bd) { bd = dist; best = e; }
    }
    if (best) {
      if (ai.target !== best || now - ai.seenAt > 0.6) {
        ai.spottedAt = now; ai.burstLeft = d.burst + Math.floor(Math.random() * 3);
        ai.aimHead = Math.random() < d.hs;
        ai.resolve = Math.random() < d.resolver;
        ai.missed = 0;
      }
      ai.target = best; ai.seenAt = now;
      ai.lastKnown = (best === game.player ? game.net.origin : best.origin).clone(); ai.lastKnownTime = now;
    } else if (ai.target && now - ai.seenAt > 0.4) {
      ai.target = null;
    }
  }
  if (ai.target && !ai.target.alive) { ai.target = null; ai.lastKnown = null; }

  const eye = bot.eye(new THREE.Vector3());
  const w = bot.weapon;
  let wantYaw = ai.aimYaw, wantPitch = ai.aimPitch;
  let moveTarget = null;

  if (ai.target) {
    const tp = perceivedPoint(game, bot, ai.target, ai.aimHead ? 'head' : 'chest');
    const a = anglesTo(eye, tp);
    // aim error that slowly drifts
    if (now >= ai.errAt) {
      ai.errAt = now + 0.25 + Math.random() * 0.3;
      const e = d.error * (0.4 + Math.random());
      const t = Math.random() * Math.PI * 2;
      ai.errYaw = Math.cos(t) * e; ai.errPitch = Math.sin(t) * e * 0.6;
    }
    const dist = eye.distanceTo(tp);
    const errScale = clamp(300 / Math.max(dist, 1), 0.35, 1.6);
    wantYaw = a.yaw + ai.errYaw * errScale;
    wantPitch = a.pitch + ai.errPitch * errScale;
    // recoil control
    wantYaw -= bot.ws.punchY * 2 * d.rcs;
    wantPitch += bot.ws.punchP * 2 * d.rcs;

    // scope snipers
    if (w.sniper && !bot.ws.scoped && now - ai.spottedAt > 0.1) out.attack2 = true;
    // shooting
    const aimErr = Math.hypot(wrap180(wantYaw - ai.aimYaw), wantPitch - ai.aimPitch);
    const reacted = now - ai.spottedAt > d.reaction;
    const tol = clamp(Math.atan2(14, dist) / D2R * 1.5, 0.6, 8);
    if (reacted && aimErr < tol && now >= ai.burstPauseUntil) {
      if (w.melee) { if (dist < 70) out.attack = true; }
      else if (!w.sniper || bot.ws.scoped) {
        if (w.auto) {
          out.attack = true;
          if (bot.ws.lastShot === now) { /* fired this tick */ }
        } else {
          out.attack = (game.tickCount + bot.id) % 2 === 0; // semi auto: tap
        }
      }
    }
    if (w.auto && out.attack && game.time - bot.ws.lastShot < 0.02) {
      ai.burstLeft--;
      if (ai.burstLeft <= 0) {
        ai.burstLeft = d.burst + Math.floor(Math.random() * 3);
        ai.burstPauseUntil = now + (dist > 800 ? 0.35 : 0.12) + Math.random() * 0.2;
      }
    }
    // movement while fighting: counter-strafe or ADAD strafe
    if (dist > 700 || w.sniper) {
      // stand still to shoot accurately
      const v = bot.velocity;
      if (bot.speed2d() > 30 && bot.onGround) {
        const yaw = bot.yaw * D2R;
        const fx = -Math.sin(yaw), fz = -Math.cos(yaw), rx = Math.cos(yaw), rz = -Math.sin(yaw);
        out.forwardmove = -(v.x * fx + v.z * fz) > 0 ? 450 : -450;
        out.sidemove = -(v.x * rx + v.z * rz) > 0 ? 450 : -450;
        if (Math.abs(v.x * fx + v.z * fz) < 30) out.forwardmove = 0;
        if (Math.abs(v.x * rx + v.z * rz) < 30) out.sidemove = 0;
      }
      if (d.burst > 6 && dist > 1200 && Math.random() < 0.002) out.duck = true;
    } else {
      if (now > ai.strafeUntil) { ai.strafeDir = -ai.strafeDir; ai.strafeUntil = now + 0.25 + Math.random() * 0.5; }
      const firingNow = out.attack;
      out.sidemove = firingNow && d.burst <= 5 ? 0 : 450 * ai.strafeDir;
      if (dist > 350 && !firingNow) out.forwardmove = 300;
      if (w.melee) { out.forwardmove = 450; out.sidemove = 0; }
    }
    if (bot.ws.reloading || (bot.ammo[w.id].clip === 0 && !w.melee)) {
      out.attack = false; out.reload = true;
      out.sidemove = 450 * ai.strafeDir; out.forwardmove = -300;
    }
  } else {
    // ---- no target: chase last known position or roam
    if (ai.lastKnown && now - ai.lastKnownTime < 6) {
      if (!ai.path.length || ai.goalKnown !== ai.lastKnownTime) {
        setPath(game, bot, nearestNode(game.nav, ai.lastKnown.x, ai.lastKnown.z, ai.lastKnown.y));
        ai.goalKnown = ai.lastKnownTime;
      }
    } else {
      ai.lastKnown = null;
      if (!ai.path.length || ai.pathIdx >= ai.path.length || now > ai.repathAt) {
        setPath(game, bot, pickRoamGoal(game, bot));
        ai.repathAt = now + 12 + Math.random() * 8;
      }
    }
    // reload when idle
    if (!w.melee && bot.ammo[w.id].clip < w.clip * 0.5 && bot.ammo[w.id].reserve > 0) out.reload = true;
    if (bot.ws.scoped && w.sniper) out.attack2 = bot.ws.scoped !== 0 && Math.random() < 0.05;
    // follow path
    const p = ai.path;
    while (ai.pathIdx < p.length) {
      const n = game.nav.nodes[p[ai.pathIdx]];
      const dd = Math.hypot(n.x - bot.origin.x, n.z - bot.origin.z);
      if (dd < 26 && Math.abs(n.y - bot.origin.y) < 40) ai.pathIdx++; else break;
    }
    if (ai.pathIdx < p.length) {
      const n = game.nav.nodes[p[ai.pathIdx]];
      moveTarget = n;
      // look ahead a few nodes for a smooth view direction
      const la = game.nav.nodes[p[Math.min(p.length - 1, ai.pathIdx + 3)]];
      const lookYaw = Math.atan2(-(la.x - bot.origin.x), -(la.z - bot.origin.z)) / D2R;
      wantYaw = lookYaw + Math.sin(now * 0.7 + bot.id) * 25;
      wantPitch = 3;
      // jump links
      if (n.y - bot.origin.y > 18 && bot.onGround && Math.hypot(n.x - bot.origin.x, n.z - bot.origin.z) < 90) { out.jump = true; out.duck = false; ai.jumpUntil = now + 0.5; }
    } else {
      ai.path = [];
    }
    if (now < ai.jumpUntil && !bot.onGround) out.duck = true;
  }

  // ---- move towards path node (independent from view direction)
  if (moveTarget) {
    const dx = moveTarget.x - bot.origin.x, dz = moveTarget.z - bot.origin.z;
    const mYaw = Math.atan2(-dx, -dz);
    const rel = mYaw - bot.yaw * D2R;
    out.forwardmove = Math.cos(rel) * 450;
    out.sidemove = -Math.sin(rel) * 450;
    // stuck detection
    if (bot.origin.distanceTo(ai.lastPos) < 0.6 * dt * 250 * 0.2) {
      ai.stuckTime += dt;
      if (ai.stuckTime > 0.5 && bot.onGround) out.jump = true;
      if (ai.stuckTime > 1.5) { ai.path = []; ai.stuckTime = 0; ai.lastKnown = null; }
    } else ai.stuckTime = Math.max(0, ai.stuckTime - dt);
  }
  ai.lastPos.copy(bot.origin);

  // ---- smooth aim
  const turn = d.turn * dt * (ai.target ? 1 : 0.5);
  const dy = wrap180(wantYaw - ai.aimYaw), dp = wantPitch - ai.aimPitch;
  const len = Math.hypot(dy, dp);
  const k = ai.target ? 0.35 : 0.15; // ease
  let sy = dy * k, sp = dp * k;
  const sl = Math.hypot(sy, sp);
  if (sl > turn) { sy *= turn / sl; sp *= turn / sl; }
  if (len < 0.05) { sy = dy; sp = dp; }
  ai.aimYaw = wrap180(ai.aimYaw + sy);
  ai.aimPitch = clamp(ai.aimPitch + sp, -89, 89);
  bot.yaw = ai.aimYaw; bot.pitch = ai.aimPitch;

  // weapon choice: knife when no ammo at all
  if (!w.melee && bot.ammo[w.id].clip === 0 && bot.ammo[w.id].reserve === 0) {
    const sec = bot.inventory[1];
    if (bot.ammo[sec].clip + bot.ammo[sec].reserve > 0) bot.switchTo(1, now); else bot.switchTo(2, now);
  }
  return out;
}

export function botOnShotMissed(bot) {
  const ai = bot.ai;
  ai.missed++;
  if (ai.missed % 3 === 0) ai.resolve = !ai.resolve || Math.random() < bot.diff.resolver;
}

export function botHearShot(game, bot, shooter) {
  if (!bot.alive || shooter.team === bot.team) return;
  const pos = shooter === game.player ? game.net.origin : shooter.origin;
  if (bot.origin.distanceTo(pos) > 1800) return;
  if (bot.ai.target) return;
  bot.ai.lastKnown = pos.clone(); bot.ai.lastKnownTime = game.time; bot.ai.path = [];
  const a = anglesTo(bot.eye(new THREE.Vector3()), pos);
  bot.ai.aimYaw = wrap180(bot.ai.aimYaw + wrap180(a.yaw - bot.ai.aimYaw) * 0.5);
}

export { nearestNode };
