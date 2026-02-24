import serial
import struct
import enum

com = serial.Serial("/dev/ttyACM1", 115200)


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
    EXTENDED = (1 << 0) # Use CAN extended 29 bit arbitration
    REMOTE = (1 << 1)   # This is a CAN remote frame
    USE_FIFO = (1 << 2) # Use TX FIFO instead of directly going into TX priority queue
    USE_UREF = (1 << 3) # Add a user reference to the frame (else it will default to 0)


class CANErrorCode(enum.IntEnum):
    CAN_ERC_NO_ERROR = 0,                               # OK
    CAN_ERC_BAD_BITRATE=1,                                # Baud rate settings are not legal
    CAN_ERC_RANGE=2,                                      # Range error on parameters
    CAN_ERC_BAD_INIT=3,                                   # Can't get the controller to initialize
    CAN_ERC_NO_ROOM=4,                                    # No room
    CAN_ERC_NO_ROOM_PRIORITY=5,                           # No room in the transmit priority queue
    CAN_ERC_NO_ROOM_FIFO=6,                               # No room in the transmit FIFO queue
    CAN_ERC_BAD_WRITE=7,                                  # Write to a controller register failed
    CAN_ERC_NO_INTERFACE=8,                               # No interface binding set for the controller


class EventType(enum.IntEnum):
    CAN_EVENT_TYPE_TRANSMITTED_FRAME = 0,       # Frame transmitted
    CAN_EVENT_TYPE_RECEIVED_FRAME = 1,          # Frame received
    CAN_EVENT_TYPE_OVERFLOW = 2,                # FIFO overflow happened
    CAN_EVENT_TYPE_CAN_ERROR = 3                # CAN error frame received


class CANTXEvent:
    def __init__(self, data: bytes):
        assert len(data) == 9, "TX event data must be 9 bytes"
        self.data = data
        self.type = EventType(data[0])
        self.timestamp : int = struct.unpack_from(">I", data, 5)[0]
        tmp = struct.unpack_from(">I", data, 1)[0]
        self.user_ref : int | None = tmp if self.type == EventType.CAN_EVENT_TYPE_TRANSMITTED_FRAME else None
        self.overflow_count : int | None = tmp if self.type == EventType.CAN_EVENT_TYPE_OVERFLOW else None

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
        self.id_filter: int = data[6] # This specifies which ID filter matched the frame
        self.data: bytes | None = None if self.remote else data[11:11+self.dlc] # No data for remote frames

        can_id: int = struct.unpack_from(">I", data, 7)[0]
        self.extended = bool(can_id & (1 << 29))
        can_id_a = can_id & 0x7FF
        can_id_b = (can_id >> 11) & 0x3FFFF
        self.arbitration_id: int = can_id_a << 18 | can_id_b if self.extended else can_id_a
        

    def __str__(self):
        return f"RX Event: Received frame (extended={self.extended}, remote={self.remote}, arbitration_id={hex(self.arbitration_id)}, dlc={self.dlc}, data={self.data.hex() if self.data else None}, id_filter={self.id_filter})"

    def __repr__(self):
        return "CANRXFrameData(extended={}, remote={}, arbitration_id={}, dlc={}, data={}, id_filter={})".format(
            self.extended, self.remote, hex(self.arbitration_id), self.dlc, self.data.hex() if self.data else None, self.id_filter)

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
        self.timestamp : int = struct.unpack_from(">I", data, 1)[0]
        self.frame : CANRXFrameData | None = CANRXFrameData(data) if self.type == EventType.CAN_EVENT_TYPE_RECEIVED_FRAME else None
        self.error : CANRXErrorData | None = CANRXErrorData(data) if self.type == EventType.CAN_EVENT_TYPE_CAN_ERROR else None
        self.overflow : CANRXOverflowData | None = CANRXOverflowData(data) if self.type == EventType.CAN_EVENT_TYPE_OVERFLOW else None

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

def make_transaction(command: int, payload: bytes) -> tuple[int, bytes]:
    # Send packet
    send_bytes = b"\xAA" + struct.pack("<BH", command, len(payload)) + payload + b"\x55"
    com.write(send_bytes)

    # Wait for response
    # TODO add timeout and error handling
    # Parse until 0xAA is found (print any messages)
    non_cmd_data = bytearray()
    while True:
        byte = com.read(1)
        if byte == b"\xAA":
            break
        non_cmd_data.extend(byte)
    
    # print any non-command data received (e.g. debug messages)
    if non_cmd_data:
        print("Received non-command data from device:")
        for line in non_cmd_data.split(b"\n"):
            if not line.strip():
                continue
            print(f"> {line.decode()}")

    
    ret_code, length = struct.unpack("<BH", com.read(3))
    payload = com.read(length) if length else b"" # Read the payload
    end = com.read(1)

    assert end == b"\x55", f"Expected frame end byte 0x55, but got {end}"

    return ret_code, payload

