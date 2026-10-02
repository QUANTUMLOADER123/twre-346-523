import * as THREE from 'three';
import {
  getSandstoneWallTex,
  getSandFloorTex,
  getWoodCrateTex,
  getWoodDoorTex,
  getDevOrangeTex,
  getDevGreyTex,
  getCyberFloorTex,
  getCyberWallTex,
  getInfernoBrickTex,
  getCobbleFloorTex,
  getNukeWallTex,
  getAsphaltFloorTex
} from './textures.js';

export const MAP_LIST = [
  {
    id: 'de_mirage_mid',
    name: 'DE_MIRAGE',
    subtitle: 'Mid, Sniper Window, Connector & Catwalk',
    shortName: 'de_mirage',
    badgeColor: '#eab308',
    description: 'Классический Мид с окном снайпера, ставней для прострела, коннектором и ящиками.'
  },
  {
    id: 'de_dust2_long',
    name: 'DE_DUST2',
    subtitle: 'Long Doors, Pit, A-Ramp & A-Site',
    shortName: 'de_dust2',
    badgeColor: '#f59e0b',
    description: 'Двойные деревянные двери на Лонге (100% Wallbang), Синий контейнер, Яма и плент А.'
  },
  {
    id: 'de_inferno_banana',
    name: 'DE_INFERNO',
    subtitle: 'Banana, Wood Logs, Car, Coffins & B-Site',
    shortName: 'de_inferno',
    badgeColor: '#ef4444',
    description: 'Узкий Банан с бревнами и машиной, фонтан на пленте B, гробы (Coffins) и прострелы.'
  },
  {
    id: 'de_nuke_outside',
    name: 'DE_NUKE',
    subtitle: 'Outside Yard, Silo, Red Container & Garage',
    shortName: 'de_nuke',
    badgeColor: '#3b82f6',
    description: 'Улица Nuke с огромной трубой Silo (Marshmallow), красным контейнером, ангаром и крышей.'
  },
  {
    id: 'de_cache_mid',
    name: 'DE_CACHE',
    subtitle: 'Mid Garage, White Box, Boost, Vents & Highway',
    shortName: 'de_cache',
    badgeColor: '#10b981',
    description: 'Индустриальный Мид с Белым боксом (White Box), простреливаемой вентиляцией (Vents), бустом и Highway.'
  },
  {
    id: 'de_overpass_park',
    name: 'DE_OVERPASS',
    subtitle: 'Bathrooms, Fountain Park, Party & Bank Sniper',
    shortName: 'de_overpass',
    badgeColor: '#06b6d4',
    description: 'Парк и Туалеты Overpass с простреливаемыми перегородками, грузовиком у Банка и балконом.'
  },
  {
    id: 'aim_redline_hvh',
    name: 'AIM_REDLINE HVH',
    subtitle: '1v1 / 5v5 Source Dev-Texture HvH Arena',
    shortName: 'aim_redline',
    badgeColor: '#f97316',
    description: 'Легендарная оранжево-серая HvH карта с мостом, рампами, хедглитч-боксами и щитами.'
  },
  {
    id: 'cs_office_cyber',
    name: 'CYBER NIGHT HVH',
    subtitle: 'Neon Nightmode Warehouse & Twin Catwalks',
    shortName: 'cyber_hvh',
    badgeColor: '#a855f7',
    description: 'Ночная кибер-арена с неоновыми колоннами, двухъярусными мостами и идеальным контрастом под Chams.'
  }
];

export class MapBuilder {
  constructor(scene) {
    this.scene = scene;
    this.currentMap = null;
    this.mapGroup = null;
    this.colliders = [];
    this.wallMeshes = [];
    this.playerSpawns = [];
    this.botSpawns = [];
    this.botWaypoints = [];
    this.skyColor = new THREE.Color(0x87b8e8);
  }

  addBox(opts) {
    const {
      x = 0,
      y = 0,
      z = 0,
      w = 64,
      h = 64,
      d = 64,
      material,
      penetrable = false,
      penetrationLoss = 0.32,
      isWall = false,
      collide = true,
      castShadow = true,
      receiveShadow = true
    } = opts;

    const geom = new THREE.BoxGeometry(w, h, d);
    const mesh = new THREE.Mesh(geom, material);
    mesh.position.set(x, y + h / 2, z);
    mesh.castShadow = castShadow;
    mesh.receiveShadow = receiveShadow;
    this.mapGroup.add(mesh);

    if (isWall) {
      this.wallMeshes.push(mesh);
    }

    if (collide) {
      const box = {
        minX: x - w / 2,
        maxX: x + w / 2,
        minY: y,
        maxY: y + h,
        minZ: z - d / 2,
        maxZ: z + d / 2,
        penetrable,
        penetrationLoss,
        mesh
      };
      mesh.userData.collider = box;
      this.colliders.push(box);
    }

    return mesh;
  }

  addCylinder(opts) {
    const {
      x = 0,
      y = 0,
      z = 0,
      radius = 64,
      h = 128,
      material,
      penetrable = false,
      penetrationLoss = 0.35,
      isWall = false
    } = opts;

    const geom = new THREE.CylinderGeometry(radius, radius, h, 20);
    const mesh = new THREE.Mesh(geom, material);
    mesh.position.set(x, y + h / 2, z);
    mesh.castShadow = true;
    mesh.receiveShadow = true;
    this.mapGroup.add(mesh);

    if (isWall) this.wallMeshes.push(mesh);

    const box = {
      minX: x - radius * 0.88,
      maxX: x + radius * 0.88,
      minY: y,
      maxY: y + h,
      minZ: z - radius * 0.88,
      maxZ: z + radius * 0.88,
      penetrable,
      penetrationLoss,
      mesh
    };
    this.colliders.push(box);
    return mesh;
  }

