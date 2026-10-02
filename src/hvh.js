import * as THREE from 'three';

export function intersectRayAABB(origin, dir, invDir, box, maxDist) {
  const t1 = (box.minX - origin.x) * invDir.x;
  const t2 = (box.maxX - origin.x) * invDir.x;
  const t3 = (box.minY - origin.y) * invDir.y;
  const t4 = (box.maxY - origin.y) * invDir.y;
  const t5 = (box.minZ - origin.z) * invDir.z;
  const t6 = (box.maxZ - origin.z) * invDir.z;

  const tMin = Math.max(Math.min(t1, t2), Math.min(t3, t4), Math.min(t5, t6));
  const tMax = Math.min(Math.max(t1, t2), Math.max(t3, t4), Math.max(t5, t6));

  if (tMax < 0 || tMin > tMax || tMin > maxDist) return null;
  return { tMin: Math.max(0, tMin), tMax: Math.min(maxDist, tMax) };
}

export function simulateFireBullet(origin, targetPoint, weapon, hitboxType, colliders, allowAutoWall = true) {
  const dx = targetPoint.x - origin.x;
  const dy = targetPoint.y - origin.y;
  const dz = targetPoint.z - origin.z;
  const dist = Math.hypot(dx, dy, dz);
  if (dist < 0.001) return { damage: 0, wallbanged: false, wallCount: 0 };

  if (weapon.isShortRange && dist > (weapon.range || 185)) {
    return { damage: 0, wallbanged: false, wallCount: 0 };
  }

  const dir = { x: dx / dist, y: dy / dist, z: dz / dist };
  const invDir = {
    x: Math.abs(dir.x) > 1e-6 ? 1 / dir.x : 1e6,
    y: Math.abs(dir.y) > 1e-6 ? 1 / dir.y : 1e6,
    z: Math.abs(dir.z) > 1e-6 ? 1 / dir.z : 1e6
  };

  let wallCount = 0;
  let dmgScale = 1.0;

  for (let i = 0; i < colliders.length; i++) {
    const c = colliders[i];
    const hit = intersectRayAABB(origin, dir, invDir, c, dist);
    if (!hit) continue;

    if (!c.penetrable || !allowAutoWall || weapon.penetrationPower <= 0) {
      return { damage: 0, wallbanged: false, wallCount: 0, blockedAt: hit.tMin };
    }

    wallCount++;
    if (wallCount > 3) {
      return { damage: 0, wallbanged: true, wallCount, blockedAt: hit.tMin };
    }

    const thickness = Math.max(8, hit.tMax - hit.tMin);
    const thicknessFactor = Math.min(1.6, thickness / 55);
    const loss = (c.penetrationLoss * thicknessFactor) / (weapon.penetrationPower * 0.55);
    dmgScale *= Math.max(0.22, 1 - loss);
  }

  let mult = 1.0;
  if (hitboxType === 'head') mult = weapon.headMultiplier;
  else if (hitboxType === 'stomach' || hitboxType === 'pelvis') mult = weapon.stomachMultiplier;
  else if (hitboxType === 'chest') mult = weapon.chestMultiplier;
  else if (hitboxType === 'legs') mult = weapon.legMultiplier;

  const distFalloff = Math.max(0.82, 1 - dist / 12000);
  const finalDamage = Math.round(weapon.damage * mult * dmgScale * distFalloff);

  return {
    damage: finalDamage,
    wallbanged: wallCount > 0,
    wallCount
  };
}

export function traceWorldRay(origin, dir, colliders, maxDist = 4000) {
  const invDir = {
    x: Math.abs(dir.x) > 1e-6 ? 1 / dir.x : 1e6,
    y: Math.abs(dir.y) > 1e-6 ? 1 / dir.y : 1e6,
    z: Math.abs(dir.z) > 1e-6 ? 1 / dir.z : 1e6
  };

  let closestT = maxDist;
  for (let i = 0; i < colliders.length; i++) {
    const hit = intersectRayAABB(origin, dir, invDir, colliders[i], closestT);
    if (hit && hit.tMin < closestT) {
      closestT = hit.tMin;
    }
  }

  return new THREE.Vector3(
    origin.x + dir.x * closestT,
    origin.y + dir.y * closestT,
    origin.z + dir.z * closestT
  );
}

// ============================================================================
// ANTI-AIM CONTROLLER (Spinbot, Pitch 89°, Yaw Jitter, Body Yaw Desync, Fake Lag)
// ============================================================================
export class AntiAimController {
  constructor() {
    this.tickCount = 0;
    this.spinAngle = 0;
    this.realYaw = 0;
    this.fakeYaw = 0;
    this.cmdPitch = 0;
    this.desyncDeltaDeg = 0;
    this.chokedTicks = 0;
    this.freestandSide = 1;

    this.lastSentState = {
      pos: { x: 0, y: 0, z: 0 },
      realYaw: 0,
      lowerYaw: 0,
      pitch: 0,
      crouchAmount: 0
    };
  }

