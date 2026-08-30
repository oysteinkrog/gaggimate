# Runs on WINDOWS Python (needs pyserial), not WSL: the rig's UART is a COM
# port. Invoked by tools/rig_soak.py through cmd.exe, or by hand:
#   python tools\rig_serial.py 300 [COM3] > capture.log
# Prints serial lines for N seconds, then exits. Deliberately line-buffered so
# a killed capture still has everything it saw.
#
# DTR/RTS are held deasserted for the whole open/read/close cycle. pyserial's
# default open asserts both, and the ESP32-S3's native USB-Serial-JTAG
# interprets DTR/RTS edges as reset commands, so a default open (and the
# matching deassert at close) HARD-RESETS THE DEVICE. That is what the
# "spontaneous" bench reboots at the exact moment 8-hour captures expired
# were (2026-08-30, logs 48/51: capture deadline -> port close -> reboot,
# twice into a WiFi 4WAY_HANDSHAKE_TIMEOUT wedge). A capture must be a pure
# observer; only esptool resets the chip on purpose.
import sys
import time

import serial

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0
port = sys.argv[2] if len(sys.argv) > 2 else "COM3"

ser = serial.Serial()
ser.port = port
ser.baudrate = 115200
ser.timeout = 1
ser.dtr = False
ser.rts = False
ser.open()
try:
    deadline = time.time() + seconds
    while time.time() < deadline:
        line = ser.readline()
        if line:
            try:
                sys.stdout.write(line.decode("utf-8", "replace"))
            except Exception:
                pass
            sys.stdout.flush()
finally:
    ser.close()