  addRamp(opts) {
    const {
      x = 0,
      y = 0,
      z = 0,
      w = 140,
      h = 80,
      d = 240,
      dir = 'z+',
      steps = 10,
      material
    } = opts;

    for (let i = 0; i < steps; i++) {
      const stepH = ((i + 1) / steps) * h;
      if (dir === 'z+' || dir === 'z-') {
        const stepD = d / steps;
        const idx = dir === 'z+' ? i : steps - 1 - i;
        const sz = z - d / 2 + (idx + 0.5) * stepD;
        this.addBox({
          x,
          y,
          z: sz,
          w,
          h: stepH,
          d: stepD,
          material,
          penetrable: false,
          isWall: false
        });
      } else {
        const stepW = w / steps;
        const idx = dir === 'x+' ? i : steps - 1 - i;
        const sx = x - w / 2 + (idx + 0.5) * stepW;
        this.addBox({
          x: sx,
          y,
          z,
          w: stepW,
          h: stepH,
          d,
          material,
          penetrable: false,
          isWall: false
        });
      }
    }
  }

  buildOuterBoundary(sizeW, sizeD, wallH, matWall, matFloor) {
    this.addBox({
      x: 0,
      y: -24,
      z: 0,
      w: sizeW,
      h: 24,
      d: sizeD,
      material: matFloor,
      penetrable: false,
      isWall: false,
      castShadow: false
    });

    const t = 40;
    this.addBox({ x: 0, y: 0, z: -sizeD / 2 - t / 2, w: sizeW + t * 2, h: wallH, d: t, material: matWall, penetrable: false });
    this.addBox({ x: 0, y: 0, z: sizeD / 2 + t / 2, w: sizeW + t * 2, h: wallH, d: t, material: matWall, penetrable: false });
    this.addBox({ x: -sizeW / 2 - t / 2, y: 0, z: 0, w: t, h: wallH, d: sizeD, material: matWall, penetrable: false });
    this.addBox({ x: sizeW / 2 + t / 2, y: 0, z: 0, w: t, h: wallH, d: sizeD, material: matWall, penetrable: false });
  }

  loadMap(mapId) {
    if (this.mapGroup) {
      this.scene.remove(this.mapGroup);
    }
    this.mapGroup = new THREE.Group();
    this.colliders = [];
    this.wallMeshes = [];
    this.playerSpawns = [];
    this.botSpawns = [];
    this.botWaypoints = [];
    this.currentMap = mapId;

    if (mapId === 'de_mirage_mid') {
      this.buildMirage();
    } else if (mapId === 'de_dust2_long') {
      this.buildDust2();
    } else if (mapId === 'de_inferno_banana') {
      this.buildInferno();
    } else if (mapId === 'de_nuke_outside') {
      this.buildNuke();
    } else if (mapId === 'de_cache_mid') {
      this.buildCache();
    } else if (mapId === 'de_overpass_park') {
      this.buildOverpass();
    } else if (mapId === 'aim_redline_hvh') {
      this.buildRedline();
    } else {
      this.buildCyberWarehouse();
    }

    this.scene.add(this.mapGroup);
    return {
      colliders: this.colliders,
      playerSpawns: this.playerSpawns,
      botSpawns: this.botSpawns,
      botWaypoints: this.botWaypoints,
      skyColor: this.skyColor
    };
  }

