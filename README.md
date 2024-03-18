# RM Classic Microcontroller Interface

This repository offers a sample program for interfacing with [RM Classic](https://github.com/NaoNaoMe/RM-Classic) using an Arduino Uno. It provides the essential code needed for establishing communication between the Arduino Uno and RM Classic, aimed at monitoring and controlling internal variables.

## Usage
To get started, follow these steps:
1. Clone this repository to your local machine or download and extract the ZIP file.
2. Open the `rmDemo.ino` file in the Arduino IDE.
3. Upload the program to your Arduino Uno.

After uploading, the program is ready to begin communication with [RM Classic](https://github.com/NaoNaoMe/RM-Classic). Before proceeding, ensure you've opened the 'RMConfiguration--ArduinoUnoR3.rmxml' file found in this repository by navigating to File > Open > 'View File'. RM Classic should be configured with a baud rate of 9600bps, the password `0x0000FFFF`, and a 2-byte address. Clicking the 'Comm Open' button should display 'ArduinoUnoR3' as a confirmation of successful communication.

The 'RMConfiguration--ArduinoUnoR3.rmxml' file includes the 'testCount' symbol, the address of which may change due to code alterations or the compilation process. If necessary, use the 'readelf' command to find this symbol's address in the hex image. 'c++filt' may also be used for demangling. RM Classic is capable of importing symbol information from a '***.map' file, created from the 'readelf' output. To import this information, go to File > Open > 'Map File' in RM Classic.

## For Other Microcontrollers

It is possible to adapt the RM interface code for use with other microcontrollers. Detailed implementation guidance is provided within the `rm_bg()` function in `rmDemo.ino`.

## Contributing

Contributions are welcome! If you find a bug or have a feature request, please open an issue on GitHub.

## License

This project is licensed under the MIT License. See the LICENSE file for more information.
