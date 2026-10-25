// mathlib.hpp - Source-style math: Vector, QAngle, matrix3x4_t and helpers.
// Coordinate system is the Source one: X forward, Y left, Z up, units = inches.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <utility>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
constexpr float M_PI_F = 3.14159265358979323846f;
inline float DEG2RADF(float d) { return d * (M_PI_F / 180.f); }
inline float RAD2DEGF(float r) { return r * (180.f / M_PI_F); }

template <class T> inline T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
template <class T> inline T Lerp(float t, T a, T b) { return a + (b - a) * t; }
inline float RemapVal(float v, float a, float b, float c, float d) { if (a == b) return v >= b ? d : c; return c + (d - c) * (v - a) / (b - a); }
inline float RemapValClamped(float v, float a, float b, float c, float d) { if (a == b) return v >= b ? d : c; float t = Clamp((v - a) / (b - a), 0.f, 1.f); return c + (d - c) * t; }
inline float Approach(float target, float value, float speed) { float d = target - value; if (d > speed) return value + speed; if (d < -speed) return value - speed; return target; }
inline float SimpleSpline(float v) { float s = v * v; return 3 * s - 2 * s * v; }

//-----------------------------------------------------------------------------
struct Vector2D { float x = 0, y = 0; Vector2D() = default; Vector2D(float a, float b) : x(a), y(b) {}
  Vector2D operator+(const Vector2D& o) const { return { x + o.x, y + o.y }; }
  Vector2D operator-(const Vector2D& o) const { return { x - o.x, y - o.y }; }
  Vector2D operator*(float s) const { return { x * s, y * s }; }
  float Length() const { return std::sqrt(x * x + y * y); } };