  // MAP 1: DE_MIRAGE
  buildMirage() {
    this.skyColor.setHex(0x88b9e6);
    const matWall = new THREE.MeshStandardMaterial({ map: getSandstoneWallTex(6, 2), roughness: 0.85, transparent: true });
    const matBuilding = new THREE.MeshStandardMaterial({ map: getSandstoneWallTex(3, 2), color: 0xe6cfa8, roughness: 0.8, transparent: true });
    const matFloor = new THREE.MeshStandardMaterial({ map: getSandFloorTex(18, 18), roughness: 0.9 });
    const matWood = new THREE.MeshStandardMaterial({ map: getWoodCrateTex(1, 1), roughness: 0.75 });
    const matShutter = new THREE.MeshStandardMaterial({ map: getWoodDoorTex(1, 1), roughness: 0.7 });
    const matAccent = new THREE.MeshStandardMaterial({ color: 0x386694, roughness: 0.6 });

    this.buildOuterBoundary(2000, 2200, 280, matWall, matFloor);

    this.addBox({ x: 0, y: 0, z: -860, w: 680, h: 64, d: 340, material: matBuilding, isWall: true });
    this.addRamp({ x: -420, y: 0, z: -860, w: 160, h: 64, d: 260, dir: 'x+', steps: 8, material: matBuilding });
    this.addRamp({ x: 420, y: 0, z: -860, w: 160, h: 64, d: 260, dir: 'x-', steps: 8, material: matBuilding });
    this.addBox({ x: -220, y: 64, z: -690, w: 240, h: 160, d: 28, material: matBuilding, isWall: true });
    this.addBox({ x: 220, y: 64, z: -690, w: 240, h: 160, d: 28, material: matBuilding, isWall: true });
    this.addBox({ x: 0, y: 170, z: -690, w: 200, h: 54, d: 28, material: matAccent });
    this.addBox({ x: 0, y: 64, z: -690, w: 200, h: 34, d: 24, material: matShutter, penetrable: true, penetrationLoss: 0.22 });
    this.addBox({ x: 75, y: 98, z: -690, w: 52, h: 72, d: 14, material: matShutter, penetrable: true, penetrationLoss: 0.18 });

    this.addBox({ x: -560, y: 0, z: -180, w: 260, h: 210, d: 420, material: matBuilding, isWall: true });
    this.addBox({ x: -380, y: 0, z: -260, w: 64, h: 56, d: 64, material: matWood, penetrable: true, penetrationLoss: 0.25 });
    this.addBox({ x: -380, y: 56, z: -260, w: 54, h: 48, d: 54, material: matWood, penetrable: true, penetrationLoss: 0.25 });

    this.addBox({ x: 580, y: 0, z: -120, w: 320, h: 52, d: 980, material: matBuilding });
    this.addRamp({ x: 580, y: 0, z: 480, w: 240, h: 52, d: 220, dir: 'z-', steps: 8, material: matBuilding });
    this.addBox({ x: 430, y: 52, z: -320, w: 24, h: 46, d: 240, material: matWall, penetrable: true, penetrationLoss: 0.38, isWall: true });
    this.addBox({ x: 430, y: 52, z: 120, w: 24, h: 130, d: 90, material: matBuilding, isWall: true });

    this.addBox({ x: 90, y: 0, z: -110, w: 76, h: 56, d: 76, material: matWood, penetrable: true, penetrationLoss: 0.22 });
    this.addBox({ x: 166, y: 0, z: -110, w: 72, h: 56, d: 72, material: matWood, penetrable: true, penetrationLoss: 0.22 });
    this.addBox({ x: 125, y: 56, z: -110, w: 64, h: 52, d: 64, material: matWood, penetrable: true, penetrationLoss: 0.20 });
    this.addBox({ x: -165, y: 0, z: 80, w: 88, h: 150, d: 88, material: matBuilding, penetrable: false, isWall: true });
    this.addBox({ x: -90, y: 0, z: 80, w: 62, h: 54, d: 62, material: matWood, penetrable: true, penetrationLoss: 0.22 });

    this.addBox({ x: -240, y: 0, z: 580, w: 220, h: 165, d: 90, material: matBuilding, isWall: true });
    this.addBox({ x: 180, y: 0, z: 620, w: 96, h: 58, d: 130, material: matWood, penetrable: true, penetrationLoss: 0.24 });
    this.addBox({ x: 0, y: 0, z: 820, w: 280, h: 42, d: 84, material: matShutter, penetrable: true, penetrationLoss: 0.25 });

    this.playerSpawns = [
      { x: 0, y: 5, z: 920, yaw: 0 },
      { x: -320, y: 5, z: 880, yaw: -0.2 },
      { x: 340, y: 5, z: 860, yaw: 0.2 }
    ];
    this.botSpawns = [
      { x: 0, y: 70, z: -820, yaw: Math.PI },
      { x: -340, y: 5, z: -560, yaw: Math.PI },
      { x: 320, y: 5, z: -580, yaw: Math.PI },
      { x: 550, y: 60, z: -260, yaw: Math.PI * 0.85 },
      { x: -260, y: 5, z: -120, yaw: Math.PI },
      { x: 140, y: 5, z: -340, yaw: Math.PI }
    ];
    this.botWaypoints = [
      { x: 0, y: 64, z: -760 },
      { x: -280, y: 0, z: -420 },
      { x: 260, y: 0, z: -360 },
      { x: 0, y: 0, z: -40 },
      { x: -260, y: 0, z: 260 },
      { x: 240, y: 0, z: 260 },
      { x: 560, y: 52, z: -60 },
      { x: 0, y: 0, z: 520 }
    ];
  }

  // MAP 2: DE_DUST2
  buildDust2() {
    this.skyColor.setHex(0x98c6ec);
    const matWall = new THREE.MeshStandardMaterial({ map: getSandstoneWallTex(6, 2), color: 0xe3cba6, roughness: 0.88, transparent: true });
    const matFloor = new THREE.MeshStandardMaterial({ map: getSandFloorTex(20, 20), color: 0xd9c29c, roughness: 0.92 });
    const matDoor = new THREE.MeshStandardMaterial({ map: getWoodDoorTex(1, 1), roughness: 0.7 });
    const matCrate = new THREE.MeshStandardMaterial({ map: getWoodCrateTex(1, 1), roughness: 0.72 });
    const matBlueContainer = new THREE.MeshStandardMaterial({ color: 0x255c99, metalness: 0.35, roughness: 0.55, transparent: true });

    this.buildOuterBoundary(2100, 2400, 280, matWall, matFloor);

    this.addBox({ x: -520, y: 0, z: 520, w: 620, h: 220, d: 36, material: matWall, isWall: true });
    this.addBox({ x: 520, y: 0, z: 520, w: 620, h: 220, d: 36, material: matWall, isWall: true });
    this.addBox({ x: 0, y: 155, z: 520, w: 420, h: 65, d: 36, material: matWall, isWall: true });
    this.addBox({ x: -115, y: 0, z: 520, w: 150, h: 155, d: 16, material: matDoor, penetrable: true, penetrationLoss: 0.22 });
    this.addBox({ x: 115, y: 0, z: 520, w: 150, h: 155, d: 16, material: matDoor, penetrable: true, penetrationLoss: 0.22 });

    this.addBox({ x: -340, y: 0, z: 180, w: 150, h: 125, d: 310, material: matBlueContainer, penetrable: true, penetrationLoss: 0.45, isWall: true });
    this.addBox({ x: 560, y: 0, z: 220, w: 32, h: 64, d: 380, material: matWall, penetrable: true, penetrationLoss: 0.35, isWall: true });
    this.addBox({ x: 720, y: 0, z: 390, w: 320, h: 54, d: 64, material: matCrate, penetrable: true, penetrationLoss: 0.22 });

    this.addBox({ x: 140, y: 0, z: 20, w: 96, h: 56, d: 96, material: matCrate, penetrable: true, penetrationLoss: 0.22 });
    this.addBox({ x: 140, y: 56, z: 20, w: 68, h: 48, d: 68, material: matCrate, penetrable: true, penetrationLoss: 0.20 });
    this.addBox({ x: -120, y: 0, z: -180, w: 180, h: 155, d: 44, material: matWall, isWall: true });

    this.addBox({ x: -180, y: 0, z: -820, w: 960, h: 72, d: 440, material: matWall });
    this.addRamp({ x: 440, y: 0, z: -740, w: 280, h: 72, d: 360, dir: 'z-', steps: 10, material: matWall });
    this.addRamp({ x: -760, y: 0, z: -740, w: 200, h: 72, d: 320, dir: 'z-', steps: 10, material: matWall });

    this.addBox({ x: -80, y: 72, z: -660, w: 92, h: 58, d: 92, material: matCrate, penetrable: true, penetrationLoss: 0.20 });
    this.addBox({ x: -172, y: 72, z: -660, w: 88, h: 58, d: 88, material: matCrate, penetrable: true, penetrationLoss: 0.20 });
    this.addBox({ x: -120, y: 130, z: -660, w: 78, h: 56, d: 78, material: matCrate, penetrable: true, penetrationLoss: 0.20 });
    this.addBox({ x: -460, y: 72, z: -920, w: 120, h: 64, d: 120, material: matCrate, penetrable: true, penetrationLoss: 0.22 });

    this.playerSpawns = [
      { x: 0, y: 5, z: 920, yaw: 0 },
      { x: -260, y: 5, z: 860, yaw: -0.15 },
      { x: 260, y: 5, z: 860, yaw: 0.15 }
    ];
    this.botSpawns = [
      { x: -120, y: 80, z: -780, yaw: Math.PI },
      { x: 220, y: 80, z: -760, yaw: Math.PI },
      { x: 420, y: 5, z: -380, yaw: Math.PI },
      { x: -320, y: 5, z: -360, yaw: Math.PI },
      { x: 640, y: 5, z: 180, yaw: Math.PI * 0.7 },
      { x: 0, y: 5, z: -420, yaw: Math.PI }
    ];
    this.botWaypoints = [
      { x: -120, y: 72, z: -780 },
      { x: 420, y: 36, z: -680 },
      { x: 240, y: 0, z: -340 },
      { x: -220, y: 0, z: -340 },
      { x: 0, y: 0, z: 180 },
      { x: 640, y: 0, z: 180 },
      { x: 0, y: 0, z: 680 }
    ];
  }

