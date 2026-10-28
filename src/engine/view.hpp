// view.hpp - CViewSetup and 3D render state helpers (custom projection, Source coordinates fed directly to rlgl)
#pragma once
#include "mathlib.hpp"
#include "raylib.h"

struct CViewSetup {
  Vector origin;
  QAngle angles;
  float fov = 90.f;          // horizontal fov at 4:3, like Source (Hor+ for widescreen)
  float fovViewmodel = 68.f;
  int x = 0, y = 0, width = 1280, height = 720;
  float zNear = 3.f, zFar = 16000.f;
  // derived
  Vector forward, right, up;
  Matrix view{}, proj{}, viewProj{};
  void Compute();                                      // update matrices from origin/angles/fov
  bool WorldToScreen(const Vector& p, Vector2D& out) const; // false if behind camera
  float VerticalFov(float hfov) const;
};

Matrix BuildViewMatrix(const Vector& origin, const QAngle& angles);
Matrix BuildProjection(float vfovDeg, float aspect, float zn, float zf);

void Render_Begin3D(const Matrix& view, const Matrix& proj);
void Render_End3D();
void Render_ClearDepth();
inline CViewSetup g_CurrentView;
