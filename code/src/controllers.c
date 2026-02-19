#include "pico/stdlib.h"
#include "protocol.h"

can_controller_t can_controllers[6];

can_errorcode_t controller_status[6] = {0};
volatile uint8_t can_irq[6] = {0};



/**
 * Binds the interfaces for the 6 CAN controllers and sets them up by calling can_setup_controller and adding an IRQ handler.
 */
void setup_can_controllers(can_bitrate_t *bitrate)
{

    // add IRQ handler to call the IRQ handler on the correct CAN controller
    irq_add_shared_handler(IO_IRQ_BANK0, can_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);


    for (uint8_t i = 0; i < 6; i++)
    {

        // bind interface
        can_controllers[i]
            .host_interface = (can_interface_t){
            .spi_device = CAN_SPI,
            .spi_sck = CAN_SPI_SCK,
            .spi_tx = CAN_SPI_TX,
            .spi_rx = CAN_SPI_RX,
            .spi_cs = can_spi_cs_pins[i],
            .spi_irq = can_int_pins[i],
            .magic = 0x1e5515f0U,
        };

        // setup interrupt pin as input
        gpio_init(can_int_pins[i]);

        // setup controller
        while (true)
        {
            can_errorcode_t rc = can_setup_controller(can_controllers + i, bitrate, CAN_NO_FILTERS, CAN_MODE_NORMAL, CAN_OPTION_HARD_RESET);

            if (rc == CAN_ERC_NO_ERROR)
                break;

            // This can fail if the CAN transceiver isn't powered up properly. That might happen
            // if the board had 3.3V but not 5V (the transceiver needs 5V to operate).
            continue;
        }
    }
}