  // MAP 3: DE_INFERNO
  buildInferno() {
    this.skyColor.setHex(0x89b4db);
    const matWall = new THREE.MeshStandardMaterial({ map: getInfernoBrickTex(5, 2), roughness: 0.82, transparent: true });
    const matFloor = new THREE.MeshStandardMaterial({ map: getCobbleFloorTex(18, 18), roughness: 0.88 });
    const matWood = new THREE.MeshStandardMaterial({ map: getWoodCrateTex(1, 1), roughness: 0.72 });
    const matLogs = new THREE.MeshStandardMaterial({ map: getWoodDoorTex(1, 1), color: 0x92623a, roughness: 0.75 });
    const matRoof = new THREE.MeshStandardMaterial({ color: 0x993d26, roughness: 0.7 });
    const matFountain = new THREE.MeshStandardMaterial({ color: 0x94a3b8, roughness: 0.65 });

    this.buildOuterBoundary(1850, 2300, 270, matWall, matFloor);

    this.addBox({ x: -380, y: 0, z: 240, w: 180, h: 195, d: 920, material: matWall, isWall: true });
    this.addBox({ x: -380, y: 195, z: 240, w: 205, h: 22, d: 940, material: matRoof });
    this.addBox({ x: 380, y: 0, z: 160, w: 180, h: 195, d: 840, material: matWall, isWall: true });
    this.addBox({ x: 380, y: 195, z: 160, w: 205, h: 22, d: 860, material: matRoof });

    this.addBox({ x: -195, y: 0, z: 540, w: 135, h: 58, d: 84, material: matLogs, penetrable: true, penetrationLoss: 0.22 });
    this.addBox({ x: -210, y: 58, z: 540, w: 95, h: 38, d: 76, material: matLogs, penetrable: true, penetrationLoss: 0.20 });

    this.addBox({ x: 185, y: 0, z: 220, w: 160, h: 64, d: 34, material: matWall, penetrable: true, penetrationLoss: 0.34, isWall: true });
    this.addBox({ x: -175, y: 0, z: 40, w: 115, h: 60, d: 185, material: matWood, penetrable: true, penetrationLoss: 0.25 });
    this.addBox({ x: -175, y: 60, z: 30, w: 95, h: 38, d: 110, material: matWood, penetrable: true, penetrationLoss: 0.22 });
    this.addBox({ x: 220, y: 0, z: -180, w: 96, h: 52, d: 68, material: matWood, penetrable: true, penetrationLoss: 0.22 });

    this.addCylinder({ x: 0, y: 0, z: -560, radius: 92, h: 46, material: matFountain, penetrable: true, penetrationLoss: 0.35 });
    this.addCylinder({ x: 0, y: 46, z: -560, radius: 28, h: 95, material: matFountain, penetrable: false });

    this.addBox({ x: -420, y: 0, z: -580, w: 96, h: 62, d: 96, material: matWood, penetrable: true, penetrationLoss: 0.22 });
    this.addBox({ x: -420, y: 62, z: -580, w: 80, h: 54, d: 80, material: matWood, penetrable: true, penetrationLoss: 0.20 });
    this.addBox({ x: -240, y: 0, z: -780, w: 88, h: 115, d: 88, material: matWall, isWall: true });

    this.addBox({ x: 180, y: 0, z: -880, w: 220, h: 56, d: 56, material: matLogs, penetrable: true, penetrationLoss: 0.20 });
    this.addBox({ x: 210, y: 56, z: -880, w: 140, h: 48, d: 48, material: matLogs, penetrable: true, penetrationLoss: 0.18 });

    this.addBox({ x: 540, y: 0, z: -640, w: 280, h: 56, d: 420, material: matWall });
    this.addRamp({ x: 340, y: 0, z: -640, w: 120, h: 56, d: 240, dir: 'x+', steps: 8, material: matWall });

    this.playerSpawns = [
      { x: 0, y: 5, z: 940, yaw: 0 },
      { x: -220, y: 5, z: 920, yaw: 0 },
      { x: 220, y: 5, z: 920, yaw: 0 }
    ];
    this.botSpawns = [
      { x: 180, y: 5, z: -960, yaw: Math.PI },
      { x: -320, y: 5, z: -860, yaw: Math.PI },
      { x: 520, y: 62, z: -640, yaw: Math.PI },
      { x: 0, y: 5, z: -360, yaw: Math.PI },
      { x: 150, y: 5, z: -40, yaw: Math.PI },
      { x: -120, y: 5, z: -480, yaw: Math.PI }
    ];
    this.botWaypoints = [
      { x: 180, y: 0, z: -800 },
      { x: -280, y: 0, z: -660 },
      { x: 520, y: 56, z: -580 },
      { x: 0, y: 0, z: -320 },
      { x: 60, y: 0, z: 80 },
      { x: -40, y: 0, z: 460 },
      { x: 0, y: 0, z: 780 }
    ];
  }

