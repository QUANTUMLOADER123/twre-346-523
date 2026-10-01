import * as THREE from 'three';
import { MAPS, compileMap } from '../src/maps.js';
import { World } from '../src/world.js';
import { playerMove, MV } from '../src/movement.js';

for (const fn of MAPS) {
  const m = fn(); const c = compileMap(m);
  // connectivity
  const seen = new Set(); const st = [0]; seen.add(0);
  while (st.length) { const n = st.pop(); for (const l of c.nodes[n].links) if (!seen.has(l.to)) { seen.add(l.to); st.push(l.to); } }
  console.log(m.id, 'boxes', c.boxes.length, 'nodes', c.nodes.length, 'reachable from node0', seen.size, 'spawns', Object.fromEntries(Object.entries(c.spawns).map(([k,v])=>[k,v.length])), 'zones', c.zones.length);
}

function mkWorld(id) {
  const m = MAPS.map(f=>f()).find(x=>x.id===id); const c = compileMap(m);
  const w = new World(); for (const b of c.boxes) w.addBox(b.min, b.max, {kind:b.kind}); return {w, c, m};
}
function ent(pos) { return { origin: new THREE.Vector3(pos.x,pos.y,pos.z), velocity: new THREE.Vector3(), onGround:false, ducked:false, oldJump:false, events:[], viewOffsetSmooth:64 }; }
const dt = 1/64;
{
  const {w,c} = mkWorld('aim_redline');
  const sp = c.spawns.CT[0];
  const e = ent(sp);
  for (let i=0;i<20;i++) playerMove(w, e, {forwardmove:0,sidemove:0,yaw:180,jump:false,duck:false,walk:false,maxspeed:250}, dt);
  console.log('settled y', e.origin.y.toFixed(3), 'onGround', e.onGround);
  let t=0; for (let i=0;i<64;i++){ playerMove(w,e,{forwardmove:450,sidemove:0,yaw:180,jump:false,duck:false,walk:false,maxspeed:250},dt); if (i===15) console.log('speed after 0.25s', Math.hypot(e.velocity.x,e.velocity.z).toFixed(1)); }
  console.log('speed after 1s', Math.hypot(e.velocity.x,e.velocity.z).toFixed(1));
  let n=0; while (Math.hypot(e.velocity.x,e.velocity.z)>0.1 && n<200){ playerMove(w,e,{forwardmove:0,sidemove:0,yaw:180,jump:false,duck:false,walk:false,maxspeed:250},dt); n++; }
  console.log('stop ticks', n, (n/64).toFixed(3)+'s');
  // jump height
  const y0 = e.origin.y; let maxy=y0; let ticks=0;
  playerMove(w,e,{forwardmove:0,sidemove:0,yaw:180,jump:true,duck:false,walk:false,maxspeed:250},dt);
  do { playerMove(w,e,{forwardmove:0,sidemove:0,yaw:180,jump:false,duck:false,walk:false,maxspeed:250},dt); maxy=Math.max(maxy,e.origin.y); ticks++; } while(!e.onGround && ticks<200);
  console.log('jump height', (maxy-y0).toFixed(2), 'air ticks', ticks);
}
// bhop + optimal strafing sim in the open aim map
{
  const {w,c} = mkWorld('bhop_lava');
  const sp = c.spawns.start[0];
  const e = ent(sp);
  for (let i=0;i<10;i++) playerMove(w, e, {forwardmove:0,sidemove:0,yaw:180,jump:false,duck:false,walk:false,maxspeed:250}, dt);
  let yaw = 180; let maxs=0; let flip=false;
  for (let i=0;i<64*3;i++) {
    const sp2 = Math.hypot(e.velocity.x,e.velocity.z);
    let cmd = {forwardmove:450,sidemove:0,yaw:180,jump:true,duck:false,walk:false,maxspeed:250, autoJump:false};
    cmd.jump = e.onGround; if (e.onGround) e.oldJump=false;
    if (!e.onGround && sp2>15) {
      const velYaw = Math.atan2(-e.velocity.x,-e.velocity.z)*180/Math.PI;
      const desired = 180; let diff = ((desired-velYaw+540)%360)-180;
      flip=!flip; const side = Math.abs(diff)>5? Math.sign(diff) : (flip?1:-1);
      const moveYaw = velYaw + side*90; const rel=(moveYaw-180)*Math.PI/180;
      cmd.forwardmove=Math.cos(rel)*450; cmd.sidemove=-Math.sin(rel)*450;
    }
    playerMove(w,e,cmd,dt);
    maxs=Math.max(maxs,Math.hypot(e.velocity.x,e.velocity.z));
    if (e.origin.y < -16) { console.log('fell in lava at z', e.origin.z.toFixed(0), 'tick', i); break; }
  }
  console.log('bhop sim max speed', maxs.toFixed(1), 'pos', e.origin.toArray().map(v=>v.toFixed(0)));
}
