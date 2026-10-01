// Port of Source engine (CS:GO flavoured) player movement: friction, accelerate,
// air-accelerate with 30u wishspeed cap, step-move, slide-move clipping, ducking.
import * as THREE from 'three';
import { TraceResult } from './world.js';

export const MV = {
  gravity: 800,
  friction: 5.2,
  stopspeed: 80,
  accelerate: 5.5,
  airaccelerate: 12,
  maxvelocity: 3500,
  jumpImpulse: 301.993377, // sqrt(2 * 800 * 57)
  stepsize: 18,
  airWishCap: 30,
  enableBunnyhopping: true,  // sv_enablebunnyhopping (false = CS:GO 1.1x speed cap on jump)
  duckSpeed: 0.34,
  walkSpeed: 0.52,
};

export const HULL = {
  mins: new THREE.Vector3(-16, 0, -16),
  stand: new THREE.Vector3(16, 72, 16),
  duck: new THREE.Vector3(16, 54, 16),
};
export const EYE_STAND = 64, EYE_DUCK = 46;

const tr = new TraceResult();
const tmpA = new THREE.Vector3(), tmpB = new THREE.Vector3();
const planes = [new THREE.Vector3(), new THREE.Vector3(), new THREE.Vector3(), new THREE.Vector3(), new THREE.Vector3()];

export function hullMax(ent) { return ent.ducked ? HULL.duck : HULL.stand; }

function trace(world, ent, start, end) {
  return world.traceBox(start, end, HULL.mins, hullMax(ent), tr);
}

function clipVelocity(inV, normal, out, overbounce) {
  const backoff = inV.dot(normal) * overbounce;
  out.set(inV.x - normal.x * backoff, inV.y - normal.y * backoff, inV.z - normal.z * backoff);
  const adjust = out.dot(normal);
  if (adjust < 0) out.addScaledVector(normal, -adjust);
}

function tryPlayerMove(world, ent, dt) {
  const vel = ent.velocity, origin = ent.origin;
  const original = tmpA.copy(vel);
  const primal = new THREE.Vector3().copy(vel);
  const newVel = new THREE.Vector3();
  let numplanes = 0, timeLeft = dt, allFraction = 0, blocked = 0;
  const end = new THREE.Vector3();
  for (let bump = 0; bump < 4; bump++) {
    if (vel.lengthSq() < 1e-6) break;
    end.copy(origin).addScaledVector(vel, timeLeft);
    trace(world, ent, origin, end);
    allFraction += tr.fraction;
    if (tr.allsolid) { vel.set(0, 0, 0); return 4; }
    if (tr.fraction > 0) { origin.copy(tr.endpos); original.copy(vel); numplanes = 0; }
    if (tr.fraction === 1) break;
    if (tr.normal.y > 0.7) blocked |= 1;
    if (tr.normal.y === 0) blocked |= 2;
    timeLeft -= timeLeft * tr.fraction;
    if (numplanes >= 5) { vel.set(0, 0, 0); break; }
    planes[numplanes++].copy(tr.normal);
    if (numplanes === 1 && ent.onGround) {
      clipVelocity(original, planes[0], newVel, 1.0);
      vel.copy(newVel); original.copy(newVel);
    } else {
      let i;
      for (i = 0; i < numplanes; i++) {
        clipVelocity(original, planes[i], vel, 1.0);
        let j;
        for (j = 0; j < numplanes; j++) if (j !== i && vel.dot(planes[j]) < 0) break;
        if (j === numplanes) break;
      }
      if (i === numplanes) {
        if (numplanes !== 2) { vel.set(0, 0, 0); break; }
        const dir = tmpB.crossVectors(planes[0], planes[1]).normalize();
        const d = dir.dot(vel);
        vel.copy(dir).multiplyScalar(d);
      }
      if (vel.dot(primal) <= 0) { vel.set(0, 0, 0); break; }
    }
  }
  if (allFraction === 0) vel.set(0, 0, 0);
  return blocked;
}

function accelerate(vel, wishdir, wishspeed, accel, dt) {
  const current = vel.dot(wishdir);
  const add = wishspeed - current;
  if (add <= 0) return;
  let accelspeed = accel * dt * wishspeed;
  if (accelspeed > add) accelspeed = add;
  vel.addScaledVector(wishdir, accelspeed);
}

function airAccelerate(vel, wishdir, wishspeed, accel, dt) {
  let wishspd = wishspeed;
  if (wishspd > MV.airWishCap) wishspd = MV.airWishCap;
  const current = vel.dot(wishdir);
  const add = wishspd - current;
  if (add <= 0) return;
  let accelspeed = accel * wishspeed * dt;
  if (accelspeed > add) accelspeed = add;
  vel.addScaledVector(wishdir, accelspeed);
}