def send_can_frame(controller: int, arbitration_id: int, frame_data: bytes = b"", options: FrameOption = FrameOption.NONE, user_ref: int = 0, dlc: int = 0):
    assert 0 <= controller < 6, "Controller number must be between 0 and 5"
    assert len(frame_data) <= 8, "Frame data must be at most 8 bytes"
    assert (len(frame_data) == 0) or not (options & FrameOption.REMOTE), "Remote frames cannot have data"

    payload = struct.pack("<BBB", controller, options, dlc if (options & FrameOption.REMOTE) else len(frame_data)) + struct.pack("<I" if options & FrameOption.EXTENDED else "<H", arbitration_id)
    if options & FrameOption.USE_UREF:
        payload += struct.pack("<I", user_ref)
    payload += frame_data

    ret_code, ret_data = make_transaction(HostCommand.SEND_FRAME, payload)
    
    if ret_code != CommandResponse.CMD_RESPONSE_OK:
        raise Exception(f"Failed to send CAN frame: {CommandResponse(ret_code).name} with data {ret_data.hex()}")

def get_can_rx_events(controller: int) -> list[CANRXEvent]:
    assert 0 <= controller < 6, "Controller number must be between 0 and 5"

    payload = struct.pack("<B", controller)
    ret_code, ret_data = make_transaction(HostCommand.RECV_RX_EVENTS, payload)
    
    if ret_code != CommandResponse.CMD_RESPONSE_OK:
        raise Exception(f"Failed to get RX events: {CommandResponse(ret_code).name}")
    
    events = []
    for i in range(0, len(ret_data), 19):
        event_data = ret_data[i:i+19]
        if len(event_data) < 19:
            print(f"Warning: Incomplete RX event data received (expected 19 bytes, got {len(event_data)}), skipping")
            continue
        events.append(CANRXEvent(event_data))

    return events

def get_can_tx_events(controller: int) -> list[CANTXEvent]:
    assert 0 <= controller < 6, "Controller number must be between 0 and 5"

    payload = struct.pack("<B", controller)
    ret_code, ret_data = make_transaction(HostCommand.RECV_TX_EVENTS, payload)
    if ret_code != CommandResponse.CMD_RESPONSE_OK:
        raise Exception(f"Failed to get TX events: {CommandResponse(ret_code).name}")
    
    events = []
    for i in range(0, len(ret_data), 9):
        event_data = ret_data[i:i+9]
        if len(event_data) < 9:
            print(f"Warning: Incomplete TX event data received (expected 9 bytes, got {len(event_data)}), skipping")
            continue
        events.append(CANTXEvent(event_data))
    return events


def perf_test():
    import time
    import random

    # Small 10 sec performance test
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

        send_can_frame(0, arbitration_id, data, options=options, user_ref=uref)
        while True:
            tx_events = get_can_tx_events(0)
            if tx_events:
                break
        while True:
            rx_events = get_can_rx_events(1)
            if rx_events:
                break
        
        count += 1

    print(f"Sent and received {count} frames in 10 seconds ({count/10:.2f} frames/sec)")



##################
# Robstride 02 testing code below
##################

import time

MOTOR_ID = 0x7F

class MotorProtocol(enum.IntEnum):
    PRIVATE = 0
    CAN_OPEN = 1
    MIT = 2

#############
# For private protocol testing
#############
# Note not a full list!
class CommunicationType(enum.IntEnum):
    GET_DEVICE_ID = 0x0
    SET_CONTROL_PARAMETERS = 0x1
    _MOTOR_FEEDBACK = 0x2
    ENABLE_MOTOR = 0x3
    DISABLE_MOTOR = 0x4
    SET_ZERO = 0x6
    GET_SINGLE_PARAMETER = 0x11
    SET_SINGLE_PARAMETER = 0x12
    GET_MOTOR_FEEDBACK = 0x16
    SET_MOTOR_PROTOCOL = 0x19

def private_send_communication_frame(host_id: int, motor_id: int, communication_type: CommunicationType, payload: bytes = bytes(8)):
    assert len(payload) == 8, "Payload must be 8 bytes"
    arbitration_id = (communication_type << 24) | (host_id << 8) | motor_id
    send_can_frame(0, arbitration_id, payload, options=FrameOption.EXTENDED)

