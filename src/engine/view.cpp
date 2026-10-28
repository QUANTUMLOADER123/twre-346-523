#include "view.hpp"
#include "rlgl.h"
#include "raymath.h"
#include <cstdint>

#ifdef CS_HEADLESS
extern "C" void swClear(uint32_t bitmask);
#elif defined(_WIN32)
extern "C" __declspec(dllimport) void __stdcall glClear(unsigned int mask);
#else
extern "C" void glClear(unsigned int mask);
#endif

Matrix BuildViewMatrix(const Vector& o, const QAngle& a) {
  Vector f, r, u; AngleVectors(a, &f, &r, &u);
  Matrix m{};
  m.m0 = r.x; m.m4 = r.y; m.m8 = r.z;  m.m12 = -r.Dot(o);
  m.m1 = u.x; m.m5 = u.y; m.m9 = u.z;  m.m13 = -u.Dot(o);
  m.m2 = -f.x; m.m6 = -f.y; m.m10 = -f.z; m.m14 = f.Dot(o);
  m.m3 = 0; m.m7 = 0; m.m11 = 0; m.m15 = 1;
  return m;
}
Matrix BuildProjection(float vfovDeg, float aspect, float zn, float zf) {
  return MatrixPerspective(DEG2RADF(vfovDeg), aspect, zn, zf);
}
float CViewSetup::VerticalFov(float hfov) const {
  return RAD2DEGF(2.f * std::atan(std::tan(DEG2RADF(hfov) * 0.5f) * 0.75f));
}
void CViewSetup::Compute() {
  AngleVectors(angles, &forward, &right, &up);
  view = BuildViewMatrix(origin, angles);
  proj = BuildProjection(VerticalFov(fov), (float)width / (float)std::max(1, height), zNear, zFar);
  viewProj = MatrixMultiply(view, proj);
}
bool CViewSetup::WorldToScreen(const Vector& p, Vector2D& out) const {
  Vector4 c = { p.x, p.y, p.z, 1 };
  const Matrix& m = viewProj;
  float cx = m.m0 * c.x + m.m4 * c.y + m.m8 * c.z + m.m12;
  float cy = m.m1 * c.x + m.m5 * c.y + m.m9 * c.z + m.m13;
  float cw = m.m3 * c.x + m.m7 * c.y + m.m11 * c.z + m.m15;
  if (cw < 0.01f) return false;
  float nx = cx / cw, ny = cy / cw;
  out.x = x + (nx * 0.5f + 0.5f) * width;
  out.y = y + (1.f - (ny * 0.5f + 0.5f)) * height;
  return true;
}
void Render_Begin3D(const Matrix& view, const Matrix& proj) {
  rlDrawRenderBatchActive();
  rlMatrixMode(RL_PROJECTION); rlPushMatrix(); rlLoadIdentity();
  rlMultMatrixf(MatrixToFloat(proj));
  rlMatrixMode(RL_MODELVIEW); rlLoadIdentity();
  rlMultMatrixf(MatrixToFloat(view));
  rlEnableDepthTest();
}
void Render_End3D() {
  rlDrawRenderBatchActive();
  rlMatrixMode(RL_PROJECTION); rlPopMatrix();
  rlMatrixMode(RL_MODELVIEW); rlLoadIdentity();
  rlDisableDepthTest();
}
void Render_ClearDepth() {
  rlDrawRenderBatchActive();
#ifdef CS_HEADLESS
  swClear(0x00000100);
#else
  glClear(0x00000100);
#endif
}
