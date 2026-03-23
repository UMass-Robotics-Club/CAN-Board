from __future__ import annotations
import time
import serial
import struct
import enum

class HostCommand(enum.IntEnum):
    SEND_FRAME = 0x00
    RECV_RX_EVENTS = 0x01
    RECV_TX_EVENTS = 0x02
    RECV_CAN_INFO = 0x03


class CommandResponse(enum.IntEnum):
    CMD_RESPONSE_OK = 0x00
    CMD_RESPONSE_BAD_PKT = 0x01
    CMD_RESPONSE_CMD_UNKNOWN = 0x02
    CMD_RESPONSE_CMD_MALFORMED = 0x03
    CMD_RESPONSE_TIMEOUT = 0x04
    CMD_RESPONSE_FAILED = 0x05
    CMD_RESPONSE_NO_RESOURCES = 0x06


class FrameOption(enum.IntFlag):
    NONE = 0
    EXTENDED = (1 << 0)  # Use CAN extended 29 bit arbitration
    REMOTE = (1 << 1)    # This is a CAN remote frame
    USE_FIFO = (1 << 2)  # Use TX FIFO instead of directly going into TX priority queue
    USE_UREF = (1 << 3)  # Add a user reference to the frame (else it will default to 0)


class CANErrorCode(enum.IntEnum):
    CAN_ERC_NO_ERROR = 0         # OK
    CAN_ERC_BAD_BITRATE = 1      # Baud rate settings are not legal
    CAN_ERC_RANGE = 2            # Range error on parameters
    CAN_ERC_BAD_INIT = 3         # Can't get the controller to initialize
    CAN_ERC_NO_ROOM = 4          # No room
    CAN_ERC_NO_ROOM_PRIORITY = 5 # No room in the transmit priority queue
    CAN_ERC_NO_ROOM_FIFO = 6     # No room in the transmit FIFO queue
    CAN_ERC_BAD_WRITE = 7        # Write to a controller register failed
    CAN_ERC_NO_INTERFACE = 8     # No interface binding set for the controller


class EventType(enum.IntEnum):
    CAN_EVENT_TYPE_TRANSMITTED_FRAME = 0  # Frame transmitted
    CAN_EVENT_TYPE_RECEIVED_FRAME = 1     # Frame received
    CAN_EVENT_TYPE_OVERFLOW = 2           # FIFO overflow happened
    CAN_EVENT_TYPE_CAN_ERROR = 3          # CAN error frame received


class CANTXEvent:
    def __init__(self, data: bytes):
        assert len(data) == 9, "TX event data must be 9 bytes"
        self.data = data
        self.type = EventType(data[0])
        self.timestamp: int = struct.unpack_from(">I", data, 5)[0]
        tmp = struct.unpack_from(">I", data, 1)[0]
        self.user_ref: int | None = tmp if self.type == EventType.CAN_EVENT_TYPE_TRANSMITTED_FRAME else None
        self.overflow_count: int | None = tmp if self.type == EventType.CAN_EVENT_TYPE_OVERFLOW else None

    def __str__(self):
        if self.type == EventType.CAN_EVENT_TYPE_TRANSMITTED_FRAME:
            return f"TX Event: Transmitted frame (user_ref={hex(self.user_ref)}, timestamp={self.timestamp})"
        elif self.type == EventType.CAN_EVENT_TYPE_OVERFLOW:
            return f"TX Event: Overflow frame (count={self.overflow_count}, timestamp={self.timestamp})"

    def __repr__(self):
        return "CANTXEvent(type={}, user_ref={}, overflow_count={}, timestamp={})".format(
            self.type, hex(self.user_ref) if self.user_ref is not None else None,
            self.overflow_count, self.timestamp)


class CANRXFrameData:
    def __init__(self, data: bytes):
        self.remote: bool = bool(data[0] & 0x80)
        self.dlc: int = data[5]
        self.id_filter: int = data[6]  # This specifies which ID filter matched the frame
        self.data: bytes | None = None if self.remote else data[11:11 + self.dlc]  # No data for remote frames

        can_id: int = struct.unpack_from(">I", data, 7)[0]
        self.extended = bool(can_id & (1 << 29))
        can_id_a = can_id & 0x7FF
        can_id_b = (can_id >> 11) & 0x3FFFF
        self.arbitration_id: int = can_id_a << 18 | can_id_b if self.extended else can_id_a

    def __str__(self):
        return (
            f"RX Event: Received frame (extended={self.extended}, remote={self.remote}, "
            f"arbitration_id={hex(self.arbitration_id)}, dlc={self.dlc}, "
            f"data={self.data.hex() if self.data else None}, id_filter={self.id_filter})"
        )

    def __repr__(self):
        return "CANRXFrameData(extended={}, remote={}, arbitration_id={}, dlc={}, data={}, id_filter={})".format(
            self.extended, self.remote, hex(self.arbitration_id), self.dlc,
            self.data.hex() if self.data else None, self.id_filter)


