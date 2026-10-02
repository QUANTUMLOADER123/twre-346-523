import * as THREE from 'three';
import { SKIN_PALETTES } from './config.js';

// Builds detailed 3D Weapon & Knife Meshes with Skin Changer support
export function createWeaponMesh(weaponId, skinOpts = {}) {
  const group = new THREE.Group();
  const skinKey = weaponId === 'knife' ? (skinOpts.knifeSkin || 'fade') : (skinOpts.weaponSkin || 'default');
  const knifeModel = skinOpts.knifeModel || 'karambit';
  const pal = SKIN_PALETTES[skinKey] || SKIN_PALETTES.default;

  const matDarkMetal = new THREE.MeshStandardMaterial({
    color: 0x1a2028,
    metalness: 0.78,
    roughness: 0.28
  });
  const matSteel = new THREE.MeshStandardMaterial({
    color: 0x64748b,
    metalness: 0.88,
    roughness: 0.22
  });
  const matSkinPrimary = new THREE.MeshStandardMaterial({
    color: pal.primary,
    emissive: pal.emissive,
    emissiveIntensity: pal.emissive ? 0.35 : 0,
    metalness: pal.metalness,
    roughness: pal.roughness
  });
  const matScopeLens = new THREE.MeshBasicMaterial({ color: 0x00f5d4 });

  if (weaponId === 'ssg08') {
    const matBody = skinKey === 'default'
      ? new THREE.MeshStandardMaterial({ color: 0x293847, roughness: 0.42, metalness: 0.35 })
      : matSkinPrimary;

    // Tactical chassis & thumbhole stock
    const chassis = new THREE.Mesh(new THREE.BoxGeometry(2.8, 4.4, 24), matBody);
    chassis.position.set(0, 0.2, -5);
    group.add(chassis);

    const buttpad = new THREE.Mesh(new THREE.BoxGeometry(2.8, 6.2, 5.5), matDarkMetal);
    buttpad.position.set(0, -0.5, 8.5);
    group.add(buttpad);

    const cheekRest = new THREE.Mesh(new THREE.BoxGeometry(2.4, 2.2, 7.5), matDarkMetal);
    cheekRest.position.set(0, 2.8, 5.5);
    group.add(cheekRest);

    // Fluted sniper barrel
    const barrel = new THREE.Mesh(new THREE.CylinderGeometry(0.65, 0.75, 30, 12), matDarkMetal);
    barrel.rotation.x = Math.PI / 2;
    barrel.position.set(0, 1.5, -24);
    group.add(barrel);

    // Bolt handle
    const bolt = new THREE.Mesh(new THREE.CylinderGeometry(0.35, 0.35, 3.2, 8), matSteel);
    bolt.rotation.z = Math.PI / 2;
    bolt.position.set(1.8, 1.6, -1.5);
    group.add(bolt);

    // Dual-ring Sniper Scope
    const mount1 = new THREE.Mesh(new THREE.BoxGeometry(1.8, 2.2, 1.4), matDarkMetal);
    mount1.position.set(0, 3.0, -4);
    const mount2 = mount1.clone();
    mount2.position.set(0, 3.0, -10);
    group.add(mount1, mount2);

    const scopeTube = new THREE.Mesh(new THREE.CylinderGeometry(1.25, 1.15, 15, 14), matDarkMetal);
    scopeTube.rotation.x = Math.PI / 2;
    scopeTube.position.set(0, 4.3, -7);
    group.add(scopeTube);

    const lens = new THREE.Mesh(new THREE.CylinderGeometry(1.1, 1.1, 0.5, 12), matScopeLens);
    lens.rotation.x = Math.PI / 2;
    lens.position.set(0, 4.3, -14.6);
    group.add(lens);

    // Folded bipod under forend
    const bipod = new THREE.Mesh(new THREE.CylinderGeometry(0.4, 0.4, 9, 8), matDarkMetal);
    bipod.rotation.x = Math.PI / 2;
    bipod.position.set(0, -1.6, -19);
    group.add(bipod);

    const mag = new THREE.Mesh(new THREE.BoxGeometry(2.1, 5.2, 4.4), matDarkMetal);
    mag.position.set(0, -3.4, -6);
    group.add(mag);
  } else if (weaponId === 'scar20') {
    const matTan = skinKey === 'default'
      ? new THREE.MeshStandardMaterial({ color: 0xb89758, roughness: 0.45, metalness: 0.38 })
      : matSkinPrimary;

    // Monolithic SCAR upper receiver
    const upper = new THREE.Mesh(new THREE.BoxGeometry(3.5, 4.6, 26), matTan);
    upper.position.set(0, 1.4, -6);
    group.add(upper);

    const lower = new THREE.Mesh(new THREE.BoxGeometry(3.1, 3.2, 14), matDarkMetal);
    lower.position.set(0, -1.6, -3);
    group.add(lower);

    // Folding UGG Boot Stock
    const stock = new THREE.Mesh(new THREE.BoxGeometry(3.2, 6.0, 10.5), matTan);
    stock.position.set(0, 0.2, 11);
    group.add(stock);

    const barrel = new THREE.Mesh(new THREE.CylinderGeometry(0.8, 0.85, 25, 12), matDarkMetal);
    barrel.rotation.x = Math.PI / 2;
    barrel.position.set(0, 1.4, -24);
    group.add(barrel);

    const scopeTube = new THREE.Mesh(new THREE.CylinderGeometry(1.4, 1.3, 15.5, 14), matDarkMetal);
    scopeTube.rotation.x = Math.PI / 2;
    scopeTube.position.set(0, 5.2, -6);
    group.add(scopeTube);

    const lens = new THREE.Mesh(new THREE.CylinderGeometry(1.2, 1.2, 0.5, 12), matScopeLens);
    lens.rotation.x = Math.PI / 2;
    lens.position.set(0, 5.2, -13.8);
    group.add(lens);

    const mag = new THREE.Mesh(new THREE.BoxGeometry(2.5, 7.8, 5.4), matSteel);
    mag.position.set(0, -5.2, -7.5);
    group.add(mag);
  } else if (weaponId === 'awp') {
    const matGreen = skinKey === 'default'
      ? new THREE.MeshStandardMaterial({ color: 0x2b4c2f, roughness: 0.48, metalness: 0.28 })
      : matSkinPrimary;

    const body = new THREE.Mesh(new THREE.BoxGeometry(3.6, 5.6, 29), matGreen);
    body.position.set(0, 0, -5);
    group.add(body);

    const stock = new THREE.Mesh(new THREE.BoxGeometry(3.2, 6.4, 11), matGreen);
    stock.position.set(0, -0.4, 12);
    group.add(stock);

    const barrel = new THREE.Mesh(new THREE.CylinderGeometry(0.92, 0.98, 34, 12), matDarkMetal);
    barrel.rotation.x = Math.PI / 2;
    barrel.position.set(0, 1.6, -27);
    group.add(barrel);

    const muzzle = new THREE.Mesh(new THREE.BoxGeometry(2.3, 2.3, 3.8), matDarkMetal);
    muzzle.position.set(0, 1.6, -44);
    group.add(muzzle);

    const scopeTube = new THREE.Mesh(new THREE.CylinderGeometry(1.75, 1.45, 18, 14), matDarkMetal);
    scopeTube.rotation.x = Math.PI / 2;
    scopeTube.position.set(0, 5.1, -7.5);
    group.add(scopeTube);

    const lens = new THREE.Mesh(new THREE.CylinderGeometry(1.55, 1.55, 0.5, 12), matScopeLens);
    lens.rotation.x = Math.PI / 2;
    lens.position.set(0, 5.1, -16.6);
    group.add(lens);
  } else if (weaponId === 'ak47') {
    const matWood = skinKey === 'default'
      ? new THREE.MeshStandardMaterial({ color: 0x7c431e, roughness: 0.58 })
      : matSkinPrimary;

    const receiver = new THREE.Mesh(new THREE.BoxGeometry(3.1, 4.6, 17.5), matDarkMetal);
    receiver.position.set(0, 0.6, -4);
    group.add(receiver);

    const handguard = new THREE.Mesh(new THREE.BoxGeometry(3.3, 3.8, 10.5), matWood);
    handguard.position.set(0, 0.8, -16.5);
    group.add(handguard);

    const stock = new THREE.Mesh(new THREE.BoxGeometry(2.9, 4.8, 11.5), matWood);
    stock.rotation.x = -0.12;
    stock.position.set(0, -0.6, 9.5);
    group.add(stock);

    const barrel = new THREE.Mesh(new THREE.CylinderGeometry(0.65, 0.65, 17, 10), matDarkMetal);
    barrel.rotation.x = Math.PI / 2;
    barrel.position.set(0, 1.5, -24.5);
    group.add(barrel);

    const gasTube = new THREE.Mesh(new THREE.CylinderGeometry(0.48, 0.48, 11, 8), matDarkMetal);
    gasTube.rotation.x = Math.PI / 2;
    gasTube.position.set(0, 2.5, -18);
    group.add(gasTube);

    // Iconic curved banana mag
    const mag = new THREE.Mesh(new THREE.BoxGeometry(2.3, 9.2, 3.8), matWood);
    mag.rotation.x = -0.38;
    mag.position.set(0, -5.0, -8.5);
    group.add(mag);
  } else if (weaponId === 'm4a1s') {
    const matBody = skinKey === 'default' ? matDarkMetal : matSkinPrimary;
    const receiver = new THREE.Mesh(new THREE.BoxGeometry(3.1, 4.8, 16), matBody);
    receiver.position.set(0, 0.8, -3);
    group.add(receiver);

    const handguard = new THREE.Mesh(new THREE.CylinderGeometry(1.7, 1.7, 12, 10), matBody);
    handguard.rotation.x = Math.PI / 2;
    handguard.position.set(0, 1.2, -16);
    group.add(handguard);

    // Long M4A1-S Silencer / Suppressor!
    const suppressor = new THREE.Mesh(new THREE.CylinderGeometry(1.15, 1.15, 14, 14), matSteel);
    suppressor.rotation.x = Math.PI / 2;
    suppressor.position.set(0, 1.2, -31);
    group.add(suppressor);

    const stock = new THREE.Mesh(new THREE.BoxGeometry(2.8, 4.8, 10), matDarkMetal);
    stock.position.set(0, 0.2, 9.5);
    group.add(stock);

    const mag = new THREE.Mesh(new THREE.BoxGeometry(2.2, 7.5, 3.6), matSteel);
    mag.rotation.x = -0.15;
    mag.position.set(0, -4.4, -6);
    group.add(mag);
  } else if (weaponId === 'deagle') {
    const matSlide = skinKey === 'default' ? matSteel : matSkinPrimary;
    const slide = new THREE.Mesh(new THREE.BoxGeometry(2.9, 3.8, 14), matSlide);
    slide.position.set(0, 1.6, -6.5);
    group.add(slide);

    const grip = new THREE.Mesh(new THREE.BoxGeometry(2.4, 6.5, 3.6), matDarkMetal);
    grip.rotation.x = 0.22;
    grip.position.set(0, -2.5, -1.5);
    group.add(grip);
  } else if (weaponId === 'r8') {
    const matBody = skinKey === 'default' ? matSteel : matSkinPrimary;
    const barrel = new THREE.Mesh(new THREE.BoxGeometry(2.2, 3.2, 11.5), matBody);
    barrel.position.set(0, 1.8, -8);
    group.add(barrel);

    const cylinder = new THREE.Mesh(new THREE.CylinderGeometry(2.1, 2.1, 4.2, 12), matDarkMetal);
    cylinder.rotation.x = Math.PI / 2;
    cylinder.position.set(0, 1.2, -2.2);
    group.add(cylinder);

    const grip = new THREE.Mesh(new THREE.BoxGeometry(2.2, 6.0, 3.2), matDarkMetal);
    grip.rotation.x = 0.3;
    grip.position.set(0, -2.5, 1.0);
    group.add(grip);
  } else if (weaponId === 'zeus') {
    const matYellow = new THREE.MeshStandardMaterial({ color: 0xeab308, roughness: 0.35 });
    const body = new THREE.Mesh(new THREE.BoxGeometry(3.2, 3.8, 10.5), matDarkMetal);
    body.position.set(0, 1.0, -4);
    group.add(body);
    const stripe = new THREE.Mesh(new THREE.BoxGeometry(3.3, 1.4, 8.5), matYellow);
    stripe.position.set(0, 1.4, -4.5);
    group.add(stripe);
    const grip = new THREE.Mesh(new THREE.BoxGeometry(2.4, 5.4, 3.2), matDarkMetal);
    grip.rotation.x = 0.25;
    grip.position.set(0, -2.2, -1);
    group.add(grip);
  } else if (weaponId === 'knife') {
    const bladeMat = skinKey === 'default'
      ? new THREE.MeshStandardMaterial({ color: 0xa855f7, emissive: 0xec4899, emissiveIntensity: 0.4, metalness: 0.92, roughness: 0.14 })
      : matSkinPrimary;

    if (knifeModel === 'm9') {
      // M9 Bayonet: Heavy combat guard + long serrated blade
      const handle = new THREE.Mesh(new THREE.CylinderGeometry(1.1, 1.1, 6.8, 10), matDarkMetal);
      handle.rotation.x = Math.PI / 2;
      handle.position.set(0, 0, -1.5);
      group.add(handle);

      const guard = new THREE.Mesh(new THREE.BoxGeometry(1.4, 4.8, 0.8), matSteel);
      guard.position.set(0, 0, -5.0);
      group.add(guard);

      const blade = new THREE.Mesh(new THREE.BoxGeometry(0.6, 2.4, 11.5), bladeMat);
      blade.position.set(0, 0.2, -11.0);
      group.add(blade);
    } else if (knifeModel === 'butterfly') {
      // Butterfly Knife (Balisong): Twin skeletonized handles + spear-point blade
      const h1 = new THREE.Mesh(new THREE.BoxGeometry(0.8, 1.1, 6.8), matDarkMetal);
      h1.position.set(0, 0.8, -1.5);
      const h2 = new THREE.Mesh(new THREE.BoxGeometry(0.8, 1.1, 6.8), matDarkMetal);
      h2.position.set(0, -0.8, -1.5);
      group.add(h1, h2);

      const blade = new THREE.Mesh(new THREE.ConeGeometry(1.3, 10.5, 4), bladeMat);
      blade.rotation.x = -Math.PI / 2;
      blade.position.set(0, 0, -9.8);
      group.add(blade);
    } else {
      // Karambit: Finger ring + ergonomic curved handle + curved tiger-claw blade
      const ring = new THREE.Mesh(new THREE.TorusGeometry(1.65, 0.42, 10, 18), matDarkMetal);
      ring.rotation.y = Math.PI / 2;
      ring.position.set(0, -2.4, 1.8);
      group.add(ring);

      const handle = new THREE.Mesh(new THREE.BoxGeometry(1.4, 2.3, 6.6), matDarkMetal);
      handle.position.set(0, 0, -2);
      group.add(handle);

      const blade = new THREE.Mesh(new THREE.ConeGeometry(1.75, 10.2, 4), bladeMat);
      blade.rotation.x = -Math.PI / 2 - 0.38;
      blade.position.set(0, 0.6, -8.8);
      group.add(blade);
    }
  }

  return group;
}

