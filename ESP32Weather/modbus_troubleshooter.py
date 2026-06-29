#!/usr/bin/env python3
"""
ESP32 Modbus RTU Troubleshooter + Actuator Control
Compatible with pymodbus 3.x (uses device_id= parameter)
"""

import serial
import serial.tools.list_ports
import time
import sys

try:
    from pymodbus.client import ModbusSerialClient
    import pymodbus
    PYMODBUS_VERSION = tuple(int(x) for x in pymodbus.__version__.split(".")[:2])
except ImportError:
    print("❌ pymodbus not installed. Run: pip install pymodbus")
    sys.exit(1)


# ==================== COLOR CODES ====================
class Colors:
    GREEN  = '\033[92m'
    RED    = '\033[91m'
    YELLOW = '\033[93m'
    BLUE   = '\033[94m'
    CYAN   = '\033[96m'
    MAGENTA= '\033[95m'
    END    = '\033[0m'
    BOLD   = '\033[1m'

def ok(text):   print(f"{Colors.GREEN}✅ {text}{Colors.END}")
def err(text):  print(f"{Colors.RED}❌ {text}{Colors.END}")
def warn(text): print(f"{Colors.YELLOW}⚠️  {text}{Colors.END}")
def info(text): print(f"{Colors.BLUE}ℹ️  {text}{Colors.END}")
def header(text):
    print(f"\n{Colors.CYAN}{'='*60}{Colors.END}")
    print(f"{Colors.BOLD}{text:^60}{Colors.END}")
    print(f"{Colors.CYAN}{'='*60}{Colors.END}\n")


# ==================== REGISTER MAP ====================
# Matches Project2.ino exactly
ACTUATOR_REGS = [
    {"name": "Servo",   "reg": 0, "min": 0,   "max": 180, "unit": "°",   "desc": "Angle (0–180°)"},
    {"name": "ESC",     "reg": 1, "min": 0,   "max": 100, "unit": "%",   "desc": "Throttle (0–100%)"},
    {"name": "LED 1",   "reg": 2, "min": 0,   "max": 255, "unit": "",    "desc": "Brightness (0–255)"},
    {"name": "LED 2",   "reg": 3, "min": 0,   "max": 255, "unit": "",    "desc": "Brightness (0–255)"},
    {"name": "LED 3",   "reg": 4, "min": 0,   "max": 255, "unit": "",    "desc": "Brightness (0–255)"},
]

# Weather station register map (Project1.ino) — read only
WEATHER_REGS_PER_CITY = [
    "Temperature  (×0.1 °C)",
    "Feels Like   (×0.1 °C)",
    "Humidity     (%)",
    "Pressure     (hPa)",
    "Wind Speed   (×0.1 m/s)",
    "Wind Dir     (°)",
    "Visibility   (m)",
    "Cloud Cover  (%)",
]


# ==================== PORT GUARD ====================
_active_client = None

def release_port():
    global _active_client
    if _active_client is not None:
        try:
            _active_client.close()
        except Exception:
            pass
        _active_client = None
    time.sleep(0.15)


# ==================== MODBUS HELPERS ====================
def modbus_read(client, address, count, slave_id):
    major, minor = PYMODBUS_VERSION
    if major >= 3 and minor >= 6:
        return client.read_holding_registers(address, count=count, device_id=slave_id)
    elif major >= 3:
        try:
            return client.read_holding_registers(address, count, slave=slave_id)
        except TypeError:
            return client.read_holding_registers(address, count, device_id=slave_id)
    else:
        return client.read_holding_registers(address, count, unit=slave_id)


def modbus_write(client, address, value, slave_id):
    major, minor = PYMODBUS_VERSION
    if major >= 3 and minor >= 6:
        return client.write_register(address, value, device_id=slave_id)
    elif major >= 3:
        try:
            return client.write_register(address, value, slave=slave_id)
        except TypeError:
            return client.write_register(address, value, device_id=slave_id)
    else:
        return client.write_register(address, value, unit=slave_id)


def make_client(port, baudrate=9600, parity='N', stopbits=1):
    return ModbusSerialClient(
        port=port,
        baudrate=baudrate,
        parity=parity,
        stopbits=stopbits,
        bytesize=8,
        timeout=2,
        retries=1,
    )


def open_client(port, baud=9600, parity='N'):
    """Open and connect a client, return it or None on failure."""
    global _active_client
    release_port()
    client = make_client(port, baud, parity)
    _active_client = client
    if not client.connect():
        err("Could not open port — is something else using it?")
        _active_client = None
        return None
    return client