def private_get_communication_response(host_id: int, timeout: float = 1.0) -> tuple[int, int, bytes]:
    # Read RX events and find the one with matching host_id and motor_id in the arbitration ID
    start_time = time.time()
    while time.time() - start_time < timeout:
        resp = get_can_rx_events(0)
        for event in resp:
            if event.type == EventType.CAN_EVENT_TYPE_RECEIVED_FRAME:
                comm_type = (event.frame.arbitration_id >> 24) & 0x1F
                extra_data = (event.frame.arbitration_id >> 8) & 0xFFFF
                host_id = event.frame.arbitration_id & 0xFF
                if host_id == host_id:
                    return (comm_type, extra_data, event.frame.data)

    raise Exception(f"No response received for host_id={host_id}")

# TODO fix
def private_get_single_param(host_id: int, motor_id: int, Index: int) -> bytes:
    data = int.to_bytes(Index, 2, "little") + bytes(6)
    private_send_communication_frame(host_id, motor_id, CommunicationType.GET_SINGLE_PARAMETER, data)
    return private_get_communication_response(host_id)

def _private_get_MCU_id(host_id: int, motor_id: int, timeout: float = 1.0) -> bytes:
    start = time.time()
    while time.time() - start < timeout:
        resp = get_can_rx_events(0)
        for event in resp:
            if event.type == EventType.CAN_EVENT_TYPE_RECEIVED_FRAME:
                if event.frame.arbitration_id == (motor_id << 8 | 0xFE):
                    return event.frame.data

    raise Exception(f"No response received for motor_id={motor_id} (host_id={host_id})")

def private_get_MCU_id(host_id: int, motor_id: int, timeout: float = 1.0) -> bytes:
    private_send_communication_frame(host_id, motor_id, CommunicationType.GET_DEVICE_ID)
    return _private_get_MCU_id(host_id, motor_id, timeout)

def private_set_motor_protocol(host_id: int, motor_id: int, protocol: MotorProtocol, timeout: float = 1.0):
    private_send_communication_frame(host_id, motor_id, CommunicationType.SET_MOTOR_PROTOCOL, b"\x01\x02\x03\x04\x05\x06" + int.to_bytes(protocol, 2, "little"))
    return _private_get_MCU_id(host_id, motor_id, timeout)


#############
# CANopen protocol testing
#############

class CANOpenFunctionCode(enum.IntEnum):
    NMT = 0x0
    SYNC = 0x80
    TIME_STAMP = 0x100
    PDO1_TX = 0x180
    PDO1_RX = 0x200
    PDO2_TX = 0x280
    PDO2_RX = 0x300
    PDO3_TX = 0x380
    PDO3_RX = 0x400
    PDO4_TX = 0x480
    PDO4_RX = 0x500
    SDO_TX = 0x580
    SDO_RX = 0x600
    EMCY = 0x80
    LSS_TX = 0x7E4
    LSS_RX = 0x7E5

class CANOpenNMTCommand(enum.IntEnum):
    ENTER_OPERATIONAL = 0x01
    ENTER_STOPPED = 0x02
    ENTER_PRE_OPERATIONAL = 0x80
    RESET_NODE = 0x81
    RESET_COMMUNICATION = 0x82

class CANOpenSDOCommandSpecifier(enum.IntEnum):
    SEGMENT_DOWNLOAD = 0x00
    INIT_DOWNLOAD = 0x01
    INIT_UPLOAD = 0x02
    SEGMENT_UPLOAD = 0x03
    ABORT_TRANSFER = 0x04
    BLOCK_UPLOAD = 0x05
    BLOCK_DOWNLOAD = 0x06

def canopen_set_motor_protocol(protocol: MotorProtocol, timeout: float = 1.0, motor_id: int | None = None) -> tuple[int, bytes]:
    send_can_frame(0, 0xFFF, b"\x01\x02\x03\x04\x05\x06" + int.to_bytes(protocol, 2, "little"))
    start = time.time()
    while time.time() - start < timeout:
        resp = get_can_rx_events(0)
        for event in resp:
            if event.type == EventType.CAN_EVENT_TYPE_RECEIVED_FRAME:
                if not event.frame.extended and (event.frame.arbitration_id == motor_id or motor_id is None):
                    return (event.frame.arbitration_id, event.frame.data)
                
def canopen_send_message(function: CANOpenFunctionCode, data: bytes = b'', motor_id: int | None = None):
    arbitration_id = function.value + (motor_id if motor_id is not None else 0)
    send_can_frame(0, arbitration_id, data)

