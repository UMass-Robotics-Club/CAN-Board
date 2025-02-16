#include <stdio.h>
#include "pico/stdlib.h"

#include "canapi.h"
#include "pins.h"

#define EXT_SPI_BAUD 1000000
#define CAN_BITRATE CAN_BITRATE_500K_75

// enable different levels of logging
#define ENABLE_DEBUG
#define ENABLE_INFO
#define ENABLE_WARNING
#define ENABLE_ERROR

#include "logging.h"

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
    debug("Setting up CAN controllers...\n")
    
    // add IRQ handler to call the IRQ handler on the correct CAN controller
    irq_add_shared_handler(IO_IRQ_BANK0, can_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);

    debug("CAN IRQ handler added\n")

    for(uint8_t i = 0; i < 6; i++){
        debug("Setting up CAN controller %d...\n", i+1);

        // bind interface
        can_controllers[i].host_interface = (can_interface_t){
            .spi_device = spi0,
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
            error("Failed to initialize CAN controller %d with error code %d\n", i + 1, rc)
            // Try again after 1 second
            sleep_ms(1000);
        }

        debug("Done setting up CAN controller %d\n", i+1)
    }

    info("CAN controllers setup\n")
}

/**
 * Initializes the external spi(spi1)
 */
void init_ext_spi() {
    debug("Setting up external SPI...\n")

    uint baud = spi_init(spi1, EXT_SPI_BAUD);
    spi_set_slave(spi1, true);

    gpio_set_function(EXT_SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(EXT_SPI_TX, GPIO_FUNC_SPI);
    gpio_set_function(EXT_SPI_RX, GPIO_FUNC_SPI);
    gpio_set_function(EXT_SPI_CS, GPIO_FUNC_SPI);

    debug("Done setting up external SPI at baud %u\n", baud)
}

int main() {
    stdio_init_all();
    init_ext_spi();
    setup_can_controllers(&(can_bitrate_t){.profile=CAN_BITRATE});

    //TODO figure out what to do for main loop

    // Create a CAN frame with 11-bit ID of 0x123 and 5 byte payload of deadbeef00
    uint8_t data[4] = {0xdeU, 0xadU, 0xbeU, 0xefU};
    can_frame_t my_tx_frame;
    can_make_frame(&my_tx_frame, false, 0x123, sizeof(data), data, false);

    uint32_t queued_ok[] = {0};

    while(1){
        for(uint8_t i = 0; i < 6; i++){
            can_errorcode_t rc = can_send_frame(can_controllers + i, &my_tx_frame, false);
            if (rc != CAN_ERC_NO_ERROR) {
                // This can happen if there is no room in the transmit queue, which can
                // happen if the CAN controller is connected to a CAN bus but there are no
                // other CAN controllers connected and able to ACK a CAN frame, so the
                // transmit queue fills up and then cannot accept any more frames.
                error("CAN send error on controller %i: %d, sent=%d\n", i+1, rc, queued_ok[i]);
            }
            else {
                queued_ok[i]++;
                info("Frames queued OK on controller %i=%d\n", i+1, queued_ok[i]);
            }
        }
    }
}