# ==================== MODBUS SCAN ====================
def test_modbus_scan(port):
    global _active_client
    header(f"Modbus RTU Scan on {port}")
    info(f"pymodbus {pymodbus.__version__}  "
         f"(param: {'device_id' if PYMODBUS_VERSION>=(3,6) else 'slave' if PYMODBUS_VERSION[0]>=3 else 'unit'}=)")

    release_port()

    configs = [
        {'baudrate': 9600, 'parity': 'N', 'slave_id': 10},
        {'baudrate': 9600, 'parity': 'N', 'slave_id': 11},
    ]

    found_devices = 0

    for cfg in configs:
        baud, parity, slave_id = cfg['baudrate'], cfg['parity'], cfg['slave_id']
        info(f"Testing {baud} baud | parity={parity} | slave={slave_id}")

        client = None
        try:
            client = make_client(port, baud, parity)
            _active_client = client
            if not client.connect():
                warn("  Could not open port")
                _active_client = None
                time.sleep(0.3)
                continue

            result = modbus_read(client, 0, 1, slave_id)
            if result and not result.isError():
                ok(f"Device responded!  baud={baud}  parity={parity}  slave={slave_id}")
                print(f"  Reg[0] = {result.registers[0]}")
                found_devices += 1
                
                # Read and display based on slave ID:
                if slave_id == 11:
                    r = modbus_read(client, 0, 5, slave_id)
                    if r and not r.isError():
                        _print_actuators(r.registers)
                    else:
                        warn(f"  Could not read actuator registers (5 regs)")
                elif slave_id == 10:
                    r = modbus_read(client, 0, 24, slave_id)
                    if r and not r.isError():
                        _print_weather(r.registers)
                    else:
                        warn(f"  Could not read weather registers (24 regs)")
                else:
                    # Try both for unknown slave IDs
                    r = modbus_read(client, 0, 5, slave_id)
                    if r and not r.isError():
                        _print_actuators(r.registers)
                    else:
                        r = modbus_read(client, 0, 24, slave_id)
                        if r and not r.isError():
                            _print_weather(r.registers)
            else:
                warn("  No response")

        except Exception as e:
            err(f"  {str(e).splitlines()[0]}")
        finally:
            if client is not None:
                try: client.close()
                except: pass
            _active_client = None
        time.sleep(0.3)

    if found_devices == 0:
        err("No Modbus response with any configuration.")
        print()
        _diagnose(port)
        return False
    else:
        ok(f"Scan complete! Found {found_devices} device(s)")
        return True

def _diagnose(port):
    print(f"{Colors.YELLOW}Diagnosis:{Colors.END}\n")
    try:
        s = serial.Serial(port, 9600, timeout=0.5)
        s.close()
        print("  Port opens fine — possible causes:")
        print("  • Wrong COM port — try the other one.")
        print("  • TX/RX wires swapped — swap them and retry.")
        print("  • No GND connection between ESP32 and adapter.")
        print("  • ESP32 Modbus not initialised — check Serial Monitor")
        print("    on the programming port for '[MODBUS] Initialized'.")
        print("  • RS-485: A/B polarity reversed — swap A and B wires.")
    except PermissionError:
        print(f"  {Colors.RED}✖ Port locked by another process.{Colors.END}")
        print("    Close Arduino IDE Serial Monitor and retry.")
    except Exception as e:
        print(f"  Port error: {e}")


# ==================== READ ACTUATORS ====================
def read_actuators(port, slave_id=11):
    """Read all 5 actuator registers and display current state."""
    client = open_client(port)
    if not client:
        return

    try:
        result = modbus_read(client, 0, len(ACTUATOR_REGS), slave_id)
        if result and not result.isError():
            _print_actuators(result.registers)
        else:
            warn("No response — is this the actuator ESP32 (Project2)?")
    except Exception as e:
        err(str(e).splitlines()[0])
    finally:
        release_port()


def _print_actuators(data):
    print(f"\n{Colors.MAGENTA}{'─'*40}{Colors.END}")
    print(f"{Colors.BOLD}  Current Actuator State{Colors.END}")
    print(f"{Colors.MAGENTA}{'─'*40}{Colors.END}")
    for i, reg in enumerate(ACTUATOR_REGS):
        if i >= len(data):
            break
        val = data[i]
        bar = _bar(val, reg["min"], reg["max"])
        print(f"  Reg {reg['reg']}  {reg['name']:<8} {bar}  {val}{reg['unit']}")
    print(f"{Colors.MAGENTA}{'─'*40}{Colors.END}\n")


def _bar(val, lo, hi, width=20):
    """Simple ASCII progress bar."""
    frac = (val - lo) / max(hi - lo, 1)
    filled = round(frac * width)
    return f"[{Colors.CYAN}{'█'*filled}{'░'*(width-filled)}{Colors.END}]"


