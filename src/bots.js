import * as THREE from 'three';
import { SourcePlayerPhysics } from './physics.js';
import { CharacterRig } from './models.js';
import { WEAPONS, matchConfig } from './config.js';
import { simulateFireBullet } from './hvh.js';

export const BOT_NAMES = [
  'gamesense.pub [UID:42]',
  'skeet.cc_enjoyer',
  'neverlose_king',
  'fatality_user',
  'onetap_v4_crack',
  'primordial_hvh',
  'nixware_spinner',
  'ev0lve_xyz',
  'aimware_v5',
  'nemesis_dt'
];

export const BOT_RANKS = [
  'The Global Elite',
  'Supreme Master First Class',
  'Legendary Eagle Master',
  'Distinguished Master Guardian',
  'Gold Nova Master',
  'Silver Elite Master'
];

export class BotEntity {
  constructor(index, scene) {
    this.index = index;
    this.scene = scene;
    this.name = BOT_NAMES[index % BOT_NAMES.length];
    this.rank = BOT_RANKS[index % BOT_RANKS.length];
    this.wins = 140 + ((index * 73) % 620);

    // Per-player Skeet Player-List settings
    this.plist = {
      whitelist: false,
      highPriority: false,
      forceBodyAim: false,
      forceYaw: 'auto' // 'auto' | 'left' | 'right' | 'center'
    };

    this.weaponId = this.pickWeaponId();
    this.weapon = WEAPONS[this.weaponId];

    this.physics = new SourcePlayerPhysics();
    this.rig = new CharacterRig({ isEnemy: true });
    this.rig.setWeapon(this.weaponId);
    this.scene.add(this.rig.root);

    this.btGhostRig = new CharacterRig({
      isEnemy: false,
      isGhost: true,
      ghostColor: '#38bdf8',
      ghostOpacity: 0.28,
      wireframe: false
    });
    this.scene.add(this.btGhostRig.root);

    this.alive = true;
    this.hp = 100;
    this.maxHp = 100;
    this.kills = 0;
    this.deaths = 0;
    this.respawnTimer = 0;

    this.spinYaw = Math.random() * Math.PI * 2;
    this.realYaw = 0;
    this.fakeYaw = 0;
    this.pitch = 0;
    this.targetWaypoint = null;
    this.waypointTimer = 0;
    this.fireCooldown = 0.8 + Math.random() * 0.8;
    this.backtrackHistory = [];
  }

  pickWeaponId() {
    const poolType = matchConfig.botWeapons;
    let pool = ['ssg08', 'scar20', 'awp', 'ak47', 'm4a1s', 'deagle'];
    if (poolType === 'snipers') pool = ['ssg08', 'scar20', 'awp'];
    else if (poolType === 'pistols') pool = ['deagle', 'r8'];
    else if (poolType === 'knives') pool = ['knife'];
    return pool[(this.index + Math.floor(Math.random() * pool.length)) % pool.length];
  }

  destroy() {
    this.scene.remove(this.rig.root);
    this.scene.remove(this.btGhostRig.root);
  }

  spawn(spawnPoint) {
    this.alive = true;
    this.hp = matchConfig.startingHp || 100;
    this.maxHp = this.hp;
    this.physics.reset(spawnPoint);
    this.rig.root.visible = true;
    this.backtrackHistory = [];
    this.weaponId = this.pickWeaponId();
    this.weapon = WEAPONS[this.weaponId];
    this.rig.setWeapon(this.weaponId);
  }

  takeDamage(amount) {
    if (!this.alive) return false;
    this.hp = Math.max(0, this.hp - amount);
    if (this.hp <= 0) {
      this.alive = false;
      this.deaths++;
      this.respawnTimer = 2.2;
      this.rig.root.visible = false;
      this.btGhostRig.root.visible = false;
      return true;
    }
    return false;
  }

