#include <stdint.h>
#include "config.h"

typedef struct CANPacket {
    uint8_t controller;
    uint32_t id;
    uint8_t data_size;
    uint8_t data[64];
} CANPacket_t;

typedef struct CANTXPacket {
    uint64_t seq;
    uint8_t priority;
    CANPacket_t pkt;
} CANTXPacket_t;

typedef struct CANRXPacket {
    uint64_t timestamp;
    CANPacket_t pkt;
} CANRXPacket_t;

typedef struct CommandHeader {
    uint8_t command;
    uint16_t length;
} CommandHeader_t;

typedef struct ResponseHeader {
    uint8_t status;
    uint16_t length;
} ResponseHeader_t;

CANTXPacket_t tx_pkts[TRANSMIT_PACKET_SLOTS];
CANPacket_t rx_pkts[RECV_PACKET_SLOTS];

#define read_bytes(buff, len) fread(buff, len, 1, stdin);
#define write_bytes(buff, len) fwrite(buff, len, 1, stdin);