class CANRXErrorData:
    def __init__(self, data: bytes):
        self.details: int = struct.unpack_from(">I", data, 7)[0]

    def __str__(self):
        return f"RX Event: CAN error frame (details={hex(self.details)})"

    def __repr__(self):
        return "CANRXErrorData(details={})".format(hex(self.details))


class CANRXOverflowData:
    def __init__(self, data: bytes):
        self.frame_count, self.error_count = struct.unpack_from(">II", data, 7)

    def __str__(self):
        return f"RX Event: Overflow frame (frame_count={self.frame_count}, error_count={self.error_count})"

    def __repr__(self):
        return f"CANRXOverflowData(frame_count={self.frame_count}, error_count={self.error_count})"


class CANRXEvent:
    def __init__(self, data: bytes):
        assert len(data) == 19, "RX event data must be 19 bytes"
        self.data = data
        self.type = EventType(data[0] & 0x7F)
        self.timestamp: int = struct.unpack_from(">I", data, 1)[0]
        self.frame: CANRXFrameData | None = CANRXFrameData(data) if self.type == EventType.CAN_EVENT_TYPE_RECEIVED_FRAME else None
        self.error: CANRXErrorData | None = CANRXErrorData(data) if self.type == EventType.CAN_EVENT_TYPE_CAN_ERROR else None
        self.overflow: CANRXOverflowData | None = CANRXOverflowData(data) if self.type == EventType.CAN_EVENT_TYPE_OVERFLOW else None

    def __str__(self):
        if self.type == EventType.CAN_EVENT_TYPE_RECEIVED_FRAME:
            return str(self.frame)
        elif self.type == EventType.CAN_EVENT_TYPE_CAN_ERROR:
            return str(self.error)
        elif self.type == EventType.CAN_EVENT_TYPE_OVERFLOW:
            return str(self.overflow)
        else:
            return f"Unknown RX event type {self.type} with timestamp {self.timestamp}"

    def __repr__(self):
        return "CANRXEvent(type={}, timestamp={}, frame={}, error={}, overflow={})".format(
            self.type, self.timestamp, repr(self.frame), repr(self.error), repr(self.overflow))


class CANChannel:
    def __init__(self, board: "CANBoard", controller_num: int):
        self.board = board
        self.controller_num = controller_num

    def send_frame(self, arbitration_id: int, data: bytes = b"", options: FrameOption = FrameOption.NONE, user_ref: int = 0):
        self.board.send_frame(self.controller_num, arbitration_id, data, options=options, user_ref=user_ref)

    def get_rx_events(self) -> list[CANRXEvent]:
        return self.board.get_rx_events(self.controller_num)

    def get_rx_events_blocking(self, timeout: float = 0) -> list[CANRXEvent]:
        return self.board.get_rx_events_blocking(self.controller_num, timeout)

    def get_tx_events(self) -> list[CANTXEvent]:
        return self.board.get_tx_events(self.controller_num)


