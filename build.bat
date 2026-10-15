@echo off
rem Builds the Custom Camo GML plugin and the camo array container it ships.
rem   build.bat            build into build\
rem   build.bat install    also copy it into the game (refuses while the game runs)
rem
rem Input: Assets\camo.png / .jpg / .webp - a seamless camo tile (any size; scaled to 2048x2048).
rem Steps: the picture -> the game's slice format (Utils\UAssetParser camoprep); the game's camo array
rem package (/Game/Characters/Common/Camo, extracted with retoc) -> the same package with our slice
rem added as slice 23 (camoarray add) -> a Zen container in build\Paks (retoc to-zen) -> the plugin,
rem with the 512x512 swatch built in. Needs the .NET 10 SDK, Visual Studio 2022 and the GML SDK 2.2+.
setlocal
set ROOT=%~dp0
set OUT=%ROOT%build
set TOOLS=%ROOT%..\Utils
set PARSER=%TOOLS%\UAssetParser\bin\Debug\net10.0\UAssetParser.dll
set RETOC=%TOOLS%\retoc\retoc.exe
set SDK=%ROOT%external\gml\include
if not exist "%SDK%\GML\GML.hpp" set SDK=%ROOT%..\ModLoader\include
if defined GML_SDK set SDK=%GML_SDK%
set GAME=C:\Program Files (x86)\Steam\steamapps\common\GERONIMO\Geronimo\Binaries\Win64
if defined GML_GAME_DIR set GAME=%GML_GAME_DIR%
set PAKS=%GAME%\..\..\Content\Paks

set CAMO=
for %%E in (png jpg jpeg webp) do if exist "%ROOT%Assets\camo.%%E" set CAMO=%ROOT%Assets\camo.%%E
if not defined CAMO (
  echo Put the camo picture in Assets\camo.png ^(or .jpg / .webp^).
  exit /b 1
)
if not exist "%PARSER%" (
  echo === UAssetParser
  dotnet build -v q "%TOOLS%\UAssetParser" || exit /b 1
)
for %%D in ("%OUT%\obj" "%OUT%\gen" "%OUT%\Paks" "%OUT%\CustomCamo" "%OUT%\tests\CustomCamoDev") do if not exist "%%~D" mkdir "%%~D"

echo === camo picture -^> 2048x2048 slice and swatch
dotnet "%PARSER%" camoprep "%CAMO%" "%OUT%\gen" || exit /b 1
if not exist "%OUT%\legacy\Geronimo\Content\Characters\Common\Camo.uasset" (
  echo === the game's camo array
  "%RETOC%" to-legacy "%PAKS%" "%OUT%\legacy" --version UE5_7 -f "Content/Characters/Common/Camo." || exit /b 1
)
echo === camo array + our slice
if exist "%OUT%\stage" rmdir /s /q "%OUT%\stage"
dotnet "%PARSER%" camoarray add "%OUT%\legacy\Geronimo\Content\Characters\Common\Camo.uasset" "%OUT%\gen\slice_2048.png" "%OUT%\stage" || exit /b 1
del /q "%OUT%\Paks\*" 2>nul
"%RETOC%" to-zen --version UE5_7 "%OUT%\stage" "%OUT%\Paks\zzz_CustomCamo_P.utoc" || exit /b 1

if not defined VCToolsInstallDir (
  call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)
echo === plugin
rem rc looks for the .rc's build\gen\ files in the current directory first: run it from this plugin's folder
pushd "%ROOT%" || exit /b 1
rc /nologo /I "%ROOT%." /fo"%OUT%\obj\CustomCamo.res" "%ROOT%src\CustomCamo.rc" || (popd & exit /b 1)
popd
cl /nologo /std:c++20 /O2 /MT /EHsc /W3 /Zi /DUNICODE /D_UNICODE /I"%SDK%" /LD /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\\" "%ROOT%src\CustomCamo.cpp" "%OUT%\obj\CustomCamo.res" /Fe"%OUT%\CustomCamo\CustomCamo.dll" /link /DEBUG /INCREMENTAL:NO || exit /b 1
echo === test harness (never installed)
cl /nologo /std:c++20 /O2 /MT /EHsc /W3 /Zi /DUNICODE /D_UNICODE /I"%SDK%" /LD /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\\" "%ROOT%tests\CustomCamoDev\CustomCamoDev.cpp" /Fe"%OUT%\tests\CustomCamoDev\CustomCamoDev.dll" /link /DEBUG /INCREMENTAL:NO || exit /b 1
for /r "%OUT%" %%F in (*.exp *.lib *.ilk) do del /q "%%F"

if /i "%1"=="install" (
  tasklist /FI "IMAGENAME eq Geronimo-Win64-Shipping.exe" | find /I "Geronimo-Win64" >nul && (
    echo Geronimo is running - close it first.
    exit /b 1
  )
  echo === install to "%GAME%\GML\plugins\CustomCamo"
  if not exist "%GAME%\GML\plugins\CustomCamo\Paks" mkdir "%GAME%\GML\plugins\CustomCamo\Paks"
  copy /y "%OUT%\CustomCamo\CustomCamo.dll" "%GAME%\GML\plugins\CustomCamo\" >nul || exit /b 1
  copy /y "%OUT%\Paks\*" "%GAME%\GML\plugins\CustomCamo\Paks\" >nul || exit /b 1
)
echo === done
exit /b 0