// ============================================================================
// HIGH-DETAIL ARTICULATED CS:GO AGENT 3D MODEL (72 Hammer Units Tall)
// ============================================================================
export class CharacterRig {
  constructor(opts = {}) {
    const {
      isEnemy = false,
      isGhost = false,
      ghostColor = '#a855f7',
      ghostOpacity = 0.42,
      wireframe = false
    } = opts;

    this.isEnemy = isEnemy;
    this.isGhost = isGhost;
    this.root = new THREE.Group();

    // Rich Tactical Agent Materials (CT SAS/Seal Navy vs T Phoenix/Balkan Olive/Sand)
    this.baseSkinMat = new THREE.MeshStandardMaterial({
      color: isEnemy ? 0xc68665 : 0x475569,
      roughness: 0.55
    });
    this.baseSuitMat = new THREE.MeshStandardMaterial({
      color: isEnemy ? 0x5c4632 : 0x1e324a,
      roughness: 0.68
    });
    this.basePantsMat = new THREE.MeshStandardMaterial({
      color: isEnemy ? 0x363b30 : 0x172436,
      roughness: 0.72
    });
    this.baseVestMat = new THREE.MeshStandardMaterial({
      color: isEnemy ? 0x262421 : 0x111827,
      roughness: 0.48,
      metalness: 0.32
    });
    this.baseGearMat = new THREE.MeshStandardMaterial({
      color: 0x0f141c,
      roughness: 0.4,
      metalness: 0.45
    });
    this.baseVisorMat = new THREE.MeshStandardMaterial({
      color: isEnemy ? 0xf59e0b : 0x00f5d4,
      emissive: isEnemy ? 0xd97706 : 0x00f5d4,
      emissiveIntensity: 0.45,
      roughness: 0.18,
      metalness: 0.85
    });

    this.ghostMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color(ghostColor),
      emissive: new THREE.Color(ghostColor),
      emissiveIntensity: 0.45,
      transparent: true,
      opacity: ghostOpacity,
      wireframe,
      depthWrite: false
    });

    // Chams Materials (Visible + XQZ Wallhack pass)
    this.chamsVisMat = new THREE.MeshStandardMaterial({
      color: new THREE.Color('#95b806'),
      emissive: new THREE.Color('#95b806'),
      emissiveIntensity: 0.35,
      metalness: 0.75,
      roughness: 0.25
    });

    this.chamsXqzMat = new THREE.MeshBasicMaterial({
      color: new THREE.Color('#38bdf8'),
      transparent: true,
      opacity: 0.68,
      depthTest: false,
      depthWrite: false
    });

    this.bodyMeshes = [];
    this.xqzMeshes = [];

    // ------------------------------------------------------------------------
    // SKELETON HIERARCHY (72 units total standing height):
    // ------------------------------------------------------------------------
    this.lowerBodyGroup = new THREE.Group();
    this.lowerBodyGroup.position.set(0, 35, 0);
    this.root.add(this.lowerBodyGroup);

    this.upperBodyGroup = new THREE.Group();
    this.upperBodyGroup.position.set(0, 37, 0);
    this.root.add(this.upperBodyGroup);

    // 1. PELVIS & TACTICAL UTILITY BELT (y = 35)
    this.pelvisMesh = this.addPart(
      this.lowerBodyGroup,
      new THREE.CylinderGeometry(7.2, 6.6, 6.5, 12),
      0, 0, 0,
      'pants'
    );
    // Tactical belt + pouches + rear utility pack
    this.addPart(this.lowerBodyGroup, new THREE.CylinderGeometry(7.6, 7.6, 2.6, 12), 0, 1.8, 0, 'gear');
    this.addPart(this.lowerBodyGroup, new THREE.BoxGeometry(4.2, 4.6, 3.2), -6.5, 0.5, 2.5, 'vest');
    this.addPart(this.lowerBodyGroup, new THREE.BoxGeometry(5.5, 5.2, 3.8), 0, 0.8, 6.5, 'vest');

    // 2. ARTICULATED LEGS (Thigh -> Knee Joint + Knee Pad -> Shin/Calf -> Combat Boot)
    this.leftLegPivot = new THREE.Group();
    this.leftLegPivot.position.set(-4.3, -2.5, 0);
    this.lowerBodyGroup.add(this.leftLegPivot);
    this.buildArticulatedLeg(this.leftLegPivot, false);

    this.rightLegPivot = new THREE.Group();
    this.rightLegPivot.position.set(4.3, -2.5, 0);
    this.lowerBodyGroup.add(this.rightLegPivot);
    this.buildArticulatedLeg(this.rightLegPivot, true);

    // 3. SCULPTED TORSO & PLATE CARRIER TACTICAL VEST
    // Abdomen
    this.stomachMesh = this.addPart(
      this.upperBodyGroup,
      new THREE.CylinderGeometry(7.6, 6.8, 9.5, 12),
      0, 4.5, 0,
      'suit'
    );
    // Upper Chest / Ribcage
    this.chestMesh = this.addPart(
      this.upperBodyGroup,
      new THREE.CylinderGeometry(8.8, 7.6, 13.5, 12),
      0, 15.0, 0,
      'suit'
    );
    // Ballistic Plate Carrier Front & Back
    this.addPart(this.upperBodyGroup, new THREE.BoxGeometry(14.6, 13.5, 10.6), 0, 14.5, 0, 'vest');
    // 3 Front STANAG Magazine Pouches
    this.addPart(this.upperBodyGroup, new THREE.BoxGeometry(3.4, 5.8, 2.4), -4.2, 11.2, -5.8, 'gear');
    this.addPart(this.upperBodyGroup, new THREE.BoxGeometry(3.4, 5.8, 2.4), 0, 11.2, -5.8, 'gear');
    this.addPart(this.upperBodyGroup, new THREE.BoxGeometry(3.4, 5.8, 2.4), 4.2, 11.2, -5.8, 'gear');
    // Tactical Radio + Whip Antenna on Left Shoulder
    this.addPart(this.upperBodyGroup, new THREE.BoxGeometry(2.6, 4.8, 2.0), -5.2, 18.0, -5.4, 'gear');
    this.addPart(this.upperBodyGroup, new THREE.CylinderGeometry(0.18, 0.25, 11.0, 6), -6.2, 24.5, -4.5, 'gear');
    // Hydration / Assault Pack on Back
    this.addPart(this.upperBodyGroup, new THREE.BoxGeometry(11.2, 13.0, 5.2), 0, 14.8, 6.8, 'vest');
    // Collar / Trapezius + Neck
    this.addPart(this.upperBodyGroup, new THREE.CylinderGeometry(5.6, 7.4, 3.2, 12), 0, 22.0, 0, 'vest');
    this.addPart(this.upperBodyGroup, new THREE.CylinderGeometry(3.1, 3.5, 4.2, 10), 0, 24.2, 0, 'skin');

    // 4. SCULPTED HEAD, FAST HELMET, COMTAC HEADSET & TACTICAL GOGGLES
    this.headPivot = new THREE.Group();
    this.headPivot.position.set(0, 25.0, 0);
    this.upperBodyGroup.add(this.headPivot);

    // Cranial sphere + lower jaw cylinder
    this.headMesh = this.addPart(
      this.headPivot,
      new THREE.SphereGeometry(5.1, 14, 12),
      0, 5.4, 0,
      'skin'
    );
    this.addPart(this.headPivot, new THREE.CylinderGeometry(4.2, 3.4, 5.2, 12), 0, 3.2, -0.4, 'gear');
    // FAST Tactical Helmet Dome
    const helmetDome = this.addPart(
      this.headPivot,
      new THREE.SphereGeometry(5.65, 14, 10, 0, Math.PI * 2, 0, Math.PI * 0.58),
      0, 5.8, 0,
      'vest'
    );
    helmetDome.rotation.x = -0.12;
    // NVG Shroud on Forehead
    this.addPart(this.headPivot, new THREE.BoxGeometry(2.6, 2.4, 1.4), 0, 8.2, -5.3, 'gear');
    // Peltor ComTac Ear-Pro Headset Cups (Left & Right)
    const earL = this.addPart(this.headPivot, new THREE.CylinderGeometry(2.1, 2.1, 1.8, 10), -5.2, 5.0, 0.2, 'gear');
    earL.rotation.z = Math.PI / 2;
    const earR = this.addPart(this.headPivot, new THREE.CylinderGeometry(2.1, 2.1, 1.8, 10), 5.2, 5.0, 0.2, 'gear');
    earR.rotation.z = Math.PI / 2;
    // Tactical Ski Goggles Frame + Glowing Visor Lens (-Z is forward)
    this.addPart(this.headPivot, new THREE.BoxGeometry(8.8, 3.2, 2.4), 0, 5.6, -4.2, 'gear');
    this.addPart(this.headPivot, new THREE.BoxGeometry(8.0, 2.3, 1.2), 0, 5.6, -5.0, 'visor');

    // 5. ARTICULATED ARMS (Shoulder -> Bicep -> Elbow -> Forearm -> Tactical Gloved Hand)
    this.leftArmPivot = new THREE.Group();
    this.leftArmPivot.position.set(-10.2, 19.5, 0);
    this.upperBodyGroup.add(this.leftArmPivot);
    this.buildArticulatedArm(this.leftArmPivot, false);

    this.rightArmPivot = new THREE.Group();
    this.rightArmPivot.position.set(10.2, 19.5, 0);
    this.upperBodyGroup.add(this.rightArmPivot);
    this.buildArticulatedArm(this.rightArmPivot, true);

    // Two-handed rifle aiming pose
    this.leftArmPivot.rotation.set(0.95, 0.48, 0.12);
    this.rightArmPivot.rotation.set(1.08, -0.12, -0.05);

    this.weaponHolder = new THREE.Group();
    this.weaponHolder.position.set(-2.8, -12.5, -5.5);
    this.rightArmPivot.add(this.weaponHolder);

    this.currentWeaponId = null;
    this.currentSkinHash = '';
    if (!this.isGhost) {
      this.setWeapon('ssg08');
    }
  }

  buildArticulatedLeg(legPivot, isRight) {
    // Hip joint + sculpted thigh
    this.addPart(legPivot, new THREE.SphereGeometry(3.4, 10, 8), 0, 0, 0, 'pants');
    this.addPart(legPivot, new THREE.CylinderGeometry(3.5, 2.9, 15.5, 10), 0, -8.0, 0, 'pants');

    // Drop-leg holster on right thigh / cargo pocket on left thigh
    if (isRight) {
      this.addPart(legPivot, new THREE.BoxGeometry(1.8, 5.5, 3.6), 3.4, -7.5, 0, 'gear');
    } else {
      this.addPart(legPivot, new THREE.BoxGeometry(1.4, 5.0, 4.0), -3.2, -7.5, 0, 'pants');
    }

    // Knee Pivot (allows realistic knee bending during walk/run/crouch/bhop!)
    const kneePivot = new THREE.Group();
    kneePivot.position.set(0, -15.5, 0);
    legPivot.add(kneePivot);
    if (isRight) this.rightKneePivot = kneePivot;
    else this.leftKneePivot = kneePivot;

    // Knee joint + Tactical Knee Pad
    this.addPart(kneePivot, new THREE.SphereGeometry(2.8, 10, 8), 0, 0, 0, 'pants');
    this.addPart(kneePivot, new THREE.BoxGeometry(4.6, 4.2, 2.2), 0, 0, -2.4, 'gear');

    // Lower calf / shin
    this.addPart(kneePivot, new THREE.CylinderGeometry(2.8, 2.2, 14.5, 10), 0, -7.5, 0, 'pants');

    // Combat Boot (Ankle collar + Midfoot + Rounded Toe Cap + Sole)
    this.addPart(kneePivot, new THREE.CylinderGeometry(2.5, 2.6, 4.2, 10), 0, -14.5, 0, 'gear');
    this.addPart(kneePivot, new THREE.BoxGeometry(5.2, 3.4, 9.4), 0, -15.8, -1.8, 'gear');
  }

  buildArticulatedArm(armPivot, isRight) {
    // Deltoid shoulder pad + bicep
    this.addPart(armPivot, new THREE.SphereGeometry(3.2, 10, 8), 0, 0, 0, 'vest');
    this.addPart(armPivot, new THREE.CylinderGeometry(2.6, 2.3, 10.5, 10), 0, -5.2, 0, 'suit');

    // Elbow + Forearm angled forward toward weapon grip
    const forearmGroup = new THREE.Group();
    forearmGroup.position.set(0, -10.2, 0);
    forearmGroup.rotation.x = isRight ? 0.52 : 0.68;
    armPivot.add(forearmGroup);

    this.addPart(forearmGroup, new THREE.SphereGeometry(2.2, 8, 8), 0, 0, 0, 'gear');
    this.addPart(forearmGroup, new THREE.CylinderGeometry(2.2, 1.8, 9.5, 10), 0, -4.8, 0, 'suit');
    // Tactical Gloved Hand
    this.addPart(forearmGroup, new THREE.BoxGeometry(2.8, 3.6, 3.2), 0, -10.2, -0.4, 'gear');
  }

  addPart(parent, geom, x, y, z, matType) {
    let baseMat = this.baseSuitMat;
    if (matType === 'skin') baseMat = this.baseSkinMat;
    else if (matType === 'pants') baseMat = this.basePantsMat;
    else if (matType === 'vest') baseMat = this.baseVestMat;
    else if (matType === 'gear') baseMat = this.baseGearMat;
    else if (matType === 'visor') baseMat = this.baseVisorMat;

    const mat = this.isGhost ? this.ghostMat : baseMat;
    const mesh = new THREE.Mesh(geom, mat);
    mesh.position.set(x, y, z);
    mesh.castShadow = !this.isGhost;
    mesh.receiveShadow = !this.isGhost;
    mesh.userData.baseMat = baseMat;
    mesh.renderOrder = 2;
    parent.add(mesh);
    this.bodyMeshes.push(mesh);

    if (this.isEnemy && !this.isGhost) {
      const xqzMesh = new THREE.Mesh(geom, this.chamsXqzMat);
      xqzMesh.position.set(x, y, z);
      xqzMesh.renderOrder = 1;
      xqzMesh.visible = false;
      parent.add(xqzMesh);
      this.xqzMeshes.push(xqzMesh);
    }

    return mesh;
  }

  setWeapon(weaponId, skinOpts = {}) {
    const hash = `${weaponId}_${skinOpts.weaponSkin || 'default'}_${skinOpts.knifeModel || 'karambit'}_${skinOpts.knifeSkin || 'fade'}`;
    if (this.currentWeaponId === weaponId && this.currentSkinHash === hash) return;
    this.currentWeaponId = weaponId;
    this.currentSkinHash = hash;

    while (this.weaponHolder.children.length > 0) {
      this.weaponHolder.remove(this.weaponHolder.children[0]);
    }
    const wMesh = createWeaponMesh(weaponId, skinOpts);
    this.weaponHolder.add(wMesh);
  }

  updatePose({
    pos,
    realYaw = 0,
    lowerYaw = 0,
    pitch = 0,
    crouchAmount = 0,
    speed = 0,
    walkCycle = 0,
    slideWalk = false,
    onGround = true
  }) {
    const crouchDrop = crouchAmount * 18;
    this.root.position.set(pos.x, pos.y, pos.z);

    this.lowerBodyGroup.position.y = 35 - crouchDrop * 0.62;
    this.upperBodyGroup.position.y = 37 - crouchDrop;

    this.lowerBodyGroup.rotation.y = lowerYaw;
    this.upperBodyGroup.rotation.y = realYaw;

    // Pitch rotation on headPivot (Pitch Down 89° tucks head deep into vest)
    this.headPivot.rotation.x = pitch;

    const moveFactor = Math.min(1, speed / 210);
    if (!onGround) {
      // Airborne / Bhop tuck pose with bent knees!
      this.leftLegPivot.rotation.x = 0.48;
      this.rightLegPivot.rotation.x = 0.22;
      if (this.leftKneePivot) this.leftKneePivot.rotation.x = -0.65;
      if (this.rightKneePivot) this.rightKneePivot.rotation.x = -0.45;
    } else if (slideWalk && speed > 15) {
      // Classic HvH Slide-Walk leg freeze
      this.leftLegPivot.rotation.x = 0.58 * moveFactor;
      this.rightLegPivot.rotation.x = -0.58 * moveFactor;
      if (this.leftKneePivot) this.leftKneePivot.rotation.x = -0.15;
      if (this.rightKneePivot) this.rightKneePivot.rotation.x = -0.15;
    } else {
      const swing = Math.sin(walkCycle) * 0.68 * moveFactor;
      const kneeBendL = Math.max(0, -Math.cos(walkCycle)) * 0.55 * moveFactor + crouchAmount * 0.65;
      const kneeBendR = Math.max(0, Math.cos(walkCycle)) * 0.55 * moveFactor + crouchAmount * 0.65;

      this.leftLegPivot.rotation.x = swing + crouchAmount * 0.45;
      this.rightLegPivot.rotation.x = -swing + crouchAmount * 0.45;
      if (this.leftKneePivot) this.leftKneePivot.rotation.x = -kneeBendL;
      if (this.rightKneePivot) this.rightKneePivot.rotation.x = -kneeBendR;
    }
  }

  updateChams(visualsCfg) {
    if (this.isGhost) return;

    if (this.isEnemy) {
      const useChams = visualsCfg.enemyChams;
      const useXqz = useChams && visualsCfg.enemyChamsXqz;
      const style = visualsCfg.enemyChamsStyle;

      this.chamsVisMat.color.set(visualsCfg.enemyVisColor);
      this.chamsVisMat.emissive.set(visualsCfg.enemyVisColor);
      this.chamsVisMat.wireframe = style === 'wireframe';
      this.chamsVisMat.metalness = style === 'metallic' ? 0.85 : 0.1;
      this.chamsVisMat.roughness = style === 'metallic' ? 0.2 : 0.8;
      this.chamsVisMat.emissiveIntensity = style === 'flat' ? 0.85 : 0.35;

      this.chamsXqzMat.color.set(visualsCfg.enemyXqzColor);

      for (const m of this.bodyMeshes) {
        if (useChams && style !== 'textured') {
          m.material = this.chamsVisMat;
        } else {
          m.material = m.userData.baseMat;
        }
      }
      for (const xm of this.xqzMeshes) {
        xm.visible = useXqz;
      }
    } else {
      const style = visualsCfg.localChamsStyle;
      if (visualsCfg.localChams && style !== 'textured') {
        this.chamsVisMat.color.set(visualsCfg.localRealColor);
        this.chamsVisMat.emissive.set(visualsCfg.localRealColor);
        this.chamsVisMat.wireframe = style === 'wireframe';
        this.chamsVisMat.metalness = style === 'metallic' ? 0.85 : 0.15;
        this.chamsVisMat.emissiveIntensity = style === 'flat' ? 0.75 : 0.35;
        for (const m of this.bodyMeshes) {
          m.material = this.chamsVisMat;
        }
      } else {
        for (const m of this.bodyMeshes) {
          m.material = m.userData.baseMat;
        }
      }
    }
  }

  getHitboxes(multipointScale = 0.78) {
    this.root.updateMatrixWorld(true);

    const headCenter = new THREE.Vector3();
    this.headMesh.getWorldPosition(headCenter);

    const chestCenter = new THREE.Vector3();
    this.chestMesh.getWorldPosition(chestCenter);

    const stomachCenter = new THREE.Vector3();
    this.stomachMesh.getWorldPosition(stomachCenter);

    const pelvisCenter = new THREE.Vector3();
    this.pelvisMesh.getWorldPosition(pelvisCenter);

    const leftFoot = new THREE.Vector3(0, -14, 0).applyMatrix4(
      (this.leftKneePivot || this.leftLegPivot).matrixWorld
    );
    const rightFoot = new THREE.Vector3(0, -14, 0).applyMatrix4(
      (this.rightKneePivot || this.rightLegPivot).matrixWorld
    );

    const leftShoulder = new THREE.Vector3();
    this.leftArmPivot.getWorldPosition(leftShoulder);
    const rightShoulder = new THREE.Vector3();
    this.rightArmPivot.getWorldPosition(rightShoulder);

    const r = 4.6 * multipointScale;
    const chestR = 7.6 * multipointScale;

    return {
      head: headCenter,
      headMultipoints: [
        headCenter,
        headCenter.clone().add(new THREE.Vector3(0, r, 0)),
        headCenter.clone().add(new THREE.Vector3(r, 0, 0)),
        headCenter.clone().add(new THREE.Vector3(-r, 0, 0))
      ],
      chest: chestCenter,
      chestMultipoints: [
        chestCenter,
        chestCenter.clone().add(new THREE.Vector3(chestR, 0, 0)),
        chestCenter.clone().add(new THREE.Vector3(-chestR, 0, 0))
      ],
      stomach: stomachCenter,
      pelvis: pelvisCenter,
      leftLeg: leftFoot,
      rightLeg: rightFoot,
      leftShoulder,
      rightShoulder
    };
  }
}

