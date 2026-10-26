// vgui_images.hpp - UI images from the Counter-Strike: Source content (materials/vgui/...): buy menu weapon pictures,
// agent portraits, map thumbnails. Loaded on first use and cached; nullptr when the content is not installed.
#pragma once
#include "raylib.h"
#include <string>

const Texture2D* VGUI_Image(const std::string& vtf);        // "materials/vgui/gfx/vgui/ak47.vtf"
const Texture2D* VGUI_WeaponImage(int weaponId);              // buy menu picture of a weapon (cropped)
const Texture2D* VGUI_WeaponIcon(int weaponId);               // white silhouette of it (kill feed, HUD), tint when drawing
const Texture2D* VGUI_ItemImage(const char* name);            // "kevlar", "kevlar_helmet", "defuser", "bullet" ...
const Texture2D* VGUI_SvgIcon(const char* name);              // CS:GO icon "weapon_c4", "item_defuser" ... (downloaded extras)
const Texture2D* VGUI_FileTexture(const std::string& path);   // plain png shipped with the game ("assets/hud/hp_plate.png")
// draw keeping the aspect ratio, centered in the box
void VGUI_DrawImageFit(const Texture2D& t, float x, float y, float w, float h, Color tint = WHITE);
void VGUI_ClearImages();
