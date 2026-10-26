@echo off
rem ---------------------------------------------------------------------------------------------
rem  Counter-Strike: HvH - downloads Counter-Strike: Source content (maps, models, textures, sounds)
rem
rem  Uses Valve's official SteamCMD with an ANONYMOUS login to install the free
rem  "Counter-Strike: Source Dedicated Server" (Steam app 232330, about 2 GB) into content\css.
rem  No Steam account and no game purchase are needed. Nothing is redistributed by this project.
rem ---------------------------------------------------------------------------------------------
setlocal
cd /d "%~dp0\.."
set "ROOT=%CD%"
set "STEAMCMD=%ROOT%\content\steamcmd"
set "TARGET=%ROOT%\content\css"

if not exist "%STEAMCMD%\steamcmd.exe" (
  echo [1/2] Downloading SteamCMD from Valve...
  mkdir "%STEAMCMD%" 2>nul
  powershell -NoProfile -ExecutionPolicy Bypass -Command "[Net.ServicePointManager]::SecurityProtocol='Tls12'; Invoke-WebRequest -UseBasicParsing -Uri 'https://steamcdn-a.akamaihd.net/client/installer/steamcmd.zip' -OutFile '%STEAMCMD%\steamcmd.zip'; Expand-Archive -Force '%STEAMCMD%\steamcmd.zip' '%STEAMCMD%'"
  if not exist "%STEAMCMD%\steamcmd.exe" (
    echo Could not download SteamCMD. Get it manually from https://developer.valvesoftware.com/wiki/SteamCMD
    echo and unpack steamcmd.exe into "%STEAMCMD%".
    pause
    exit /b 1
  )
)

echo [2/2] Installing Counter-Strike: Source Dedicated Server content into "%TARGET%"...
echo       (about 2 GB - the first SteamCMD start updates itself, this can take a while)
rem SteamCMD sometimes quits after its self-update: retry a few times
for /L %%i in (1,1,3) do (
  if not exist "%TARGET%\cstrike\cstrike_pak_dir.vpk" (
    "%STEAMCMD%\steamcmd.exe" +force_install_dir "%TARGET%" +login anonymous +app_update 232330 validate +quit
  )
)

if exist "%TARGET%\cstrike\cstrike_pak_dir.vpk" (
  echo.
  echo Done. Start Counter-Strike: HvH - CS:S maps, models, textures and sounds are used automatically.
) else (
  echo.
  echo The download did not finish. Run this script again ^(it resumes^).
)
pause