# ==================== CONTROL ACTUATORS ====================
def control_actuators(port, slave_id=11):
    """Interactive actuator control sub-menu."""
    header(f"Actuator Control  (Slave {slave_id} | 9600 8N1)")

    # Show current state first
    client = open_client(port)
    if not client:
        return
    try:
        result = modbus_read(client, 0, len(ACTUATOR_REGS), slave_id)
        if result and not result.isError():
            _print_actuators(result.registers)
        release_port()
    except Exception as e:
        err(str(e).splitlines()[0])
        release_port()
        return

    while True:
        print(f"{Colors.CYAN}{'─'*60}{Colors.END}")
        print(f"{Colors.BOLD}  Select actuator to control:{Colors.END}")
        for i, reg in enumerate(ACTUATOR_REGS):
            print(f"  {i+1}. {reg['name']:<8}  {reg['desc']}")
        print(f"  a. 🔴  Emergency Stop  (ESC→0, LEDs→0, Servo→90°)")
        print(f"  r. 📊  Read current state")
        print(f"  0. ↩   Back")
        print(f"{Colors.CYAN}{'─'*60}{Colors.END}")

        choice = input(f"{Colors.BOLD}  Choice: {Colors.END}").strip().lower()

        if choice == '0':
            break

        elif choice == 'r':
            read_actuators(port, slave_id_actuator)

        elif choice == 'a':
            _emergency_stop(port, slave_id)

        elif choice.isdigit() and 1 <= int(choice) <= len(ACTUATOR_REGS):
            reg = ACTUATOR_REGS[int(choice) - 1]
            _write_single(port, slave_id, reg)

        else:
            err("Invalid choice")


def _write_single(port, slave_id, reg):
    """Prompt for a value and write a single register."""
    print(f"\n  {Colors.BOLD}{reg['name']}{Colors.END}  —  {reg['desc']}")
    print(f"  Valid range: {reg['min']} – {reg['max']}{reg['unit']}")

    raw = input(f"  Enter value (or Enter to cancel): ").strip()
    if not raw:
        return

    try:
        val = int(raw)
    except ValueError:
        err("Not a number")
        return

    if not reg['min'] <= val <= reg['max']:
        err(f"Value {val} out of range ({reg['min']}–{reg['max']})")
        return

    client = open_client(port)
    if not client:
        return
    try:
        result = modbus_write(client, reg['reg'], val, slave_id)
        if result and not result.isError():
            ok(f"{reg['name']} → {val}{reg['unit']}")
        else:
            warn("Write failed — no response from ESP32")
    except Exception as e:
        err(str(e).splitlines()[0])
    finally:
        release_port()

    # Confirm by reading back
    time.sleep(0.1)
    client = open_client(port)
    if not client:
        return
    try:
        rb = modbus_read(client, reg['reg'], 1, slave_id)
        if rb and not rb.isError():
            readback = rb.registers[0]
            if readback == val:
                ok(f"Readback confirmed: {readback}{reg['unit']}")
            else:
                warn(f"Readback mismatch: wrote {val}, got {readback}")
    except Exception:
        pass
    finally:
        release_port()


def _emergency_stop(port, slave_id):
    """Write safe values to all registers at once."""
    safe = {
        0: 90,   # Servo centre
        1: 0,    # ESC stop
        2: 0,    # LED 1 off
        3: 0,    # LED 2 off
        4: 0,    # LED 3 off
    }
    warn("Emergency stop — writing safe values to all registers...")
    client = open_client(port)
    if not client:
        return
    try:
        all_ok = True
        for reg_addr, val in safe.items():
            result = modbus_write(client, reg_addr, val, slave_id)
            if not result or result.isError():
                err(f"  Failed to write Reg {reg_addr}")
                all_ok = False
            time.sleep(0.05)
        if all_ok:
            ok("All actuators set to safe state")
            _print_actuators(list(safe.values()))
    except Exception as e:
        err(str(e).splitlines()[0])
    finally:
        release_port()


# ==================== READ WEATHER ====================
def read_weather(port, slave_id=10):
    """Read 24 weather registers from Project1 ESP32."""
    client = open_client(port)
    if not client:
        return
    try:
        result = modbus_read(client, 0, 24, slave_id)
        if result and not result.isError():
            _print_weather(result.registers)
        else:
            warn("No response — is this the weather ESP32 (Project1)?")
    except Exception as e:
        err(str(e).splitlines()[0])
    finally:
        release_port()


