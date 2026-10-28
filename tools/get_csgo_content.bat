@echo off
rem ---------------------------------------------------------------------------------------------
rem  Counter-Strike: HvH - downloads the REAL CS:GO content (agents, weapons, textures, sounds, maps)
rem
rem  Uses Valve's official SteamCMD with an ANONYMOUS login to install the free
rem  "Counter-Strike: Global Offensive Dedicated Server" (Steam app 740, about 2 GB) into content\csgo.
rem  No Steam account and no game purchase are needed. Nothing is redistributed by this project:
rem  everything is read straight out of pak01_dir.vpk at runtime.
rem ---------------------------------------------------------------------------------------------
setlocal
cd /d "%~dp0\.."
set "ROOT=%CD%"
set "STEAMCMD=%ROOT%\content\steamcmd"
set "TARGET=%ROOT%\content\csgo"

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

echo [2/2] Installing the CS:GO Dedicated Server content into "%TARGET%"...
echo       (about 2 GB - the first SteamCMD start updates itself, this can take a while)
rem SteamCMD sometimes quits after its self-update: retry a few times
for /L %%i in (1,1,3) do (
  if not exist "%TARGET%\pak01_dir.vpk" if not exist "%TARGET%\csgo\pak01_dir.vpk" (
    "%STEAMCMD%\steamcmd.exe" +force_install_dir "%TARGET%" +login anonymous +app_update 740 validate +quit
  )
)

if exist "%TARGET%\pak01_dir.vpk" goto done
if exist "%TARGET%\csgo\pak01_dir.vpk" goto done
echo.
echo The download did not finish. Run this script again ^(it resumes^).
pause
exit /b 0

:done
echo.
echo Done. Start Counter-Strike: HvH - the CS:GO content (agents, weapons, textures, maps) is used automatically.
pause
