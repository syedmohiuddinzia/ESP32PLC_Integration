# ESP32 PLC Integration
Bridge the gap between ESP32 and Industrial Control Systems with Modbus RTU.

## Introduction
This repository provides a complete industrial IoT solution for integrating ESP32 microcontrollers with PLC (Programmable Logic Controller) systems using Modbus RTU protocol. 
The project includes both firmware for ESP32 devices and a comprehensive Python-based troubleshooting tool, enabling seamless communication between ESP32-based sensors/actuators and industrial control systems

## Project Overview
The ESP32 PLC Integration project demonstrates how to use cost-effective ESP32 microcontrollers as Modbus RTU slave devices in industrial automation environments. 
The system supports two primary applications:

- **Weather Station (Slave ID 10):** Collects and serves real-time weather data from OpenWeatherMap API via Modbus registers
- **Actuator Control (Slave ID 11):** Controls servos, ESCs, and LEDs through Modbus register writes

## Features
### ESP32 Firmware
- Modbus RTU Slave Implementation: Dual UART support (Serial1 and Serial2) for flexible connectivity
- Web-Based Configuration: Intuitive web interface for WiFi setup and device configuration
- Secure Authentication: Password-protected settings management
- Real-time Control: Immediate response to Modbus register writes and web API calls
- Persistent Storage: Configuration saved in non-volatile memory using Preferences library
- Auto-Recovery: Automatically switches to AP mode if WiFi connection fails
- mDNS Support: Access devices via .local hostnames

### Python Troubleshooter
- Automatic Port Detection: Lists available COM ports with descriptions
- Modbus Device Scanning: Automatically detects devices with different baud rates and parity settings
- Interactive Control: Real-time actuator control with value validation
- Data Visualization: ASCII progress bars for actuator states
- Weather Monitoring: Read and display weather data from all configured cities
- HTTP API Testing: Test ESP32's web endpoints directly from the tool
- Emergency Stop: One-click safety shutdown for all actuators
- Readback Verification: Confirms writes with automatic read operations

## Hardware Requirements
### ESP32 Weather Station (Slave ID 10)
- ESP32 Development Board (any variant)
- RS485 Transceiver or USB-to-Serial adapter
- Internet connection (WiFi) for weather data
- Power supply (5V/3.3V)

### ESP32 Actuator Controller (Slave ID 11)
- ESP32 Development Board
- Servo motor (GPIO 13)
- ESC/Brushless motor (GPIO 12)
- 3x LEDs with resistors (GPIO 25, 26, 27)
- RS485 Transceiver or USB-to-Serial adapter
- Power supply (5V/3.3V)

## Software Requirements
### Arduino IDE Libraries

