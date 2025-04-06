import serial
import random
import struct
import time

PORT = "/dev/ttyACM0"
BAUD = 115200

SPARK_POSITION_SETPOINT = 0x2050100
AK60_POSITION_SETPOINT = 0x400
ROBORIO_HEARTBEAT = 0x01011840

conn = serial.Serial(PORT, BAUD)

def read_n_lines(c: serial.Serial, lines: int) -> list[bytes]:
    out = []
    for _ in range(lines):
        out.append(c.readline())
    return out

def gen_can_header(channel: int, data: int, arbitration: int) -> bytes:
    return (channel + (data << 3)).to_bytes() + arbitration.to_bytes(4, "little", signed=False)

def gen_spark_packet(channel: int, id: int, degree: float):
    return  gen_can_header(channel, 8, ROBORIO_HEARTBEAT) + (b'\xff' * 8) + \
            gen_can_header(channel, 8, SPARK_POSITION_SETPOINT + id) + struct.pack("<f", degree / 360) + (b'\x00' * 4)

def gen_ak60_packet(channel: int, id: int, degree: float):
    return gen_can_header(channel, 4, AK60_POSITION_SETPOINT + id) + struct.pack(">i", degree * 10000)

def test_controller(controller_num: int):
    print(f"Testing controller {controller_num}")
    conn.write(gen_can_header(controller_num, 0, 0))
    print(b''.join(read_n_lines(conn, 4)).decode())

def test_random():
    test_controller(random.randint(0,0))
    time.sleep(0.05)

def test_input():
    controller = int(input("Enter controller number: "))
    if controller > 5:
        print("Enter a number from 0-5!")
        return
    test_controller(controller)

while(1):
    pass
    # test_input()
    # test_random()