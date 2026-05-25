@echo off
setlocal EnableDelayedExpansion

REM ---------------------------------------------------------------------------
REM Flash the exported Arduino build/ output to an ESP32-S3 Super Mini.
REM
REM Usage:
REM   1. In Arduino IDE: Sketch -> Export Compiled Binary
REM      (this populates esp32s3-supermini-led-buddy\build\<fqbn>\*.bin)
REM   2. Double-click this file, enter the COM port when asked.
REM
REM Optional: pass the port as an argument to skip the prompt:
REM   flash.bat COM5
REM ---------------------------------------------------------------------------

set "SCRIPT_DIR=%~dp0"
set "SKETCH=esp32s3-supermini-led-buddy"
set "BUILD_DIR=%SCRIPT_DIR%%SKETCH%\build"
set "BAUD=921600"

REM --- COM port ----------------------------------------------------------------
set "PORT=%~1"
if "%PORT%"=="" (
  set /p PORT="Enter COM port (e.g. COM5): "
)
if "%PORT%"=="" (
  echo [error] No COM port specified.
  goto :fail
)

REM --- Locate the FQBN subfolder under build\ ---------------------------------
if not exist "%BUILD_DIR%" (
  echo [error] Build folder not found: %BUILD_DIR%
  echo         Run Sketch -^> Export Compiled Binary in Arduino IDE first.
  goto :fail
)

set "FQBN_DIR="
for /d %%D in ("%BUILD_DIR%\*") do set "FQBN_DIR=%%D"
if "%FQBN_DIR%"=="" (
  echo [error] No FQBN subfolder found under %BUILD_DIR%
  goto :fail
)

set "APP=%FQBN_DIR%\%SKETCH%.ino.bin"
set "BOOT=%FQBN_DIR%\%SKETCH%.ino.bootloader.bin"
set "PART=%FQBN_DIR%\%SKETCH%.ino.partitions.bin"
set "MERGED=%FQBN_DIR%\%SKETCH%.ino.merged.bin"

REM --- Locate esptool from the Arduino ESP32 core, or fall back to PATH -------
set "ESPTOOL="
for /f "delims=" %%F in ('dir /b /s "%LOCALAPPDATA%\Arduino15\packages\esp32\tools\esptool_py\*\esptool.exe" 2^>nul') do set "ESPTOOL=%%F"
if "%ESPTOOL%"=="" (
  where esptool.exe >nul 2>nul && set "ESPTOOL=esptool.exe"
)
if "%ESPTOOL%"=="" (
  where esptool >nul 2>nul && set "ESPTOOL=esptool"
)
if "%ESPTOOL%"=="" (
  echo [error] Could not find esptool. Install the Arduino ESP32 core,
  echo         or `pip install esptool`, or put esptool on PATH.
  goto :fail
)

echo.
echo Port:    %PORT%
echo Build:   %FQBN_DIR%
echo esptool: %ESPTOOL%
echo.

REM --- Prefer merged.bin (single image at 0x0) when present -------------------
if exist "%MERGED%" (
  echo Flashing merged image...
  "%ESPTOOL%" --chip esp32s3 --port %PORT% --baud %BAUD% write_flash 0x0 "%MERGED%"
  if errorlevel 1 goto :fail
  goto :ok
)

REM --- Otherwise flash the individual partitions ------------------------------
if not exist "%APP%"  ( echo [error] Missing %APP%  & goto :fail )
if not exist "%BOOT%" ( echo [error] Missing %BOOT% & goto :fail )
if not exist "%PART%" ( echo [error] Missing %PART% & goto :fail )

REM boot_app0.bin lives in the ESP32 core install; pick whichever version exists
set "BOOT_APP0="
for /f "delims=" %%F in ('dir /b /s "%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\*\tools\partitions\boot_app0.bin" 2^>nul') do set "BOOT_APP0=%%F"
if "%BOOT_APP0%"=="" (
  echo [error] Could not locate boot_app0.bin in the ESP32 core install.
  goto :fail
)

echo Flashing bootloader + partitions + app...
"%ESPTOOL%" --chip esp32s3 --port %PORT% --baud %BAUD% write_flash ^
  0x0     "%BOOT%" ^
  0x8000  "%PART%" ^
  0xe000  "%BOOT_APP0%" ^
  0x10000 "%APP%"
if errorlevel 1 goto :fail

:ok
echo.
echo [ok] Flash complete.
pause
endlocal
exit /b 0

:fail
echo.
echo [fail] Flash aborted.
pause
endlocal
exit /b 1
