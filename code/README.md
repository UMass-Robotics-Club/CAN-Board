# Firmware for CAN Controller
This firmware is build for the RP2040 using the [picoSDK](https://github.com/raspberrypi/pico-sdk) and uses the [CAN SDK](https://github.com/kentindell/canis-can-sdk) by [CANIS labs](https://canislabs.com/canpico/) to interface with the CAN controllers.

## Build Instructions
1. Make a build folder inside `code`
```
mkdir build
cd build
```
2. Create the make files thorough cmake
```
cmake ..
```
3. Build the firmware using make
```
make
```
4. The output files should be inside the `build` folder