function friction(ent, dt) {
  const v = ent.velocity;
  const speed = v.length();
  if (speed < 0.1) return;
  let drop = 0;
  if (ent.onGround) {
    const control = speed < MV.stopspeed ? MV.stopspeed : speed;
    drop += control * MV.friction * dt;
  }
  let newspeed = speed - drop;
  if (newspeed < 0) newspeed = 0;
  if (newspeed !== speed) v.multiplyScalar(newspeed / speed);
}

function stayOnGround(world, ent) {
  const start = tmpA.copy(ent.origin); start.y += 2;
  const up = trace(world, ent, ent.origin, start);
  start.copy(up.endpos);
  const end = tmpB.copy(ent.origin); end.y -= MV.stepsize;
  trace(world, ent, start, end);
  if (tr.fraction > 0 && tr.fraction < 1 && !tr.startsolid && tr.normal.y >= 0.7) {
    const dy = Math.abs(ent.origin.y - tr.endpos.y);
    if (dy > 0.5 * DIST_EPS) ent.origin.copy(tr.endpos);
  }
}
const DIST_EPS = 0.03125;

function stepMove(world, ent, dt) {
  const startPos = ent.origin.clone(), startVel = ent.velocity.clone();
  // try sliding along the ground
  tryPlayerMove(world, ent, dt);
  const downPos = ent.origin.clone(), downVel = ent.velocity.clone();
  // reset, try stepping up
  ent.origin.copy(startPos); ent.velocity.copy(startVel);
  const upEnd = startPos.clone(); upEnd.y += MV.stepsize + DIST_EPS;
  trace(world, ent, ent.origin, upEnd);
  if (!tr.startsolid && !tr.allsolid) ent.origin.copy(tr.endpos);
  tryPlayerMove(world, ent, dt);
  const downEnd = ent.origin.clone(); downEnd.y -= MV.stepsize + DIST_EPS;
  trace(world, ent, ent.origin, downEnd);
  if (!tr.startsolid && !tr.allsolid) ent.origin.copy(tr.endpos);
  if (tr.fraction !== 1 && tr.normal.y < 0.7) {
    ent.origin.copy(downPos); ent.velocity.copy(downVel); return;
  }
  const upPos = ent.origin;
  const downDist = (downPos.x - startPos.x) ** 2 + (downPos.z - startPos.z) ** 2;
  const upDist = (upPos.x - startPos.x) ** 2 + (upPos.z - startPos.z) ** 2;
  if (downDist > upDist) {
    ent.origin.copy(downPos); ent.velocity.copy(downVel);
  } else {
    ent.velocity.y = downVel.y;
  }
}

function walkMove(world, ent, wishdir, wishspeed, maxspeed, dt) {
  ent.velocity.y = 0;
  accelerate(ent.velocity, wishdir, wishspeed, MV.accelerate, dt);
  ent.velocity.y = 0;
  const spd = ent.velocity.length();
  if (spd > maxspeed) ent.velocity.multiplyScalar(maxspeed / spd);
  if (spd < 1) { ent.velocity.set(0, 0, 0); return; }
  const dest = tmpA.copy(ent.origin).addScaledVector(ent.velocity, dt);
  trace(world, ent, ent.origin, dest);
  if (tr.fraction === 1) {
    ent.origin.copy(tr.endpos);
    stayOnGround(world, ent);
    return;
  }
  stepMove(world, ent, dt);
  stayOnGround(world, ent);
}

function airMove(world, ent, wishdir, wishspeed, dt) {
  airAccelerate(ent.velocity, wishdir, wishspeed, MV.airaccelerate, dt);
  tryPlayerMove(world, ent, dt);
}

function categorizePosition(world, ent) {
  const wasOnGround = ent.onGround;
  if (ent.velocity.y > 140) { ent.onGround = false; }
  else {
    const end = tmpA.copy(ent.origin); end.y -= 2;
    trace(world, ent, ent.origin, end);
    if (tr.fraction < 1 && tr.normal.y >= 0.7 && !tr.allsolid) {
      ent.onGround = true;
      if (!tr.startsolid) ent.origin.copy(tr.endpos);
    } else ent.onGround = false;
  }
  if (ent.onGround) {
    if (!wasOnGround) {
      ent.landVelocity = ent.velocity.y;
      ent.events.push('land');
    }
    ent.velocity.y = 0;
  }
}

function unstick(world, ent) {
  if (!world.isStuck(ent.origin, HULL.mins, hullMax(ent))) return;
  const p = new THREE.Vector3();
  for (let r = 1; r <= 40; r += 1) {
    for (const [x, y, z] of [[0, 1, 0], [1, 0, 0], [-1, 0, 0], [0, 0, 1], [0, 0, -1], [0, -1, 0], [1, 1, 0], [-1, 1, 0], [0, 1, 1], [0, 1, -1]]) {
      p.set(ent.origin.x + x * r, ent.origin.y + y * r, ent.origin.z + z * r);
      if (!world.isStuck(p, HULL.mins, hullMax(ent))) { ent.origin.copy(p); return; }
    }
  }
}

