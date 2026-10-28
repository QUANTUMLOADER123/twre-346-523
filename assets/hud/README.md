# CS2 HUD artwork

PNG plates and icons used by the HUD (`src/game/client/hud.cpp`). They are the art files of the
reference HUD implementation **mioxive/scaleform-hud-cs2** (MIT), which re-creates the Counter-Strike 2
HUD in Panorama: `art/hp_plate.png`, `art/hp_red.png`, `art/armor_plate.png`, `art/ammo_plate.png`,
`art/hp_icon.png`, `art/armor_icon.png`, `art/helmet_icon.png`, `art/alive_ct.png`, `art/alive_t.png`,
`art/defuse_icon.png`. Copied unchanged.

Everything else the HUD shows (weapon silhouettes, the knife, grenades, the C4 icon) is the game's own
CS:GO icon set (`content/extras/csgo_icons/svg_normal/*.svg`, downloaded on first start) - the HUD never
draws a hand-made substitute while a real asset exists.
