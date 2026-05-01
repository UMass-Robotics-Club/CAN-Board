import enum
import time
import struct

from can_api import CANBoard, CANChannel, FrameOption, EventType

class MotorProtocol(enum.IntEnum):
    PRIVATE = 0
    CAN_OPEN = 1
    MIT = 2

#############
# Private Protocol
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

def private_send_communication_frame(ch: CANChannel, host_id: int, motor_id: int, communication_type: CommunicationType, payload: bytes = bytes(8)):
    assert len(payload) == 8, "Payload must be 8 bytes"
    arbitration_id = (communication_type << 24) | (host_id << 8) | motor_id
    ch.send_frame(arbitration_id, payload, options=FrameOption.EXTENDED)

def private_get_communication_response(ch: CANChannel, host_id: int, timeout: float = 1.0) -> tuple[int, int, bytes]:
    # Read RX events and find the one with matching host_id and motor_id in the arbitration ID
    start_time = time.time()
    while time.time() - start_time < timeout:
        resp = ch.get_rx_events()
        for event in resp:
            if event.type == EventType.CAN_EVENT_TYPE_RECEIVED_FRAME:
                comm_type = (event.frame.arbitration_id >> 24) & 0x1F
                extra_data = (event.frame.arbitration_id >> 8) & 0xFFFF
                host_id = event.frame.arbitration_id & 0xFF
                if host_id == host_id:
                    return (comm_type, extra_data, event.frame.data)

    raise Exception(f"No response received for host_id={host_id}")

# TODO fix
def private_get_single_param(ch: CANChannel, host_id: int, motor_id: int, Index: int) -> bytes:
    data = int.to_bytes(Index, 2, "little") + bytes(6)
    private_send_communication_frame(ch, host_id, motor_id, CommunicationType.GET_SINGLE_PARAMETER, data)
    return private_get_communication_response(ch, host_id)[2]

def _private_get_MCU_id(ch: CANChannel, host_id: int, motor_id: int, timeout: float = 1.0) -> bytes:
    start = time.time()
    while time.time() - start < timeout:
        resp = ch.get_rx_events()
        for event in resp:
            if event.type == EventType.CAN_EVENT_TYPE_RECEIVED_FRAME:
                if event.frame.arbitration_id == (motor_id << 8 | 0xFE):
                    return event.frame.data

    raise Exception(f"No response received for motor_id={motor_id} (host_id={host_id})")

def private_get_MCU_id(ch: CANChannel, host_id: int, motor_id: int, timeout: float = 1.0) -> bytes:
    private_send_communication_frame(ch, host_id, motor_id, CommunicationType.GET_DEVICE_ID)
    return _private_get_MCU_id(ch, host_id, motor_id, timeout)

def private_set_motor_protocol(ch: CANChannel, host_id: int, motor_id: int, protocol: MotorProtocol, timeout: float = 1.0):
    private_send_communication_frame(ch, host_id, motor_id, CommunicationType.SET_MOTOR_PROTOCOL, b"\x01\x02\x03\x04\x05\x06" + int.to_bytes(protocol, 2, "little"))
    return _private_get_MCU_id(ch, host_id, motor_id, timeout)


#############
# CANopen Protocol
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

def canopen_set_motor_protocol(ch: CANChannel, protocol: MotorProtocol, motor_id: int | None = None, timeout: float = 1.0) -> tuple[int, bytes]:
    ch.send_frame(0xFFF, b"\x01\x02\x03\x04\x05\x06" + int.to_bytes(protocol, 2, "little"), options=FrameOption.EXTENDED)
    start = time.time()
    while time.time() - start < timeout:
        resp = ch.get_rx_events()
        for event in resp:
            if event.type == EventType.CAN_EVENT_TYPE_RECEIVED_FRAME:
                if not event.frame.extended and (event.frame.arbitration_id == motor_id or motor_id is None):
                    return (event.frame.arbitration_id, event.frame.data)
    
    raise Exception("No response received for set_motor_protocol")
                
def canopen_send_message(ch: CANChannel, function: CANOpenFunctionCode, data: bytes = b'', motor_id: int | None = None):
    arbitration_id = function.value + (motor_id if motor_id is not None else 0)
    ch.send_frame(arbitration_id, data)

def canopen_send_MNT(ch: CANChannel, command: CANOpenNMTCommand, motor_id: int = 0):
    canopen_send_message(ch, CANOpenFunctionCode.NMT, struct.pack("BB", command, motor_id))

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
def canopen_SDO_read(ch: CANChannel, index: int, subindex: int, motor_id: int, timeout: float = 1.0) -> bytes:
    payload = canopen_generate_SDO_message(CANOpenSDOCommandSpecifier.INIT_UPLOAD, False, False, index, subindex)
    canopen_send_message(ch, CANOpenFunctionCode.SDO_RX, payload, motor_id)
    start = time.time()
    while time.time() - start < timeout:
        resp = ch.get_rx_events()
        for event in resp:
            if event.type == EventType.CAN_EVENT_TYPE_RECEIVED_FRAME:
                if not event.frame.extended and (event.frame.arbitration_id == CANOpenFunctionCode.SDO_TX.value + motor_id):
                    return event.frame.data
    raise TimeoutError("SDO read timed out")

