@echo off
rem SHIV - flash the ready-made firmware to an M5Stack StickS3. No compiling, no Arduino libraries needed.
rem Uses the esptool.exe that the Arduino IDE already installed. Keeps the Wi-Fi/settings saved on the stick.
setlocal EnableExtensions
title SHIV flasher
cd /d "%~dp0"
echo.
echo   SHIV firmware flasher
echo   ---------------------
echo.

if not exist "SHIV.ino.bin" goto nofiles
if not exist "SHIV.ino.bootloader.bin" goto nofiles
if not exist "SHIV.ino.partitions.bin" goto nofiles
if not exist "boot_app0.bin" goto nofiles

set "ESPTOOL="
if exist "%~dp0esptool.exe" set "ESPTOOL=%~dp0esptool.exe"
if defined ESPTOOL goto found
for /f "delims=" %%F in ('dir /b /s "%LOCALAPPDATA%\Arduino15\packages\m5stack\tools\esptool_py\esptool.exe" 2^>nul') do set "ESPTOOL=%%F"
if defined ESPTOOL goto found
for /f "delims=" %%F in ('dir /b /s "%LOCALAPPDATA%\Arduino15\packages\esp32\tools\esptool_py\esptool.exe" 2^>nul') do set "ESPTOOL=%%F"
if defined ESPTOOL goto found
for /f "delims=" %%F in ('dir /b /s "%USERPROFILE%\.platformio\packages\tool-esptoolpy\esptool.exe" 2^>nul') do set "ESPTOOL=%%F"
if defined ESPTOOL goto found
goto notool

:found
echo   Using: %ESPTOOL%
echo.
set "PORTARG="
set /p "COMPORT=  COM port of the StickS3 (example COM5) - or just press Enter to auto-detect: "
if not "%COMPORT%"=="" set "PORTARG=--port %COMPORT%"
echo.
rem esptool 5 renamed its options (write-flash, --flash-mode ...); 4.x only knows the old underscore names.
set "NEWSYNTAX="
"%ESPTOOL%" version > "%TEMP%\shiv_esptool_ver.txt" 2>&1
findstr /r /c:"v[5-9]\." /c:"v[1-9][0-9]\." /c:"^[5-9]\." "%TEMP%\shiv_esptool_ver.txt" >nul 2>&1
if not errorlevel 1 set "NEWSYNTAX=1"
del "%TEMP%\shiv_esptool_ver.txt" >nul 2>&1
echo   Flashing... (about 20 seconds)
echo.
if defined NEWSYNTAX goto flashnew
"%ESPTOOL%" --chip esp32s3 %PORTARG% --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 8MB 0x0 "SHIV.ino.bootloader.bin" 0x8000 "SHIV.ino.partitions.bin" 0xe000 "boot_app0.bin" 0x10000 "SHIV.ino.bin"
if errorlevel 1 goto failed
goto flashed

:flashnew
"%ESPTOOL%" --chip esp32s3 %PORTARG% --baud 921600 --before default-reset --after hard-reset write-flash -z --flash-mode dio --flash-freq 80m --flash-size 8MB 0x0 "SHIV.ino.bootloader.bin" 0x8000 "SHIV.ino.partitions.bin" 0xe000 "boot_app0.bin" 0x10000 "SHIV.ino.bin"
if errorlevel 1 goto failed

:flashed
echo.
echo   DONE. The stick restarts into SHIV. Your saved Wi-Fi and settings were kept.
echo.
pause
exit /b 0

:failed
echo.
echo   FLASH FAILED.
echo    - Close the Arduino IDE serial monitor (it holds the COM port).
echo    - Long-press the StickS3 power key to enter download mode, then run this again.
echo    - Try typing the COM port instead of auto-detect (Device Manager - Ports).
echo.
pause
exit /b 1

:notool
echo   Could not find esptool.exe.
echo   It comes with the Arduino IDE's M5Stack (or esp32) board package. Either install that,
echo   or drop an esptool.exe next to this file (github.com/espressif/esptool/releases) and run again.
echo.
pause
exit /b 2

:nofiles
echo   The firmware .bin files are missing. Unzip the WHOLE folder first, then run flash.bat from inside it.
echo.
pause
exit /b 3
