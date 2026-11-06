@echo off
setlocal
cd /d "%~dp0"
if not defined VCPKG_ROOT (
  echo Set VCPKG_ROOT to your bootstrapped vcpkg checkout.
  exit /b 1
)
for %%T in (gcc g++ cmake ninja ctest) do (
  where %%T >nul 2>nul
  if errorlevel 1 (
    echo Missing %%T. Add the MSYS2 MINGW64 bin directory to PATH before opening VS Code.
    exit /b 1
  )
)
cmake --preset windows-release %*
if errorlevel 1 exit /b %errorlevel%
cmake --build --preset windows-release --parallel
if errorlevel 1 exit /b %errorlevel%
ctest --preset windows-release
if errorlevel 1 exit /b %errorlevel%
cmake --install build/windows-release --prefix dist/windows
if errorlevel 1 exit /b %errorlevel%