  update({
    dt,
    cfg,
    playerPhysics,
    playerAA,
    colliders,
    botSpawns,
    botWaypoints,
    allowRespawn = true,
    isFreezeTime = false,
    onBotShoot
  }) {
    if (!this.alive) {
      if (!allowRespawn) return;
      this.respawnTimer -= dt;
      if (this.respawnTimer <= 0 && botSpawns.length > 0) {
        const sp = botSpawns[(this.index + Math.floor(Math.random() * botSpawns.length)) % botSpawns.length];
        this.spawn(sp);
      }
      return;
    }

    const mode = matchConfig.botBehavior; // 'legit' | 'hvh' | 'moving' | 'static'

    this.waypointTimer -= dt;
    if ((!this.targetWaypoint || this.waypointTimer <= 0) && botWaypoints.length > 0) {
      this.targetWaypoint = botWaypoints[Math.floor(Math.random() * botWaypoints.length)];
      this.waypointTimer = 3.0 + Math.random() * 2.5;
    }

    const keys = {
      forward: false,
      backward: false,
      left: false,
      right: false,
      jump: false,
      jumpPressedOnce: false,
      crouch: false,
      walk: false
    };

    let moveYaw = 0;
    if (!isFreezeTime && mode !== 'static' && this.targetWaypoint) {
      const dx = this.targetWaypoint.x - this.physics.pos.x;
      const dz = this.targetWaypoint.z - this.physics.pos.z;
      const dist = Math.hypot(dx, dz);
      moveYaw = Math.atan2(-dx, -dz);

      if (dist > 45) {
        keys.forward = true;
        if (mode === 'hvh' || mode === 'moving') {
          keys.jump = Math.random() < 0.45;
        }
      } else {
        this.waypointTimer = 0;
      }
    }

    const botCfg = {
      movement: {
        ...cfg.movement,
        bhop: true,
        autoStrafe: false,
        preSpeed: false,
        duckInAir: false,
        edgeJump: false,
        airStuckMode: 'off'
      },
      antiaim: { enabled: false }
    };
    this.physics.stepSimulation({
      dt,
      keys,
      viewYaw: moveYaw,
      maxWeaponSpeed: this.weapon.maxSpeed * 0.88,
      cfg: botCfg,
      colliders
    });

    const dxPlayer = playerPhysics.pos.x - this.physics.pos.x;
    const dzPlayer = playerPhysics.pos.z - this.physics.pos.z;
    const yawToPlayer = Math.atan2(-dxPlayer, -dzPlayer);

    if (mode === 'hvh') {
      this.spinYaw = (this.spinYaw + dt * 16) % (Math.PI * 2);
      this.realYaw = this.index % 2 === 0
        ? this.spinYaw
        : yawToPlayer + Math.PI + Math.sin(performance.now() * 0.025) * 1.1;
      this.fakeYaw = this.realYaw + (this.index % 2 === 0 ? 0.95 : -0.95);
      this.pitch = -1.42;
    } else {
      this.realYaw = yawToPlayer;
      this.fakeYaw = yawToPlayer;
      this.pitch = 0;
    }

    let displayedYaw = cfg.rage.resolver ? this.realYaw : this.fakeYaw;
    if (this.plist.forceYaw === 'left') displayedYaw = yawToPlayer + Math.PI / 2;
    else if (this.plist.forceYaw === 'right') displayedYaw = yawToPlayer - Math.PI / 2;
    else if (this.plist.forceYaw === 'center') displayedYaw = yawToPlayer + Math.PI;

    this.rig.updatePose({
      pos: this.physics.pos,
      realYaw: displayedYaw,
      lowerYaw: this.fakeYaw,
      pitch: this.pitch,
      crouchAmount: this.physics.crouchAmount,
      speed: this.physics.getSpeed2D(),
      walkCycle: this.physics.walkCycle,
      slideWalk: mode === 'hvh',
      onGround: this.physics.onGround
    });
    this.rig.updateChams(cfg.visuals);

    const hb = this.rig.getHitboxes((cfg.rage.multipointScale || 75) / 100);
    this.backtrackHistory.push({
      pos: { ...this.physics.pos },
      realYaw: displayedYaw,
      lowerYaw: this.fakeYaw,
      pitch: this.pitch,
      crouchAmount: this.physics.crouchAmount,
      hitboxes: hb
    });
    if (this.backtrackHistory.length > 16) {
      this.backtrackHistory.shift();
    }

    if (
      cfg.visuals.backtrackChams &&
      cfg.rage.backtrack &&
      this.backtrackHistory.length > 3 &&
      this.physics.getSpeed2D() > 35
    ) {
      const oldest = this.backtrackHistory[0];
      this.btGhostRig.root.visible = true;
      this.btGhostRig.updatePose({
        pos: oldest.pos,
        realYaw: oldest.realYaw,
        lowerYaw: oldest.lowerYaw,
        pitch: oldest.pitch,
        crouchAmount: oldest.crouchAmount,
        speed: 0,
        walkCycle: 0,
        onGround: true
      });
    } else {
      this.btGhostRig.root.visible = false;
    }

    // Bot Shooting AI
    if (!isFreezeTime && (mode === 'hvh' || mode === 'legit') && !this.weapon.isMelee) {
      this.fireCooldown -= dt;
      if (this.fireCooldown <= 0) {
        const botEye = new THREE.Vector3(
          this.physics.pos.x,
          this.physics.pos.y + 58,
          this.physics.pos.z
        );

        const targetPos = cfg.antiaim.enabled && cfg.antiaim.fakeLag && cfg.antiaim.fakeLagMode !== 'off'
          ? playerAA.lastSentState.pos
          : playerPhysics.pos;

        const targetAim = new THREE.Vector3(
          targetPos.x,
          targetPos.y + 42,
          targetPos.z
        );

        if (cfg.antiaim.enabled) {
          const desyncTrick = cfg.antiaim.bodyYaw ? (cfg.antiaim.desyncLimit / 60) * 18 : 0;
          const spinTrick = cfg.antiaim.yawMode === 'spin' ? 14 : 6;
          const missOffset = (desyncTrick + spinTrick) * (Math.random() < 0.78 ? 1 : 0.15);
          targetAim.x += Math.cos(playerAA.fakeYaw) * missOffset;
          targetAim.z += Math.sin(playerAA.fakeYaw) * missOffset;
        }

        const sim = simulateFireBullet(
          botEye,
          targetAim,
          this.weapon,
          'chest',
          colliders,
          mode === 'hvh'
        );

        if (sim.damage > 0) {
          const diff = matchConfig.botDifficulty || 55;
          this.fireCooldown = Math.max(0.5, this.weapon.fireRate * (1.75 - (diff / 100) * 0.75));
          if (onBotShoot) {
            onBotShoot(this, botEye, targetAim, sim);
          }
        } else {
          this.fireCooldown = 0.25;
        }
      }
    }
  }
}