struct Vector {
  float x = 0, y = 0, z = 0;
  Vector() = default;
  constexpr Vector(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
  float& operator[](int i) { return (&x)[i]; }
  float operator[](int i) const { return (&x)[i]; }
  Vector operator+(const Vector& o) const { return { x + o.x, y + o.y, z + o.z }; }
  Vector operator-(const Vector& o) const { return { x - o.x, y - o.y, z - o.z }; }
  Vector operator*(float s) const { return { x * s, y * s, z * s }; }
  Vector operator*(const Vector& o) const { return { x * o.x, y * o.y, z * o.z }; }
  Vector operator/(float s) const { float i = 1.f / s; return { x * i, y * i, z * i }; }
  Vector operator-() const { return { -x, -y, -z }; }
  Vector& operator+=(const Vector& o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vector& operator-=(const Vector& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  Vector& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
  Vector& operator/=(float s) { float i = 1.f / s; x *= i; y *= i; z *= i; return *this; }
  bool operator==(const Vector& o) const { return x == o.x && y == o.y && z == o.z; }
  bool operator!=(const Vector& o) const { return !(*this == o); }
  void Init(float X = 0, float Y = 0, float Z = 0) { x = X; y = Y; z = Z; }
  float Length() const { return std::sqrt(x * x + y * y + z * z); }
  float LengthSqr() const { return x * x + y * y + z * z; }
  float Length2D() const { return std::sqrt(x * x + y * y); }
  float Length2DSqr() const { return x * x + y * y; }
  float Dot(const Vector& o) const { return x * o.x + y * o.y + z * o.z; }
  float DistTo(const Vector& o) const { return (*this - o).Length(); }
  float DistToSqr(const Vector& o) const { return (*this - o).LengthSqr(); }
  bool IsZero(float tol = 0.01f) const { return std::fabs(x) < tol && std::fabs(y) < tol && std::fabs(z) < tol; }
  float NormalizeInPlace() { float l = Length(); if (l > 1e-6f) { float i = 1.f / l; x *= i; y *= i; z *= i; } else { x = y = 0; z = 0; } return l; }
  Vector Normalized() const { Vector v = *this; v.NormalizeInPlace(); return v; }
  Vector Cross(const Vector& o) const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
};
inline Vector operator*(float s, const Vector& v) { return v * s; }
inline float DotProduct(const Vector& a, const Vector& b) { return a.Dot(b); }
inline Vector CrossProduct(const Vector& a, const Vector& b) { return a.Cross(b); }
inline float VectorNormalize(Vector& v) { return v.NormalizeInPlace(); }
inline Vector VectorLerp(const Vector& a, const Vector& b, float t) { return a + (b - a) * t; }
inline void VectorMA(const Vector& start, float scale, const Vector& dir, Vector& dest) { dest = start + dir * scale; }
inline Vector VecMin(const Vector& a, const Vector& b) { return { std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) }; }
inline Vector VecMax(const Vector& a, const Vector& b) { return { std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) }; }
inline const Vector vec3_origin(0, 0, 0);

//-----------------------------------------------------------------------------
struct QAngle {
  float x = 0, y = 0, z = 0; // pitch, yaw, roll
  QAngle() = default;
  constexpr QAngle(float p, float y_, float r) : x(p), y(y_), z(r) {}
  float& operator[](int i) { return (&x)[i]; }
  float operator[](int i) const { return (&x)[i]; }
  QAngle operator+(const QAngle& o) const { return { x + o.x, y + o.y, z + o.z }; }
  QAngle operator-(const QAngle& o) const { return { x - o.x, y - o.y, z - o.z }; }
  QAngle operator*(float s) const { return { x * s, y * s, z * s }; }
  QAngle& operator+=(const QAngle& o) { x += o.x; y += o.y; z += o.z; return *this; }
  QAngle& operator-=(const QAngle& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  QAngle& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
  void Init(float p = 0, float yy = 0, float r = 0) { x = p; y = yy; z = r; }
  float Length() const { return std::sqrt(x * x + y * y + z * z); }
};
enum { PITCH = 0, YAW = 1, ROLL = 2 };

inline float AngleNormalize(float a) { a = std::fmod(a, 360.f); if (a > 180.f) a -= 360.f; if (a < -180.f) a += 360.f; return a; }
inline float AngleNormalizePositive(float a) { a = std::fmod(a, 360.f); if (a < 0) a += 360.f; return a; }
inline float AngleDiff(float dest, float src) { return AngleNormalize(dest - src); }
inline float ApproachAngle(float target, float value, float speed) {
  float delta = AngleDiff(target, value); if (speed < 0) speed = -speed;
  if (delta > speed) value += speed; else if (delta < -speed) value -= speed; else value = target;
  return AngleNormalize(value);
}
inline void NormalizeAngles(QAngle& a) { a.x = AngleNormalize(a.x); a.y = AngleNormalize(a.y); a.z = AngleNormalize(a.z); }
inline void ClampAngles(QAngle& a) { a.x = Clamp(AngleNormalize(a.x), -89.f, 89.f); a.y = AngleNormalize(a.y); a.z = 0; }

inline void AngleVectors(const QAngle& a, Vector* fwd, Vector* right = nullptr, Vector* up = nullptr) {
  float sp = std::sin(DEG2RADF(a.x)), cp = std::cos(DEG2RADF(a.x));
  float sy = std::sin(DEG2RADF(a.y)), cy = std::cos(DEG2RADF(a.y));
  float sr = std::sin(DEG2RADF(a.z)), cr = std::cos(DEG2RADF(a.z));
  if (fwd) *fwd = { cp * cy, cp * sy, -sp };
  if (right) *right = { -1 * sr * sp * cy + -1 * cr * -sy, -1 * sr * sp * sy + -1 * cr * cy, -1 * sr * cp };
  if (up) *up = { cr * sp * cy + -sr * -sy, cr * sp * sy + -sr * cy, cr * cp };
}
inline Vector AngleForward(const QAngle& a) { Vector f; AngleVectors(a, &f); return f; }
inline void VectorAngles(const Vector& fwd, QAngle& out) {
  float yaw, pitch;
  if (fwd.y == 0 && fwd.x == 0) { yaw = 0; pitch = fwd.z > 0 ? 270.f : 90.f; }
  else {
    yaw = RAD2DEGF(std::atan2(fwd.y, fwd.x)); if (yaw < 0) yaw += 360.f;
    float tmp = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y);
    pitch = RAD2DEGF(std::atan2(-fwd.z, tmp)); if (pitch < 0) pitch += 360.f;
  }
  out.x = AngleNormalize(pitch); out.y = AngleNormalize(yaw); out.z = 0;
}
inline QAngle CalcAngle(const Vector& from, const Vector& to) { QAngle a; VectorAngles(to - from, a); return a; }
inline float GetFov(const QAngle& view, const QAngle& aim) {
  Vector a = AngleForward(view), b = AngleForward(aim);
  return RAD2DEGF(std::acos(Clamp(a.Dot(b), -1.f, 1.f)));
}

//-----------------------------------------------------------------------------
// 3x4 matrix (rotation + translation in column 3), same layout as Source
struct matrix3x4_t {
  float m[3][4];
  matrix3x4_t() { Identity(); }
  float* operator[](int i) { return m[i]; }
  const float* operator[](int i) const { return m[i]; }
  void Identity() { std::memset(m, 0, sizeof(m)); m[0][0] = m[1][1] = m[2][2] = 1.f; }
  Vector GetOrigin() const { return { m[0][3], m[1][3], m[2][3] }; }
  void SetOrigin(const Vector& v) { m[0][3] = v.x; m[1][3] = v.y; m[2][3] = v.z; }
  Vector Column(int c) const { return { m[0][c], m[1][c], m[2][c] }; }
  void SetColumn(int c, const Vector& v) { m[0][c] = v.x; m[1][c] = v.y; m[2][c] = v.z; }
};
inline Vector VectorTransform(const Vector& v, const matrix3x4_t& m) {
  return { v.x * m[0][0] + v.y * m[0][1] + v.z * m[0][2] + m[0][3],
           v.x * m[1][0] + v.y * m[1][1] + v.z * m[1][2] + m[1][3],
           v.x * m[2][0] + v.y * m[2][1] + v.z * m[2][2] + m[2][3] };
}
inline Vector VectorRotate(const Vector& v, const matrix3x4_t& m) {
  return { v.x * m[0][0] + v.y * m[0][1] + v.z * m[0][2],
           v.x * m[1][0] + v.y * m[1][1] + v.z * m[1][2],
           v.x * m[2][0] + v.y * m[2][1] + v.z * m[2][2] };
}
inline Vector VectorIRotate(const Vector& v, const matrix3x4_t& m) {
  return { v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0],
           v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1],
           v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2] };
}
inline Vector VectorITransform(const Vector& v, const matrix3x4_t& m) { return VectorIRotate(v - m.GetOrigin(), m); }
inline void ConcatTransforms(const matrix3x4_t& a, const matrix3x4_t& b, matrix3x4_t& out) {
  matrix3x4_t r;
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 4; j++)
      r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + (j == 3 ? a[i][3] : 0.f);
  }
  out = r;
}
inline void MatrixInvert(const matrix3x4_t& in, matrix3x4_t& out) { // orthonormal rotation (with uniform scale handled loosely)
  matrix3x4_t r;
  for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r[i][j] = in[j][i];
  Vector t = in.GetOrigin();
  r[0][3] = -(t.x * r[0][0] + t.y * r[0][1] + t.z * r[0][2]);
  r[1][3] = -(t.x * r[1][0] + t.y * r[1][1] + t.z * r[1][2]);
  r[2][3] = -(t.x * r[2][0] + t.y * r[2][1] + t.z * r[2][2]);
  out = r;
}
inline void AngleMatrix(const QAngle& a, matrix3x4_t& m) {
  float sp = std::sin(DEG2RADF(a.x)), cp = std::cos(DEG2RADF(a.x));
  float sy = std::sin(DEG2RADF(a.y)), cy = std::cos(DEG2RADF(a.y));
  float sr = std::sin(DEG2RADF(a.z)), cr = std::cos(DEG2RADF(a.z));
  m[0][0] = cp * cy; m[1][0] = cp * sy; m[2][0] = -sp;
  float crcy = cr * cy, crsy = cr * sy, srcy = sr * cy, srsy = sr * sy;
  m[0][1] = sp * srcy - crsy; m[1][1] = sp * srsy + crcy; m[2][1] = sr * cp;
  m[0][2] = sp * crcy + srsy; m[1][2] = sp * crsy - srcy; m[2][2] = cr * cp;
  m[0][3] = m[1][3] = m[2][3] = 0;
}
inline void AngleMatrix(const QAngle& a, const Vector& pos, matrix3x4_t& m) { AngleMatrix(a, m); m.SetOrigin(pos); }