  update({
    cfg,
    viewYaw,
    viewPitch,
    playerPos,
    playerSpeed,
    onGround,
    crouchAmount,
    bots,
    colliders,
    didShootThisTick
  }) {
    this.tickCount++;
    const aa = cfg.antiaim;

    if (!aa.enabled) {
      this.realYaw = viewYaw;
      this.fakeYaw = viewYaw;
      this.cmdPitch = viewPitch;
      this.desyncDeltaDeg = 0;
      this.chokedTicks = 0;
      this.lastSentState = {
        pos: { ...playerPos },
        realYaw: viewYaw,
        lowerYaw: viewYaw,
        pitch: viewPitch,
        crouchAmount
      };
      return;
    }

    // 1. PITCH ANTI-AIM
    if (didShootThisTick && !cfg.rage.hideShots) {
      this.cmdPitch = viewPitch;
    } else if (aa.pitch === 'down') {
      this.cmdPitch = -1.48; // ~ -89° looking down at floor
    } else if (aa.pitch === 'up') {
      this.cmdPitch = 1.48;
    } else if (aa.pitch === 'zero') {
      this.cmdPitch = 0;
    } else if (aa.pitch === 'jitter') {
      this.cmdPitch = this.tickCount % 2 === 0 ? -1.48 : 1.35;
    } else {
      this.cmdPitch = viewPitch;
    }

    // 2. YAW BASE ('local' vs 'at_targets')
    let baseYaw = viewYaw;
    let closestBot = null;
    let closestDist = Infinity;
    for (const b of bots) {
      if (!b.alive) continue;
      const d = Math.hypot(b.physics.pos.x - playerPos.x, b.physics.pos.z - playerPos.z);
      if (d < closestDist) {
        closestDist = d;
        closestBot = b;
      }
    }

    if (aa.yawBase === 'at_targets' && closestBot) {
      const dx = closestBot.physics.pos.x - playerPos.x;
      const dz = closestBot.physics.pos.z - playerPos.z;
      baseYaw = Math.atan2(-dx, -dz);
    }

    // 3. FREESTANDING
    if (aa.freestanding && closestBot) {
      const eyeY = playerPos.y + 58;
      const rightVecX = Math.cos(baseYaw) * 24;
      const rightVecZ = -Math.sin(baseYaw) * 24;
      const leftHead = { x: playerPos.x - rightVecX, y: eyeY, z: playerPos.z - rightVecZ };
      const rightHead = { x: playerPos.x + rightVecX, y: eyeY, z: playerPos.z + rightVecZ };
      const enemyEye = {
        x: closestBot.physics.pos.x,
        y: closestBot.physics.pos.y + 58,
        z: closestBot.physics.pos.z
      };

      const dummyWep = { damage: 100, headMultiplier: 1, penetrationPower: 1.5 };
      const leftDmg = simulateFireBullet(enemyEye, leftHead, dummyWep, 'head', colliders, true).damage;
      const rightDmg = simulateFireBullet(enemyEye, rightHead, dummyWep, 'head', colliders, true).damage;

      if (leftDmg < rightDmg) this.freestandSide = -1;
      else if (rightDmg < leftDmg) this.freestandSide = 1;
    }

    // 4. YAW MODE (Spinbot / Backward / Jitter / Sideways / Random)
    let yawRad = baseYaw;
    if (aa.yawMode === 'spin') {
      this.spinAngle = (this.spinAngle + aa.spinSpeed) % 360;
      yawRad = baseYaw + (this.spinAngle * Math.PI) / 180;
    } else if (aa.yawMode === 'backward') {
      yawRad = baseYaw + Math.PI;
    } else if (aa.yawMode === 'jitter') {
      const jRad = (aa.jitterRange * Math.PI) / 180;
      yawRad = baseYaw + Math.PI + (this.tickCount % 2 === 0 ? jRad : -jRad);
    } else if (aa.yawMode === 'sideways') {
      yawRad = baseYaw + (Math.PI / 2) * (aa.inverted ? -1 : 1);
    } else if (aa.yawMode === 'random') {
      yawRad = baseYaw + (Math.random() * 2 - 1) * Math.PI;
    }

    if (aa.jitterMode !== 'off' && aa.yawMode !== 'spin') {
      const jr = (aa.jitterRange * Math.PI) / 180;
      if (aa.jitterMode === 'center') {
        yawRad += (this.tickCount % 2 === 0 ? 1 : -1) * (jr * 0.5);
      } else if (aa.jitterMode === 'offset') {
        yawRad += this.tickCount % 2 === 0 ? jr : 0;
      } else if (aa.jitterMode === '3way') {
        const step = (this.tickCount % 3) - 1;
        yawRad += step * jr;
      } else if (aa.jitterMode === 'random') {
        yawRad += (Math.random() - 0.5) * jr;
      }
    }

    this.realYaw = yawRad;

    // 5. BODY YAW (Desync)
    if (aa.bodyYaw) {
      let side = aa.inverted ? -1 : 1;
      if (aa.freestanding) side = this.freestandSide * (aa.inverted ? -1 : 1);

      let deltaDeg = aa.desyncLimit;
      if (aa.bodyYawMode === 'jitter') {
        side *= this.tickCount % 2 === 0 ? 1 : -1;
      } else if (aa.bodyYawMode === 'sway') {
        deltaDeg = aa.desyncLimit * Math.sin(performance.now() * 0.005);
      } else if (aa.bodyYawMode === 'opposite') {
        deltaDeg = Math.min(60, aa.desyncLimit);
      }

      this.desyncDeltaDeg = Math.round(Math.abs(deltaDeg));
      this.fakeYaw = this.realYaw + ((deltaDeg * side) * Math.PI) / 180;
    } else {
      this.desyncDeltaDeg = 0;
      this.fakeYaw = this.realYaw;
    }

    // 6. FAKE LAG
    const flActive = aa.fakeLag && aa.fakeLagMode !== 'off' && (!didShootThisTick || aa.fakeLagWhileShooting);
    if (flActive) {
      let maxChoke = aa.fakeLagLimit;
      if (aa.fakeLagMode === 'dynamic') {
        maxChoke = playerSpeed > 20 ? aa.fakeLagLimit : Math.max(2, Math.floor(aa.fakeLagLimit * 0.35));
      } else if (aa.fakeLagMode === 'fluctuate') {
        maxChoke = Math.max(2, Math.floor((Math.sin(this.tickCount * 0.15) * 0.5 + 0.5) * aa.fakeLagLimit));
      } else if (aa.fakeLagMode === 'break_lc') {
        maxChoke = !onGround && playerSpeed > 230 ? 16 : aa.fakeLagLimit;
      }

      this.chokedTicks++;
      if (this.chokedTicks >= maxChoke) {
        this.chokedTicks = 0;
        this.lastSentState = {
          pos: { ...playerPos },
          realYaw: this.realYaw,
          lowerYaw: this.fakeYaw,
          pitch: this.cmdPitch,
          crouchAmount
        };
      }
    } else {
      this.chokedTicks = 0;
      this.lastSentState = {
        pos: { ...playerPos },
        realYaw: this.realYaw,
        lowerYaw: this.fakeYaw,
        pitch: this.cmdPitch,
        crouchAmount
      };
    }
  }
}