class CANBoard:
    def __init__(self, port: str = "/dev/ttyACM0", baudrate: int = 115200, serial_timeout: float = 2.0):
        # serial_timeout prevents make_transaction from hanging forever if the device stops responding
        self.com = serial.Serial(port, baudrate, timeout=serial_timeout)
        self.channels = [CANChannel(self, i) for i in range(6)]  # Create 6 channels (0-5)

    def make_transaction(self, command: int, payload: bytes) -> tuple[int, bytes]:
        # Send packet
        send_bytes = b"\xAA" + struct.pack("<BH", command, len(payload)) + payload + b"\x55"
        self.com.write(send_bytes)

        # Scan for 0xAA start byte, buffering in chunks for efficiency
        buf = bytearray()
        while True:
            chunk = self.com.read(self.com.in_waiting or 1)
            if chunk is None or len(chunk) == 0:
                raise TimeoutError("Timed out waiting for response start byte 0xAA")
            buf.extend(chunk)
            idx = buf.find(b"\xAA")
            if idx != -1:
                non_cmd_data = buf[:idx]
                break

        # Print any non-command data received (e.g. debug messages from device)
        if non_cmd_data:
            print("Received non-command data from device:")
            for line in non_cmd_data.split(b"\n"):
                if not line.strip():
                    continue
                print(f"> {line.decode(errors='replace')}")

        header = self.com.read(3)
        if len(header) < 3:
            raise TimeoutError("Timed out reading response header")
        ret_code, length = struct.unpack("<BH", header)

        payload = self.com.read(length) if length else b""
        if length and len(payload) < length:
            raise TimeoutError("Timed out reading response payload")

        end = self.com.read(1)
        if end is None or len(end) == 0:
            raise TimeoutError("Timed out reading response end byte")
        assert end == b"\x55", f"Expected frame end byte 0x55, but got {end}"

        return ret_code, payload

    def send_frame(self, controller: int, arbitration_id: int, frame_data: bytes = b"", options: FrameOption = FrameOption.NONE, user_ref: int = 0, dlc: int = 0):
        assert 0 <= controller < 6, "Controller number must be between 0 and 5"
        assert len(frame_data) <= 8, "Frame data must be at most 8 bytes"
        assert (len(frame_data) == 0) or not (options & FrameOption.REMOTE), "Remote frames cannot have data"

        payload = (
            struct.pack("<BBB", controller, options, dlc if (options & FrameOption.REMOTE) else len(frame_data))
            + struct.pack("<I" if options & FrameOption.EXTENDED else "<H", arbitration_id)
        )
        if options & FrameOption.USE_UREF:
            payload += struct.pack("<I", user_ref)
        payload += frame_data

        ret_code, ret_data = self.make_transaction(HostCommand.SEND_FRAME, payload)

        if ret_code != CommandResponse.CMD_RESPONSE_OK:
            raise Exception(f"Failed to send CAN frame: {CommandResponse(ret_code).name} with data {ret_data.hex()}")

    def get_rx_events(self, controller: int) -> list[CANRXEvent]:
        assert 0 <= controller < 6, "Controller number must be between 0 and 5"

        payload = struct.pack("<B", controller)
        ret_code, ret_data = self.make_transaction(HostCommand.RECV_RX_EVENTS, payload)

        if ret_code != CommandResponse.CMD_RESPONSE_OK:
            raise Exception(f"Failed to get RX events: {CommandResponse(ret_code).name}")

        events = []
        for i in range(0, len(ret_data), 19):
            event_data = ret_data[i:i + 19]
            if len(event_data) < 19:
                print(f"Warning: Incomplete RX event data received (expected 19 bytes, got {len(event_data)}), skipping")
                continue
            events.append(CANRXEvent(event_data))

        return events

    def get_rx_events_blocking(self, controller: int, timeout: float = 0) -> list[CANRXEvent]:
        """Poll for RX events until at least one arrives.

        Args:
            controller: CAN controller index (0-5).
            timeout: Maximum wait time in seconds. 0 means wait indefinitely.
        """
        start = time.time()
        while timeout == 0 or (time.time() - start < timeout):
            events = self.get_rx_events(controller)
            if events:
                return events
            time.sleep(0.001)  # Yield to avoid saturating the serial bus

        raise TimeoutError(f"No RX events received for controller {controller} within {timeout} seconds")

    def get_tx_events(self, controller: int) -> list[CANTXEvent]:
        assert 0 <= controller < 6, "Controller number must be between 0 and 5"

        payload = struct.pack("<B", controller)
        ret_code, ret_data = self.make_transaction(HostCommand.RECV_TX_EVENTS, payload)
        if ret_code != CommandResponse.CMD_RESPONSE_OK:
            raise Exception(f"Failed to get TX events: {CommandResponse(ret_code).name}")

        events = []
        for i in range(0, len(ret_data), 9):
            event_data = ret_data[i:i + 9]
            if len(event_data) < 9:
                print(f"Warning: Incomplete TX event data received (expected 9 bytes, got {len(event_data)}), skipping")
                continue
            events.append(CANTXEvent(event_data))
        return events


def perf_test(board: CANBoard):
    import random

    start = time.time()
    count = 0
    while time.time() - start < 10:
        extended = random.choice([True, False])
        use_uref = random.choice([True, False])

        arbitration_id = random.randint(0, 0x1FFFFFFF if extended else 0x7FF)
        data = random.randbytes(random.randint(0, 8))
        uref = random.randint(0, 0xFFFFFFFF) if use_uref else 0
        options = FrameOption(0)
        if extended:
            options |= FrameOption.EXTENDED
        if use_uref:
            options |= FrameOption.USE_UREF

        board.send_frame(0, arbitration_id, data, options=options, user_ref=uref)
        board.get_rx_events_blocking(0, timeout=1.0)  # Wait for TX ack
        board.get_rx_events_blocking(1, timeout=1.0)  # Wait for loopback RX

        count += 1

    print(f"Sent and received {count} frames in 10 seconds ({count / 10:.2f} frames/sec)")
