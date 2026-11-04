@echo off
setlocal
cd /d "%~dp0"

echo PETBOT USB installer
echo This updates the bootloader, OTA partition table and firmware.
echo Your NVS settings (including saved Wi-Fi credentials) are not erased.
echo.

where py >nul 2>nul
if errorlevel 1 (
  echo ERROR: Python Launcher was not found.
  echo Install Python 3 from https://www.python.org/downloads/windows/
  echo Then run this file again.
  pause
  exit /b 1
)

set "PORT=%~1"
if "%PORT%"=="" set /p PORT=Enter ESP32 COM port (example COM3): 
if "%PORT%"=="" (
  echo ERROR: No COM port entered.
  pause
  exit /b 1
)

echo.
echo Installing esptool...
py -3 -m pip install --user "esptool==4.8.1"
if errorlevel 1 (
  echo ERROR: Could not install esptool.
  pause
  exit /b 1
)

echo.
echo Flashing PETBOT to %PORT%...
py -3 -m esptool --chip esp32 --port "%PORT%" --baud 460800 write_flash ^
  --flash_mode dio --flash_freq 40m --flash_size 4MB ^
  0x1000 bootloader.bin ^
  0x8000 partitions.bin ^
  0xe000 boot_app0.bin ^
  0x10000 firmware.bin
if errorlevel 1 (
  echo.
  echo FLASH FAILED. Do not unplug while flashing is in progress.
  pause
  exit /b 1
)

echo.
echo PETBOT USB installation completed.
echo Reboot the board and check its Serial Monitor.
pause