  // MAP 4: DE_NUKE
  buildNuke() {
    this.skyColor.setHex(0x9bc5e8);
    const matWall = new THREE.MeshStandardMaterial({ map: getNukeWallTex(5, 2), roughness: 0.65, metalness: 0.25, transparent: true });
    const matFloor = new THREE.MeshStandardMaterial({ map: getAsphaltFloorTex(20, 20), roughness: 0.85 });
    const matRed = new THREE.MeshStandardMaterial({ color: 0xb91c1c, metalness: 0.45, roughness: 0.45, transparent: true });
    const matSilo = new THREE.MeshStandardMaterial({ color: 0xe2e8f0, roughness: 0.55, metalness: 0.3 });
    const matCrate = new THREE.MeshStandardMaterial({ map: getWoodCrateTex(1, 1), color: 0x93c5fd, roughness: 0.65 });

    this.buildOuterBoundary(2100, 2300, 280, matWall, matFloor);

    this.addBox({ x: -640, y: 0, z: 0, w: 420, h: 145, d: 1180, material: matWall, isWall: true });
    this.addRamp({ x: -360, y: 0, z: 540, w: 140, h: 145, d: 320, dir: 'z-', steps: 12, material: matWall });
    this.addCylinder({ x: -380, y: 0, z: 240, radius: 135, h: 175, material: matSilo, penetrable: false, isWall: true });
    this.addBox({ x: 110, y: 0, z: 120, w: 145, h: 125, d: 340, material: matRed, penetrable: true, penetrationLoss: 0.35, isWall: true });

    this.addBox({ x: 180, y: 0, z: -260, w: 210, h: 115, d: 36, material: matSilo, penetrable: true, penetrationLoss: 0.28, isWall: true });
    this.addBox({ x: -120, y: 0, z: -220, w: 86, h: 58, d: 86, material: matCrate, penetrable: true, penetrationLoss: 0.22 });
    this.addBox({ x: -120, y: 58, z: -220, w: 72, h: 52, d: 72, material: matCrate, penetrable: true, penetrationLoss: 0.20 });

    this.addBox({ x: 680, y: 0, z: -380, w: 340, h: 165, d: 480, material: matWall, isWall: true });
    this.addBox({ x: 460, y: 0, z: 320, w: 96, h: 58, d: 96, material: matCrate, penetrable: true, penetrationLoss: 0.22 });

    this.addBox({ x: 0, y: 0, z: -880, w: 780, h: 68, d: 280, material: matWall });
    this.addRamp({ x: -480, y: 0, z: -880, w: 180, h: 68, d: 240, dir: 'x+', steps: 9, material: matWall });
    this.addRamp({ x: 480, y: 0, z: -880, w: 180, h: 68, d: 240, dir: 'x-', steps: 9, material: matWall });
    this.addBox({ x: 0, y: 68, z: -745, w: 260, h: 48, d: 20, material: matCrate, penetrable: true, penetrationLoss: 0.20 });

    this.playerSpawns = [
      { x: 0, y: 5, z: 920, yaw: 0 },
      { x: 280, y: 5, z: 880, yaw: 0 },
      { x: -180, y: 5, z: 920, yaw: 0 }
    ];
    this.botSpawns = [
      { x: 0, y: 74, z: -880, yaw: Math.PI },
      { x: -260, y: 74, z: -860, yaw: Math.PI },
      { x: 260, y: 74, z: -860, yaw: Math.PI },
      { x: 340, y: 5, z: -420, yaw: Math.PI },
      { x: -180, y: 5, z: -440, yaw: Math.PI },
      { x: -560, y: 150, z: -120, yaw: Math.PI * 0.85 }
    ];
    this.botWaypoints = [
      { x: 0, y: 68, z: -860 },
      { x: 320, y: 0, z: -420 },
      { x: -180, y: 0, z: -420 },
      { x: 320, y: 0, z: 120 },
      { x: -120, y: 0, z: 120 },
      { x: -560, y: 145, z: 80 },
      { x: 120, y: 0, z: 620 }
    ];
  }

