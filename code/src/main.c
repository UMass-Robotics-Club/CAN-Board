#include "pico/stdlib.h"
#include "pins.h"
#include "canapi.h"
#include "pico/multicore.h"

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

#define wrap_led_on(statements) \
    gpio_put(EXT_GPIO_15, 0);   \
    statements                  \
    gpio_put(EXT_GPIO_15, 1);

can_controller_t can_controllers[6];
volatile uint8_t can_irq[6] = {0};
volatile can_frame_t tx_frame;
volatile bool new_frame = false;
mutex_t frame_mutex;

/**
 * Handles IRQs by calling mcp25xxfd_irq_handler on the controller that triggered the interrupt.
 */
void TIME_CRITICAL can_irq_handler(void)
{
    for (uint8_t i = 0; i < 6; i++)
    {
        uint8_t spi_irq = can_controllers[i].host_interface.spi_irq;
        uint32_t events = gpio_get_irq_event_mask(spi_irq);

        if (events & GPIO_IRQ_LEVEL_LOW)
        {
            can_irq[i] = 1;
        }
    }
}

/**
 * Binds the interfaces for the 6 CAN controllers and sets them up by calling can_setup_controller and adding an IRQ handler.
 */
void setup_can_controllers(can_bitrate_t *bitrate)
{
    debug("CAN setup: Starting setup...\n")

        // add IRQ handler to call the IRQ handler on the correct CAN controller
        irq_add_shared_handler(IO_IRQ_BANK0, can_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);

    debug("CAN setup: IRQ handler added!\n")

        for (uint8_t i = 0; i < 6; i++)
    {
        debug("CAN setup: Controller %d: Staring setup...\n", i)

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
void init_ext_spi()
{
    debug("EXT SPI setup: Starting setup...\n")

        uint baud = spi_init(EXT_SPI, EXT_SPI_BAUD);
    spi_set_slave(EXT_SPI, true);

    gpio_set_function(EXT_SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(EXT_SPI_TX, GPIO_FUNC_SPI);
    gpio_set_function(EXT_SPI_RX, GPIO_FUNC_SPI);
    gpio_set_function(EXT_SPI_CS, GPIO_FUNC_SPI);

    debug("EXT SPI setup: Done setting up at baud %u!\n", baud)
}

typedef struct __attribute__((packed))
{
    uint8_t controller_num : 3;
    uint8_t data_len : 5; // should be larger to support max 64 bytes of CAN FD
} can_command_header_t;

void handle_input()
{
    getc(stdin);

    while (1)
    {
        // wait for new frame to be sent
        while(new_frame){}
        
        // get header
        can_command_header_t header;
        read_bytes(&header, sizeof(header))

        if (header.controller_num > 5)
        {
            error("Command header: Invalid CAN controller number: %d\n", header.controller_num) return;
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
        mutex_enter_blocking(&frame_mutex);
        can_make_frame(&tx_frame, true, arbitration, header.data_len, buffer, false);
        new_frame = true;
        mutex_exit(&frame_mutex);
    }
}

void loop()
{
    // handle interrupts
    for (uint8_t i = 0; i < 6; i++)
    {
        if (can_irq[i])
        {
            mcp25xxfd_irq_handler(can_controllers + i);
            can_irq[i] = 0;
        }
    }

    // check if there is new frame
    mutex_enter_blocking(&frame_mutex);
    if (!new_frame){
        mutex_exit(&frame_mutex);
        return;
    }
     
    uint8_t controller_num = multicore_fifo_pop_blocking();
    for (uint8_t retry = 0; retry < MAX_TRYS; retry++)
    {
        wrap_led_on(can_errorcode_t rc = can_send_frame(can_controllers + controller_num, &tx_frame, false);)

            if (rc != CAN_ERC_NO_ERROR)
        {
            // This can happen if there is no room in the transmit queue, which can
            // happen if the CAN controller is connected to a CAN bus but there are no
            // other CAN controllers connected and able to ACK a CAN frame, so the
            // transmit queue fills up and then cannot accept any more frames.
            if (retry + 1 == MAX_TRYS)
            {
                error("CAN send: Failure on controller %d: (err=%d, retries=%d)\n", controller_num, rc, retry)
            }
            else
            {
                warning("CAN send: Error on controller %d: (err=%d, retries=%d), will retry in 100 ms\n", controller_num, rc, retry)
                    sleep_ms(100);
            }
        }
        else
        {
            debug("CAN send: Frame queued OK on controller %d\n", controller_num);
            break;
        }
    }
}

int main()
{
    stdio_init_all();

    // Turn off led for start
    gpio_init(EXT_GPIO_15);
    gpio_set_dir(EXT_GPIO_15, 1);
    gpio_put(EXT_GPIO_15, 1);

    info("Starting setup...")

    init_ext_spi();
    can_bitrate_t bitrate = {.profile = CAN_BITRATE};
    setup_can_controllers(&bitrate);
    mutex_init(&frame_mutex);
    multicore_launch_core1(handle_input);

    info("CAN board ready!\n")

    while (1)
        loop();
}