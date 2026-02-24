#include "pico/stdlib.h"
#include "malloc.h"

#include "tusb.h" // Required for tud_cdc_connected()
#include "hardware/spi.h"

#include "canapi.h"

#include "pins.h"
#include "logging.h"
#include "can.h"

// TODO figure out why uint16 is not working in intellisense
// Temp fix
//  typedef unsigned short uint16_t;
//  typedef unsigned int uint32_t;

///////////////////////////////////////
// User params
///////////////////////////////////////

#define EXT_SPI_BAUD 1000000
#define CAN_BITRATE CAN_BITRATE_1M_50

// get protocol data over stdio else it will use SPI
#define PKT_OVER_STDIO

#ifdef PKT_OVER_STDIO
#define read_bytes(buff, len) fread(buff, len, 1, stdin)
#define write_bytes(buff, len)    \
    fwrite(buff, len, 1, stdout); \
    fflush(stdout)
#else
#define read_bytes(buff, len) spi_read_blocking(EXT_SPI, 0, buff, len)
#define write_bytes(buff, len) spi_write_blocking(EXT_SPI, buff, len)
#endif

#define wrap_led_on(statements) \
    gpio_put(EXT_GPIO_15, 0);   \
    statements                  \
        gpio_put(EXT_GPIO_15, 1);

/**
 * Initializes the external spi
 */
