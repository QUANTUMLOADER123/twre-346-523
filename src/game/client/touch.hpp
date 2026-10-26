// touch.hpp - on-screen controls for phones / tablets (Android build; touch_enable 1 shows them anywhere)
//
//   left half   : floating movement stick (analog, like a gamepad)
//   right half  : swipe to look around
//   buttons     : FIRE (hold + drag also aims), JUMP, DUCK, SCOPE, RELOAD, USE, next weapon, slots 1-5,
//                 BUY, TEAM, SCORE, MENU (= Esc / Back), AW (cheat menu)
// Buttons send the same "+jump" / "-jump" commands the keyboard binds send, so everything (cheat features,
// autostrafer, binds) works the same way.
#pragma once

void Touch_Frame(float dt);                 // read touches, press / release buttons, turn the view (in game only)
void Touch_GetMove(float& forward, float& side);   // analog stick, -1..1
bool Touch_Active();                        // controls are on screen and own the touch input this frame
void Touch_Draw();                          // overlay (after the HUD)
void Touch_ReleaseAll();