def canopen_send_MNT(command: CANOpenNMTCommand, motor_id: int = 0):
    canopen_send_message(CANOpenFunctionCode.NMT, struct.pack("BB", command, motor_id))

def canopen_generate_SDO_message(command: CANOpenSDOCommandSpecifier, expedited: bool, size_included: bool, index: int, subindex: int, data: bytes = b'', size: int = 0) -> bytes:
    assert len(data) <= 4, "SDO data must be at most 4 bytes"
    cmd_specifier = command.value << 5
    if expedited:
        cmd_specifier |= 0x02
    if size_included:
        cmd_specifier |= 0x01
        if expedited:
            # Size is in command specifier
            cmd_specifier |= (4 - len(data)) << 2
        else:
            # Size is the data
            data = int.to_bytes(size, 4, 'little')
    return struct.pack("<BHB", cmd_specifier, index, subindex) + data.ljust(4, b"\x00")
    
# TODO add SDO upload and download functions that handle segmentation for larger data sizes
def canopen_SDO_read(index: int, subindex: int, motor_id: int, timeout: float = 1.0) -> bytes:
    payload = canopen_generate_SDO_message(CANOpenSDOCommandSpecifier.INIT_UPLOAD, False, False, index, subindex)
    canopen_send_message(CANOpenFunctionCode.SDO_RX, payload, motor_id)
    start = time.time()
    while time.time() - start < timeout:
        resp = get_can_rx_events(0)
        for event in resp:
            if event.type == EventType.CAN_EVENT_TYPE_RECEIVED_FRAME:
                if not event.frame.extended and (event.frame.arbitration_id == CANOpenFunctionCode.SDO_TX.value + motor_id):
                    return event.frame.data
    raise TimeoutError("SDO read timed out")

def canopen_SDO_write_expedited(index: int, subindex: int, data: bytes, motor_id: int):
    assert len(data) <= 4, "SDO write data must be at most 4 bytes"
    payload = canopen_generate_SDO_message(CANOpenSDOCommandSpecifier.INIT_DOWNLOAD, True, True, index, subindex, data)
    canopen_send_message(CANOpenFunctionCode.SDO_RX, payload, motor_id)


class SDOIndex(enum.IntEnum):
    MOTOR_STATE = 0x6040
    OPERATION_MODE = 0x6060
    TARGET_TORQUE = 0x6071
    TARGET_VELOCITY = 0x60FF

print(f"Device Type: {int.from_bytes(canopen_SDO_read(0x603F, 0x00, MOTOR_ID), 'little')}") # Read the "Device Type" object

print("Setting motor to ready to enable")
canopen_SDO_write_expedited(SDOIndex.MOTOR_STATE, 0x00, int.to_bytes(6, 2, 'little'), MOTOR_ID) 
print("Setting motor to enabled")
canopen_SDO_write_expedited(SDOIndex.MOTOR_STATE, 0x00, int.to_bytes(7, 2, 'little'), MOTOR_ID) 

print("Configuring motor for velocity control mode...")
print("Setting operation mode to velocity control (0x3)")
canopen_SDO_write_expedited(SDOIndex.OPERATION_MODE, 0x00, int.to_bytes(3, 1, 'little'), MOTOR_ID)
print("Setting torque limit to 50")
canopen_SDO_write_expedited(SDOIndex.TARGET_TORQUE, 0x00, int.to_bytes(50, 2, 'little'), MOTOR_ID)
print("Setting target velocity to 0 (safe start)")
canopen_SDO_write_expedited(SDOIndex.TARGET_VELOCITY, 0x00, int.to_bytes(0, 4, 'little'), MOTOR_ID)
print("Motor configured!")

input("Press Enter to start motor at target velocity of 100...")
canopen_SDO_write_expedited(SDOIndex.MOTOR_STATE, 0x00, int.to_bytes(15, 2, 'little'), MOTOR_ID)
canopen_SDO_write_expedited(SDOIndex.TARGET_VELOCITY, 0x00, int.to_bytes(100, 4, 'little'), MOTOR_ID)
time.sleep(10)
print("Stopping motor...")
canopen_SDO_write_expedited(SDOIndex.TARGET_VELOCITY, 0x00, int.to_bytes(0, 4, 'little'), MOTOR_ID)
canopen_SDO_write_expedited(SDOIndex.MOTOR_STATE, 0x00, int.to_bytes(1, 2, 'little'), MOTOR_ID) 