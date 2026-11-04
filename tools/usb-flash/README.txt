PETBOT USB INSTALLER (Windows)

1. Extract the entire ZIP into one folder.
2. Connect the PETBOT ESP32 board to the PC with USB.
3. Close Arduino Serial Monitor / PlatformIO Monitor if it is open.
4. Double-click flash.bat.
5. Enter the board's COM port (for example COM3).
6. Wait until the script says installation completed.

Python 3 is required. If it is missing, install it from:
https://www.python.org/downloads/windows/

The script installs esptool and flashes:
- bootloader at 0x1000
- partition table at 0x8000
- OTA boot data at 0xE000
- firmware at 0x10000

It deliberately does NOT erase flash and does NOT write the NVS area,
so saved Wi-Fi credentials and device settings are preserved.
Do not disconnect the board during flashing.
