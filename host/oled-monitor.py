# /// script
# requires-python = ">=3.12"
# dependencies = ["pyserial"]
# ///
"""Push live temps, fan percents, and load to the OLED Nano.

One line a second, at 115200:
gpuC,cpuC,sysC,gpuFan,cpuFan,sysFan,gpuUse,cpuUse

2500 RPM is 100% for the CPU fan and the system fan. The GPU uses the
max the card publishes. A fan spinning faster than that reads 101, 102,
and so on, which is the cue to raise the cap. Load stays in 0..100.
"""

import time
from pathlib import Path

import serial

PORT = "/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0"
BAUD = 115200
INTERVAL_S = 1.0

CPU_FAN_MAX = 2500
SYS_FAN_MAX = 2500

_cpu_prev = None


def find_hwmon(prefix):
    root = Path("/sys/class/hwmon")
    for node in root.iterdir():
        try:
            name = (node / "name").read_text().strip()
        except OSError:
            continue
        if name == prefix or name.startswith(prefix + "_"):
            return node
    return None


def read_int(path):
    return int(path.read_text().strip())


def temp_c(node, index):
    milli = read_int(node / f"temp{index}_input")
    return (milli + 500) // 1000


def fan_percent(rpm, maximum):
    if rpm <= 0 or maximum <= 0:
        return 0
    percent = (rpm * 100 + maximum // 2) // maximum
    if rpm > maximum and percent < 101:
        percent = 101
    return percent


def cpu_util():
    global _cpu_prev
    parts = Path("/proc/stat").read_text().splitlines()[0].split()
    fields = [int(part) for part in parts[1:9]]
    idle = fields[3] + fields[4]
    total = sum(fields)
    prev = _cpu_prev
    _cpu_prev = (idle, total)
    if prev is None:
        return 0
    idle_d = idle - prev[0]
    total_d = total - prev[1]
    if total_d <= 0:
        return 0
    used = (total_d - idle_d) * 100 + total_d // 2
    percent = used // total_d
    if percent < 0:
        return 0
    if percent > 100:
        return 100
    return percent


def sample():
    gpu = find_hwmon("amdgpu")
    cpu = find_hwmon("k10temp")
    board = find_hwmon("it8688")
    if gpu is None or cpu is None or board is None:
        missing = [
            name
            for name, node in (("amdgpu", gpu), ("k10temp", cpu), ("it8688", board))
            if node is None
        ]
        raise FileNotFoundError("missing hwmon: " + ", ".join(missing))

    gpu_max = read_int(gpu / "fan1_max")
    gpu_use = read_int(gpu / "device" / "gpu_busy_percent")
    if gpu_use < 0:
        gpu_use = 0
    if gpu_use > 100:
        gpu_use = 100
    return (
        temp_c(gpu, 1),
        temp_c(cpu, 1),
        temp_c(board, 1),
        fan_percent(read_int(gpu / "fan1_input"), gpu_max),
        fan_percent(read_int(board / "fan1_input"), CPU_FAN_MAX),
        fan_percent(read_int(board / "fan2_input"), SYS_FAN_MAX),
        gpu_use,
        cpu_util(),
    )


def open_port():
    link = Path(PORT)
    if not link.exists():
        raise FileNotFoundError(PORT)
    port = serial.Serial()
    port.port = str(link)
    port.baudrate = BAUD
    port.timeout = 0
    port.write_timeout = 1
    # Hold the lines off so opening the port does not reboot the Nano.
    port.dtr = False
    port.rts = False
    port.open()
    port.dtr = False
    port.rts = False
    return port


def main():
    port = None
    while True:
        started = time.monotonic()
        try:
            if port is None or not port.is_open:
                port = open_port()
            frame = sample()
            line = ",".join(str(value) for value in frame) + "\n"
            port.write(line.encode("ascii"))
            port.flush()
            print(line, end="", flush=True)
        except (OSError, serial.SerialException) as exc:
            print(f"retry: {exc}", flush=True)
            if port is not None:
                try:
                    port.close()
                except serial.SerialException:
                    pass
                port = None
        elapsed = time.monotonic() - started
        time.sleep(max(0.0, INTERVAL_S - elapsed))


if __name__ == "__main__":
    main()
