import math
import serial
import struct
import time
from dataclasses import dataclass
from enum import IntEnum

from can_connector import CANBoard, CANChannel, CANRXEvent, FrameOption

COM_PORT = "COM11"
SPARK_ID = 1


class SparkParamType(IntEnum):
    UNUSED = 0
    INT = 1
    UINT = 2
    FLOAT = 3
    BOOL = 4


class SparkParamWriteResult(IntEnum):
    SUCCESS = 0
    INVALID_ID = 1
    MISMATCHED_TYPE = 2
    ACCESS_MODE = 3
    INVALID = 4
    NOT_IMPLEMENTED = 5


@dataclass
class SparkStatus0:
    applied_output: float
    bus_voltage: float
    output_current: float
    motor_temperature_c: int | None
    hard_forward_limit_reached: bool
    hard_reverse_limit_reached: bool
    soft_forward_limit_reached: bool
    soft_reverse_limit_reached: bool
    inverted: bool
    primary_heartbeat_lock: bool
    spark_model: int


@dataclass
class SparkStatus2:
    primary_encoder_velocity: float
    primary_encoder_position: float


def make_FRC_arbitration_id(
    device_type: int,
    manufacturer: int,
    api_class: int,
    api_index: int,
    device_id: int,
) -> int:
    return (
        ((device_type & 0x1F) << 24)
        | ((manufacturer & 0xFF) << 16)
        | ((api_class & 0x3F) << 10)
        | ((api_index & 0xF) << 6)
        | (device_id & 0x3F)
    )


