// soundemitter.hpp - CSoundEmitterSystem interface (implemented by the client sound system, no-op on dedicated)
#pragma once
#include "../../engine/mathlib.hpp"

enum SoundChannel { CHAN_AUTO = 0, CHAN_WEAPON, CHAN_VOICE, CHAN_ITEM, CHAN_BODY, CHAN_STATIC };
// name e.g. "Weapon_AK47.Single", "Player.StepSand", "Player.DamageHelmet", "c4.beep", "HEGrenade.Bounce"
void EmitSound(const char* name, const Vector& origin, int entindex = -1, float volume = 1.f, float pitch = 1.f);
void EmitSound2D(const char* name, float volume = 1.f, float pitch = 1.f); // UI / local
