# Installation guide
At first , Please install PlatformIO in VSCode.

1. Download the project folder and open it in VSCode using “Open Project”.
2. PlatformIO will automatically install all required libraries based on the platformio.ini file. You don’t need to install them manually.
3. Connect the device to your PC.
4. In the bottom-left corner of VSCode, open the PlatformIO menu and select Upload. This will build and flash the firmware to the device.

# Behavior of the program

Tactile switch: When pressed, a message will be shown via Serial communication.

SD card: If an SD card is inserted, a text file named hello will be written to it.

Full-color LED: Always stays on.

Slide switch:
  
  If switched to one side → Serial shows mode 1
  
  If switched to the other side → Serial shows mode 2
  
  If left in between → Serial shows unknown

# About the Sample Code

The sample code in this repository uses the following libraries:

- [FastLED](https://github.com/FastLED/FastLED) (License: MIT)
- SPI (Arduino standard library, License: LGPL-2.1)
- SD (Arduino standard library, License: LGPL-2.1)
