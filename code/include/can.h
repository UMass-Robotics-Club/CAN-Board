#ifndef CAN_H
#define CAN_H

#include "canapi.h"

#define NUM_CAN_CONTROLLERS 6
#define CAN_SETUP_MAX_RETRIES 5
#define CAN_TRANSMIT_MAX_TRYS 1

extern can_controller_t can_controllers[NUM_CAN_CONTROLLERS];

void setup_can_controllers(can_bitrate_t *bitrate);
can_errorcode_t send_can_frame(int controller, can_frame_t *frame, bool fifo);
int get_can_rx_events(int controller, uint8_t **pbuff);
int get_can_tx_events(int controller, uint8_t **pbuff);

#endif