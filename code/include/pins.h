#ifndef PINS_H
#define PINS_H

#include "pico/stdlib.h"

// CAN controllers
#define CAN_SPI_SCK 2
#define CAN_SPI_TX 3
#define CAN_SPI_RX 4

uint8_t can_spi_cs_pins[6] = {5,6,7,8,9,10};
uint8_t can_int_pins[6] = {29,25,23,21,19,17};
uint8_t can_rx_int_pins[6] = {28,24,22,20,18,16};

// External UART
#define EXT_UART_TX 0
#define EXT_UART_RX 1

// External SPI
#define EXT_SPI_TX 11
#define EXT_SPI_RX 12
#define EXT_SPI_CS 13
#define EXT_SPI_SCK 14

// External GPIO
#define EXT_GPIO_15 15
#define EXT_GPIO_26 26
#define EXT_GPIO_27 27

#endif // PINS_H