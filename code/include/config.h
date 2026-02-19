///////////////////////////////////////
// User params
///////////////////////////////////////

// Speed for CAN controllers (TODO add individual speeds)
#define CAN_BITRATE CAN_BITRATE_1M_50
// Number of tries for each operation (read/write from CAN controller)
#define CAN_CONTROLLER_OPERATION_MAX_TRYS 1

// Timeout of protocol operations TODO

// Number of transmit packet slots
#define RECV_PACKET_SLOTS 64
#define TRANSMIT_PACKET_SLOTS 64