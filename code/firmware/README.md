# Firmware — CAN Controller Board

## Overview

This folder contains the RP2040 firmware for the CAN controller board. It uses the Raspberry Pi Pico SDK and the MCP25xxFD CAN driver.

- **Microcontroller:** Raspberry Pi RP2040 (Pico)
- **CAN Controller:** MCP25xxFD (via SPI)
- **Number of CAN controllers:** 6 (configurable)
- **Communication:** USB CDC (default) or external SPI
- **Protocol:** Simple framed packet protocol (half-duplex, command-response)

## Contents

| Item | Purpose |
|------|---------|
| [src/](src/) | Application sources: [main.c](src/main.c), [can.c](src/can.c) |
| [include/](include/) | Headers: [can.h](include/can.h), [pins.h](include/pins.h), [logging.h](include/logging.h) |
| [lib/mcp25xxfd/](lib/mcp25xxfd/) | Third-party MCP25xxFD CAN driver and RP2040 bindings |
| [CMakeLists.txt](CMakeLists.txt) | CMake build configuration for Pico SDK |

## Quick Start (Command-line Build)

From the `code` directory:

```bash
mkdir build
cd build
cmake ..
make
```

Build outputs: `can_board.elf`, `can_board.uf2` in `build/` directory.

## Building & Uploading with VS Code + Pico Extension

### Installation

1. **Install the official Raspberry Pi Pico extension**
   - Open VS Code Extensions (`Ctrl+Shift+X`)
   - Search for `Raspberry Pi Pico` (Publisher: Raspberry Pi)
   - Click **Install** on the official [Raspberry Pi Pico extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico)
   - The extension will automatically download and configure the toolchain, SDK, and build tools (Ninja, CMake, arm-none-eabi-gcc)
   - **No manual toolchain installation required** — the extension handles it all

2. **Platform-specific notes**
   - **Windows / Raspberry Pi OS**: No additional prerequisites
   - **macOS**: Run `xcode-select --install` to install Xcode Command Line Tools
   - **Linux**: Ensure Python 3.10+, Git 2.28+, and Tar are installed and on your `PATH`

### Project Setup

1. **Open the folder in VS Code**
   - Open the project folder in VS Code (single folder workspace only)
   - The Pico extension will automatically detect the `CMakeLists.txt`

2. **Extension auto-configuration**
   - The extension automatically configures CMake on project load
   - You'll see the Pico extension status in the status bar at the bottom
   - If prompted, select **"Pico"** as the CMake kit

### Build Workflow

1. **Build** — Use any of the following:
   - Click the **Compile** button in the Pico extension status bar (bottom right)
   - Run `Ctrl+Shift+P` → Search `Raspberry Pi Pico: Compile Project`
   - Build output appears in the terminal; artifacts in `build/` folder (`can_board.elf`, `can_board.uf2`, etc.)

2. **Flash/Upload** — After building successfully:
   - **Option A (UF2 mass-storage, easiest):**
     - Put Pico into BOOTSEL mode: Hold BOOTSEL button while plugging USB into computer
     - Mounted drive will appear (RPI-RP2)
     - Copy `build/can_board.uf2` to the drive
     - Pico will auto-reboot and run the firmware
   
   - **Option B (Picotool, via extension):**
     - Connect Pico via USB with SWD/JTAG debugger (optional)
     - Run `Ctrl+Shift+P` → Search `Raspberry Pi Pico: Upload Project` (if available)
     - Extension uses `picotool` to flash via USB

3. **Debug** (optional, requires hardware debugger):
   - Connect CMSIS-DAP, ST-Link, or other SWD debugger to Pico
   - Run `Ctrl+Shift+P` → Search `Raspberry Pi Pico: Debug Project`
   - Extension automatically configures OpenOCD and GDB
   - Set breakpoints and step through code in the debugger

### Troubleshooting Extension Issues

- **CMake configuration fails**: Clear extension settings (`Ctrl+Shift+P` → `Preferences: Open Settings (UI)` → search `raspberry-pi-pico` → reset to defaults) then reload VS Code
- **Tools not downloading**: Check GitHub API rate limits; create a GitHub personal access token (with `public_repo` scope) and add to `raspberry-pi-pico.githubToken` in settings
- **Build fails**: Ensure `pico_sdk_import.cmake` is in the `firmware/` directory (it is, by default)

## Configuration

All configuration options are located in the files below. Edit them before building.

### Logging — [include/logging.h](include/logging.h)

Enable/disable log levels by defining or commenting out:

```c
#define ENABLE_TEST      // Development/testing (interferes with protocol in IRQ handler!)
#define ENABLE_DEBUG     // Debug-level messages
#define ENABLE_INFO      // Informational messages
#define ENABLE_WARNING   // Warning messages
#define ENABLE_ERROR     // Error messages
```

