@echo off
setlocal
set "BASE_DIR=%~dp0"
set "APP_DIR=%BASE_DIR%ASEappSurfaceBuilder-1.3.4"
set "APP_EXE=%APP_DIR%\ASEappSurfaceBuilder-1.3.4.exe"

if not exist "%APP_EXE%" (
  echo [ASEapp] Application executable not found:
  echo   "%APP_EXE%"
  echo.
  echo [ASEapp] Expected the extracted standalone folder next to this .cmd file.
  pause
  exit /b 1
)

start "" /D "%APP_DIR%" "%APP_EXE%"
exit /b 0
