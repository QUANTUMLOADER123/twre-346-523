// Authentic Valve Source Engine (CS:GO CGameMovement) Physics & Movement Implementation
// Includes Ground Friction, Counter-Strafing, AirAccelerate (30 u/s cap), Bhop, Auto-Strafe,
// Z-Hop, Pre-Speed, Duck in Air, Edge Jump & Air-Stuck!

export const PLAYER_RADIUS = 16;
export const PLAYER_HEIGHT_STAND = 72;
export const PLAYER_HEIGHT_CROUCH = 54;
export const EYE_HEIGHT_STAND = 64;
export const EYE_HEIGHT_CROUCH = 46;
export const STEP_HEIGHT = 18;
export const JUMP_IMPULSE = 301.993377; // Exact CS:GO sv_jump_impulse

export class SourcePlayerPhysics {
  constructor() {
    this.pos = { x: 0, y: 5, z: 0 };
    this.vel = { x: 0, y: 0, z: 0 };
    this.onGround = false;
    this.wasOnGround = false;
    this.crouchAmount = 0;
    this.stamina = 0;
    this.strafeFlip = 1;
    this.lastYaw = 0;
    this.walkCycle = 0;
    this.stepTimer = 0;
  }

  reset(spawn) {
    this.pos.x = spawn.x;
    this.pos.y = spawn.y;
    this.pos.z = spawn.z;
    this.vel.x = 0;
    this.vel.y = 0;
    this.vel.z = 0;
    this.onGround = false;
    this.wasOnGround = false;
    this.crouchAmount = 0;
    this.stamina = 0;
  }

  getSpeed2D() {
    return Math.hypot(this.vel.x, this.vel.z);
  }

  getCurrentHeight() {
    return PLAYER_HEIGHT_STAND - this.crouchAmount * (PLAYER_HEIGHT_STAND - PLAYER_HEIGHT_CROUCH);
  }

  getEyeY() {
    return this.pos.y + (EYE_HEIGHT_STAND - this.crouchAmount * (EYE_HEIGHT_STAND - EYE_HEIGHT_CROUCH));
  }

  // Exact CS:GO Ground Friction (pm_shared.c / gamemovement.cpp)
  applyFriction(dt, movCfg, surfaceFriction = 1.0) {
    const speed = Math.hypot(this.vel.x, this.vel.z);
    if (speed < 0.1) {
      this.vel.x = 0;
      this.vel.z = 0;
      return;
    }

    const stopspeed = movCfg.sv_stopspeed;
    const friction = movCfg.sv_friction * surfaceFriction;
    const control = speed < stopspeed ? stopspeed : speed;
    const drop = control * friction * dt;

    let newspeed = speed - drop;
    if (newspeed < 0) newspeed = 0;

    if (newspeed !== speed) {
      newspeed /= speed;
      this.vel.x *= newspeed;
      this.vel.z *= newspeed;
    }
  }

  // Exact CS:GO Ground Accelerate
  accelerate(wishdirX, wishdirZ, wishspeed, accel, dt) {
    const currentspeed = this.vel.x * wishdirX + this.vel.z * wishdirZ;
    const addspeed = wishspeed - currentspeed;
    if (addspeed <= 0) return;

    let accelspeed = accel * dt * wishspeed;
    if (accelspeed > addspeed) accelspeed = addspeed;

    this.vel.x += accelspeed * wishdirX;
    this.vel.z += accelspeed * wishdirZ;
  }

  // Exact CS:GO AirAccelerate (with classic 30 u/s air_wishspeed cap!)
  airAccelerate(wishdirX, wishdirZ, wishspeed, airaccel, dt) {
    const wishspd = Math.min(wishspeed, 30.0);
    const currentspeed = this.vel.x * wishdirX + this.vel.z * wishdirZ;
    const addspeed = wishspd - currentspeed;
    if (addspeed <= 0) return;

    let accelspeed = airaccel * wishspeed * dt;
    if (accelspeed > addspeed) accelspeed = addspeed;

    this.vel.x += accelspeed * wishdirX;
    this.vel.z += accelspeed * wishdirZ;
  }

