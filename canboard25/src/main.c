#include "pico/stdlib.h"
#include "pins.h"
#include "canapi.h"

// enable different levels of logging
#define ENABLE_DEBUG
#define ENABLE_INFO
#define ENABLE_WARNING
#define ENABLE_ERROR

#include "logging.h"

///////////////////////////////////////
// User params
///////////////////////////////////////

#define EXT_SPI_BAUD 1000000
#define CAN_BITRATE CAN_BITRATE_1M_75


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
        // three is broken for rn
        if (i == 3)
            continue;
        
        debug("CAN setup: Controller %d: Starting setup...\n", i);

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

        // setup interrupt pin as input (don't know why but breaks without this)
        gpio_init(can_int_pins[i]);
        gpio_set_dir(can_int_pins[i], GPIO_IN);

        //recommended to prevent SPI hangs
        gpio_init(can_spi_cs_pins[i]);
        gpio_set_dir(can_spi_cs_pins[i], GPIO_OUT);
        gpio_put(can_spi_cs_pins[i], 1);   // idle high

        // setup controller
        while (true) {
            can_errorcode_t rc = can_setup_controller(can_controllers + i, bitrate, CAN_NO_FILTERS, CAN_MODE_NORMAL, CAN_OPTION_HARD_RESET);
            
            if (rc == CAN_ERC_NO_ERROR)
                break;
            
            // This can fail if the CAN transceiver isn't powered up properly. That might happen
            // if the board had 3.3V but not 5V (the transceiver needs 5V to operate). 
            error("CAN Setup: Controller %d: Failed to initialize with error code %d. Will retry in 1 second...\n", i, rc)
            sleep_ms(1000);
            continue;
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
    spi_set_slave(EXT_SPI, true); //set to slave 

    gpio_set_function(EXT_SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(EXT_SPI_TX, GPIO_FUNC_SPI);
    gpio_set_function(EXT_SPI_RX, GPIO_FUNC_SPI);
    gpio_set_function(EXT_SPI_CS, GPIO_FUNC_SPI);

    debug("EXT SPI setup: Done setting up at baud %u!\n", baud)
}

/*
typedef struct __attribute__((packed)) {
    uint8_t controller_num : 3;
    uint8_t data_len: 5; //should be larger to support max 64 bytes of CAN FD
} can_command_header_t;
*/

void loop() {
    
    /*
    Format from rs02.c motor driver

    Standard Format: [ext: 1 byte | channel: 1 byte | 2 don't care bytes | id: 2 bytes | data: 8 bytes] = 14 bytes
    Extended Format: [ext: 1 byte | channel: 1 byte | id: 4 bytes | data: 8 bytes] = 14 bytes */
    
    uint8_t spi_rx_frame[14];
    spi_read_blocking(EXT_SPI, 0, spi_rx_frame, 14);


    uint8_t ext = spi_rx_frame[0];
    uint8_t channel = spi_rx_frame[1];
    uint32_t arbitration;


    if (ext == 0)//standard frame, 11-bit arbitration
        arbitration = (spi_rx_frame[4] << 8) || spi_rx_frame[5];
    
    else { //extended 29-bit arbitration
        arbitration = (spi_rx_frame[2] << 32);
        arbitration |= (spi_rx_frame[3] << 24);
        arbitration |= (spi_rx_frame[4] << 16);
        arbitration |= spi_rx_frame[5];
    }

    //data
    uint8_t buffer[8];

    for (int j = 0; j < 8; j++){
        buffer[j] = spi_rx_frame[j+6]; 
    }


    //Create can frame
    can_frame_t tx_frame;


    if (ext==0) can_make_frame(&tx_frame, false, (uint16_t)arbitration, 8, buffer, false);

    else can_make_frame(&tx_frame, true, arbitration, 8, buffer, false);

    while(1){

        can_errorcode_t rc = can_send_frame(can_controllers + channel, &tx_frame, false);
        if (rc != CAN_ERC_NO_ERROR) {
            // This can happen if there is no room in the transmit queue, which can
            // happen if the CAN controller is connected to a CAN bus but there are no
            // other CAN controllers connected and able to ACK a CAN frame, so the
            // transmit queue fills up and then cannot accept any more frames.
            error("CAN send: Error on controller %d: %d,\n", channel, rc);
        }
        else {
            debug("CAN send: Frame queued OK on controller %d\n", channel);
            break;
        }
        sleep_ms(1000); //delay for 1s

        can_rx_event_t rx_event;
        uint32_t n = 0;

        //print received frame - CHANGE TO SEND OVER SPI TO JETSON
        can_rx_event_t *e = &rx_event;

        if (can_recv((can_controllers+channel), e) && can_event_is_frame(e)) {
            can_frame_t* rx_frame = can_event_get_frame(e);
            n++;
        }
        
        //spi_write_blocking(); //write can frame through spi to jetson for debugging
    }
    
    
}

void test_controller(int i){
    // Create a CAN frame
    can_frame_t my_tx_frame;
    can_make_frame(&my_tx_frame, false, 0xaa, 12, "hello world!", false);

    while(1){
        
        can_errorcode_t rc = can_send_frame(can_controllers + i, &my_tx_frame, false);
        if (rc != CAN_ERC_NO_ERROR) {
            // This can happen if there is no room in the transmit queue, which can
            // happen if the CAN controller is connected to a CAN bus but there are no
            // other CAN controllers connected and able to ACK a CAN frame, so the
            // transmit queue fills up and then cannot accept any more frames.
            error("CAN send: Error on controller %d: %d,\n", i, rc);
        }
        else {
            debug("CAN send: Frame queued OK on controller %d\n", i);
        }
        sleep_ms(1000);
    }
}

int main() {
    stdio_init_all();

    // while(!stdio_usb_connected()){}

    gpio_init(EXT_GPIO_15);
    gpio_set_dir(EXT_GPIO_15, 1);
    gpio_put(EXT_GPIO_15, 0);

    info("Starting setup...")

    init_ext_spi();
    can_bitrate_t bitrate = {.profile=CAN_BITRATE};
    setup_can_controllers(&bitrate);

    info("CAN board ready!\n")

    test_controller(1);

    while(1)
        loop();
}