class SparkMax:
    DEVICE_TYPE_MOTOR_CONTROLLER = 0x02
    MANUFACTURER_REV = 0x05

    FRC_HEARTBEAT_ID = make_FRC_arbitration_id(0x01, 0x01, 0x06, 0x01, 0x00)

    DUTY_CYCLE_SETPOINT_FRAME_ID = 0x2050080
    VELOCITY_SETPOINT_FRAME_ID = 0x2050000
    POSITION_SETPOINT_FRAME_ID = 0x2050100
    VOLTAGE_SETPOINT_FRAME_ID = 0x2050140
    CURRENT_SETPOINT_FRAME_ID = 0x2050180

    GET_FIRMWARE_VERSION_FRAME_ID = 0x2052600
    PARAMETER_WRITE_FRAME_ID = 0x2053800
    PARAMETER_WRITE_RESPONSE_FRAME_ID = 0x2053840
    GET_PARAMETER_TYPES_BASE_FRAME_ID = 0x2053400
    READ_PARAMETER_BASE_FRAME_ID = 0x2053C00

    STATUS_0_FRAME_ID = 0x205B800
    STATUS_1_FRAME_ID = 0x205B840
    STATUS_2_FRAME_ID = 0x205B880

    SPARK_K_STATUS0_PERIOD = 158
    SPARK_K_STATUS1_PERIOD = 159
    SPARK_K_STATUS2_PERIOD = 160

    def __init__(self, channel: CANChannel, device_id: int) -> None:
        self.channel = channel
        self.device_id = device_id
        self._last_good_bus_voltage: float | None = None
        self._last_good_current: float | None = None
        self._last_good_temp_c: int | None = None

    def _frame_id_for_device(self, base_frame_id: int) -> int:
        return base_frame_id + self.device_id

    def _send_remote_request(self, arbitration_id: int, dlc: int = 8) -> None:
        self.channel.board.send_frame(
            self.channel.controller_num,
            arbitration_id,
            b"",
            options=FrameOption.EXTENDED | FrameOption.REMOTE,
            dlc=dlc,
        )

    def _wait_for_frame_data(self, expected_arbitration_id: int, timeout: float) -> bytes:
        start = time.time()
        while time.time() - start < timeout:
            events: list[CANRXEvent] = self.channel.get_rx_events()
            for event in events:
                if event.frame is None:
                    continue
                frame = event.frame
                if frame.arbitration_id == expected_arbitration_id and frame.data is not None:
                    return frame.data
        raise TimeoutError(f"Timed out waiting for frame {hex(expected_arbitration_id)}")

    def _request_frame_data(self, frame_id: int, timeout: float = 0.2, dlc: int = 8) -> bytes:
        # Some setups provide periodic status frames without needing an explicit request.
        passive_window = max(0.02, timeout * 0.25)
        try:
            return self._wait_for_frame_data(frame_id, timeout=passive_window)
        except TimeoutError:
            pass

        # First probe with an RTR frame.
        request_window = max(0.03, timeout * 0.375)
        try:
            self._send_remote_request(frame_id, dlc=dlc)
            return self._wait_for_frame_data(frame_id, timeout=request_window)
        except TimeoutError:
            pass

        # Fallback: some firmwares expect a standard data frame instead of RTR.
        self.channel.send_frame(frame_id, b"\x00" * dlc, FrameOption.EXTENDED)
        return self._wait_for_frame_data(frame_id, timeout=max(0.03, timeout * 0.375))

    def send_frc_heartbeat(self) -> None:
        self.channel.send_frame(
            self.FRC_HEARTBEAT_ID,
            b"\xff" * 8,
            FrameOption.EXTENDED,
        )

    def set_duty_cycle(self, duty_cycle: float) -> None:
        duty_cycle = max(-1.0, min(1.0, duty_cycle))
        self.channel.send_frame(
            self._frame_id_for_device(self.DUTY_CYCLE_SETPOINT_FRAME_ID),
            struct.pack("<f", duty_cycle) + (b"\x00" * 4),
            FrameOption.EXTENDED,
        )

    def set_voltage(self, volts: float) -> None:
        self.channel.send_frame(
            self._frame_id_for_device(self.VOLTAGE_SETPOINT_FRAME_ID),
            struct.pack("<f", volts) + (b"\x00" * 4),
            FrameOption.EXTENDED,
        )

    def set_velocity(self, rpm: float) -> None:
        self.channel.send_frame(
            self._frame_id_for_device(self.VELOCITY_SETPOINT_FRAME_ID),
            struct.pack("<f", rpm) + (b"\x00" * 4),
            FrameOption.EXTENDED,
        )

    def set_position(self, rotations: float) -> None:
        self.channel.send_frame(
            self._frame_id_for_device(self.POSITION_SETPOINT_FRAME_ID),
            struct.pack("<f", rotations) + (b"\x00" * 4),
            FrameOption.EXTENDED,
        )

    def set_current(self, amps: float) -> None:
        self.channel.send_frame(
            self._frame_id_for_device(self.CURRENT_SETPOINT_FRAME_ID),
            struct.pack("<f", amps) + (b"\x00" * 4),
            FrameOption.EXTENDED,
        )

    def get_firmware_version(self, timeout: float = 0.2) -> tuple[int, int, int, bool, int]:
        frame_id = self._frame_id_for_device(self.GET_FIRMWARE_VERSION_FRAME_ID)
        data = self._request_frame_data(frame_id, timeout=timeout, dlc=8)
        if len(data) < 8:
            raise ValueError(f"Unexpected firmware frame length {len(data)}")
        major, minor, build, debug_build, hw_rev, _reserved = struct.unpack("<BBHBBH", data)
        return major, minor, build, bool(debug_build), hw_rev

    def get_parameter_types_block(self, block_index: int, timeout: float = 0.2) -> list[SparkParamType]:
        if block_index < 0:
            raise ValueError("block_index must be >= 0")
        frame_id = self._frame_id_for_device(
            self.GET_PARAMETER_TYPES_BASE_FRAME_ID + (block_index * 0x40)
        )
        data = self._request_frame_data(frame_id, timeout=timeout, dlc=8)
        if len(data) < 8:
            raise ValueError(f"Unexpected parameter-types frame length {len(data)}")

        types: list[SparkParamType] = []
        for b in data[:8]:
            types.append(SparkParamType(b & 0x0F))
            types.append(SparkParamType((b >> 4) & 0x0F))
        return types

    def get_parameter_type(self, parameter_id: int, timeout: float = 0.2) -> SparkParamType:
        if parameter_id < 0:
            raise ValueError("parameter_id must be >= 0")
        block_index = parameter_id // 16
        idx_in_block = parameter_id % 16
        return self.get_parameter_types_block(block_index, timeout=timeout)[idx_in_block]

    def read_parameter_raw(self, parameter_id: int, timeout: float = 0.2) -> int:
        if parameter_id < 0:
            raise ValueError("parameter_id must be >= 0")

        pair_frame = self.READ_PARAMETER_BASE_FRAME_ID + ((parameter_id // 2) * 0x40)
        frame_id = self._frame_id_for_device(pair_frame)
        data = self._request_frame_data(frame_id, timeout=timeout, dlc=8)
        if len(data) < 8:
            raise ValueError(f"Unexpected read-parameter frame length {len(data)}")

        first, second = struct.unpack("<II", data)
        return first if (parameter_id % 2 == 0) else second

    def read_parameter(self, parameter_id: int, timeout: float = 0.2):
        raw = self.read_parameter_raw(parameter_id, timeout=timeout)
        ptype = self.get_parameter_type(parameter_id, timeout=timeout)

        if ptype == SparkParamType.FLOAT:
            return struct.unpack("<f", struct.pack("<I", raw))[0]
        if ptype == SparkParamType.BOOL:
            return bool(raw & 0x1)
        if ptype == SparkParamType.INT:
            return struct.unpack("<i", struct.pack("<I", raw))[0]
        return raw

    def write_parameter(
        self,
        parameter_id: int,
        value,
        parameter_type: SparkParamType | None = None,
        timeout: float = 0.2,
    ) -> tuple[SparkParamWriteResult, SparkParamType, int]:
        if parameter_type is None:
            parameter_type = self.get_parameter_type(parameter_id, timeout=timeout)

        if parameter_type == SparkParamType.FLOAT:
            raw_value = struct.unpack("<I", struct.pack("<f", float(value)))[0]
        elif parameter_type == SparkParamType.BOOL:
            raw_value = 1 if bool(value) else 0
        elif parameter_type == SparkParamType.INT:
            raw_value = struct.unpack("<I", struct.pack("<i", int(value)))[0]
        else:
            raw_value = int(value) & 0xFFFFFFFF

        frame_id = self._frame_id_for_device(self.PARAMETER_WRITE_FRAME_ID)
        response_id = self._frame_id_for_device(self.PARAMETER_WRITE_RESPONSE_FRAME_ID)
        payload = struct.pack("<BI", parameter_id & 0xFF, raw_value)
        self.channel.send_frame(frame_id, payload, FrameOption.EXTENDED)

        data = self._wait_for_frame_data(response_id, timeout=timeout)
        if len(data) < 7:
            raise ValueError(f"Unexpected parameter-write-response frame length {len(data)}")

        resp_param_id = data[0]
        resp_type = SparkParamType(data[1])
        resp_value = struct.unpack("<I", data[2:6])[0]
        resp_result = SparkParamWriteResult(data[6])

        if resp_param_id != (parameter_id & 0xFF):
            raise RuntimeError(
                f"Parameter write response ID mismatch (expected {parameter_id}, got {resp_param_id})"
            )

        return resp_result, resp_type, resp_value

    def get_status0(self, timeout: float = 0.2) -> SparkStatus0:
        frame_id = self._frame_id_for_device(self.STATUS_0_FRAME_ID)
        data = self._request_frame_data(frame_id, timeout=timeout, dlc=8)
        if len(data) < 8:
            raise ValueError(f"Unexpected STATUS_0 frame length {len(data)}")

        # STATUS_0 from REV is little-endian and fixed-width in this order.
        applied_raw, voltage_raw, current_raw, temp_raw, flags = struct.unpack("<hHHBB", data[:8])

        applied_output = applied_raw * 3.082369457075716e-05
        bus_voltage = voltage_raw * 0.0073260073260073
        output_current = current_raw * 0.0366300366300366

        if 6.0 <= bus_voltage <= 32.0:
            if self._last_good_bus_voltage is None:
                self._last_good_bus_voltage = bus_voltage
            else:
                self._last_good_bus_voltage = (0.75 * self._last_good_bus_voltage) + (0.25 * bus_voltage)
        elif self._last_good_bus_voltage is not None:
            bus_voltage = self._last_good_bus_voltage

        # Idle brushed setups tend to bounce around a few hundred mA of noise.
        if abs(applied_output) < 0.05 and output_current < 0.20:
            output_current = 0.0

        if 0.0 <= output_current <= 250.0:
            if self._last_good_current is None:
                self._last_good_current = output_current
            else:
                self._last_good_current = (0.7 * self._last_good_current) + (0.3 * output_current)
            output_current = self._last_good_current
        elif self._last_good_current is not None:
            output_current = self._last_good_current

        # 160C is commonly observed as a sentinel when no motor temperature source exists.
        if temp_raw in (0xA0, 0xFF):
            motor_temp_c: int | None = self._last_good_temp_c
        elif 0 <= temp_raw <= 130:
            motor_temp_c = temp_raw
            self._last_good_temp_c = temp_raw
        else:
            motor_temp_c = self._last_good_temp_c

        return SparkStatus0(
            applied_output=applied_output,
            bus_voltage=bus_voltage,
            output_current=output_current,
            motor_temperature_c=motor_temp_c,
            hard_forward_limit_reached=bool(flags & (1 << 0)),
            hard_reverse_limit_reached=bool(flags & (1 << 1)),
            soft_forward_limit_reached=bool(flags & (1 << 2)),
            soft_reverse_limit_reached=bool(flags & (1 << 3)),
            inverted=bool(flags & (1 << 4)),
            primary_heartbeat_lock=bool(flags & (1 << 5)),
            spark_model=(flags >> 6) & 0x03,
        )

    def get_status2(self, timeout: float = 0.2) -> SparkStatus2:
        frame_id = self._frame_id_for_device(self.STATUS_2_FRAME_ID)
        data = self._request_frame_data(frame_id, timeout=timeout, dlc=8)
        if len(data) < 8:
            raise ValueError(f"Unexpected STATUS_2 frame length {len(data)}")
        velocity, position = struct.unpack("<ff", data)
        return SparkStatus2(velocity, position)

    def configure_status_periods(
        self,
        status0_ms: int | None = None,
        status1_ms: int | None = None,
        status2_ms: int | None = None,
        timeout: float = 0.2,
    ) -> None:
        if status0_ms is not None:
            self.write_parameter(
                self.SPARK_K_STATUS0_PERIOD,
                int(status0_ms),
                parameter_type=SparkParamType.UINT,
                timeout=timeout,
            )
        if status1_ms is not None:
            self.write_parameter(
                self.SPARK_K_STATUS1_PERIOD,
                int(status1_ms),
                parameter_type=SparkParamType.UINT,
                timeout=timeout,
            )
        if status2_ms is not None:
            self.write_parameter(
                self.SPARK_K_STATUS2_PERIOD,
                int(status2_ms),
                parameter_type=SparkParamType.UINT,
                timeout=timeout,
            )

    def monitor_basic(self, duration_s: float = 3.0, period_s: float = 0.1) -> None:
        t_start = time.time()
        while time.time() - t_start < duration_s:
            self.send_frc_heartbeat()
            s0 = self.get_status0()
            print(
                f"Vbus={s0.bus_voltage:6.2f}V  I={s0.output_current:6.2f}A  "
                f"Out={s0.applied_output:6.3f}  "
                f"T={f'{s0.motor_temperature_c:3d}C' if s0.motor_temperature_c is not None else 'N/A':>5}"
            )
            time.sleep(period_s)


def demo_sine_drive() -> None:
    board: CANBoard | None = None
    spark: SparkMax | None = None

    def connect() -> tuple[CANBoard, SparkMax]:
        local_board = CANBoard(COM_PORT, log_non_command_data=False)
        local_spark = SparkMax(local_board.channels[0], SPARK_ID)

        try:
            major, minor, build, debug, hw_rev = local_spark.get_firmware_version()
            print(
                f"SPARK FW: {major}.{minor}.{build} "
                f"debug={debug} hw_rev={hw_rev}"
            )
        except TimeoutError:
            print("Warning: could not read firmware version (continuing without it)")

        local_spark.configure_status_periods(status0_ms=20, status2_ms=20)
        return local_board, local_spark

    board, spark = connect()

    while True:
        try:
            spark.send_frc_heartbeat()
            spark.set_duty_cycle(math.sin(time.time() / 2.0))

            status0 = spark.get_status0(timeout=0.2)
            temp_text = (
                f"{status0.motor_temperature_c:3d}C"
                if status0.motor_temperature_c is not None
                else "N/A"
            )
            print(
                f"Vbus={status0.bus_voltage:5.2f}V  "
                f"I={status0.output_current:5.6f}A  "
                f"T={temp_text:>5}"
            )
            time.sleep(0.05)
        except (serial.SerialException, OSError) as exc:
            print(f"Serial link fault: {exc}")
            print("Attempting reconnect to CAN board...")
            try:
                if board is not None:
                    board.com.close()
            except Exception:
                pass
            time.sleep(0.5)
            board, spark = connect()
        except KeyboardInterrupt:
            print("Stopping demo")
            try:
                spark.set_duty_cycle(0.0)
            except Exception:
                pass
            break

def demo_user_controlled_drive() -> None:
    pass

if __name__ == "__main__":
    demo_sine_drive()