  // Auto-Stop / Fast-Stop (Counter-strafe helper)
  applyQuickStop(dt, movCfg) {
    const speed = Math.hypot(this.vel.x, this.vel.z);
    if (speed <= 15) {
      this.vel.x = 0;
      this.vel.z = 0;
      return;
    }
    const oppX = -this.vel.x / speed;
    const oppZ = -this.vel.z / speed;
    this.accelerate(oppX, oppZ, 250, movCfg.sv_accelerate * 2.2, dt);
  }

  // Checks if there is ground at a given (x, y, z) position (used for Edge Jump!)
  hasSupportAt(x, y, z, colliders) {
    if (y <= 1.0) return true;
    const r = PLAYER_RADIUS * 0.85;
    for (const c of colliders) {
      if (x + r <= c.minX || x - r >= c.maxX) continue;
      if (z + r <= c.minZ || z - r >= c.maxZ) continue;
      if (Math.abs(y - c.maxY) <= 4.0) return true;
    }
    return false;
  }

  stepSimulation({
    dt,
    keys,
    viewYaw,
    maxWeaponSpeed,
    cfg,
    colliders,
    forceAutoStop = false,
    onJumpCallback = null,
    onFootstepCallback = null
  }) {
    const movCfg = cfg.movement;
    const aaCfg = cfg.antiaim;

    // Air-Stuck exploit (freezes player mid-air!)
    if (movCfg.airStuckMode !== 'off' && movCfg.airStuckActive && !this.onGround) {
      this.vel.x = 0;
      this.vel.y = 0;
      this.vel.z = 0;
      return;
    }

    // 1. Handle Crouch, Duck-in-Air & Fake-Duck
    let targetCrouch = 0;
    if (keys.fakeDuck || (aaCfg.enabled && aaCfg.fakeDuck && keys.crouch)) {
      targetCrouch = 0.58 + Math.sin(performance.now() * 0.018) * 0.32;
    } else if (keys.crouch || (movCfg.duckInAir && !this.onGround)) {
      targetCrouch = 1.0;
    }
    const crouchSpeed = 8.8;
    this.crouchAmount += (targetCrouch - this.crouchAmount) * Math.min(1, dt * crouchSpeed);

    // 2. Recover stamina
    if (this.stamina > 0) {
      this.stamina = Math.max(0, this.stamina - dt * 1600);
    }

    // 3. Compute Forward & Right unit vectors from viewYaw
    const fwdX = -Math.sin(viewYaw);
    const fwdZ = -Math.cos(viewYaw);
    const rightX = Math.cos(viewYaw);
    const rightZ = -Math.sin(viewYaw);

    let fmove = (keys.forward ? 1 : 0) - (keys.backward ? 1 : 0);
    let smove = (keys.right ? 1 : 0) - (keys.left ? 1 : 0);

    // 4. Max wishspeed calculation (Weapon speed + crouch + walk / slowwalk + Pre-speed)
    let maxSpeed = maxWeaponSpeed;
    if (movCfg.preSpeed && this.onGround && (fmove !== 0 || smove !== 0)) {
      maxSpeed = Math.max(maxSpeed, 288); // Pre-speed ground boost up to 288 u/s!
    }
    if (this.crouchAmount > 0.45 && this.onGround) {
      maxSpeed *= 0.34;
    } else if (keys.walk || (aaCfg.enabled && aaCfg.slowWalk && keys.walk)) {
      maxSpeed = aaCfg.enabled ? aaCfg.slowWalkSpeed : maxWeaponSpeed * 0.52;
    }
    if (!movCfg.infiniteStamina && this.stamina > 0 && this.onGround) {
      maxSpeed *= Math.max(0.55, 1 - this.stamina / 1800);
    }

    // 5. Edge Jump detection (if grounded now, elevated, and next frame has no floor support)
    let edgeJumpTriggered = false;
    if (movCfg.edgeJump && this.onGround && this.pos.y > 4) {
      const nextX = this.pos.x + this.vel.x * dt * 2.2;
      const nextZ = this.pos.z + this.vel.z * dt * 2.2;
      if (!this.hasSupportAt(nextX, this.pos.y, nextZ, colliders)) {
        edgeJumpTriggered = true;
      }
    }

    // 6. Check Jump / Bunnyhop BEFORE Friction!
    const wantsJump = (movCfg.bhop ? keys.jump : keys.jumpPressedOnce) || edgeJumpTriggered;
    if (this.onGround && wantsJump) {
      this.onGround = false;
      this.vel.y = JUMP_IMPULSE;
      if (!movCfg.infiniteStamina) {
        this.stamina = 400;
      }
      if (onJumpCallback) onJumpCallback();
    }
    keys.jumpPressedOnce = false;

    // 7. Compute Wishdir & Wishspeed
    let wishX = fwdX * fmove + rightX * smove;
    let wishZ = fwdZ * fmove + rightZ * smove;
    let wishLen = Math.hypot(wishX, wishZ);
    if (wishLen > 0.0001) {
      wishX /= wishLen;
      wishZ /= wishLen;
    }
    let wishspeed = wishLen > 0 ? maxSpeed : 0;

    // 8. Ground Move vs Air Move
    if (this.onGround) {
      this.applyFriction(dt, movCfg, 1.0);

      if (forceAutoStop) {
        this.applyQuickStop(dt, movCfg);
      } else if (wishLen === 0 && movCfg.fastStop) {
        this.applyQuickStop(dt, movCfg);
      } else if (wishLen > 0) {
        this.accelerate(wishX, wishZ, wishspeed, movCfg.sv_accelerate, dt);
      }

      const gSpeed = Math.hypot(this.vel.x, this.vel.z);
      if (gSpeed > maxSpeed && !keys.jump) {
        const scale = maxSpeed / gSpeed;
        this.vel.x *= scale;
        this.vel.z *= scale;
      }

      if (gSpeed > 25) {
        this.walkCycle += gSpeed * dt * 0.065;
        if (gSpeed > 135) {
          this.stepTimer += gSpeed * dt;
          if (this.stepTimer > 95) {
            this.stepTimer = 0;
            if (onFootstepCallback) onFootstepCallback();
          }
        }
      }
    } else {
      // AIR MOVE (AirAccelerate + Auto-Strafe + Z-Hop!)
      const yawDelta = viewYaw - this.lastYaw;
      const speed2D = Math.hypot(this.vel.x, this.vel.z);

      if (movCfg.autoStrafe && (keys.jump || fmove !== 0 || smove !== 0 || Math.abs(yawDelta) > 0.0005)) {
        let moveAngleOffset = 0;
        if (movCfg.autoStrafeMode !== 'view' && (fmove !== 0 || smove !== 0)) {
          moveAngleOffset = Math.atan2(-smove, fmove);
        }
        const desiredYaw = viewYaw + moveAngleOffset;
        const desiredDirX = -Math.sin(desiredYaw);
        const desiredDirZ = -Math.cos(desiredYaw);

        if (speed2D < 30) {
          this.accelerate(desiredDirX, desiredDirZ, 230, movCfg.sv_accelerate * 2, dt);
        } else {
          const velYaw = Math.atan2(-this.vel.x, -this.vel.z);
          let diff = desiredYaw - velYaw;
          while (diff > Math.PI) diff -= Math.PI * 2;
          while (diff < -Math.PI) diff += Math.PI * 2;

          this.strafeFlip = -this.strafeFlip;
          const strafeSign = Math.abs(diff) > 0.02 ? Math.sign(diff) : this.strafeFlip;
          const orthoYaw = velYaw + strafeSign * (Math.PI * 0.47);
          const airWishX = -Math.sin(orthoYaw);
          const airWishZ = -Math.cos(orthoYaw);

          const effectiveAirAccel = movCfg.zHop ? movCfg.sv_airaccelerate * 1.65 : movCfg.sv_airaccelerate;
          this.airAccelerate(airWishX, airWishZ, 250, effectiveAirAccel, dt);

          const maxAirCap = movCfg.zHop ? 680 : 580;
          const newSpeed = Math.min(maxAirCap, Math.hypot(this.vel.x, this.vel.z));
          const steerRate = (movCfg.strafeSmooth / 100) * 6.5 * dt;
          const zHopWave = movCfg.zHop ? Math.sin(performance.now() * 0.035) * 0.015 : 0;
          const blendedYaw = velYaw + Math.max(-steerRate, Math.min(steerRate, diff)) + zHopWave;
          this.vel.x = -Math.sin(blendedYaw) * newSpeed;
          this.vel.z = -Math.cos(blendedYaw) * newSpeed;
        }
      } else if (wishLen > 0) {
        // Pure Manual CS:GO Air-Strafe (A/D + mouse movement)
        this.airAccelerate(wishX, wishZ, wishspeed, movCfg.sv_airaccelerate, dt);
      }
    }

    this.lastYaw = viewYaw;

    // 9. Apply Gravity
    this.vel.y -= movCfg.sv_gravity * dt;

    // 10. Integrate Position & Resolve AABB Collisions with Step-Slide
    this.resolveCollisions(dt, colliders);
  }