def canopen_SDO_write_expedited(ch: CANChannel, index: int, subindex: int, data: bytes, motor_id: int):
    assert len(data) <= 4, "SDO write data must be at most 4 bytes"
    payload = canopen_generate_SDO_message(CANOpenSDOCommandSpecifier.INIT_DOWNLOAD, True, True, index, subindex, data)
    canopen_send_message(ch, CANOpenFunctionCode.SDO_RX, payload, motor_id)


class SDOIndex(enum.IntEnum):
    MOTOR_STATE = 0x6040
    OPERATION_MODE = 0x6060
    TARGET_TORQUE = 0x6071
    TARGET_VELOCITY = 0x60FF


#########
# MIT Protocol
#########

class MITCommand(enum.IntEnum):
    ENABLE = 0xFC
    DISABLE = 0xFD
    SET_ZERO = 0xFE

def mit_set_motor_protocol(ch: CANChannel, protocol: MotorProtocol, motor_id: int, timeout: float = 1.0) -> bytes:
    payload = b"\xFF\xFF\xFF\xFF\xFF\xFF" + protocol.to_bytes() + b"\xFD"
    ch.send_frame(motor_id, payload)
    start = time.time()
    while time.time() - start < timeout:
        resp = ch.get_rx_events()
        for event in resp:
            if event.type == EventType.CAN_EVENT_TYPE_RECEIVED_FRAME:
                if not event.frame.extended and event.frame.arbitration_id == motor_id:
                    return event.frame.data
    
    raise Exception("No response received for mit_set_motor_protocol")

def _float_to_uint(x: float, x_min: float, x_max: float, bits: int) -> int:
    """Convert float to unsigned int with given range and bit width."""
    assert x_min <= x <= x_max, f"Value {x} out of range [{x_min}, {x_max}]"
    span = x_max - x_min
    return int((x - x_min) * ((1 << bits) - 1) / span)


def _mit_pack_dynamic_params(p: float, v: float, kp: float, kd: float, t: float) -> bytes:
    """
    Pack MIT motor command into 8-byte CAN frame.

    Inputs are floats in their respective ranges.
    Returns: 8 bytes with packed command according to MIT protocol specification.
    """

    # ---- Define limits (adjust per motor spec) ----
    P_MIN, P_MAX = -12.5, 12.5
    V_MIN, V_MAX = -45.0, 45.0
    KP_MIN, KP_MAX = 0.0, 500.0
    KD_MIN, KD_MAX = 0.0, 5.0
    T_MIN, T_MAX = -18.0, 18.0

    # ---- Convert to integers ----
    p_int  = _float_to_uint(p,  P_MIN,  P_MAX, 16)
    v_int  = _float_to_uint(v,  V_MIN,  V_MAX, 12)
    kp_int = _float_to_uint(kp, KP_MIN, KP_MAX, 12)
    kd_int = _float_to_uint(kd, KD_MIN, KD_MAX, 12)
    t_int  = _float_to_uint(t,  T_MIN,  T_MAX, 12)

    # ---- Pack bits ----
    data = bytearray(8)

    data[0] = (p_int >> 8) & 0xFF
    data[1] = p_int & 0xFF
    data[2] = (v_int >> 4) & 0xFF
    data[3] = ((v_int & 0xF) << 4) | ((kp_int >> 8) & 0xF)
    data[4] = kp_int & 0xFF
    data[5] = (kd_int >> 4) & 0xFF
    data[6] = ((kd_int & 0xF) << 4) | ((t_int >> 8) & 0xF)
    data[7] = t_int & 0xFF

    return bytes(data)
     
def mit_send_command(ch: CANChannel, motor_id: int, cmd: MITCommand):
    payload = b"\xFF\xFF\xFF\xFF\xFF\xFF\xFF" + cmd.to_bytes(1, "little")
    ch.send_frame(motor_id, payload)

def mit_send_dynamic_command(ch: CANChannel, motor_id: int, angle: float, speed: float, kp: float, kd: float, torque: float):
    payload = _mit_pack_dynamic_params(angle, speed, kp, kd, torque)
    ch.send_frame(motor_id, payload)

if __name__ == "__main__":

    MOTOR_ID = 0x01

    SPEED = 1
    KP = 10
    KD = 1
    TORQUE = 5   

    START_ANGLE = 0
    END_ANGLE = 10
    STEPS = 50

    board = CANBoard("COM11", 115200)
    ch = board.channels[0]


    print("Sending ENABLE command...")
    mit_send_command(ch, MOTOR_ID, MITCommand.ENABLE)

    # angle = START_ANGLE
    # direction = 1
    # while True:
    #     mit_send_dynamic_command(ch, MOTOR_ID, angle, SPEED, KP, KD, TORQUE)
    #     angle += (END_ANGLE - START_ANGLE) / STEPS * direction
    #     if angle > END_ANGLE or angle < START_ANGLE:
    #         direction *= -1

    while True:
        user_input = input("Enter target angle or 'q' to quit: ")
        if user_input == 'q':
            break
        try:
            angle = float(user_input)
            mit_send_dynamic_command(ch, MOTOR_ID, angle, SPEED, KP, KD, TORQUE)
        except ValueError:
            print("Invalid input. Please enter a valid number.")

    print("Sending DISABLE command...")
    mit_send_command(ch, MOTOR_ID, MITCommand.DISABLE)

    print("Done")
