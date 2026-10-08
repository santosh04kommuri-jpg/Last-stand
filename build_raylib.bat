@echo off
rem One-time script: builds raylib 6.0 static library WITH JPG support.
rem
rem Why: the prebuilt libraylib.a from the C:\raylib installer was compiled with
rem SUPPORT_FILEFORMAT_JPG=0 (raylib 6.0 default), so Sky.jpg fails to load with
rem "IMAGE: Data format not supported". This script rebuilds raylib from the
rem official source at C:\raylib\raylib\src (read-only - never modified), with
rem -DSUPPORT_FILEFORMAT_JPG=1, into build\raylib-src\libraylib_jpg.a.
rem
rem Only needed once - build.bat links against the resulting library.

setlocal

set "PATH=C:\raylib\w64devkit\bin;%PATH%"

if not exist "build" mkdir build

if not exist "build\raylib-src\Makefile" (
    echo Copying raylib source to build\raylib-src ...
    xcopy /E /I /Q /Y "C:\raylib\raylib\src" "build\raylib-src" >nul
)

echo Building libraylib_jpg.a with JPG support...
pushd build\raylib-src
make.exe PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC RAYLIB_RELEASE_PATH=. RAYLIB_LIB_NAME=raylib_jpg CUSTOM_CFLAGS="-DSUPPORT_FILEFORMAT_JPG=1"
set MAKERR=%ERRORLEVEL%
popd

if not "%MAKERR%"=="0" goto failed
if not exist "build\raylib-src\libraylib_jpg.a" goto failed

echo RAYLIB BUILD OK: build\raylib-src\libraylib_jpg.a
goto end

:failed
echo RAYLIB BUILD FAILED
exit /b 1

:end
endlocal