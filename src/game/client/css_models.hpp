// css_models.hpp - Counter-Strike: Source player models, weapon models and view models (when CS:S content is installed)
//
// Player models: ct_urban/gsg9/sas/gign, t_phoenix/leet/arctic/guerilla, animated from the CS:S animation libraries
// (cs_player_shared.mdl + ak_anims_*.mdl): 9-way movement blend (move_x/move_y), upper body weapon layer, aim
// matrix (body_yaw/body_pitch), shoot / reload / jump / death. Carried weapons are bone-merged w_ models.
// View models: v_*.mdl (with the CS:S hands), sequences picked by activity (ACT_VM_DRAW, ACT_VM_PRIMARYATTACK ...).
// BuildPlayerPose (cs_playeranimstate.hpp) is implemented here: the gameplay hitboxes come from the model's hitbox set.
#pragma once
#include "../shared/cs_playeranimstate.hpp"

class CCSPlayer;
class C_WeaponCSBase;
struct CViewSetup;
struct StudioDrawParams;

bool CSS_ModelsActive();
// agents (player models): cl_agent_ct / cl_agent_t pick the local player's model
const char* CSS_AgentName(int team, int idx);
bool CSS_DrawAgent(int team, int agent, const PlayerPoseInput& in, const StudioDrawParams& p);
// any static CS:S model (planted C4 ...), false when not available
bool CSS_DrawModelFile(const char* path, const matrix3x4_t& xf, const StudioDrawParams& p);
// draw a pose built by BuildPlayerPose (returns false when the pose has no CS:S model)
bool CSS_DrawPose(const PlayerPoseOutput& pose, const StudioDrawParams& p, bool drawWeapon);
// CS:S view model; false when not available (falls back to CS 1.6 / built-in)
bool CSS_DrawViewModel(const CViewSetup& view, CCSPlayer* p, C_WeaponCSBase* w, float bobTime, float bob, const QAngle& sway, float landKick, Vector& muzzleOut);
// dropped weapons / projectiles: CS:S world model (w_*.mdl), false when not available
bool CSS_DrawWorldWeapon(int weaponId, const matrix3x4_t& xf, const StudioDrawParams& p, bool thrown);