| Library | Installation Method | Repository/Link |
|---------|---------------------|-----------------|
| **WiFi.h** | Built-in with ESP32 package | - |
| **WebServer.h** | Built-in with ESP32 package | - |
| **Preferences.h** | Built-in with ESP32 package | - |
| **ESPmDNS.h** | Built-in with ESP32 package | - |
| **HTTPClient.h** | Built-in with ESP32 package | - |
| **ArduinoJson.h** | Library Manager | [ArduinoJson](https://github.com/bblanchon/ArduinoJson) |
| **ESP32Servo.h** | Library Manager | [ESP32Servo](https://github.com/madhephaestus/ESP32Servo) |
| **ModbusRTU.h** | Library Manager | [modbus-esp8266](https://github.com/emelianov/modbus-esp8266) |

### Installation via Library Manager

1. Open Arduino IDE
2. Go to **Sketch > Include Library > Manage Libraries**
3. Search for each library by name
4. Click **Install** on the matching result

### Manual Installation

Alternatively, install manually using the repository links:

```bash
# ArduinoJson
git clone https://github.com/bblanchon/ArduinoJson.git

# ESP32Servo
git clone https://github.com/madhephaestus/ESP32Servo.git

# ModbusRTU (modbus-esp8266 works for ESP32 as well)
git clone https://github.com/emelianov/modbus-esp8266.git

### Python Requirements
```text
pymodbus >= 3.0.0
pyserial >= 3.0
```

## Quick Start
### 1- Install Python Dependencies
```bash
pip install pymodbus pyserial
```
### 2. Upload Firmware
Open the Arduino sketches in the IDE, select your ESP32 board, and upload:
'ESP32WeatherStation.ino' for weather monitoring
'ESP32ActuatorController.ino' for actuator control
### 3. Configure WiFi
After upload, connect to the ESP32's AP mode and configure WiFi credentials via the web interface.
### 4. Run the Troubleshooter
```bash
python modbus_troubleshooter.py
```
Select your COM port and start scanning for Modbus devices.

## Pin Configuration
### ESP32 Hardware Configuration
**Weather Station** (Slave 10)
````bash
Serial1.begin(9600, SERIAL_8N1, 32, 33);
````
| GPIO   | Function |
| ------ | -------- |
| GPIO32 | RX       |
| GPIO33 | TX       |
'Serial1.begin(9600, SERIAL_8N1, 32, 33);'

**Actuator Controller** (Slave 11)
````bash
Serial1.begin(9600, SERIAL_8N1, 32, 33);
````
| GPIO   | Function |
| ------ | -------- |
| GPIO17 | RX       |
| GPIO16 | TX       |
'Serial2.begin(9600, SERIAL_8N1, 17, 16);'

**Actuator Outputs**
| GPIO | Device         |
| ---- | -------------- |
| 13   | Servo          |
| 12   | ESC / DC Motor |
| 25   | LED1           |
| 26   | LED2           |
| 27   | LED3           |

## Register Maps
### Weather Station (Slave ID 10) - 24 Registers
Each city occupies 8 consecutive holding registers, allowing data for 3 cities.
| Register Offset | Parameter      | Data Type | Format          | Example         |
| --------------: | -------------- | --------- | --------------- | --------------- |
|              +0 | Temperature    | int16     | °C × 10         | 253 = 25.3°C    |
|              +1 | Feels Like     | int16     | °C × 10         | 271 = 27.1°C    |
|              +2 | Humidity       | uint16    | %               | 65 = 65%        |
|              +3 | Pressure       | uint16    | hPa             | 1013 = 1013 hPa |
|              +4 | Wind Speed     | uint16    | m/s × 10        | 42 = 4.2 m/s    |
|              +5 | Wind Direction | uint16    | Degrees (0–359) | 270 = West      |
|              +6 | Visibility     | uint16    | Meters          | 10000 = 10 km   |
|              +7 | Cloud Cover    | uint16    | %               | 75 = 75%        |

| Operation           | Function Code                 | Start Register | Quantity | Description                             |
| ------------------- | ----------------------------- | -------------: | -------: | --------------------------------------- |
| Read City 1 Weather | 0x03 (Read Holding Registers) |              0 |        8 | Reads all weather parameters for City 1 |
| Read City 2 Weather | 0x03 (Read Holding Registers) |              8 |        8 | Reads all weather parameters for City 2 |
| Read City 3 Weather | 0x03 (Read Holding Registers) |             16 |        8 | Reads all weather parameters for City 3 |


### Actuator Controller (Slave ID 11) - 5 Registers
Each actuator occupies one holding register, for a total of 5 consecutive holding registers.
| Register | Device | Data Type | Range | Description            |
| -------: | ------ | --------- | ----- | ---------------------- |
|        0 | Servo  | uint16    | 0–180 | Servo angle in degrees |
|        1 | ESC    | uint16    | 0–100 | Throttle percentage    |
|        2 | LED 1  | uint16    | 0–255 | Brightness (PWM)       |
|        3 | LED 2  | uint16    | 0–255 | Brightness (PWM)       |
|        4 | LED 3  | uint16    | 0–255 | Brightness (PWM)       |

| Device | Function Code                | Register | Example Value | Description                     |
| ------ | ---------------------------- | -------: | ------------: | ------------------------------- |
| Servo  | 0x06 (Write Single Register) |        0 |            90 | Set servo angle to **90°**      |
| ESC    | 0x06 (Write Single Register) |        1 |            60 | Set ESC throttle to **60%**     |
| LED 1  | 0x06 (Write Single Register) |        2 |           255 | Set LED 1 brightness to maximum |
| LED 2  | 0x06 (Write Single Register) |        3 |           128 | Set LED 2 brightness to 50%     |
| LED 3  | 0x06 (Write Single Register) |        4 |             0 | Turn LED 3 off                  |

Note: The register layout is contiguous and optimized for Modbus RTU communication. 
A single read request of 8 holding registers retrieves all weather data for one city, 
while a single write request updates the corresponding actuator register independently.

## Web Interface
Access the ESP32's web interface via:
- IP address: 'http://[ESP32_IP]'
- mDNS hostname: 'http://esp32-actuator.local' or 'http://esp32-weather.local'

## Available Endpoints
### Actuator Controller
- GET / - Web UI for actuator control
- POST /control - JSON payload for writing registers
- GET /status - Current status and register values
- POST /auth - Authentication for configuration
- POST /save - Save WiFi credentials
- POST /reset-device - Factory reset

### Weather Station
- GET / - Web UI for weather display
- GET /api?city=0 - Get weather for specific city
- GET /all - Get weather for all cities
- GET /status - Device status
- POST /auth - Authentication
- POST /save - Save configuration
- POST /reset-device - Factory reset

## API Examples
### Control Actuators via HTTP
````bash
curl -X POST http://esp32-actuator.local/control \
  -H "Content-Type: application/json" \
  -d '{"servo":90,"esc":50,"led1":255,"led2":128,"led3":0}'
````
### Read Weather Data
````bash
curl http://esp32-weather.local/api?city=0
curl http://esp32-weather.local/all
````
### Read Modbus Registers (Python)
````bash
from pymodbus.client import ModbusSerialClient

client = ModbusSerialClient(port='COM1', baudrate=9600)
client.connect()
result = client.read_holding_registers(0, 5, device_id=11)
print(result.registers)  # [servo, esc, led1, led2, led3]
client.close()
````

### Troubleshooting
## Common Issues
**1. No Modbus Response**
- Check TX/RX wiring (swap if necessary)
- Verify common ground
- Ensure correct COM port
- Check baud rate settings
  
**2. Port Access Denied**
- Close other serial applications (Arduino IDE, etc.)
- Check permissions (Linux/macOS: add user to dialout group)
  
**3. WiFi Connection Fails**
- Device automatically switches to AP mode
- Connect to 'ESP32-Actuator' or 'ESP32-Weather' SSID
- Access web interface at '192.168.4.1'
  
**4. Negative Temperature Readings**
- Values are stored as signed int16 (two's complement)
- Master device must cast uint16 back to int16

## Applications
- Industrial Automation: Control actuators in manufacturing processes
- Environmental Monitoring: Real-time weather data collection
- Remote Control: Web-based actuator control from anywhere
- PLC Integration: Replace expensive I/O modules with ESP32
- IoT Prototyping: Rapid development of industrial IoT solutions
- Educational Projects: Learn Modbus protocol implementation

## Contributing
Contributions are welcome! Please feel free to submit issues, pull requests, or suggestions for improvements.

## License
This project is open-source and available under the MIT License.

## Support
For issues, questions, or contributions, please open an issue on the GitHub repository.





