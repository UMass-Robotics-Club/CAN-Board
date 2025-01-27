#ifndef __CAN_H
#define __CAN_H

#include "pico/stdlib.h"

enum can_type {
    STANDARD,
    EXTENDED
};

typedef struct can_packet
{
    can_type type;
    uint32_t id;
    uint8_t len;
    uint8_t data[];
} can_packet_t;

#endif