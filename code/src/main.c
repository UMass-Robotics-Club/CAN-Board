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
#define CAN_BITRATE CAN_BITRATE_1M_50
#define MAX_TRYS 1

// get protocol data over stdio else it will use SPI
#define PKT_OVER_STDIO



#ifdef PKT_OVER_STDIO
    #define read_bytes(buff, len) fread(buff, len, 1, stdin);
    #define write_bytes(buff, len) fwrite(buff, len, 1, stdin);
#else
    #define read_bytes(buff, len) spi_read_blocking(EXT_SPI, 0, buff, len);
    #define write_bytes(buff, len) spi_write_blocking(EXT_SPI, buff, len);
#endif

can_controller_t can_controllers[6];

/**
 * Binds the interfaces for the 6 CAN controllers and sets them up by calling can_setup_controller and adding an IRQ handler.
 */
void setup_can_controllers(can_bitrate_t *bitrate) {
    debug("CAN setup: Starting setup...\n")

    for(uint8_t i = 0; i < 6; i++){
        debug("CAN setup: Controller %d: Staring setup...\n", i)

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

        debug("CAN setup: Controller %d: Done setting up!\n", i)
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
    read_bytes(&header, sizeof(header))

    if(header.controller_num > 5){
        error("Command header: Invalid CAN controller number: %d\n", header.controller_num)
        return;
    }

    debug("Got header: (channel=%hhd, size=%hhd)\n", header.controller_num, header.data_len)

    // get arbitration (29 bit)
    uint32_t arbitration;
    read_bytes(&arbitration, sizeof(arbitration));

    debug("Got arbitration: %x\n", arbitration)

    // get CAN frame data
    uint8_t buffer[32];
    read_bytes(buffer, header.data_len);

    debug("Got data\n")

    // Create a CAN frame
    can_frame_t my_tx_frame;
    can_make_frame(&my_tx_frame, true, arbitration, header.data_len, buffer, false);
    
    for(uint8_t retry = 0; retry < MAX_TRYS; retry++){
        gpio_put(EXT_GPIO_15, 0);
        can_errorcode_t rc = can_send_frame(can_controllers + header.controller_num, &my_tx_frame, false);
        gpio_put(EXT_GPIO_15, 1);
        if (rc != CAN_ERC_NO_ERROR) {
            // This can happen if there is no room in the transmit queue, which can
            // happen if the CAN controller is connected to a CAN bus but there are no
            // other CAN controllers connected and able to ACK a CAN frame, so the
            // transmit queue fills up and then cannot accept any more frames.
            if(retry + 1 == MAX_TRYS){
                error("CAN send: Failure on controller %d: (err=%d, retries=%d)\n", header.controller_num, rc, retry)
            } else {
                warning("CAN send: Error on controller %d: (err=%d, retries=%d), will retry in 1 second\n", header.controller_num, rc, retry)
                sleep_ms(1000);
            }
        }
        else {
            debug("CAN send: Frame queued OK on controller %d\n", header.controller_num);
            break;
        }
    }
}

int main() {
    stdio_init_all();

    // Turn off led for start
    gpio_init(EXT_GPIO_15);
    gpio_set_dir(EXT_GPIO_15, 1);
    gpio_put(EXT_GPIO_15, 1);

    info("Starting setup...")

    init_ext_spi();
    can_bitrate_t bitrate = {.profile=CAN_BITRATE};
    setup_can_controllers(&bitrate);

    // disable all interrupts
    irq_set_enabled(IO_IRQ_BANK0, false);

    info("CAN board ready!\n")

    while(1)
        loop();
}