function handleDuck(world, ent, wantDuck) {
  if (wantDuck && !ent.ducked) {
    if (ent.onGround) {
      ent.ducked = true;
    } else {
      // in-air duck pulls the feet up (crouch-jump)
      const test = tmpA.copy(ent.origin); test.y += HULL.stand.y - HULL.duck.y;
      ent.ducked = true;
      if (!world.isStuck(test, HULL.mins, HULL.duck)) {
        ent.origin.copy(test);
        ent.viewOffsetSmooth -= HULL.stand.y - HULL.duck.y;
      }
    }
  } else if (!wantDuck && ent.ducked) {
    if (ent.onGround) {
      if (!world.isStuck(ent.origin, HULL.mins, HULL.stand)) ent.ducked = false;
    } else {
      const test = tmpA.copy(ent.origin); test.y -= HULL.stand.y - HULL.duck.y;
      if (!world.isStuck(test, HULL.mins, HULL.stand)) {
        ent.origin.copy(test); ent.ducked = false;
        ent.viewOffsetSmooth += HULL.stand.y - HULL.duck.y;
      } else if (!world.isStuck(ent.origin, HULL.mins, HULL.stand)) ent.ducked = false;
    }
  }
}

/**
 * cmd: { forwardmove, sidemove, yaw (deg), jump, duck, walk, maxspeed, moveYawOverride? }
 */
export function playerMove(world, ent, cmd, dt) {
  unstick(world, ent);
  handleDuck(world, ent, cmd.duck);

  const yaw = THREE.MathUtils.degToRad(cmd.yaw);
  const fx = -Math.sin(yaw), fz = -Math.cos(yaw);
  const rx = Math.cos(yaw), rz = -Math.sin(yaw);
  let fm = cmd.forwardmove, sm = cmd.sidemove;
  let maxspeed = cmd.maxspeed;
  if (ent.ducked && ent.onGround) maxspeed *= MV.duckSpeed;
  else if (cmd.walk && ent.onGround) maxspeed *= MV.walkSpeed;

  const wish = tmpB.set(fx * fm + rx * sm, 0, fz * fm + rz * sm);
  let wishspeed = wish.length();
  const wishdir = new THREE.Vector3();
  if (wishspeed > 0) wishdir.copy(wish).divideScalar(wishspeed);
  if (wishspeed > maxspeed) wishspeed = maxspeed;

  // start gravity
  if (!ent.onGround) ent.velocity.y -= MV.gravity * 0.5 * dt;

  // jump (before friction -> perfect bhops keep their speed)
  if (cmd.jump && ent.onGround && (!ent.oldJump || cmd.autoJump)) {
    if (!MV.enableBunnyhopping) {
      const cap = 1.1 * cmd.maxspeed;
      const hs = Math.hypot(ent.velocity.x, ent.velocity.z);
      if (hs > cap) { ent.velocity.x *= cap / hs; ent.velocity.z *= cap / hs; }
    }
    ent.onGround = false;
    ent.velocity.y = MV.jumpImpulse;
    ent.events.push('jump');
    ent.velocity.y -= MV.gravity * 0.5 * dt;
  }
  ent.oldJump = cmd.jump;

  if (ent.onGround) friction(ent, dt);

  if (ent.onGround) walkMove(world, ent, wishdir, wishspeed, maxspeed, dt);
  else airMove(world, ent, wishdir, wishspeed, dt);

  categorizePosition(world, ent);

  if (!ent.onGround) ent.velocity.y -= MV.gravity * 0.5 * dt;

  // clamp
  const mv = MV.maxvelocity;
  ent.velocity.x = THREE.MathUtils.clamp(ent.velocity.x, -mv, mv);
  ent.velocity.y = THREE.MathUtils.clamp(ent.velocity.y, -mv, mv);
  ent.velocity.z = THREE.MathUtils.clamp(ent.velocity.z, -mv, mv);
}

export function predictOnGroundNextTick(world, ent, dt) {
  // cheap prediction: is there ground right under us within the next tick of falling?
  if (ent.onGround) return true;
  if (ent.velocity.y > 0) return false;
  const fall = -ent.velocity.y * dt + MV.gravity * dt * dt + 2;
  const end = tmpA.copy(ent.origin); end.y -= fall;
  end.x += ent.velocity.x * dt; end.z += ent.velocity.z * dt;
  trace(world, ent, ent.origin, end);
  return tr.fraction < 1 && tr.normal.y >= 0.7;
}