// quaternion (x,y,z,w)
struct Quat { float x = 0, y = 0, z = 0, w = 1; };
inline Quat QuatMul(const Quat& a, const Quat& b) {
  return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
           a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
inline Quat AxisAngleQuat(const Vector& axis, float deg) {
  float h = DEG2RADF(deg) * 0.5f, s = std::sin(h); Vector n = axis.Normalized();
  return { n.x * s, n.y * s, n.z * s, std::cos(h) };
}
inline Quat QuatSlerp(const Quat& a, Quat b, float t) {
  float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  if (d < 0) { d = -d; b = { -b.x, -b.y, -b.z, -b.w }; }
  float k0, k1;
  if (d > 0.9995f) { k0 = 1 - t; k1 = t; }
  else { float th = std::acos(d), s = std::sin(th); k0 = std::sin((1 - t) * th) / s; k1 = std::sin(t * th) / s; }
  Quat r{ a.x * k0 + b.x * k1, a.y * k0 + b.y * k1, a.z * k0 + b.z * k1, a.w * k0 + b.w * k1 };
  float l = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w); r.x /= l; r.y /= l; r.z /= l; r.w /= l; return r;
}
inline void QuaternionMatrix(const Quat& q, const Vector& pos, const Vector& scale, matrix3x4_t& m) {
  float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
  m[0][0] = (1 - 2 * (yy + zz)) * scale.x; m[0][1] = 2 * (xy - wz) * scale.y; m[0][2] = 2 * (xz + wy) * scale.z;
  m[1][0] = 2 * (xy + wz) * scale.x; m[1][1] = (1 - 2 * (xx + zz)) * scale.y; m[1][2] = 2 * (yz - wx) * scale.z;
  m[2][0] = 2 * (xz - wy) * scale.x; m[2][1] = 2 * (yz + wx) * scale.y; m[2][2] = (1 - 2 * (xx + yy)) * scale.z;
  m.SetOrigin(pos);
}

