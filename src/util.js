import * as THREE from 'three';
export const D2R = Math.PI / 180, R2D = 180 / Math.PI;
export const wrap180 = a => ((a + 180) % 360 + 360) % 360 - 180;
export const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
export const lerp = (a, b, t) => a + (b - a) * t;

// pitch: + = down (CS convention). yaw 0 = -Z
export function angleVectors(yaw, pitch, out = new THREE.Vector3()) {
  const y = yaw * D2R, p = pitch * D2R;
  return out.set(-Math.sin(y) * Math.cos(p), -Math.sin(p), -Math.cos(y) * Math.cos(p));
}
export function vectorAngles(dir) {
  const yaw = Math.atan2(-dir.x, -dir.z) * R2D;
  const pitch = -Math.atan2(dir.y, Math.hypot(dir.x, dir.z)) * R2D;
  return { yaw, pitch };
}
export function anglesTo(from, to) {
  return vectorAngles(new THREE.Vector3().subVectors(to, from));
}
export function angleDist(y1, p1, y2, p2) {
  return Math.hypot(wrap180(y1 - y2), p1 - p2);
}

// ray (ro, unit rd) vs capsule (pa,pb,r). returns t or -1
export function rayCapsule(ro, rd, pa, pb, ra) {
  const bax = pb.x - pa.x, bay = pb.y - pa.y, baz = pb.z - pa.z;
  const oax = ro.x - pa.x, oay = ro.y - pa.y, oaz = ro.z - pa.z;
  const baba = bax * bax + bay * bay + baz * baz;
  if (baba < 1e-6) return raySphere(ro, rd, pa, ra);
  const bard = bax * rd.x + bay * rd.y + baz * rd.z;
  const baoa = bax * oax + bay * oay + baz * oaz;
  const rdoa = rd.x * oax + rd.y * oay + rd.z * oaz;
  const oaoa = oax * oax + oay * oay + oaz * oaz;
  const a = baba - bard * bard;
  let b = baba * rdoa - baoa * bard;
  let c = baba * oaoa - baoa * baoa - ra * ra * baba;
  let h = b * b - a * c;
  if (h >= 0 && a > 1e-9) {
    const t = (-b - Math.sqrt(h)) / a;
    const y = baoa + t * bard;
    if (y > 0 && y < baba) return t;
  }
  // caps
  const t1 = raySphere(ro, rd, pa, ra), t2 = raySphere(ro, rd, pb, ra);
  if (t1 < 0) return t2; if (t2 < 0) return t1; return Math.min(t1, t2);
}
export function raySphere(ro, rd, c, r) {
  const ox = ro.x - c.x, oy = ro.y - c.y, oz = ro.z - c.z;
  const b = ox * rd.x + oy * rd.y + oz * rd.z;
  const cc = ox * ox + oy * oy + oz * oz - r * r;
  const h = b * b - cc;
  if (h < 0) return -1;
  const t = -b - Math.sqrt(h);
  return t >= 0 ? t : (cc < 0 ? 0 : -1);
}
export function capsuleCenter(h, out = new THREE.Vector3()) { return out.addVectors(h.wa, h.wb).multiplyScalar(0.5); }