  // MAP 5: DE_CACHE (MID GARAGE, WHITE BOX, BOOST, VENTS & HIGHWAY)
  buildCache() {
    this.skyColor.setHex(0x88a89a);
    const matWall = new THREE.MeshStandardMaterial({ map: getNukeWallTex(5, 2), color: 0xb5c2b7, roughness: 0.78, transparent: true });
    const matFloor = new THREE.MeshStandardMaterial({ map: getAsphaltFloorTex(18, 18), color: 0x6b726e, roughness: 0.88 });
    const matWhiteBox = new THREE.MeshStandardMaterial({ color: 0xf1f5f9, roughness: 0.55, transparent: true });
    const matWood = new THREE.MeshStandardMaterial({ map: getWoodCrateTex(1, 1), roughness: 0.72 });
    const matVent = new THREE.MeshStandardMaterial({ map: getWoodDoorTex(1, 1), color: 0x94a3b8, roughness: 0.5 });

    this.buildOuterBoundary(2000, 2200, 270, matWall, matFloor);

    // Mid Garage Wall & Roll-up Entrance (South Z = +580)
    this.addBox({ x: -480, y: 0, z: 580, w: 560, h: 210, d: 36, material: matWall, isWall: true });
    this.addBox({ x: 480, y: 0, z: 580, w: 560, h: 210, d: 36, material: matWall, isWall: true });
    this.addBox({ x: 0, y: 145, z: 580, w: 400, h: 65, d: 36, material: matWall, isWall: true });

    // Boost Boxes on South-West (X = -520, Z = +340)
    this.addBox({ x: -520, y: 0, z: 340, w: 92, h: 56, d: 92, material: matWood, penetrable: true, penetrationLoss: 0.22 });
    this.addBox({ x: -520, y: 56, z: 260, w: 140, h: 64, d: 140, material: matWall });

    // Iconic Center White Box (X = 0, Z = -120)
    this.addBox({ x: -20, y: 0, z: -120, w: 135, h: 125, d: 135, material: matWhiteBox, penetrable: true, penetrationLoss: 0.35, isWall: true });
    // Sandbags & Wood Crates next to White Box
    this.addBox({ x: 180, y: 0, z: -40, w: 96, h: 56, d: 96, material: matWood, penetrable: true, penetrationLoss: 0.20 });
    this.addBox({ x: 180, y: 56, z: -40, w: 72, h: 48, d: 72, material: matWood, penetrable: true, penetrationLoss: 0.18 });

    // Vents Building on West (X = -580, Z = -180, with Wallbangable Vent Shutters!)
    this.addBox({ x: -640, y: 0, z: -180, w: 320, h: 185, d: 520, material: matWall, isWall: true });
    this.addBox({ x: -475, y: 0, z: -180, w: 18, h: 88, d: 140, material: matVent, penetrable: true, penetrationLoss: 0.16 });

    // Highway & Z-Connector Platform (North Z = -780)
    this.addBox({ x: 120, y: 0, z: -820, w: 920, h: 64, d: 340, material: matWall });
    this.addRamp({ x: 580, y: 0, z: -640, w: 220, h: 64, d: 280, dir: 'z-', steps: 9, material: matWall });
    this.addRamp({ x: -340, y: 0, z: -820, w: 180, h: 64, d: 240, dir: 'x+', steps: 8, material: matWall });
    this.addBox({ x: 0, y: 64, z: -665, w: 240, h: 52, d: 24, material: matWood, penetrable: true, penetrationLoss: 0.20 });

    this.playerSpawns = [
      { x: 0, y: 5, z: 900, yaw: 0 },
      { x: -260, y: 5, z: 860, yaw: 0 },
      { x: 260, y: 5, z: 860, yaw: 0 }
    ];
    this.botSpawns = [
      { x: 0, y: 70, z: -820, yaw: Math.PI },
      { x: 320, y: 70, z: -820, yaw: Math.PI },
      { x: -240, y: 5, z: -520, yaw: Math.PI },
      { x: 280, y: 5, z: -420, yaw: Math.PI },
      { x: -340, y: 5, z: -180, yaw: Math.PI },
      { x: 420, y: 5, z: 120, yaw: Math.PI }
    ];
    this.botWaypoints = [
      { x: 0, y: 64, z: -800 },
      { x: -240, y: 0, z: -480 },
      { x: 280, y: 0, z: -380 },
      { x: -260, y: 0, z: 60 },
      { x: 260, y: 0, z: 120 },
      { x: 0, y: 0, z: 380 }
    ];
  }

