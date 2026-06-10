@echo off
setlocal EnableExtensions

if "%~4"=="" (
  echo Usage: msvc_build.cmd ROOT OUT_DIR TARGET ARCH
  echo TARGET is testTransforms or kanzi
  exit /b 2
)

set "ROOT=%~f1"
set "OUT_DIR=%~f2"
set "TARGET=%~3"
set "ARCH=%~4"
set "OBJ_DIR=%OUT_DIR%\obj-%TARGET%-%ARCH%"

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if "%VSINSTALL%"=="" (
  echo Visual Studio installation not found
  exit /b 1
)

call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=%ARCH% -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
if not exist "%OBJ_DIR%" mkdir "%OBJ_DIR%"

pushd "%ROOT%"
if errorlevel 1 exit /b %errorlevel%

git ls-files "src/*.cpp" "src/*/*.cpp" | findstr /v /i /c:"src/test/" /c:"src/app/" > "%OUT_DIR%\kanzi_lib_sources.rsp"
if errorlevel 1 exit /b %errorlevel%

git ls-files "src/*.cpp" "src/*/*.cpp" | findstr /v /i /c:"src/test/" > "%OUT_DIR%\kanzi_cli_sources.rsp"
if errorlevel 1 exit /b %errorlevel%

if /i "%TARGET%"=="testTransforms" goto build_testTransforms
if /i "%TARGET%"=="kanzi" goto build_kanzi

popd
echo Unknown target: %TARGET%
exit /b 2

:build_testTransforms
(
  echo src\test\TestTransforms.cpp
  type "%OUT_DIR%\kanzi_lib_sources.rsp"
) > "%OUT_DIR%\test_transform_sources.rsp"
cl /nologo /EHsc /MT /O2 /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /DTestTransforms_main=main /GR- /std:c++17 /Zc:__cplusplus /I "%ROOT%\src" /Fo"%OBJ_DIR%\\" /Fe"%OUT_DIR%\testTransforms.exe" @"%OUT_DIR%\test_transform_sources.rsp"
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%

:build_kanzi
cl /nologo /EHsc /MT /O2 /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /GR- /std:c++17 /Zc:__cplusplus /I "%ROOT%\src" /Fo"%OBJ_DIR%\\" /Fe"%OUT_DIR%\kanzi.exe" @"%OUT_DIR%\kanzi_cli_sources.rsp"
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%
