@echo off
setlocal
set "VSLANG=1033"
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" (
  echo Install Visual Studio with the Desktop development with C++ workload.
  exit /b 1
)
for /f "usebackq delims=" %%I in (`call "%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"
if not defined VS_INSTALL (
  echo Visual Studio with the x64 C++ tools was not found.
  exit /b 1
)
call "%VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
set "PATH=%VS_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
%*