### Pins & Interfaces — [include/pins.h](include/pins.h)

| Parameter | Default | Purpose |
|-----------|---------|---------|
| `CAN_SPI` | `spi0` | Primary SPI bus for all 6 CAN controllers |
| `CAN_SPI_SCK` | 2 | SPI clock pin |
| `CAN_SPI_TX` | 3 | SPI TX (MOSI) pin |
| `CAN_SPI_RX` | 4 | SPI RX (MISO) pin |
| `can_spi_cs_pins[6]` | 5,6,7,8,9,10 | Chip select pins for 6 controllers |
| `can_int_pins[6]` | 29,25,23,21,19,17 | Interrupt pins for 6 controllers |
| `EXT_SPI` | `spi1` | External SPI (when `PKT_OVER_STDIO` is undefined) |
| `EXT_SPI_TX` | 11 | External SPI TX pin |
| `EXT_SPI_RX` | 12 | External SPI RX pin |
| `EXT_SPI_SCK` | 14 | External SPI clock pin |
| `EXT_SPI_CS` | 13 | External SPI chip select pin |
| `EXT_GPIO_15` | 15 | Activity LED GPIO (toggled during command processing) |

### User Parameters — [src/main.c](src/main.c)

Located in the "User params" section at the top of the file:

```c
#define EXT_SPI_BAUD 1000000        // External SPI baud rate (1 MHz default)
#define CAN_BITRATE CAN_BITRATE_1M_50   // CAN bitrate (see list below)
#define PKT_OVER_STDIO              // Use USB CDC for packets (comment to use EXT_SPI)
```

#### CAN Bitrate Profiles

All profiles are defined in [lib/mcp25xxfd/canapi.h](lib/mcp25xxfd/canapi.h). Use one as the value for `CAN_BITRATE`:

**Standard bitrates (75% sample point)**
- `CAN_BITRATE_500K_75` — 500 kbit/sec (default)
- `CAN_BITRATE_250K_75` — 250 kbit/sec
- `CAN_BITRATE_125K_75` — 125 kbit/sec
- `CAN_BITRATE_1M_75` — 1 Mbit/sec

**Standard bitrates (50% sample point)**
- `CAN_BITRATE_500K_50` — 500 kbit/sec
- `CAN_BITRATE_250K_50` — 250 kbit/sec
- `CAN_BITRATE_125K_50` — 125 kbit/sec
- `CAN_BITRATE_1M_50` — 1 Mbit/sec

**High-speed bitrates (50% sample point, non-standard)**
- `CAN_BITRATE_2M_50` — 2 Mbit/sec
- `CAN_BITRATE_4M_90` — 4 Mbit/sec (90% sample)

**Specialized bitrates (75%-87.5% sample points)**
- `CAN_BITRATE_2_5M_75` — 2.5 Mbit/sec (75% sample)
- `CAN_BITRATE_2M_80` — 2 Mbit/sec (80% sample)
- `CAN_BITRATE_500K_875` — 500 kbit/sec (87.5% sample)
- `CAN_BITRATE_250K_875` — 250 kbit/sec (87.5% sample, J1939/CANopen)
- `CAN_BITRATE_125K_875` — 125 kbit/sec (87.5% sample)
- `CAN_BITRATE_1M_875` — 1 Mbit/sec (85.5% sample)

**Custom bitrate**
- `CAN_BITRATE_CUSTOM` — Use a custom profile (manually set `brp`, `tseg1`, `tseg2`, `sjw` in `can_bitrate_t`)

### Controller Limits — [include/can.h](include/can.h)

```c
#define NUM_CAN_CONTROLLERS 6           // Number of MCP25xxFD controllers (fixed to 6)
#define CAN_SETUP_MAX_RETRIES 5         // Setup retry count (if transceiver slow to power)
#define CAN_TRANSMIT_MAX_TRYS 1         // Transmit retry count
```

## Code Structure

### [src/main.c](src/main.c)

**Entry point and packet loop.**

- **`main()`** — Initializes stdio, waits for USB CDC connection, initializes external SPI, calls `setup_can_controllers()`, enters main packet loop.
- **`loop()`** — Reads framed packets from transport (stdio or SPI), dispatches commands, sends responses.
- **Command handlers** — `handle_send_frame_cmd()`, `handle_recv_rx_events_cmd()`, `handle_recv_tx_events_cmd()`.

**Defines:**
- `host_command_t` — Command enum (SEND_FRAME, RECV_RX_EVENTS, RECV_TX_EVENTS, RECV_CAN_INFO)
- `command_resp_t` — Response codes (OK, BAD_PKT, CMD_UNKNOWN, CMD_MALFORMED, TIMEOUT, FAILED, NO_RESOURCES)
- `frame_options_t` — Frame flags (EXTENDED, REMOTE, USE_FIFO, USE_UREF)

