@echo off
setlocal
cd /d "%~dp0"
if not defined VCPKG_ROOT (
  echo Set VCPKG_ROOT to your bootstrapped vcpkg checkout.
  exit /b 1
)
where cl >nul 2>nul
if errorlevel 1 (
  echo Run this script from an x64 Native Tools Command Prompt for Visual Studio.
  exit /b 1
)
cmake --preset windows-release %*
if errorlevel 1 exit /b %errorlevel%
cmake --build --preset windows-release --parallel
if errorlevel 1 exit /b %errorlevel%
ctest --preset windows-release
if errorlevel 1 exit /b %errorlevel%
cmake --install build-vcpkg --prefix dist/windows
if errorlevel 1 exit /b %errorlevel%