  // MAP 6: DE_OVERPASS (BATHROOMS, FOUNTAIN PARK, PARTY & BANK SNIPER)
  buildOverpass() {
    this.skyColor.setHex(0x7ca7cf);
    const matWall = new THREE.MeshStandardMaterial({ map: getDevGreyTex(6, 2), color: 0x94a3b8, roughness: 0.78, transparent: true });
    const matFloor = new THREE.MeshStandardMaterial({ map: getCobbleFloorTex(18, 18), roughness: 0.85 });
    const matWood = new THREE.MeshStandardMaterial({ map: getWoodCrateTex(1, 1), roughness: 0.7 });
    const matStall = new THREE.MeshStandardMaterial({ map: getWoodDoorTex(1, 1), color: 0x38bdf8, roughness: 0.55 });
    const matTruck = new THREE.MeshStandardMaterial({ color: 0x1e3a8a, metalness: 0.4, roughness: 0.5 });

    this.buildOuterBoundary(2000, 2300, 270, matWall, matFloor);

    // Bathrooms Corridor on West (X = -520, with thin Wallbangable Stall Partitions!)
    this.addBox({ x: -320, y: 0, z: 0, w: 36, h: 175, d: 780, material: matWall, isWall: true });
    this.addBox({ x: -560, y: 0, z: -180, w: 220, h: 115, d: 18, material: matStall, penetrable: true, penetrationLoss: 0.18 });
    this.addBox({ x: -560, y: 0, z: 180, w: 220, h: 115, d: 18, material: matStall, penetrable: true, penetrationLoss: 0.18 });

    // Party & Park Divider (East Side X = +380)
    this.addBox({ x: 380, y: 0, z: 260, w: 140, h: 155, d: 340, material: matWall, isWall: true });
    this.addBox({ x: 120, y: 0, z: 340, w: 96, h: 58, d: 96, material: matWood, penetrable: true, penetrationLoss: 0.22 });
    this.addBox({ x: -80, y: 0, z: 460, w: 160, h: 52, d: 42, material: matWood, penetrable: true, penetrationLoss: 0.20 });

    // A-Site SWAT Truck & Default Crates (Z = -440)
    this.addBox({ x: 180, y: 0, z: -420, w: 135, h: 110, d: 280, material: matTruck, penetrable: true, penetrationLoss: 0.38, isWall: true });
    this.addBox({ x: -120, y: 0, z: -460, w: 92, h: 58, d: 92, material: matWood, penetrable: true, penetrationLoss: 0.20 });
    this.addBox({ x: -120, y: 58, z: -460, w: 76, h: 50, d: 76, material: matWood, penetrable: true, penetrationLoss: 0.18 });

    // Bank Sniper Stairs & Balcony (North Z = -880)
    this.addBox({ x: 0, y: 0, z: -900, w: 840, h: 72, d: 280, material: matWall });
    this.addRamp({ x: -520, y: 0, z: -900, w: 200, h: 72, d: 260, dir: 'x+', steps: 10, material: matWall });
    this.addRamp({ x: 520, y: 0, z: -900, w: 200, h: 72, d: 260, dir: 'x-', steps: 10, material: matWall });
    this.addBox({ x: 0, y: 72, z: -765, w: 260, h: 48, d: 22, material: matWood, penetrable: true, penetrationLoss: 0.18 });

    this.playerSpawns = [
      { x: 0, y: 5, z: 940, yaw: 0 },
      { x: -460, y: 5, z: 880, yaw: 0 },
      { x: 260, y: 5, z: 900, yaw: 0 }
    ];
    this.botSpawns = [
      { x: 0, y: 78, z: -900, yaw: Math.PI },
      { x: -280, y: 78, z: -880, yaw: Math.PI },
      { x: 280, y: 78, z: -880, yaw: Math.PI },
      { x: -480, y: 5, z: -420, yaw: Math.PI },
      { x: 0, y: 5, z: -260, yaw: Math.PI },
      { x: 480, y: 5, z: -180, yaw: Math.PI }
    ];
    this.botWaypoints = [
      { x: 0, y: 72, z: -880 },
      { x: -480, y: 0, z: -360 },
      { x: -480, y: 0, z: 240 },
      { x: 0, y: 0, z: -180 },
      { x: 0, y: 0, z: 260 },
      { x: 520, y: 0, z: -120 }
    ];
  }

  // MAP 7: AIM_REDLINE_HVH
  buildRedline() {
    this.skyColor.setHex(0x52657c);
    const matOrange = new THREE.MeshStandardMaterial({ map: getDevOrangeTex(2, 2), roughness: 0.75, transparent: true });
    const matGrey = new THREE.MeshStandardMaterial({ map: getDevGreyTex(14, 14), roughness: 0.82 });
    const matGreyWall = new THREE.MeshStandardMaterial({ map: getDevGreyTex(4, 2), roughness: 0.8, transparent: true });
    const matWood = new THREE.MeshStandardMaterial({ map: getWoodCrateTex(1, 1), roughness: 0.7 });

    this.buildOuterBoundary(1800, 2200, 260, matGreyWall, matGrey);

    this.addBox({ x: 0, y: 0, z: 0, w: 960, h: 72, d: 260, material: matOrange });
    this.addRamp({ x: -600, y: 0, z: 0, w: 240, h: 72, d: 240, dir: 'x+', steps: 9, material: matOrange });
    this.addRamp({ x: 600, y: 0, z: 0, w: 240, h: 72, d: 240, dir: 'x-', steps: 9, material: matOrange });

    this.addBox({ x: -220, y: 72, z: -115, w: 180, h: 68, d: 16, material: matWood, penetrable: true, penetrationLoss: 0.18 });
    this.addBox({ x: 220, y: 72, z: 115, w: 180, h: 68, d: 16, material: matWood, penetrable: true, penetrationLoss: 0.18 });
    this.addBox({ x: 0, y: 72, z: 0, w: 96, h: 110, d: 96, material: matWood, penetrable: true, penetrationLoss: 0.22 });

    for (const sign of [-1, 1]) {
      const zMid = sign * 480;
      this.addBox({ x: -380, y: 0, z: zMid, w: 130, h: 150, d: 130, material: matOrange, isWall: true });
      this.addBox({ x: 380, y: 0, z: zMid, w: 130, h: 150, d: 130, material: matOrange, isWall: true });
      this.addBox({ x: 0, y: 0, z: zMid, w: 280, h: 110, d: 20, material: matWood, penetrable: true, penetrationLoss: 0.20 });

      this.addBox({ x: 0, y: 0, z: sign * 860, w: 720, h: 56, d: 220, material: matGreyWall });
      this.addRamp({ x: -460, y: 0, z: sign * 860, w: 200, h: 56, d: 200, dir: 'x+', steps: 8, material: matOrange });
      this.addRamp({ x: 460, y: 0, z: sign * 860, w: 200, h: 56, d: 200, dir: 'x-', steps: 8, material: matOrange });

      this.addBox({ x: -180, y: 56, z: sign * 760, w: 130, h: 52, d: 24, material: matWood, penetrable: true, penetrationLoss: 0.18 });
      this.addBox({ x: 180, y: 56, z: sign * 760, w: 130, h: 52, d: 24, material: matWood, penetrable: true, penetrationLoss: 0.18 });
    }

    this.playerSpawns = [
      { x: 0, y: 62, z: 860, yaw: 0 },
      { x: -280, y: 62, z: 860, yaw: 0 },
      { x: 280, y: 62, z: 860, yaw: 0 }
    ];
    this.botSpawns = [
      { x: 0, y: 62, z: -860, yaw: Math.PI },
      { x: -240, y: 62, z: -860, yaw: Math.PI },
      { x: 240, y: 62, z: -860, yaw: Math.PI },
      { x: -240, y: 5, z: -480, yaw: Math.PI },
      { x: 240, y: 5, z: -480, yaw: Math.PI },
      { x: 0, y: 78, z: -60, yaw: Math.PI }
    ];
    this.botWaypoints = [
      { x: 0, y: 56, z: -860 },
      { x: -260, y: 0, z: -480 },
      { x: 260, y: 0, z: -480 },
      { x: -260, y: 72, z: 0 },
      { x: 260, y: 72, z: 0 },
      { x: -260, y: 0, z: 480 },
      { x: 260, y: 0, z: 480 }
    ];
  }

