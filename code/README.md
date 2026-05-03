# Firmware for CAN Controller
This firmware is made for the RP2040 using the [picoSDK](https://github.com/raspberrypi/pico-sdk) along with the [CAN SDK](https://github.com/kentindell/canis-can-sdk) by [CANIS labs](https://canislabs.com/canpico/) to interface with the CAN controllers.

## Configuration
### Logging
By default, the firmware will output all log messages over USB. You can change what level of messages get logged by changing what macros are defined right above `#include "logging.h"` in [main.c](./src/main.c). You can also select what these messages are sent over (USB/external UART) by modifying the bottom of [CMakeLists.txt](./CMakeLists.txt).

### SPI and CAN
You can change both the SPI BAUD rate and the CAN bitrate at the top of [main.c](./src/main.c) under the user params section. The predefined CAN bit rates are in [canapi.h](./include/canapi.h) as follows:
```c
CAN_BITRATE_500K_75 = 0,    // 500kbit/sec 75% sample (default)    
CAN_BITRATE_250K_75,        // 250kbit/sec 75% sample point 
CAN_BITRATE_125K_75,        // 125kbit/sec 75% sample point
CAN_BITRATE_1M_75,          // 1Mbit/sec 75% sample point
CAN_BITRATE_500K_50,        // 500kbit/sec 50% sample (default)
CAN_BITRATE_250K_50,        // 250kbit/sec 50% sample point 
CAN_BITRATE_125K_50,        // 125kbit/sec 50% sample point
CAN_BITRATE_1M_50,          // 1Mbit/sec 50% sample point
CAN_BITRATE_2M_50,          // 2Mbit/sec 50% sample point NON STANDARD
CAN_BITRATE_4M_90,          // 4Mbit/sec 50% sample point NON STANDARD
CAN_BITRATE_2_5M_75 ,       // 2.5Mbit/sec 75% sample point NON STANDARD
CAN_BITRATE_2M_80,          // 2Mbit/sec 80% sample point NON STANDARD
CAN_BITRATE_500K_875,       // 500kbit/sec 87.5% sample
CAN_BITRATE_250K_875,       // 250kbit/sec 87.5% sample point (J1939, CANOpen) 
CAN_BITRATE_125K_875,       // 125kbit/sec 87.5% sample point
CAN_BITRATE_1M_875,         // 1Mbit/sec 85.5% sample point
```

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

## Upload
```
sudo openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -c "program can_board.elf verify reset exit"
```

## debug
```
gdb ./build/can_board.elf -ex 'set arch arm' -ex 'target extended-remote :3333'
```


# Better Communication Protocol

## Overall Architecture
The protocol is half duplex (even if the underlying transport is full duplex) and is based on a host-peripheral command architecture. The host will send a command along with any needed data and the peripheral will respond with a status code along with any data to return. The host MUST wait to the peripheral to respond before sending another command. If the peripheral does not receive a packet in time then it will send a timeout packet back. All data and commands will be wrapped in a general packet structure defined below. 

### General Command Structure
Host -> Peripheral:
- Frame start (1 byte): 0xAA
- Command (1 byte): The command that the controller should execute
- Data size (2 bytes): The size of the data included
- Data (n bytes): The data for the command (command specific)
- Frame end (1 byte): 0x55

Peripheral -> Host:
- Frame start (1 byte): 0xAA
- Response Code (1 byte): Status of the command
- Data size (2 bytes): The size of the data included
- Data (n bytes): The data for the command (command specific)
- Frame end (1 byte): 0x55

### Specific Command Structure

#### (0x00) Get Board Status Command

#### (0x01) Get Controller Status Command

#### (0x02) Get Controller Time Command

#### (0x03) Setup CAN Controller Command

#### (0x04) Send CAN Frame Command



#### Send CAN Frame Command
Host -> Peripheral Data:
- Controller ID (1 byte): The ID of the controller to send the frame on
- Frame Options (1 byte): The options for the frame (e.g. extended ID, remote frame, etc.)
- Data Length Code (1 byte): The length of the data in bytes (0-8)
- Arbitration ID (2-4 bytes): The arbitration ID of the frame
- Frame Data (0-8 bytes): The data to be sent in the frame