def _print_weather(data):
    def s16(v): return v if v < 0x8000 else v - 0x10000
    for i, name in enumerate(["City 1", "City 2", "City 3"]):
        base = i * 8
        if base + 7 >= len(data):
            break
        d = data[base:base+8]
        print(f"\n{Colors.CYAN}📍 {name}:{Colors.END}")
        print(f"  Temperature  : {s16(d[0])/10.0:.1f} °C")
        print(f"  Feels Like   : {s16(d[1])/10.0:.1f} °C")
        print(f"  Humidity     : {d[2]} %")
        print(f"  Pressure     : {d[3]} hPa")
        print(f"  Wind Speed   : {d[4]/10.0:.1f} m/s")
        print(f"  Wind Dir     : {d[5]} °")
        print(f"  Visibility   : {d[6]} m")
        print(f"  Cloud Cover  : {d[7]} %")


# ==================== HTTP TEST ====================
def test_wifi_api(ip):
    import urllib.request, json
    header(f"HTTP API Test  →  http://{ip}/all")
    try:
        with urllib.request.urlopen(f"http://{ip}/all", timeout=8) as r:
            data = json.loads(r.read())
        for i, city in enumerate(data):
            if city is None:
                print(f"\n📍 City {i+1}: not configured")
                continue
            print(f"\n{Colors.CYAN}📍 {city.get('city','?')}:{Colors.END}")
            print(f"  Temperature  : {city.get('temp')} °C")
            print(f"  Feels Like   : {city.get('feels_like')} °C")
            print(f"  Humidity     : {city.get('humidity')} %")
            print(f"  Pressure     : {city.get('pressure')} hPa")
            print(f"  Wind Speed   : {city.get('wind_speed')} m/s")
            print(f"  Wind Dir     : {city.get('wind_dir')} °")
            print(f"  Visibility   : {city.get('visibility')} m")
            print(f"  Cloud Cover  : {city.get('clouds')} %")
        ok("HTTP read successful")
        return True
    except Exception as e:
        err(f"HTTP error: {e}")
        print()
        print("  Possible causes:")
        print("  • ESP32 is in AP mode — connect to its hotspot")
        print(f"    and use 192.168.4.1 instead of {ip}")
        print("  • PC and ESP32 on different subnets.")
        print("  • Firewall blocking port 80.")
        return False


# ==================== MAIN ====================
def main():
    header("ESP32 Modbus Tool")

    ports = list(serial.tools.list_ports.comports())
    if not ports:
        err("No serial ports found!")
        return

    print("Available COM Ports:")
    for i, p in enumerate(ports):
        print(f"  {i+1}. {p.device}  —  {p.description}")
        if p.manufacturer:
            print(f"       Manufacturer: {p.manufacturer}")

    try:
        choice = int(input(f"\nSelect port (1-{len(ports)}): "))
        port = ports[choice - 1].device
    except (ValueError, IndexError):
        err("Invalid selection")
        return

    slave_id_weather  = 10   # Project1 — weather station
    slave_id_actuator = 11   # Project2 — actuator control

    while True:
        print(f"\n{Colors.CYAN}{'='*60}{Colors.END}")
        print(f"{Colors.BOLD}  ESP32 Modbus Tool  —  {port}{Colors.END}")
        print(f"{Colors.BOLD}  Weather=Slave {slave_id_weather}  |  Actuator=Slave {slave_id_actuator}{Colors.END}")
        print(f"{Colors.CYAN}{'='*60}{Colors.END}")
        print(f"  1. 🔍  Scan Modbus          (find device + baud)")
        print(f"  2. 📊  Read Weather          (Project1 — slave {slave_id_weather}, 24 regs)")
        print(f"  3. 🎮  Actuator Control      (Project2 — slave {slave_id_actuator}, read + write)")
        print(f"  4. 🌐  Read via HTTP         (WiFi /all endpoint)")
        print(f"  0. 🚪  Exit")
        print(f"{Colors.CYAN}{'='*60}{Colors.END}")

        choice = input(f"{Colors.BOLD}Enter choice: {Colors.END}").strip().lower()

        if choice == '0':
            release_port()
            info("Goodbye!")
            break
        elif choice == '1':
            test_modbus_scan(port)
        elif choice == '2':
            read_weather(port, slave_id_weather)
        elif choice == '3':
            control_actuators(port, slave_id_actuator)
        elif choice == '4':
            ip = input("  Enter ESP32 IP address (e.g. 192.168.1.219): ").strip()
            ip = ip.removeprefix("https://").removeprefix("http://").rstrip("/")
            test_wifi_api(ip)
        else:
            err("Invalid choice")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\nStopped by user")
        release_port()
    except Exception as e:
        err(f"Unexpected error: {e}")
        release_port()