//-----------------------------------------------------------------------------
// Ray / shape helpers
inline bool IntersectRayWithAABB(const Vector& o, const Vector& d, const Vector& mins, const Vector& maxs, float& tmin, float& tmax) {
  tmin = 0; tmax = 1;
  for (int i = 0; i < 3; i++) {
    if (std::fabs(d[i]) < 1e-8f) { if (o[i] < mins[i] || o[i] > maxs[i]) return false; continue; }
    float inv = 1.f / d[i], t1 = (mins[i] - o[i]) * inv, t2 = (maxs[i] - o[i]) * inv;
    if (t1 > t2) std::swap(t1, t2);
    tmin = std::max(tmin, t1); tmax = std::min(tmax, t2);
    if (tmin > tmax) return false;
  }
  return true;
}
// segment (o, o+d, t in [0,1]) against capsule (a,b,r); returns entry fraction
inline bool IntersectSegmentCapsule(const Vector& o, const Vector& d, const Vector& a, const Vector& b, float r, float& tHit) {
  // closest approach between segment and capsule axis, then sphere test refinement
  Vector ab = b - a, ao = o - a;
  float abab = ab.Dot(ab), abd = ab.Dot(d), abao = ab.Dot(ao), dd = d.Dot(d), dao = d.Dot(ao), aoao = ao.Dot(ao);
  float best = 2.f;
  // cylinder part
  float A = abab * dd - abd * abd, B = abab * dao - abao * abd, C = abab * aoao - abao * abao - r * r * abab;
  if (std::fabs(A) > 1e-9f) {
    float disc = B * B - A * C;
    if (disc >= 0) {
      float t = (-B - std::sqrt(disc)) / A;
      float s = abao + t * abd;
      if (t >= 0 && t <= 1 && s >= 0 && s <= abab) best = t;
    }
  }
  // end spheres
  auto sphere = [&](const Vector& c) {
    Vector oc = o - c; float bq = oc.Dot(d), cq = oc.Dot(oc) - r * r; float disc = bq * bq - dd * cq;
    if (disc < 0) return; float t = (-bq - std::sqrt(disc)) / dd;
    if (t < 0 && cq < 0) t = 0; // inside
    if (t >= 0 && t <= 1 && t < best) best = t;
  };
  sphere(a); sphere(b);
  if (best <= 1.f) { tHit = best; return true; }
  return false;
}
inline float DistancePointSegment(const Vector& p, const Vector& a, const Vector& b) {
  Vector ab = b - a; float t = ab.LengthSqr() > 0 ? Clamp((p - a).Dot(ab) / ab.LengthSqr(), 0.f, 1.f) : 0.f;
  return (a + ab * t - p).Length();
}

// convenience helpers
inline QAngle VectorAngles(const Vector& fwd) { QAngle a; VectorAngles(fwd, a); return a; }
inline float FLerp(float a, float b, float t) { return a + (b - a) * t; }
inline float LerpAngle(float a, float b, float t) { return AngleNormalize(a + AngleDiff(b, a) * t); }
