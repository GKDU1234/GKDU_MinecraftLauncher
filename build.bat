@echo off
rem ============================================================
rem  GDML launcher one-click build script (pure-ASCII sources)
rem  Requires: Dev-Cpp bundled TDM-GCC 64-bit compiler
rem ============================================================
setlocal
set GCC=D:\Dev-Cpp\MinGW64\bin\g++.exe
set GCC_DIR=D:\Dev-Cpp\MinGW64\bin
set PLUGDIR=%APPDATA%\GDML\plugins

if not exist "%GCC%" (
    echo [Error] Compiler not found: %GCC%
    echo Please edit the GCC variable in this script to your g++.exe path
    pause
    exit /b 1
)

echo [1/4] Building GDML.exe ...
"%GCC%" -O2 -static -std=c++14 -mwindows -o GDML.exe main.cpp -lwinhttp -lole32 -lshell32
if errorlevel 1 (
    echo [Failed] Main program build error
    pause
    exit /b 1
)

echo [2/4] Building simple plugins ...
for %%p in (example_plugin game_time crash_collect auto_backup multi_account download_stats download_limiter server_join command_lines) do (
    "%GCC%" -O2 -std=c++14 -shared -o %%p.dll %%p.cpp
    if errorlevel 1 (
        echo [Failed] %%p plugin build error
        pause
        exit /b 1
    )
)

echo [3/4] Building special plugins ...
"%GCC%" -O2 -std=c++14 -shared -o game_sound.dll game_sound.cpp -lwinmm
if errorlevel 1 ( echo [Failed] game_sound & pause & exit /b 1 )
"%GCC%" -O2 -std=c++14 -shared -o schedule_check.dll schedule_check.cpp -lwinhttp
if errorlevel 1 ( echo [Failed] schedule_check & pause & exit /b 1 )
"%GCC%" -O2 -std=c++14 -shared -o skin_avatar.dll skin_avatar.cpp -lwinhttp -lgdiplus
if errorlevel 1 ( echo [Failed] skin_avatar & pause & exit /b 1 )
"%GCC%" -O2 -std=c++14 -shared -o icon_plugin.dll icon_plugin.cpp -lgdi32 -luser32
if errorlevel 1 ( echo [Failed] icon_plugin & pause & exit /b 1 )
echo Building cubiomes static library ...
for %%f in (generator biomenoise layers finders util noise biomes quadbase) do (
    "%GCC_DIR%\gcc.exe" -O2 -c -Icubiomes cubiomes\%%f.c -o cubiomes\%%f.o
    if errorlevel 1 ( echo [Failed] cubiomes\%%f & pause & exit /b 1 )
)
"%GCC_DIR%\ar.exe" rcs cubiomes\libcubiomes.a cubiomes\*.o
"%GCC%" -O2 -std=c++14 -shared -o seed_mapAfind.dll seed_mapAfind.cpp -Icubiomes cubiomes\libcubiomes.a -lgdi32 -luser32 -lshell32 -lcomctl32
if errorlevel 1 ( echo [Failed] seed_mapAfind & pause & exit /b 1 )

echo [4/4] Deploying plugins to %PLUGDIR% ...
if not exist "%PLUGDIR%" mkdir "%PLUGDIR%"
for %%p in (example_plugin game_time crash_collect auto_backup multi_account download_stats download_limiter server_join game_sound schedule_check skin_avatar icon_plugin command_lines seed_mapAfind) do (
    copy /Y %%p.dll "%PLUGDIR%\" >nul
)

echo.
echo Build complete: GDML.exe and 12 plugin DLLs deployed to %PLUGDIR%
pause
