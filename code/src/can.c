#include "pico/stdlib.h"
#include "malloc.h"

#include "can.h"
#include "pins.h"
#include "logging.h"
#include "rp2/mcp25xxfd-rp2.h"

//TODO figure out why uint16 is not working in intellisense
//Temp fix
// typedef unsigned short uint16_t;
// typedef unsigned int uint32_t;

#define NUM_RX_EVENT_BYTES (19U)          // Must be the same as NUM_RX_EVENT_BYTES in canapi.h
#define NUM_TX_EVENT_BYTES (9U)           // Must be the same as NUM_TX_EVENT_BYTES in canapi.h


can_controller_t can_controllers[NUM_CAN_CONTROLLERS];

/*
New version of mcp25xxfd_spi_gpio_enable_irq in lib/mcp25xxfd/rp2/mcp25xxfd-rp2.h that enables interrupts on all CAN controllers
*/
inline void mcp25xxfd_spi_gpio_enable_irq(can_interface_t *interface)
{
    debug("IRQ: Enabling interrupts\n");
    // If the GPIO interrupts are shared (i.e. another device is connected to GPIO interrupts) then
    // disable them by disabling interrupts on the pin, so that the other devices can continue to
    // handle interrupts through critical sections where the SPI is being accessed.

    for(uint8_t i = 0; i < NUM_CAN_CONTROLLERS; i++){
        gpio_set_irq_enabled(can_controllers[i].host_interface.spi_irq, LEVEL_SENSITIVE_LOW, true);
    }
}

/*
New version of mcp25xxfd_spi_gpio_disable_irq in lib/mcp25xxfd/rp2/mcp25xxfd-rp2.h that disables interrupts on all CAN controllers (because they all share a single SPI bus)
*/
inline void mcp25xxfd_spi_gpio_disable_irq(can_interface_t *interface)
{
    debug("IRQ: Disabling interrupts\n");
    // If the GPIO interrupts are shared (i.e. another device is connected to GPIO interrupts) then
    // disable them by disabling interrupts on the pin, so that the other devices can continue to
    // handle interrupts through critical sections where the SPI is being accessed.

    for(uint8_t i = 0; i < NUM_CAN_CONTROLLERS; i++){
        gpio_set_irq_enabled(can_controllers[i].host_interface.spi_irq, LEVEL_SENSITIVE_LOW, false);
    }
}



/**
 * Handles IRQs by checking which interrupt hit and servicing it.
 * TODO: is this okay for performance?
 */
void TIME_CRITICAL can_irq_handler()
{
    for (uint8_t i = 0; i < NUM_CAN_CONTROLLERS; i++)
    {                
        // Work out if this interrupt is from the the MCP25xxFD. The bound interface
        // defines the pin used for the interrupt line from the CAN controller.
        uint8_t spi_irq = can_controllers[i].host_interface.spi_irq;
        uint32_t events = gpio_get_irq_event_mask(spi_irq); 

        if (events & GPIO_IRQ_LEVEL_LOW) {
            test("IRQ %hhd called\n", i);
            mcp25xxfd_irq_handler(can_controllers + i);
        }
    }
}


/**
 * Binds the interfaces for the 6 CAN controllers and sets them up by calling can_setup_controller and adding an IRQ handler.
 */
void setup_can_controllers(can_bitrate_t *bitrate)
{
    debug("CAN setup: Starting setup...\n");

    // add IRQ handler to call the IRQ handler on the correct CAN controller
    irq_add_shared_handler(IO_IRQ_BANK0, can_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);

    debug("CAN setup: IRQ handler added!\n");

    debug("CAN setup: Configuring interfaces...\n");
    for(uint i = 0; i < NUM_CAN_CONTROLLERS; i++){
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
    }
    debug("CAN setup: Done configuring interfaces!\n");


    for (uint8_t i = 0; i < NUM_CAN_CONTROLLERS; i++)
    {
        debug("CAN setup: Controller %d: Staring setup...\n", i);
    
        // setup controller
        while (true)
        {
            can_errorcode_t rc = can_setup_controller(can_controllers + i, bitrate, CAN_NO_FILTERS, CAN_MODE_NORMAL, CAN_OPTION_HARD_RESET | CAN_OPTION_RECV_ERRORS | CAN_OPTION_RECORD_TX_EVENTS);

            if (rc == CAN_ERC_NO_ERROR)
                break;

            // This can fail if the CAN transceiver isn't powered up properly. That might happen
            // if the board had 3.3V but not 5V (the transceiver needs 5V to operate).
            error("CAN Setup: Controller %d: Failed to initialize with error code %d. Will retry in 1 second...\n", i, rc);
            sleep_ms(1000);
        }

        debug("CAN setup: Controller %d: Done setting up!\n", i);
    }

    debug("CAN setup: Done setting up!\n");
}