// ============================================================================
// RAGEBOT + KNIFEBOT + MULTIPOINT + BACKTRACK + RESOLVER
// ============================================================================
export class RagebotController {
  constructor() {
    this.lastTargetPoint = null;
    this.lastTargetBot = null;
    this.dtCharge = 1.0;
    this.dtCooldown = 0;
  }

  updateRecharge(dt, rageCfg) {
    if (!rageCfg.doubleTap) {
      this.dtCharge = 0;
      return;
    }
    if (this.dtCooldown > 0) {
      this.dtCooldown = Math.max(0, this.dtCooldown - dt);
      this.dtCharge = 0;
    } else {
      this.dtCharge = Math.min(1.0, this.dtCharge + dt * 1.8);
    }
  }

  computeHitchance(weapon, playerPhysics, isScoped) {
    if (weapon.isMelee) return 100;

    const speed = playerPhysics.getSpeed2D();
    const onGround = playerPhysics.onGround;
    const crouched = playerPhysics.crouchAmount > 0.6;

    let spread = weapon.baseSpread;
    if (weapon.hasScope && isScoped) {
      spread = weapon.scopedSpread;
    }

    if (!onGround) {
      if (weapon.id === 'ssg08' && Math.abs(playerPhysics.vel.y) < 75) {
        spread += 0.0025;
      } else {
        spread += weapon.airSpread;
      }
    } else if (speed > 15) {
      const ratio = Math.min(1.5, speed / weapon.maxSpeed);
      if (ratio > 0.34) {
        spread += weapon.moveSpread * (ratio - 0.25);
      }
    }

    if (crouched && onGround) {
      spread *= 0.65;
    }

    return Math.round(Math.max(12, Math.min(100, (1 - spread * 9.5) * 100)));
  }