// Firstperson 3D Viewmodel (Gloved Hands + Sleeves + Active Weapon + Inspect Animation)
export class FirstpersonViewmodel {
  constructor(camera) {
    this.camera = camera;
    this.group = new THREE.Group();
    this.camera.add(this.group);

    this.bobGroup = new THREE.Group();
    this.group.add(this.bobGroup);

    const matSleeve = new THREE.MeshStandardMaterial({ color: 0x1e293b, roughness: 0.65 });
    const matGlove = new THREE.MeshStandardMaterial({ color: 0x0f172a, roughness: 0.45, metalness: 0.25 });

    // Right arm + specialist glove
    const rightArm = new THREE.Mesh(new THREE.CylinderGeometry(1.5, 1.8, 14, 10), matSleeve);
    rightArm.rotation.x = Math.PI / 2 - 0.15;
    rightArm.rotation.z = -0.18;
    rightArm.position.set(6.4, -6.4, -8.5);
    this.bobGroup.add(rightArm);

    const rightGlove = new THREE.Mesh(new THREE.BoxGeometry(2.6, 2.8, 3.8), matGlove);
    rightGlove.position.set(5.6, -5.6, -14.2);
    this.bobGroup.add(rightGlove);

    // Left support arm + glove
    this.leftArmGroup = new THREE.Group();
    const leftArm = new THREE.Mesh(new THREE.CylinderGeometry(1.4, 1.7, 16, 10), matSleeve);
    leftArm.rotation.x = Math.PI / 2 - 0.2;
    leftArm.rotation.z = 0.42;
    leftArm.position.set(-1.5, -6.5, -13);
    this.leftArmGroup.add(leftArm);
    this.bobGroup.add(this.leftArmGroup);

    this.weaponContainer = new THREE.Group();
    this.weaponContainer.position.set(5.5, -5.1, -14.2);
    this.bobGroup.add(this.weaponContainer);

    this.currentWeaponId = null;
    this.currentSkinHash = '';
    this.recoilKick = 0;
    this.slashAnim = 0;
    this.inspectAnim = 0;
    this.setWeapon('ssg08');
  }

