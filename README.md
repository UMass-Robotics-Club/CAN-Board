# CAN Board
![](./images/CAN%20Board%20Front.png)
![](./images/CAN%20Board%20Back.png)

Features:
* 6 CAN lines
* [RP2040](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf) microcontroller
* 16MB flash
* Many Interfaces
    * USB-C
    * UART
    * SPI
    * GPIO
* SWD debugging

This board has 6 independent CAN lines driven by 6 [MCP251863T-E/9PX](https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP251863-External-CAN-FD-Controller-with-Integrated-Transceiver-DS20006624.pdf) CAN controllers. Each CAN controller is interfaced with the central microcontroller through an internal SPI bus along with 2 interrupt pins for general purpose and RX interrupts. There is also a STBY pin for the CAN transceiver that is connected to the XSTBY pin on the CAN controller.

## RP2040 Pinout
| GPIO | Function |
|------|----------|
| 0 | UART TX | 
| 1 | UART RX |
| 2 | SPI0 SCK |
| 3 | SPI0 TX |
| 4 | SPI0 RX |
| 5 | SPI0 CS 1 |
| 6 | SPI0 CS 2 |
| 7 | SPI0 CS 3 |
| 8 | SPI0 CS 4 |
| 9 | SPI0 CS 5 |
| 10 | SPI0 CS 6 |
| 11 | SPI1 TX |
| 12 | SPI1 RX |
| 13 | SPI1 CS |
| 14 | SPI1 SCK |
| 15 | GPIO pin 1 |
| 16 | CAN6 RX INT |
| 17 | CAN6 INT |
| 18 | CAN5 RX INT |
| 19 | CAN5 INT |
| 20 | CAN4 RX INT |
| 21 | CAN4 INT |
| 22 | CAN3 RX INT |
| 23 | CAN3 INT |
| 24 | CAN2 RX INT |
| 25 | CAN2 INT |
| 26 | GPIO pin 2 |
| 27 | GPIO pin 3 |
| 28 | CAN1 RX INT |
| 29 | CAN1 INT |

Notes:
* `SPI0` is the internal SPI bus which connects to the CAN controllers with `SPI0 CS X` being the chip select for CAN controller X.
* `SPI1` is the SPI bus exposed over the SPI connector.
* `GPIO 15 (GPIO pin 1)` is attached to a mosfet and LED as an indicator and therefore should have pull down enabled if being used as an input.
* `GPIO 26 & 27 (GPIO pin 26 & 27)` can be used an analog inputs.
* `CANX INT` is a general purpose interrupt pin for CAN controller X that will trigger when any interrupt in the CAN controller is triggered. This pin is inverted (active LOW).
* `CANX RX INT` is configurable to trigger when CAN controller X's RX interrupt is triggered. As an interrupt, this pin is inverted (active LOW).

## Project Structure
* `CAD`: The kiCAD project (PCB/schematics)
    * `Components`: Imported components each in their own folder
    * `Fab`: All the fabrication files including BOM, gerber, and component placement
* `code`: Firmware for the microcontroller in a pretty standard C project structure
    * `include`: Header files
    * `src`: C/C++ files
* `images`: All the images