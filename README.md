[日本語READMEはこちら](./README.ja.md)

# Installation guide
At first , Please install PlatformIO in VSCode.

1. Download the project folder and open it in VSCode using “Open Project”.
2. PlatformIO will automatically install all required libraries based on the platformio.ini file. You don’t need to install them manually.
3. Connect the device to your PC.
4. In the bottom-left corner of VSCode, open the PlatformIO menu and select Upload. This will build and flash the firmware to the device.

# Behavior of the program

In the sample code, it operates as follows. Please refer to the comments in the sample code for details of the operation.

***Tactile switch*** : When pressed, a message will be shown via Serial communication.

***SD card*** : If an SD card is inserted, a text file named hello will be written to it.

***Full-color LED*** : Always stays on. The color changes over time.

Slide switch:

	•	If switched to one side → Serial shows mode 1
	•	If switched to the other side → Serial shows mode 2
	•	If left in between → Serial shows unknown

# About the Enclosure

This enclosure is specifically designed for the IoT Gateway Board.

Thanks to the snap-fit structure,  can be assembled without using screws.

It has also been confirmed that this enclosure can be manufactured with a 3D printer, so please try producing it with the 3D printer you have at home.

Note: We have confirmed that this enclosure can be manufactured without issues under the following conditions:

	•	3D Printer: Bmamulab X1 Carbon
	•	Nozzle: 0.4 mm
	•	Material: PLA

# About the Sample Code License

The sample code in this repository uses the following libraries:

- [FastLED](https://github.com/FastLED/FastLED) (License: MIT)
- SPI (Arduino standard library, License: LGPL-2.1)
- SD (Arduino standard library, License: LGPL-2.1)
