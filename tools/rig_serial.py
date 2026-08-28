# Runs on WINDOWS Python (needs pyserial), not WSL: the rig's UART is a COM
# port. Invoked by tools/rig_soak.py through cmd.exe, or by hand:
#   python tools\rig_serial.py 300 [COM3] > capture.log
# Prints serial lines for N seconds, then exits. Deliberately line-buffered so
# a killed capture still has everything it saw.
import sys
import time

import serial

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0
port = sys.argv[2] if len(sys.argv) > 2 else "COM3"

with serial.Serial(port, 115200, timeout=1) as ser:
    deadline = time.time() + seconds
    while time.time() < deadline:
        line = ser.readline()
        if line:
            try:
                sys.stdout.write(line.decode("utf-8", "replace"))
            except Exception:
                pass
            sys.stdout.flush()