### [src/can.c](src/can.c)

**CAN controller management and IRQ handling.**

- **`setup_can_controllers()`** — Initializes all 6 CAN controllers with given bitrate.
- **`send_can_frame()`** — Queue a CAN frame for transmission (with retries).
- **`get_can_rx_events()`** — Retrieve pending RX events for a controller.
- **`get_can_tx_events()`** — Retrieve pending TX events for a controller.
- **`can_irq_handler()`** — Shared IRQ handler (TIME_CRITICAL) that services all controllers.

### [include/can.h](include/can.h)

**Public constants and function declarations:**
- `NUM_CAN_CONTROLLERS`
- `can_controllers[]` — Array of `can_controller_t` instances
- Function prototypes for setup, send, and event retrieval

### [include/pins.h](include/pins.h)

**Board pin definitions:**
- SPI pins for CAN controllers
- External UART/SPI pins
- CS and interrupt pin arrays
- GPIO pins for activity indication

### [include/logging.h](include/logging.h)

**Logging macros:**
- `test()`, `debug()`, `info()`, `warning()`, `error()`
- Enable/disable via preprocessor defines
- Output via printf (configured in CMake to use stdio USB or UART)

### [lib/mcp25xxfd/](lib/mcp25xxfd/)

**Third-party CAN driver:**
- [lib/mcp25xxfd/canapi.h](lib/mcp25xxfd/canapi.h) — CAN API, bitrate profiles, data structures
- [lib/mcp25xxfd/mcp25xxfd.c](lib/mcp25xxfd/mcp25xxfd.c) — Main driver implementation
- [lib/mcp25xxfd/rp2/mcp25xxfd-rp2.h](lib/mcp25xxfd/rp2/mcp25xxfd-rp2.h) — RP2040-specific bindings

See [lib/mcp25xxfd/canapi.h](lib/mcp25xxfd/canapi.h) for `can_frame_t`, `can_rx_event_t`, `can_tx_event_t`, and other data structures.

## Host ↔ Peripheral Protocol

### Overview

The firmware implements a **half-duplex, framed packet protocol** over either USB CDC (STDIO) or external SPI. The host sends a command, the peripheral processes it, and responds with a status code and optional data.

### Packet Frame Format (both directions)

```
[0xAA] [Command/Response] [Data Size (2 bytes LE)] [Data (n bytes)] [0x55]
```

| Field | Size | Value |
|-------|------|-------|
| Frame start | 1 byte | `0xAA` |
| Command (host) / Response (peripheral) | 1 byte | See tables below |
| Data size | 2 bytes | Little-endian, 0–65535 |
| Data | n bytes | Command-specific payload |
| Frame end | 1 byte | `0x55` |

### Host → Peripheral Commands

From [src/main.c](src/main.c) `host_command_t`:

| Command | Byte | Payload | Purpose |
|---------|------|---------|---------|
| `SEND_FRAME` | 0 | CAN frame + options | Send a CAN frame on specified controller |
| `RECV_RX_EVENTS` | 1 | Controller ID (1 byte) | Request pending RX events |
| `RECV_TX_EVENTS` | 2 | Controller ID (1 byte) | Request pending TX events |
| `RECV_CAN_INFO` | 3 | (reserved) | Reserved for future use |

### Peripheral → Host Responses

From [src/main.c](src/main.c) `command_resp_t`:

| Response | Value | Meaning |
|----------|-------|---------|
| `CMD_RESPONSE_OK` | 0 | Success |
| `CMD_RESPONSE_BAD_PKT` | 1 | Frame start/end invalid |
| `CMD_RESPONSE_CMD_UNKNOWN` | 2 | Unknown command code |
| `CMD_RESPONSE_CMD_MALFORMED` | 3 | Payload size mismatch or invalid parameters |
| `CMD_RESPONSE_TIMEOUT` | 4 | Timeout waiting for input |
| `CMD_RESPONSE_FAILED` | 5 | Command execution failed (e.g., TX queue full) |
| `CMD_RESPONSE_NO_RESOURCES` | 6 | Memory allocation failed |

### SEND_FRAME Command Details

**Payload structure:**

```
[Controller ID] [Frame Options] [DLC] [Arbitration ID] [User Ref?] [Data]
```

| Field | Size | Notes |
|-------|------|-------|
| Controller ID | 1 byte | 0–5 (for 6 controllers) |
| Frame Options | 1 byte | Bit flags (see below) |
| DLC | 1 byte | Data length 0–8 |
| Arbitration ID | 2 or 4 bytes | 2 bytes if standard (11-bit), 4 bytes if extended (29-bit) |
| User Reference | 4 bytes | *Optional*, only if `FRAME_OPTION_USE_UREF` set |
| Frame Data | 0–8 bytes | Omitted if remote frame; length = DLC |