can_errorcode_t send_can_frame(int controller, can_frame_t *frame, bool fifo) {
    debug("Send frame: Sending TX frame on controller %hhd\n", controller);

    for (uint8_t retry = 0; retry < CAN_TRANSMIT_MAX_TRYS; retry++)
    {
        can_errorcode_t rc = can_send_frame(can_controllers + controller, frame, fifo);

        if (rc != CAN_ERC_NO_ERROR)
        {
            // This can happen if there is no room in the transmit queue, which can
            // happen if the CAN controller is connected to a CAN bus but there are no
            // other CAN controllers connected and able to ACK a CAN frame, so the
            // transmit queue fills up and then cannot accept any more frames.
            if (retry + 1 == CAN_TRANSMIT_MAX_TRYS)
            {
                error("Send frame: Failure on controller %d: (err=%d, retries=%d)\n", controller, rc, retry);
                return rc;
            }
            
            warning("Send frame: Error on controller %d: (err=%d, retries=%d), will retry in 100 ms\n", controller, rc, retry);
            sleep_ms(100);
        }
        else
        {
            debug("Send frame: Frame queued OK on controller %d\n", controller);
            return rc;
        }
    }
}


int get_can_rx_events(int controller, uint8_t **pbuff){
    // Disable interrupts while accessing the receive FIFO to avoid concurrency issues with the interrupt handler
    // We don't need to use mcp25xxfd_spi_gpio_disable_irq because we are not using the SPI bus so other CAN controllers can still receive interrupts and use the SPI bus while we are accessing the receive FIFO for this controller
    gpio_set_irq_enabled(can_controllers[controller].host_interface.spi_irq, LEVEL_SENSITIVE_LOW, false);

    // Get the number of pending events for the controller
    uint32_t pending_events = can_recv_pending(can_controllers + controller);
    debug("Receive RX events: Controller %d has %d pending events\n", controller, pending_events);

    uint8_t *buff = malloc(pending_events * NUM_RX_EVENT_BYTES);
    *pbuff = buff;
    if (!buff){
        error("Receive RX events: Failed to allocate memory for %d events\n", pending_events);
        gpio_set_irq_enabled(can_controllers[controller].host_interface.spi_irq, LEVEL_SENSITIVE_LOW, true);
        return -1;
    }   

    // Read out the events into the buffer
    for (uint32_t i = 0; i < pending_events; i++){
        can_recv_as_bytes_safe(can_controllers + controller, buff + i * NUM_RX_EVENT_BYTES);
    }
    debug("Receive RX events: Read out %d events on controller %d\n", pending_events, controller);
    
    // Re-enable interrupts
    gpio_set_irq_enabled(can_controllers[controller].host_interface.spi_irq, LEVEL_SENSITIVE_LOW, true);

    return pending_events * NUM_RX_EVENT_BYTES;
}

int get_can_tx_events(int controller, uint8_t **pbuff){
    // Disable interrupts while accessing the transmit event FIFO to avoid concurrency issues with the interrupt handler
    // We don't need to use mcp25xxfd_spi_gpio_disable_irq because we are not using the SPI bus so other CAN controllers can still receive interrupts and use the SPI bus while we are accessing the transmit event FIFO for this controller
    gpio_set_irq_enabled(can_controllers[controller].host_interface.spi_irq, LEVEL_SENSITIVE_LOW, false);

    // Get the number of pending events for the controller
    uint32_t pending_events = can_recv_tx_events_pending(can_controllers + controller);
    debug("Receive TX events: Controller %d has %d pending events\n", controller, pending_events);

    // TODO remove extra byte
    uint8_t *buff = malloc(pending_events * NUM_TX_EVENT_BYTES);
    *pbuff = buff;
    if (!buff){
        error("Receive TX events: Failed to allocate memory for %d events\n", pending_events);
        gpio_set_irq_enabled(can_controllers[controller].host_interface.spi_irq, LEVEL_SENSITIVE_LOW, true);
        return -1;
    }
  
    // Read out the events into the buffer
    for (uint32_t i = 0; i < pending_events; i++){
        can_recv_tx_event_as_bytes_safe(can_controllers + controller, buff + i * NUM_TX_EVENT_BYTES);
    }
    debug("Receive TX events: Read out %d events on controller %d\n", pending_events, controller);
    
    // Re-enable interrupts
    gpio_set_irq_enabled(can_controllers[controller].host_interface.spi_irq, LEVEL_SENSITIVE_LOW, true);

    return pending_events * NUM_TX_EVENT_BYTES;
}

/////////////////////////////
// Callbacks
/////////////////////////////

void TIME_CRITICAL can_isr_callback_frame_tx(can_uref_t uref, uint32_t timestamp){
    test("Callback: Frame TX\n");
}

void TIME_CRITICAL can_isr_callback_frame_rx(can_frame_t *frame, uint32_t timestamp) {
    test("Callback: Frame RX\n");
}

void TIME_CRITICAL can_isr_callback_error(can_error_t error, uint32_t timestamp) {
    test("Callback: Error\n");
}

uint32_t TIME_CRITICAL can_isr_callback_uref(can_uref_t uref) {
    test("Callback: Uref %08x\n", uref.ref);
    return (uint32_t) uref.ref;
}