#!/usr/bin/env python3
"""Compile actual firmware handlers with simulated flash, HTTP, and GPIO I/O."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
json_include = root / '.pio/libdeps/esp32dev/ArduinoJson/src'
if not json_include.is_dir():
    raise SystemExit('Run pio run -e esp32dev first to install ArduinoJson.')
with tempfile.TemporaryDirectory(prefix='blsmartflow-tests-') as work:
    work = Path(work)
    for name in ['Arduino.h', 'WiFi.h', 'WiFiClient.h', 'FS.h', 'LittleFS.h',
                 'WebServer.h', 'ESPmDNS.h', 'Update.h', 'pgmspace.h',
                 'WiFiClientSecure.h', 'PubSubClient.h', 'Stream.h', 'ESP32SSDP.h']:
        (work / name).write_text('#include "arduino-stubs.h"\n')
    binary = work / 'firmware-tests'
    subprocess.run([
        os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
        '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-DSTRVERSION="host-test"', '-DSTRBUILDDATE="2026.10.23"',
        '-I' + str(work), '-I' + str(root / 'test/host'),
        '-I' + str(json_include), '-I' + str(root),
        str(root / 'test/host/firmware-tests.cpp'), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