  setWeapon(weaponId, skinOpts = {}) {
    const hash = `${weaponId}_${skinOpts.weaponSkin || 'default'}_${skinOpts.knifeModel || 'karambit'}_${skinOpts.knifeSkin || 'fade'}`;
    if (this.currentWeaponId === weaponId && this.currentSkinHash === hash) return;
    this.currentWeaponId = weaponId;
    this.currentSkinHash = hash;

    while (this.weaponContainer.children.length > 0) {
      this.weaponContainer.remove(this.weaponContainer.children[0]);
    }
    const mesh = createWeaponMesh(weaponId, skinOpts);
    this.weaponContainer.add(mesh);
  }

  triggerFire(isKnife = false) {
    this.recoilKick = 1.0;
    if (isKnife) {
      this.slashAnim = 1.0;
    }
  }

  triggerInspect() {
    this.inspectAnim = 1.0;
  }

  update(dt, speed, onGround, isScoped, thirdperson) {
    this.group.visible = !thirdperson && !isScoped;
    if (!this.group.visible) return;

    this.recoilKick = Math.max(0, this.recoilKick - dt * 6.5);
    this.slashAnim = Math.max(0, this.slashAnim - dt * 4.5);
    this.inspectAnim = Math.max(0, this.inspectAnim - dt * 1.4);

    const t = performance.now() * 0.0085;
    const moveFactor = onGround ? Math.min(1, speed / 220) : 0.15;
    const bobX = Math.cos(t) * 0.45 * moveFactor;
    const bobY = Math.abs(Math.sin(t)) * 0.38 * moveFactor;

    this.bobGroup.position.set(
      bobX,
      -bobY + this.recoilKick * 0.8,
      this.recoilKick * 2.6
    );
    this.bobGroup.rotation.x = this.recoilKick * 0.12;
    this.bobGroup.rotation.y = this.slashAnim > 0
      ? Math.sin(this.slashAnim * Math.PI) * 0.85
      : this.inspectAnim > 0
      ? Math.sin(this.inspectAnim * Math.PI * 2) * 0.55
      : 0;
  }
}
