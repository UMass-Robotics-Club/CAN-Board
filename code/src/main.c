#include <stdio.h>

#include "pico/stdlib.h"
#include "pins.h"
#include "canapi.h"

// enable different levels of logging
#define ENABLE_DEBUG
#define ENABLE_INFO
#define ENABLE_WARNING
#define ENABLE_ERROR

#include "logging.h"
#include "rev/CANSparkFrames.h"

///////////////////////////////////////
// User params
///////////////////////////////////////

#define EXT_SPI_BAUD 1000000
#define CAN_BITRATE CAN_BITRATE_500K_75


can_controller_t can_controllers[6];

/**
 * Handles IRQs by calling mcp25xxfd_irq_handler on the controller that triggered the interrupt.
 */
void TIME_CRITICAL can_irq_handler(void)
{
    for(uint8_t i = 0; i < 6; i++) {
        uint8_t spi_irq = can_controllers[i].host_interface.spi_irq;
        uint32_t events = gpio_get_irq_event_mask(spi_irq); 

        if (events & GPIO_IRQ_LEVEL_LOW) {
            mcp25xxfd_irq_handler(can_controllers + i);
        }
    }
}

/**
 * Binds the interfaces for the 6 CAN controllers and sets them up by calling can_setup_controller and adding an IRQ handler.
 */
void setup_can_controllers(can_bitrate_t *bitrate) {
    debug("CAN setup: Starting setup...\n")
    
    // add IRQ handler to call the IRQ handler on the correct CAN controller
    irq_add_shared_handler(IO_IRQ_BANK0, can_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);

    debug("CAN setup: IRQ handler added!\n")

    for(uint8_t i = 0; i < 6; i++){
        debug("CAN setup: Controller %d: Staring setup...\n", i);

        // bind interface
        can_controllers[i].host_interface = (can_interface_t){
            .spi_device = CAN_SPI,
            .spi_sck = CAN_SPI_SCK,
            .spi_tx = CAN_SPI_TX,
            .spi_rx = CAN_SPI_RX,
            .spi_cs = can_spi_cs_pins[i],
            .spi_irq = can_int_pins[i],
            .magic = 0x1e5515f0U,
        };

        // setup controller
        while (true) {
            can_errorcode_t rc = can_setup_controller(can_controllers + i, bitrate, CAN_NO_FILTERS, CAN_MODE_NORMAL, CAN_OPTIONS_NONE);
            if (rc == CAN_ERC_NO_ERROR) 
                break;

            // This can fail if the CAN transceiver isn't powered up properly. That might happen
            // if the board had 3.3V but not 5V (the transceiver needs 5V to operate). 
            error("CAN Setup: Controller %d: Failed to initialize with error code %d. Will retry in 1 second...\n", i, rc)
            sleep_ms(1000);
        }

        debug("CAN setup: Controller %d: Done setting up!\n", i+1)
    }

    debug("CAN setup: Done setting up!\n")
}

/**
 * Initializes the external spi
 */
void init_ext_spi() {
    debug("EXT SPI setup: Starting setup...\n")

    uint baud = spi_init(EXT_SPI, EXT_SPI_BAUD);
    spi_set_slave(EXT_SPI, true);

    gpio_set_function(EXT_SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(EXT_SPI_TX, GPIO_FUNC_SPI);
    gpio_set_function(EXT_SPI_RX, GPIO_FUNC_SPI);
    gpio_set_function(EXT_SPI_CS, GPIO_FUNC_SPI);

    debug("EXT SPI setup: Done setting up at baud %u!\n", baud)
}

typedef struct __attribute__((packed)) {
    uint8_t controller_num : 3;
    uint8_t data_len: 5; //should be larger to support max 64 bytes of CAN FD
} can_command_header_t;

void loop() {
    // get header
    can_command_header_t header;
    spi_read_blocking(EXT_SPI, 0, (uint8_t*)&header, sizeof(header));

    if(header.controller_num > 5){
        error("Command header: Invalid CAN controller number: %d", header.controller_num)
        return;
    }

    // get arbitration (29 bit)
    uint32_t arbitration;
    spi_read_blocking(EXT_SPI, 0, (uint8_t*)&arbitration, 4);
    // get CAN frame data
    uint8_t buffer[32];
    spi_read_blocking(EXT_SPI, 0, buffer, header.data_len);

    // Create a CAN frame
    can_frame_t my_tx_frame;
    can_make_frame(&my_tx_frame, true, arbitration, header.data_len, buffer, false);

    while(1){
        can_errorcode_t rc = can_send_frame(can_controllers + header.controller_num, &my_tx_frame, false);
        if (rc != CAN_ERC_NO_ERROR) {
            // This can happen if there is no room in the transmit queue, which can
            // happen if the CAN controller is connected to a CAN bus but there are no
            // other CAN controllers connected and able to ACK a CAN frame, so the
            // transmit queue fills up and then cannot accept any more frames.
            error("CAN send: Error on controller %d: %d,\n", header.controller_num, rc);
        }
        else {
            debug("CAN send: Frame queued OK on controller %d\n", header.controller_num);
            break;
        }
    }
}

int main() {
    stdio_init_all();
    init_ext_spi();
    setup_can_controllers(&(can_bitrate_t){.profile=CAN_BITRATE});

    info("CAN board ready!")

    while(1)
        loop();
}