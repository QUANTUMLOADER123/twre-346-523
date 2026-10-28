# Credits

## Libraries
* **raylib 6.2**, © Ramon Santamaria (raysan5) and contributors, zlib/libpng license: https://github.com/raysan5/raylib
  (includes GLFW, miniaudio, cgltf, stb_* and rlsw under their respective permissive licenses)

## Game content
* No Valve files are included in this repository. On first start the game downloads Counter-Strike: Source content
  from public GitHub mirrors (bouletmarc/css_maps_content, bouletmarc/css_content) or uses a local CS:S /
  CS:S Dedicated Server install (SteamCMD, app 232330). That content belongs to Valve Corporation.
* The earlier built-in CC0 models (Quaternius) and procedural maps were removed.
* The R8 Revolver model, textures and sounds (from Counter-Strike: Global Offensive, Valve Corporation) are downloaded
  at runtime from the TTT addon `weapon_ttt_csgo_r8revolver` in EntranceJew/gmod-addons into `content/extras/`.
  They are not part of this repository. The R8 view model arms come from the player's CS:S agent model.

## Fonts
* **DejaVu** fonts (`assets/fonts/`), Bitstream Vera / DejaVu license (see `assets/fonts/LICENSE_DEJAVU`).

## Additional third-party content
* **ARC9-GSR** (CS:GO weapon models for Garry's Mod), © Seulyy, used under GMod addon distribution.
  Commit 5e16cf9baa37348fc82ba0c824d842dbcb44ab18. Runtime download only.
  https://github.com/Seulyy/ARC9-GSR
* **csgo_memory_hacking_examples** (CS:GO SVG weapon icons), © atiksoftware, MIT license.
  Commit 4afd922fd7ae95c8a9b393d423944540182235f8. Runtime download only.
  https://github.com/atiksoftware/csgo_memory_hacking_examples
* **nanosvg**, © Mikko Mononen (memononen), zlib license.
  Commit 239e102. Included in `third_party/nanosvg/`.
  https://github.com/memononen/nanosvg

## Everything else
Code and UI were written for this project.
Counter-Strike is a trademark of Valve Corporation. This is a non-commercial fan project and is not affiliated with Valve.