  // MAP 8: CS_OFFICE_CYBER
  buildCyberWarehouse() {
    this.skyColor.setHex(0x090d1a);
    const matFloor = new THREE.MeshStandardMaterial({ map: getCyberFloorTex(16, 16), roughness: 0.5, metalness: 0.25 });
    const matWall = new THREE.MeshStandardMaterial({ map: getCyberWallTex(6, 2), roughness: 0.5, metalness: 0.3, transparent: true });
    const matNeonPillar = new THREE.MeshStandardMaterial({ color: 0x161c2e, emissive: 0x00f5d4, emissiveIntensity: 0.18, roughness: 0.4, transparent: true });
    const matNeonPink = new THREE.MeshStandardMaterial({ color: 0x251438, emissive: 0xd946ef, emissiveIntensity: 0.24, roughness: 0.4 });
    const matCrate = new THREE.MeshStandardMaterial({ map: getWoodCrateTex(1, 1), color: 0x7dd3fc, roughness: 0.6 });

    this.buildOuterBoundary(1900, 2100, 260, matWall, matFloor);

    this.addBox({ x: -640, y: 0, z: 0, w: 220, h: 64, d: 1100, material: matWall });
    this.addBox({ x: 640, y: 0, z: 0, w: 220, h: 64, d: 1100, material: matWall });
    this.addRamp({ x: -640, y: 0, z: 660, w: 180, h: 64, d: 220, dir: 'z-', steps: 8, material: matNeonPillar });
    this.addRamp({ x: 640, y: 0, z: 660, w: 180, h: 64, d: 220, dir: 'z-', steps: 8, material: matNeonPillar });
    this.addRamp({ x: -640, y: 0, z: -660, w: 180, h: 64, d: 220, dir: 'z+', steps: 8, material: matNeonPillar });
    this.addRamp({ x: 640, y: 0, z: -660, w: 180, h: 64, d: 220, dir: 'z+', steps: 8, material: matNeonPillar });

    this.addBox({ x: 0, y: 0, z: 0, w: 140, h: 170, d: 140, material: matNeonPillar, isWall: true });
    this.addBox({ x: -180, y: 0, z: -180, w: 84, h: 58, d: 84, material: matCrate, penetrable: true, penetrationLoss: 0.20 });
    this.addBox({ x: 180, y: 0, z: -180, w: 84, h: 58, d: 84, material: matCrate, penetrable: true, penetrationLoss: 0.20 });
    this.addBox({ x: -180, y: 0, z: 180, w: 84, h: 58, d: 84, material: matCrate, penetrable: true, penetrationLoss: 0.20 });
    this.addBox({ x: 180, y: 0, z: 180, w: 84, h: 58, d: 84, material: matCrate, penetrable: true, penetrationLoss: 0.20 });

    this.addBox({ x: -240, y: 0, z: -460, w: 220, h: 110, d: 20, material: matNeonPink, penetrable: true, penetrationLoss: 0.22 });
    this.addBox({ x: 240, y: 0, z: -460, w: 220, h: 110, d: 20, material: matNeonPink, penetrable: true, penetrationLoss: 0.22 });
    this.addBox({ x: -240, y: 0, z: 460, w: 220, h: 110, d: 20, material: matNeonPink, penetrable: true, penetrationLoss: 0.22 });
    this.addBox({ x: 240, y: 0, z: 460, w: 220, h: 110, d: 20, material: matNeonPink, penetrable: true, penetrationLoss: 0.22 });

    this.playerSpawns = [
      { x: 0, y: 5, z: 860, yaw: 0 },
      { x: -340, y: 5, z: 840, yaw: 0 },
      { x: 340, y: 5, z: 840, yaw: 0 }
    ];
    this.botSpawns = [
      { x: 0, y: 5, z: -840, yaw: Math.PI },
      { x: -320, y: 5, z: -760, yaw: Math.PI },
      { x: 320, y: 5, z: -760, yaw: Math.PI },
      { x: -640, y: 70, z: -240, yaw: Math.PI },
      { x: 640, y: 70, z: -240, yaw: Math.PI },
      { x: 0, y: 5, z: -320, yaw: Math.PI }
    ];
    this.botWaypoints = [
      { x: 0, y: 0, z: -760 },
      { x: -320, y: 0, z: -340 },
      { x: 320, y: 0, z: -340 },
      { x: -640, y: 64, z: 0 },
      { x: 640, y: 64, z: 0 },
      { x: -280, y: 0, z: 320 },
      { x: 280, y: 0, z: 320 }
    ];
  }
}
