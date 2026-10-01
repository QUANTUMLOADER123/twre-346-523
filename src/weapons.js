// Weapon definitions (values loosely based on CS:GO)
// spread values are in degrees. recoil pattern: per-shot [pitchUp, yaw] increments of aim punch (deg)

function pattern(arr) { return arr.map(([p, y]) => ({ p, y })); }

const AK_PATTERN = pattern([
  [0.0, 0], [0.9, 0.05], [1.1, -0.05], [1.3, 0.1], [1.4, 0.15], [1.3, -0.1], [1.1, -0.25], [0.9, 0.3], [0.7, 0.45], [0.4, 0.6],
  [0.25, 0.75], [0.2, 0.55], [0.1, -0.4], [0.05, -0.9], [0.05, -1.0], [0.1, -0.8], [0.0, -0.6], [0.05, -0.4], [0.1, 0.5], [0.05, 0.8],
  [0.0, 0.9], [0.05, 0.7], [0.1, 0.4], [0.0, -0.3], [0.05, -0.6], [0.05, -0.7], [0.0, -0.5], [0.05, 0.2], [0.0, 0.5], [0.05, 0.6],
]);
const M4_PATTERN = pattern([
  [0.0, 0], [0.7, 0.0], [0.85, -0.05], [0.95, 0.08], [1.0, 0.12], [0.9, -0.08], [0.75, -0.2], [0.6, 0.25], [0.45, 0.35], [0.3, 0.45],
  [0.2, 0.5], [0.15, 0.3], [0.05, -0.3], [0.05, -0.6], [0.05, -0.7], [0.05, -0.55], [0.0, -0.4], [0.05, -0.3], [0.05, 0.4], [0.05, 0.6],
  [0.0, 0.6], [0.05, 0.5], [0.05, 0.3], [0.0, -0.2], [0.05, -0.4], [0.0, -0.5], [0.05, -0.3], [0.0, 0.2], [0.05, 0.3], [0.0, 0.4],
]);

export const WEAPONS = {
  ak47: {
    id: 'ak47', name: 'AK-47', slot: 0, auto: true, damage: 36, armorPen: 0.775, range: 8192, rangeMod: 0.98,
    cycle: 0.1, clip: 30, reserve: 90, reload: 2.43, draw: 1.0, maxspeed: 215,
    spreadStand: 0.32, spreadCrouch: 0.22, spreadMove: 6.5, spreadAir: 14, spreadFire: 0.42, spreadFireMax: 3.0,
    pattern: AK_PATTERN, recoilScale: 1.0, kick: 1.0, sound: 'rifle', tracer: true, killIcon: 'AK-47',
  },
  m4a4: {
    id: 'm4a4', name: 'M4A4', slot: 0, auto: true, damage: 33, armorPen: 0.70, range: 8192, rangeMod: 0.97,
    cycle: 0.09, clip: 30, reserve: 90, reload: 3.07, draw: 1.0, maxspeed: 225,
    spreadStand: 0.28, spreadCrouch: 0.2, spreadMove: 5.5, spreadAir: 12, spreadFire: 0.35, spreadFireMax: 2.6,
    pattern: M4_PATTERN, recoilScale: 1.0, kick: 0.8, sound: 'rifle2', tracer: true, killIcon: 'M4A4',
  },
  awp: {
    id: 'awp', name: 'AWP', slot: 0, auto: false, damage: 115, armorPen: 0.975, range: 8192, rangeMod: 0.99,
    cycle: 1.46, clip: 5, reserve: 30, reload: 3.6, draw: 1.2, maxspeed: 200, scopedSpeed: 100,
    spreadStand: 0.05, spreadCrouch: 0.03, spreadMove: 9, spreadAir: 20, spreadFire: 0, spreadFireMax: 0,
    unscopedSpread: 8, scope: [40, 15], pattern: null, punch: 2.6, kick: 3.0, sound: 'awp', tracer: true, sniper: true, killIcon: 'AWP',
  },
  ssg08: {
    id: 'ssg08', name: 'SSG 08', slot: 0, auto: false, damage: 88, armorPen: 0.85, range: 8192, rangeMod: 0.98,
    cycle: 1.25, clip: 10, reserve: 90, reload: 3.7, draw: 1.0, maxspeed: 230, scopedSpeed: 230,
    spreadStand: 0.05, spreadCrouch: 0.03, spreadMove: 4, spreadAir: 6, spreadFire: 0, spreadFireMax: 0,
    unscopedSpread: 3.5, scope: [40, 15], pattern: null, punch: 1.6, kick: 2.0, sound: 'scout', tracer: true, sniper: true, jumpAccurate: true, killIcon: 'SSG 08',
  },
  deagle: {
    id: 'deagle', name: 'Desert Eagle', slot: 1, auto: false, damage: 63, armorPen: 0.932, range: 4096, rangeMod: 0.81,
    cycle: 0.225, clip: 7, reserve: 35, reload: 2.2, draw: 0.8, maxspeed: 230,
    spreadStand: 0.35, spreadCrouch: 0.25, spreadMove: 4.5, spreadAir: 10, spreadFire: 2.2, spreadFireMax: 5,
    pattern: null, punch: 2.4, kick: 2.2, sound: 'deagle', tracer: true, killIcon: 'Desert Eagle',
  },
  knife: {
    id: 'knife', name: 'Knife', slot: 2, auto: true, melee: true, damage: 40, damage2: 65, armorPen: 0.85, range: 64,
    cycle: 0.5, cycle2: 1.0, clip: -1, reserve: -1, reload: 0, draw: 0.6, maxspeed: 250,
    spreadStand: 0, spreadCrouch: 0, spreadMove: 0, spreadAir: 0, spreadFire: 0, spreadFireMax: 0,
    pattern: null, kick: 0, sound: 'knife', killIcon: 'Knife',
  },
};

export const PRIMARIES = ['ak47', 'm4a4', 'awp', 'ssg08'];

export const HITGROUP_MULT = { head: 4.0, chest: 1.0, stomach: 1.25, arm: 1.0, leg: 0.75 };

export function weaponInaccuracy(w, st, ent) {
  // st: weapon runtime state; ent: actor with velocity/onGround/ducked
  const hs = Math.hypot(ent.velocity.x, ent.velocity.z);
  const maxspeed = st.scoped && w.scopedSpeed ? w.scopedSpeed : w.maxspeed;
  let spread = ent.ducked && ent.onGround ? w.spreadCrouch : w.spreadStand;
  if (w.sniper && !st.scoped) spread = w.unscopedSpread;
  if (!ent.onGround) {
    let air = w.spreadAir;
    if (w.jumpAccurate && Math.abs(ent.velocity.y) < 18) air = w.spreadStand * 1.5; // jumpscout at apex
    spread += air;
  } else {
    const accurate = maxspeed * 0.34;
    if (hs > accurate) spread += w.spreadMove * Math.min(1, (hs - accurate) / (maxspeed - accurate));
  }
  spread += st.fireInacc || 0;
  return spread;
}
