from driver.can_api import CANBoard, EventType, FrameOption
import time
PORT = "COM11"

SRC_CHANNEL = 1
DST_CHANNEL = 5

board = CANBoard(PORT)

# Simple test: send a frame and wait for a response.
board.send_frame(SRC_CHANNEL, 0xdeadbeef, b'deadbeef', FrameOption.EXTENDED)
frame = board.get_rx_events_blocking(DST_CHANNEL)
print(f"Received frame: {frame}")

# Simple performance test: constantly send frame and wait for a response for 10 seconds.
# start_time = time.time()
# num_frames_sent = 0
# while time.time() - start_time < 10:
#     board.send_frame(SRC_CHANNEL, 0xdeadbeef, b'deadbeef', FrameOption.EXTENDED)
#     # recv_frame = False
#     # while not recv_frame:
#     #     frame = board.get_rx_events(DST_CHANNEL)
#     #     for f in frame:
#     #         if f.type == EventType.CAN_EVENT_TYPE_RECEIVED_FRAME:
#     #             recv_frame = True
#     #             break
#     num_frames_sent += 1

# print(f"Sent {num_frames_sent} frames in 10 seconds, average {num_frames_sent/10:.2f} frames per second")