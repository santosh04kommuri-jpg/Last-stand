@echo off
rem Build script for Wave Survival Game
rem Toolchain: w64devkit GCC 15.2.0 with bundled raylib 6.0 from the C:\raylib installer
rem Run this from the project root: c:\Users\santo\2d survival game

setlocal

rem Make GCC find the MATCHING 64-bit binutils (as, ld, windres...) from w64devkit
rem instead of the 32-bit ones in C:\MinGW\bin. setlocal keeps this PATH change
rem local to this script - the Windows system PATH is never modified.
set "PATH=C:\raylib\w64devkit\bin;%PATH%"

set CXX=C:\raylib\w64devkit\bin\g++.exe
set RAYLIB_INC=C:\raylib\w64devkit\include
set RAYLIB_LIB=C:\raylib\w64devkit\lib

if not exist "src" goto no_src
if not exist "build\raylib-src\libraylib_jpg.a" goto no_raylib
if not exist "build" mkdir build

echo Building game.exe ...
"%CXX%" -std=c++17 -O2 -Wall -Wextra -I"%RAYLIB_INC%" src\main.cpp -L"build\raylib-src" -lraylib_jpg -lopengl32 -lgdi32 -lwinmm -o build\game.exe
if errorlevel 1 goto build_failed

echo BUILD OK: build\game.exe
goto end

:no_raylib
echo ERROR: build\raylib-src\libraylib_jpg.a not found.
echo Run build_raylib.bat first - it builds raylib once with JPG support.
exit /b 1

:no_src
echo ERROR: run this script from the project root - src folder not found.
exit /b 1

:build_failed
echo BUILD FAILED
exit /b 1

:end
endlocal