  resolveCollisions(dt, colliders) {
    const r = PLAYER_RADIUS;
    const h = this.getCurrentHeight();

    // Move X
    this.pos.x += this.vel.x * dt;
    for (const c of colliders) {
      if (this.pos.y + h <= c.minY + 0.5 || this.pos.y >= c.maxY - 0.5) continue;
      if (this.pos.z + r <= c.minZ || this.pos.z - r >= c.maxZ) continue;
      if (this.pos.x + r > c.minX && this.pos.x - r < c.maxX) {
        const stepDelta = c.maxY - this.pos.y;
        if (stepDelta > 0 && stepDelta <= STEP_HEIGHT && this.vel.y <= 0) {
          this.pos.y = c.maxY;
          this.vel.y = 0;
          this.onGround = true;
        } else {
          if (this.vel.x > 0) this.pos.x = c.minX - r;
          else if (this.vel.x < 0) this.pos.x = c.maxX + r;
          this.vel.x = 0;
        }
      }
    }

    // Move Z
    this.pos.z += this.vel.z * dt;
    for (const c of colliders) {
      if (this.pos.y + h <= c.minY + 0.5 || this.pos.y >= c.maxY - 0.5) continue;
      if (this.pos.x + r <= c.minX || this.pos.x - r >= c.maxX) continue;
      if (this.pos.z + r > c.minZ && this.pos.z - r < c.maxZ) {
        const stepDelta = c.maxY - this.pos.y;
        if (stepDelta > 0 && stepDelta <= STEP_HEIGHT && this.vel.y <= 0) {
          this.pos.y = c.maxY;
          this.vel.y = 0;
          this.onGround = true;
        } else {
          if (this.vel.z > 0) this.pos.z = c.minZ - r;
          else if (this.vel.z < 0) this.pos.z = c.maxZ + r;
          this.vel.z = 0;
        }
      }
    }

    // Move Y
    this.pos.y += this.vel.y * dt;
    let grounded = false;

    for (const c of colliders) {
      if (this.pos.x + r <= c.minX + 0.5 || this.pos.x - r >= c.maxX - 0.5) continue;
      if (this.pos.z + r <= c.minZ + 0.5 || this.pos.z - r >= c.maxZ - 0.5) continue;

      if (this.vel.y <= 0 && this.pos.y <= c.maxY && this.pos.y >= c.maxY - 24) {
        this.pos.y = c.maxY;
        this.vel.y = 0;
        grounded = true;
      } else if (this.vel.y > 0 && this.pos.y + h >= c.minY && this.pos.y + h <= c.minY + 24) {
        this.pos.y = c.minY - h;
        this.vel.y = 0;
      }
    }

    if (this.pos.y < 0) {
      this.pos.y = 0;
      this.vel.y = 0;
      grounded = true;
    }

    this.wasOnGround = this.onGround;
    this.onGround = grounded;
  }
}