  scanKnifebot(eyeOrigin, bots, weapon, cfg) {
    if (!cfg.misc.knifebot || !weapon.isMelee) return null;
    for (const bot of bots) {
      if (!bot.alive || bot.plist.whitelist) continue;
      const chest = new THREE.Vector3(bot.physics.pos.x, bot.physics.pos.y + 44, bot.physics.pos.z);
      const dist = eyeOrigin.distanceTo(chest);
      if (dist <= (weapon.range || 96)) {
        return {
          bot,
          point: chest,
          hitboxType: 'stomach',
          damage: 100,
          wallbanged: false,
          btTicks: 0
        };
      }
    }
    return null;
  }

  scanTargets({
    eyeOrigin,
    viewYaw,
    viewPitch,
    weapon,
    playerPhysics,
    isScoped,
    bots,
    colliders,
    cfg
  }) {
    const rage = cfg.rage;
    this.lastTargetPoint = null;
    this.lastTargetBot = null;

    if (weapon.isMelee) {
      return this.scanKnifebot(eyeOrigin, bots, weapon, cfg);
    }

    if (!rage.enabled) return null;

    const minDmg = rage.minDamageOverrideActive ? rage.minDamageOverride : rage.minDamage;
    const scale = (rage.multipointScale || 75) / 100;

    let bestCandidate = null;
    let bestScore = -1;

    const lookDir = new THREE.Vector3(
      -Math.sin(viewYaw) * Math.cos(viewPitch),
      Math.sin(viewPitch),
      -Math.cos(viewYaw) * Math.cos(viewPitch)
    ).normalize();

    for (const bot of bots) {
      if (!bot.alive || bot.plist.whitelist) continue;

      const recordsToScan = [{ hitboxes: bot.rig.getHitboxes(scale), btTicks: 0, pos: bot.physics.pos }];
      if (rage.backtrack && bot.backtrackHistory.length > 0) {
        const maxBt = Math.min(rage.backtrackTicks, bot.backtrackHistory.length);
        const oldest = bot.backtrackHistory[bot.backtrackHistory.length - maxBt];
        if (oldest) {
          recordsToScan.push({
            hitboxes: oldest.hitboxes,
            btTicks: maxBt,
            pos: oldest.pos
          });
        }
      }

      for (const rec of recordsToScan) {
        const hb = rec.hitboxes;
        const points = [];

        const forceBaim = rage.forceBaimActive || bot.plist.forceBodyAim;

        if (!forceBaim && rage.hitboxes.head) {
          if (rage.multipoint) {
            for (const p of hb.headMultipoints) {
              points.push({ point: p, type: 'head', priority: 3 });
            }
          } else {
            points.push({ point: hb.head, type: 'head', priority: 3 });
          }
        }
        if (rage.hitboxes.stomach) {
          points.push({ point: hb.stomach, type: 'stomach', priority: 2 });
          points.push({ point: hb.pelvis, type: 'pelvis', priority: 2 });
        }
        if (rage.hitboxes.chest) {
          if (rage.multipoint) {
            for (const p of hb.chestMultipoints) {
              points.push({ point: p, type: 'chest', priority: 1.5 });
            }
          } else {
            points.push({ point: hb.chest, type: 'chest', priority: 1.5 });
          }
        }
        if (!forceBaim && rage.hitboxes.legs) {
          points.push({ point: hb.leftLeg, type: 'legs', priority: 1 });
          points.push({ point: hb.rightLeg, type: 'legs', priority: 1 });
        }

        for (const pt of points) {
          if (rage.fov < 180) {
            const toTarget = pt.point.clone().sub(eyeOrigin).normalize();
            const angleDeg = (Math.acos(Math.max(-1, Math.min(1, lookDir.dot(toTarget)))) * 180) / Math.PI;
            if (angleDeg > rage.fov) continue;
          }

          const sim = simulateFireBullet(
            eyeOrigin,
            pt.point,
            weapon,
            pt.type,
            colliders,
            rage.autoWall
          );

          if (sim.damage <= 0) continue;

          const isLethal = sim.damage >= bot.hp;
          if (sim.damage < minDmg && !isLethal) continue;

          let score = sim.damage * pt.priority;
          if (bot.plist.highPriority) score += 800;
          if (rage.preferBaim && isLethal && (pt.type === 'stomach' || pt.type === 'pelvis')) {
            score += 500;
          }

          if (score > bestScore) {
            bestScore = score;
            bestCandidate = {
              bot,
              point: pt.point,
              hitboxType: pt.type,
              damage: sim.damage,
              wallbanged: sim.wallbanged,
              btTicks: rec.btTicks
            };
          }
        }
      }
    }

    if (bestCandidate) {
      this.lastTargetPoint = bestCandidate.point;
      this.lastTargetBot = bestCandidate.bot;
    }

    return bestCandidate;
  }
}
