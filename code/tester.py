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

while(1):
    # controller = int(input("Enter controller number: "))
    # if controller > 5:
    #     print("Enter a number from 0-5!")
    #     continue

    # controller = random.randint(0,5)
    
    # print(f"Testing controller {controller}")
    # conn.write(gen_can_header(controller, 0, 0))
    # print(b''.join(read_n_lines(conn, 4)).decode())

    # conn.write(gen_packet(1, 1, position=3))
    # print(b''.join(read_n_lines(conn, 8)).decode())
    # time.sleep(0.01)