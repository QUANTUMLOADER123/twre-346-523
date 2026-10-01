// Actor: shared state for the local player and bots
import * as THREE from 'three';
import { WEAPONS } from './weapons.js';
import { EYE_STAND, EYE_DUCK } from './movement.js';
import { createCharacter } from './models.js';

let NEXT_ID = 1;

export class Actor {
  constructor({ name, team, isBot, primary = 'ak47' }) {
    this.id = NEXT_ID++;
    this.name = name; this.team = team; this.isBot = isBot;
    this.origin = new THREE.Vector3(); this.prevOrigin = new THREE.Vector3();
    this.velocity = new THREE.Vector3();
    this.onGround = false; this.ducked = false; this.oldJump = false;
    this.viewOffsetSmooth = EYE_STAND; this.duckVis = 0;
    this.events = [];
    this.yaw = 0; this.pitch = 0;           // view angles (deg). pitch + = down
    this.health = 100; this.armor = 100; this.alive = false; this.deathTime = 0; this.respawnAt = 0;
    this.kills = 0; this.deaths = 0; this.hs = 0; this.assists = 0;
    this.lastAttacker = null; this.lastDamageTime = -10;
    this.inventory = [primary, 'deagle', 'knife'];
    this.slot = 0; this.prevSlot = 1;
    this.ammo = {};
    for (const id of Object.keys(WEAPONS)) this.ammo[id] = { clip: WEAPONS[id].clip, reserve: WEAPONS[id].reserve };
    this.ws = { nextAttack: 0, reloadEnd: 0, drawEnd: 0, scoped: 0, fireInacc: 0, recoilIdx: 0, punchP: 0, punchY: 0, lastShot: -10, reloading: false, burst: 0, inspect: 0 };
    // model pose (server-side "real" angles)
    this.pose = { bodyYaw: 0, aimYaw: 0, pitch: 0, phase: 0, speedFwd: 0, speedSide: 0, duck: 0, air: false, dead: 0, deathDir: 1, pistol: false };
    this.model = createCharacter(team);
    this.model.setGun(this.weaponId);
    this.stepAccum = 0;
  }
  get weaponId() { return this.inventory[this.slot]; }
  get weapon() { return WEAPONS[this.weaponId]; }
  eye(out = new THREE.Vector3()) { return out.set(this.origin.x, this.origin.y + (this.ducked ? EYE_DUCK : EYE_STAND), this.origin.z); }
  center(out = new THREE.Vector3()) { return out.set(this.origin.x, this.origin.y + (this.ducked ? 30 : 40), this.origin.z); }
  speed2d() { return Math.hypot(this.velocity.x, this.velocity.z); }

  switchTo(slot, now) {
    if (slot === this.slot || !this.inventory[slot]) return;
    this.prevSlot = this.slot; this.slot = slot;
    const w = this.weapon;
    this.ws.drawEnd = now + w.draw; this.ws.nextAttack = Math.max(this.ws.nextAttack, now + w.draw * 0.6);
    this.ws.reloading = false; this.ws.scoped = 0; this.ws.recoilIdx = 0; this.ws.inspect = 0;
    this.model.setGun(this.weaponId);
    this.pose.pistol = w.slot === 1 || w.melee;
  }
  resetForSpawn(pos, yaw) {
    this.origin.set(pos.x, pos.y, pos.z); this.prevOrigin.copy(this.origin);
    this.velocity.set(0, 0, 0);
    this.yaw = yaw; this.pitch = 0;
    this.health = 100; this.armor = 100; this.alive = true;
    this.ducked = false; this.onGround = false; this.viewOffsetSmooth = EYE_STAND;
    for (const id of Object.keys(WEAPONS)) this.ammo[id] = { clip: WEAPONS[id].clip, reserve: WEAPONS[id].reserve };
    Object.assign(this.ws, { nextAttack: 0, reloadEnd: 0, scoped: 0, fireInacc: 0, recoilIdx: 0, punchP: 0, punchY: 0, reloading: false, burst: 0 });
    this.pose.dead = 0; this.model.root.visible = true;
    this.lastAttacker = null;
  }
}