void init_ext_spi()
{
    debug("EXT SPI setup: Starting setup...\n");

    uint baud = spi_init(EXT_SPI, EXT_SPI_BAUD);
    spi_set_slave(EXT_SPI, true);

    gpio_set_function(EXT_SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(EXT_SPI_TX, GPIO_FUNC_SPI);
    gpio_set_function(EXT_SPI_RX, GPIO_FUNC_SPI);
    gpio_set_function(EXT_SPI_CS, GPIO_FUNC_SPI);

    debug("EXT SPI setup: Done setting up at baud %u!\n", baud);
}

typedef enum : uint8_t
{
    SEND_FRAME,
    RECV_RX_EVENTS,
    RECV_TX_EVENTS,
    RECV_CAN_INFO
} host_command_t;

typedef enum : uint8_t
{
    CMD_RESPONSE_OK,
    CMD_RESPONSE_BAD_PKT,
    CMD_RESPONSE_CMD_UNKNOWN,
    CMD_RESPONSE_CMD_MALFORMED,
    CMD_RESPONSE_TIMEOUT,
    CMD_RESPONSE_FAILED,
    CMD_RESPONSE_NO_RESOURCES
} command_resp_t;

typedef enum : uint8_t
{
    FRAME_OPTION_EXTENDED = (1 << 0), // Use CAN extended 29 bit arbitration
    FRAME_OPTION_REMOTE = (1 << 1),   // This is a CAN remote frame
    FRAME_OPTION_USE_FIFO = (1 << 2), // Use TX FIFO instead of directly going into TX priority queue
    FRAME_OPTION_USE_UREF = (1 << 3), // Add a user reference to the frame (else it will default to 0)
} frame_options_t;

typedef struct
{
    uint8_t controller;
    frame_options_t options;
    uint8_t dlc;
} send_frame_cmd_data_t;

void send_response_pkt(command_resp_t resp_code, void *data, uint16_t data_size)
{
    uint8_t tmp = 0xAA;
    write_bytes(&tmp, 1);
    write_bytes(&resp_code, sizeof(resp_code));
    write_bytes(&data_size, 2);
    if (data_size)
        write_bytes(data, data_size);
    tmp = 0x55;
    write_bytes(&tmp, 1);
}

void handle_send_frame_cmd(uint8_t *data, uint16_t data_size)
{
    // Extract frame header data
    if (data_size < sizeof(send_frame_cmd_data_t))
    {
        error("Send frame: Not enough data supplied to extract header (expected %hhd, got %hhd)\n", sizeof(send_frame_cmd_data_t), data_size);
        send_response_pkt(CMD_RESPONSE_CMD_MALFORMED, NULL, 0);
        return;
    }
    send_frame_cmd_data_t *frame_cmd_data = (send_frame_cmd_data_t *)data;

    bool extended = (frame_cmd_data->options & FRAME_OPTION_EXTENDED);
    bool remote = (frame_cmd_data->options & FRAME_OPTION_REMOTE);
    bool use_fifo = (frame_cmd_data->options & FRAME_OPTION_USE_FIFO);
    bool use_uref = (frame_cmd_data->options & FRAME_OPTION_USE_UREF);

    debug("Send frame: Received header (controller=%hhd, extended=%hhd, remote=%hhd, fifo=%hhd, uref=%hhd, dlc=%hhd)\n",
          frame_cmd_data->controller,
          extended,
          remote,
          use_fifo,
          use_uref,
          frame_cmd_data->dlc);

    if (frame_cmd_data->controller >= NUM_CAN_CONTROLLERS)
    {
        error("Send frame: Invalid controller number (controller=%hhd, must be between 0 and %d)\n", frame_cmd_data->controller, NUM_CAN_CONTROLLERS - 1);
        send_response_pkt(CMD_RESPONSE_CMD_MALFORMED, NULL, 0);
        return;
    }

    if (frame_cmd_data->dlc > 8)
    {
        error("Send frame: Invalid DLC (dlc=%hhd > 8)\n", frame_cmd_data->dlc);
        send_response_pkt(CMD_RESPONSE_CMD_MALFORMED, NULL, 0);
        return;
    }

    // Calculate and verify sizes are correct
    uint8_t arbitration_idx = sizeof(send_frame_cmd_data_t);
    uint8_t uref_idx = arbitration_idx + (extended ? 4 : 2); // 16 bits for normal arbitration (11 bits) and 32 bits for extended (29 bits)
    uint8_t data_idx = uref_idx + (use_uref ? 4 : 0);        // 32 bits for user reference if included
    uint8_t expected_size = data_idx + (remote ? 0 : frame_cmd_data->dlc);                                         // Total expected size is the header + arbitration + optional user reference + data
    if (data_size != expected_size)
    {
        error("Send frame: Invalid amount of data supplied for CAN frame (expected %hhd bytes, got %hhd bytes)\n", expected_size, data_size);
        send_response_pkt(CMD_RESPONSE_CMD_MALFORMED, NULL, 0);
        return;
    }

    // Get arbitration ID
    uint32_t arbitration = 0;
    // We use memcpy here as it may not be word aligned
    memcpy(&arbitration, data + arbitration_idx, (extended ? 4 : 2));
    debug("Send frame: Arbitration ID: %08x\n", arbitration);

    // Make frame
    can_frame_t frame;
    can_make_frame(&frame, extended, arbitration, frame_cmd_data->dlc, data + data_idx, remote);

    // Set user reference if included
    if (use_uref)
    {
        uint32_t uref;
        memcpy(&uref, data + uref_idx, 4);
        debug("Send frame: User reference: %08x\n", uref);
        can_frame_set_uref(&frame, (void *)uref);
    }

    // Send frame
    can_errorcode_t rc = send_can_frame(frame_cmd_data->controller, &frame, use_fifo);
    if (rc == CAN_ERC_NO_ERROR)
    {
        send_response_pkt(CMD_RESPONSE_OK, NULL, 0);
        return;
    }

    send_response_pkt(CMD_RESPONSE_FAILED, &rc, sizeof(rc));
}

void handle_recv_rx_events_cmd(uint8_t *data, uint16_t data_size)
{
    if (data_size != 1)
    {
        error("Receive RX events: Invalid data size (expected 1 byte, got %hhd bytes)\n", data_size);
        send_response_pkt(CMD_RESPONSE_CMD_MALFORMED, NULL, 0);
        return;
    }

    // Get controller bitmap
    uint8_t controller = data[0];
    debug("Receive RX events: Received controller: %hhd\n", controller);

    if (controller >= NUM_CAN_CONTROLLERS)
    {
        error("Receive RX events: Invalid controller number (controller=%hhd, must be between 0 and %d)\n", controller, NUM_CAN_CONTROLLERS - 1);
        send_response_pkt(CMD_RESPONSE_CMD_MALFORMED, NULL, 0);
        return;
    }

    uint8_t *buff;
    int buff_size = get_can_rx_events(controller, &buff);
    if (buff_size < 0)
    {
        send_response_pkt(CMD_RESPONSE_FAILED, NULL, 0);
        return;
    }

    // Send the buffer back to the host
    send_response_pkt(CMD_RESPONSE_OK, buff, buff_size);
    free(buff);
}

void handle_recv_tx_events_cmd(uint8_t *data, uint16_t data_size)
{
    if (data_size != 1)
    {
        error("Receive TX events: Invalid data size (expected 1 byte, got %hhd bytes)\n", data_size);
        send_response_pkt(CMD_RESPONSE_CMD_MALFORMED, NULL, 0);
        return;
    }

    // Get controller
    uint8_t controller = data[0];

    debug("Receive TX events: Received controller: %hhd\n", controller);

    if (controller >= NUM_CAN_CONTROLLERS)
    {
        error("Receive TX events: Invalid controller number (controller=%hhd, must be between 0 and %d)\n", controller, NUM_CAN_CONTROLLERS - 1);
        send_response_pkt(CMD_RESPONSE_CMD_MALFORMED, NULL, 0);
        return;
    }

    uint8_t *buff;
    int buff_size = get_can_tx_events(controller, &buff);
    if (buff_size < 0)
    {
        send_response_pkt(CMD_RESPONSE_FAILED, NULL, 0);
        return;
    }

    // Send the buffer back to the host
    send_response_pkt(CMD_RESPONSE_OK, buff, buff_size);
    free(buff);
}

void loop()
{

    uint8_t start;
    read_bytes(&start, 1);
    if (start != 0xAA)
    {
        error("Received invalid start byte: %02hhx\n", start);
        return;
    }

    host_command_t cmd;
    read_bytes(&cmd, sizeof(cmd));

    uint16_t data_size;
    read_bytes(&data_size, 2);

    uint8_t *data = malloc(data_size);
    if (data == NULL)
    {
        error("Failed to allocate memory for command data\n");
        send_response_pkt(CMD_RESPONSE_NO_RESOURCES, NULL, 0);
        // We still need to read the data and end byte from the host to keep the protocol in sync, but we can just discard it since we don't have resources to handle the command
        uint8_t tmp[32];
        while (data_size > 0)
        {
            uint16_t to_read = data_size > sizeof(tmp) ? sizeof(tmp) : data_size;
            read_bytes(tmp, to_read);
            data_size -= to_read;
        }
        // Read and discard end byte
        read_bytes(tmp, 1);

        return;
    }

    read_bytes(data, data_size);

    uint8_t end;
    read_bytes(&end, 1);
    if (end != 0x55)
    {
        error("Received invalid end byte: %02hhx\n", end);
        send_response_pkt(CMD_RESPONSE_BAD_PKT, NULL, 0);
        free(data);
        return;
    }

    // Handle commands
    // TODO make these command work with more than just one controller
    switch (cmd)
    {
    case SEND_FRAME:
        wrap_led_on(handle_send_frame_cmd(data, data_size);) break;
    case RECV_RX_EVENTS:
        wrap_led_on(handle_recv_rx_events_cmd(data, data_size);) break;
    case RECV_TX_EVENTS:
        wrap_led_on(handle_recv_tx_events_cmd(data, data_size);) break;
    default:
        error("Received unknown command: %hhd\n", cmd);
        send_response_pkt(CMD_RESPONSE_CMD_UNKNOWN, NULL, 0);
        break;
    }

    free(data);
}

int main()
{
    stdio_init_all();
    stdio_set_translate_crlf(&stdio_usb, false);

    // Wait for the USB CDC connection
    while (!tud_cdc_connected())
    {
        // Optional: Add a small delay or perform other non-blocking tasks
        sleep_ms(100);
    }

    // Turn off led for start
    gpio_init(EXT_GPIO_15);
    gpio_set_dir(EXT_GPIO_15, 1);
    gpio_put(EXT_GPIO_15, 1);

    info("Starting setup...\n");

    init_ext_spi();
    can_bitrate_t bitrate = {.profile = CAN_BITRATE};
    setup_can_controllers(&bitrate);

    info("CAN board ready!\n");

    while (1)
        loop();
}