#!/usr/bin/env python3
"""Capture real-board audio telemetry at console baud; never infer listening results."""
import argparse
import json
from pathlib import Path
import re
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', default='/dev/cu.usbserial-1120')
parser.add_argument('--duration', type=float, default=660)
parser.add_argument('--log', type=Path, required=True)
parser.add_argument('--reset', action='store_true')
args = parser.parse_args()
if args.duration <= 0:
    parser.error('duration must be positive')
args.log.parent.mkdir(parents=True, exist_ok=True)
port = serial.Serial(baudrate=115200, timeout=0.1)
port.dtr = False
port.rts = False
port.port = args.port
port.open()
if args.reset:
    port.rts = True
    time.sleep(0.1)
    port.rts = False

first = last = None
first_time = last_time = None
rates = set()
faults = []
start = time.monotonic()
with port, args.log.open('w') as output:
    while time.monotonic() - start < args.duration:
        line = port.readline().decode('utf-8', errors='replace').strip()
        if not line:
            continue
        output.write(line + '\n')
        output.flush()
        if '[AUDIO] bt rate=' in line:
            fields = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', line)}
            last, last_time = fields, time.monotonic() - start
            if first is None:
                first, first_time = fields, last_time
            rates.add(fields['rate'])
        if any(tag in line for tag in ('Guru Meditation', 'Task watchdog', '[DISPLAY_STALL]', 'ASRC open failed')):
            faults.append(line)
        if any(tag in line for tag in ('[AUDIO]', 'Voice pipeline', 'WakeNet', 'MultiNet', 'UI started', 'PSRAM boot', 'Guru Meditation', 'Task watchdog')):
            print(line, flush=True)

summary = {
    'capture_seconds': round(time.monotonic() - start, 2),
    'telemetry_span_seconds': round(last_time - first_time, 2) if first else 0,
    'negotiated_rates_observed': sorted(rates),
    'first_telemetry': first,
    'last_telemetry': last,
    'counter_deltas': {k: last[k] - first[k] for k in ('rx', 'play', 'under', 'over', 'short', 'drop', 'flush', 'write_err', 'asrc_err', 'cmd_drop')} if first else {},
    'observed_fault_logs': faults,
    'ten_minute_telemetry_available': bool(first and last_time - first_time >= 600),
    'listening_key_voice_vision_results': 'Requires operator verification; not inferred from counters',
}
report = args.log.with_suffix('.json')
report.write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2), flush=True)