**Frame Options Flags:**

| Flag | Bit | Meaning |
|------|-----|---------|
| `FRAME_OPTION_EXTENDED` | 0 (1<<0) | 29-bit arbitration ID (else 11-bit) |
| `FRAME_OPTION_REMOTE` | 1 (1<<1) | Remote frame (no data payload) |
| `FRAME_OPTION_USE_FIFO` | 2 (1<<2) | Use TX FIFO (else priority queue) |
| `FRAME_OPTION_USE_UREF` | 3 (1<<3) | Include 4-byte user reference |

**Example: Send 8-byte frame on controller 0, standard ID 0x123**

```
Frame start:     0xAA
Command:         0x00 (SEND_FRAME)
Data size:       0x0C, 0x00 (12 bytes)
Controller:      0x00
Options:         0x00 (standard, not remote, priority queue, no uref)
DLC:             0x08 (8 bytes)
Arbitration ID:  0x23, 0x01 (0x0123 LE)
Frame data:      0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11
Frame end:       0x55
```

### RX/TX Events

- **`RECV_RX_EVENTS`** — Returns a packed buffer of receive events. Each event is `NUM_RX_EVENT_BYTES` bytes (19 bytes typically). Include the CAN frame, timestamp, and event type.
- **`RECV_TX_EVENTS`** — Returns a packed buffer of transmit events. Each event is `NUM_TX_EVENT_BYTES` bytes (9 bytes typically). Include user reference and timestamp.

See [lib/mcp25xxfd/canapi.h](lib/mcp25xxfd/canapi.h) for event structure definitions.

## Implementation Notes

### Key Points

- **Half-duplex protocol** — Host must wait for peripheral response before sending the next command.
- **Transport selection** — `PKT_OVER_STDIO` in [src/main.c](src/main.c) switches between USB CDC and external SPI.
- **Shared SPI bus** — All 6 CAN controllers share one SPI bus; IRQ handling adapted in [src/can.c](src/can.c).
- **Packet validation** — Strict size checking; mismatches return `CMD_RESPONSE_CMD_MALFORMED`.
- **Retry logic** — Controller setup retries up to `CAN_SETUP_MAX_RETRIES` times; useful if 3.3V present but 5V absent (affects transceiver power).

### Logging Output

- **Default:** USB CDC (`stdio_usb`)
- **Also available:** UART (`stdio_uart`)
- **Configure:** [CMakeLists.txt](CMakeLists.txt) — `pico_enable_stdio_usb()` and `pico_enable_stdio_uart()`

### Custom Bitrate

To use a custom bitrate, set `CAN_BITRATE` to `CAN_BITRATE_CUSTOM` and populate the `can_bitrate_t` struct in [src/main.c](src/main.c):

```c
can_bitrate_t bitrate = {
    .profile = CAN_BITRATE_CUSTOM,
    .brp = ...,       // Baud rate prescaler
    .tseg1 = ...,     // TSEG1 - 1
    .tseg2 = ...,     // TSEG2 - 1
    .sjw = ...        // SJW - 1
};
```

## Important Files Summary

| File | Purpose |
|------|---------|
| [src/main.c](src/main.c) | Main loop, packet parsing, command handlers |
| [src/can.c](src/can.c) | CAN setup, IRQ handler, event retrieval |
| [include/pins.h](include/pins.h) | Pin definitions |
| [include/logging.h](include/logging.h) | Logging configuration |
| [include/can.h](include/can.h) | Public CAN constants |
| [lib/mcp25xxfd/canapi.h](lib/mcp25xxfd/canapi.h) | CAN API, bitrates, structures |
| [CMakeLists.txt](CMakeLists.txt) | Build configuration |

## Troubleshooting

**CAN controller fails to initialize**
- Check that 5V power is present on the transceiver (separate from 3.3V).
- Verify SPI pins and CS/IRQ pin assignments in [include/pins.h](include/pins.h).

**No USB CDC output**
- Ensure `pico_enable_stdio_usb()` is active in [CMakeLists.txt](CMakeLists.txt).
- Try toggling `PKT_OVER_STDIO` in [src/main.c](src/main.c) to use external SPI instead.

**Packets not received**
- Check transport (USB CDC vs. external SPI) is correctly selected and connected.
- Verify frame start (`0xAA`) and end (`0x55`) bytes are correct.
- Confirm controller ID is in range 0–5.

## Next Steps

- Add a Python host example that sends `SEND_FRAME` packets over USB CDC.
- Add `.vscode/launch.json` snippet for OpenOCD + GDB debugging (hardware debugger required).
- Add unit tests for packet parsing and frame layout validation.

---

Generated from firmware code analysis. Last updated: April 2026.


