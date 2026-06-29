#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ModbusRTU.h>
#include <ESP32Servo.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

// ================= STORAGE =================
Preferences prefs;

// ================= WIFI =================
WebServer server(80);
String ssid, password;
String deviceName = "ESP32-Actuator";

// ================= AUTHENTICATION =================
String adminUser = "admin";
String adminPass = "admin123";

// ================= PINS =================
#define PIN_SERVO   13   // Servo signal
#define PIN_ESC     12   // ESC signal
#define PIN_LED1    25   // LED 1 (PWM)
#define PIN_LED2    26   // LED 2 (PWM)
#define PIN_LED3    27   // LED 3 (PWM)

// PWM settings (ESP32 Arduino core 3.x — pin-based, no channel numbers needed)
#define PWM_FREQ    1000
#define PWM_RES     8    // 8-bit → 0-255

// ================= UART PINS =================
// Serial2 only — GPIO 16 (RX), 17 (TX)
#define RXD2 17
#define TXD2 16

// ================= MODBUS =================
ModbusRTU mb;   // Single Modbus instance on Serial2 (GPIO 16/17)

#define SLAVE_ID   11
#define NUM_REGS    5

// Register indices
#define REG_SERVO   0   // 0–180  (degrees)
#define REG_ESC     1   // 0–100  (throttle %)
#define REG_LED1    2   // 0–255  (brightness)
#define REG_LED2    3   // 0–255
#define REG_LED3    4   // 0–255

uint16_t ctrlRegs[NUM_REGS] = {90, 0, 0, 0, 0};  // safe defaults: servo centre, all off
bool modbusInitialized = false;

// ================= SERVO / ESC =================
Servo servo;
Servo esc;   // ESC is controlled the same way as a servo (1000–2000 µs pulse)

// ================= HTML (PROGMEM) =================
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Actuator Control</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial;margin:20px;background:#f0f0f0}
.container{max-width:600px;margin:auto;background:#fff;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,.1)}
h2{color:#333;margin-bottom:4px}
.subtitle{color:#888;font-size:.85rem;margin-bottom:20px}
.status-bar{background:#f9f9f9;padding:12px 16px;border-radius:5px;margin-bottom:20px;font-size:.85rem;display:flex;gap:20px;flex-wrap:wrap;border-left:3px solid #2196F3}
.status-item span{color:#2196F3;font-weight:700}
.card{background:#f9f9f9;padding:18px;border-radius:8px;margin-bottom:16px;border-left:3px solid #4CAF50}
.card-title{font-size:.8rem;font-weight:700;color:#555;text-transform:uppercase;letter-spacing:1px;margin-bottom:14px}
.control-row{display:flex;align-items:center;gap:12px;margin-bottom:10px}
.control-label{width:80px;font-size:.85rem;color:#555;font-weight:600;flex-shrink:0}
input[type=range]{flex:1;-webkit-appearance:none;height:6px;border-radius:3px;background:#ddd;outline:none;cursor:pointer}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:18px;border-radius:50%;background:#2196F3;cursor:pointer}
input[type=range]::-moz-range-thumb{width:18px;height:18px;border-radius:50%;background:#2196F3;cursor:pointer;border:none}
.value-badge{width:48px;text-align:right;font-size:.9rem;color:#2196F3;font-weight:700;flex-shrink:0}
.led-row{display:flex;align-items:center;gap:12px;margin-bottom:10px}
.led-dot{width:18px;height:18px;border-radius:50%;flex-shrink:0;transition:background .2s,box-shadow .2s;border:1px solid #ddd}
.btn-row{display:flex;gap:10px;flex-wrap:wrap;margin-top:14px}
button{padding:10px 18px;border:none;border-radius:5px;cursor:pointer;font-size:.85rem;font-weight:700;transition:opacity .2s,transform .1s}
button:hover{opacity:.85;transform:translateY(-1px)}
button:active{transform:translateY(0px)}
.btn-save{background:#4CAF50;color:#fff}
.btn-edit{background:#2196F3;color:#fff}
.btn-reset{background:#f44336;color:#fff}
.btn-on{background:#4CAF50;color:#fff}
.btn-off{background:#999;color:#fff}
.modal{display:none;position:fixed;z-index:1000;left:0;top:0;width:100%;height:100%;background:rgba(0,0,0,.5);animation:fadeIn .3s}
.modal-content{background:#fff;margin:10% auto;padding:30px;border-radius:10px;width:90%;max-width:500px;box-shadow:0 4px 20px rgba(0,0,0,.3);animation:slideIn .3s}
@keyframes fadeIn{from{opacity:0}to{opacity:1}}
@keyframes slideIn{from{transform:translateY(-50px);opacity:0}to{transform:translateY(0);opacity:1}}
.close{float:right;font-size:28px;font-weight:700;cursor:pointer;color:#999}
.close:hover{color:#333}
.modal-title{margin-top:0;color:#333;font-weight:700}
.error-msg{color:#f44336;font-size:14px;margin:5px 0;min-height:20px}
.warning-text{color:#f44336;font-weight:700}
input[type=text],input[type=password]{width:100%;padding:10px;margin:5px 0 15px;border:1px solid #ddd;border-radius:5px;box-sizing:border-box;font-size:14px}
input[type=text]:focus,input[type=password]:focus{border-color:#2196F3;outline:none}
input[type=submit]{width:100%;color:#fff;border:none;padding:12px;font-size:16px;cursor:pointer;border-radius:5px;margin:5px 0;background:#2196F3}
input[type=submit]:hover{background:#0b7dda}
.label{font-weight:700;color:#555;margin-top:10px;display:block;font-size:.85rem}
.value{background:#f9f9f9;padding:10px;border-radius:5px;margin:5px 0 15px;border-left:3px solid #4CAF50;font-size:.95rem}
.button-group{display:flex;gap:10px;flex-wrap:wrap;margin-top:20px}
</style>
</head>
<body>
<div class="container">
<h2>ESP32 Actuator Control</h2>
<p class="subtitle" id="sub">Connected to <span id="device-name">Loading...</span></p>
<div class="status-bar">
  <div class="status-item">WiFi: <span id="st-wifi">---</span></div>
  <div class="status-item">IP: <span id="st-ip">---</span></div>
  <div class="status-item">Uptime: <span id="st-uptime">---</span></div>
  <div class="status-item">Modbus: <span id="st-modbus">Slave 11 | 9600 8N1</span></div>
</div>
<div class="card">
  <div class="card-title">Servo GPIO 13</div>
  <div class="control-row">
    <span class="control-label">Angle</span>
    <input type="range" id="sl-servo" min="0" max="180" value="90" oninput="updateBadge('servo',this.value);sendControl()">
    <span class="value-badge" id="bd-servo">90&deg;</span>
  </div>
  <div class="btn-row">
    <button class="btn-off" onclick="setVal('servo',0);sendControl()">0&deg;</button>
    <button class="btn-save" onclick="setVal('servo',90);sendControl()">90&deg;</button>
    <button class="btn-on" onclick="setVal('servo',180);sendControl()">180&deg;</button>
  </div>
</div>
<div class="card">
  <div class="card-title">ESC / DC Motor GPIO 12</div>
  <div class="control-row">
    <span class="control-label">Throttle</span>
    <input type="range" id="sl-esc" min="0" max="100" value="0" oninput="updateBadge('esc',this.value);sendControl()">
    <span class="value-badge" id="bd-esc">0%</span>
  </div>
  <div class="btn-row">
    <button class="btn-off" onclick="setVal('esc',0);sendControl()">Stop</button>
    <button class="btn-save" onclick="setVal('esc',50);sendControl()">50%</button>
    <button class="btn-on" onclick="setVal('esc',100);sendControl()">100%</button>
  </div>
</div>
<div class="card">
  <div class="card-title">LEDs GPIO 25 / 26 / 27</div>
  <div class="led-row">
    <div class="led-dot" id="dot-led1" style="background:#111"></div>
    <span class="control-label">LED 1</span>
    <input type="range" id="sl-led1" min="0" max="255" value="0" oninput="updateLed(1,this.value);sendControl()">
    <span class="value-badge" id="bd-led1">0</span>
  </div>
  <div class="led-row">
    <div class="led-dot" id="dot-led2" style="background:#111"></div>
    <span class="control-label">LED 2</span>
    <input type="range" id="sl-led2" min="0" max="255" value="0" oninput="updateLed(2,this.value);sendControl()">
    <span class="value-badge" id="bd-led2">0</span>
  </div>
  <div class="led-row">
    <div class="led-dot" id="dot-led3" style="background:#111"></div>
    <span class="control-label">LED 3</span>
    <input type="range" id="sl-led3" min="0" max="255" value="0" oninput="updateLed(3,this.value);sendControl()">
    <span class="value-badge" id="bd-led3">0</span>
  </div>
  <div class="btn-row">
    <button class="btn-on" onclick="allLeds(255);sendControl()">All ON</button>
    <button class="btn-off" onclick="allLeds(0);sendControl()">All OFF</button>
  </div>
</div>
<div class="button-group">
  <button class="btn-edit" onclick="openEditModal()">WiFi Settings</button>
  <button class="btn-reset" onclick="openResetModal()">Reset Device</button>
</div>
</div>
<div id="authModal" class="modal">
<div class="modal-content">
  <span class="close" onclick="closeModal('authModal')">&times;</span>
  <h3 class="modal-title">Login</h3>
  <form onsubmit="doAuth(event)">
    <div class="label">Username:</div><input type="text" id="au" required>
    <div class="label">Password:</div><input type="password" id="ap" required>
    <div class="error-msg" id="auth-err"></div>
    <input type="submit" value="Login">
  </form>
</div></div>
<div id="editModal" class="modal">
<div class="modal-content">
  <span class="close" onclick="closeModal('editModal')">&times;</span>
  <h3 class="modal-title">WiFi Settings</h3>
  <form onsubmit="saveConfig(event)">
    <div class="label">Device Name:</div><input type="text" id="f-dn" required>
    <div class="label">WiFi SSID:</div><input type="text" id="f-s" required>
    <div class="label">WiFi Password:</div><input type="password" id="f-p" placeholder="Leave blank to keep current">
    <div class="error-msg" id="save-err"></div>
    <input type="submit" value="Save & Reboot" style="background:#4CAF50">
  </form>
</div></div>
<div id="resetModal" class="modal">
<div class="modal-content">
  <span class="close" onclick="closeModal('resetModal')">&times;</span>
  <h3 class="modal-title" style="color:#f44336">Reset Device</h3>
  <p class="warning-text">WARNING: This will delete ALL configuration data!</p>
  <form onsubmit="doReset(event)">
    <div class="label">New Username:</div><input type="text" id="ru" required>
    <div class="label">New Password:</div><input type="password" id="rp" required minlength="4">
    <div class="label">Confirm Password:</div><input type="password" id="rp2" required>
    <div class="error-msg" id="reset-err"></div>
    <input type="submit" value="Reset Device" style="background:#f44336">
  </form>
</div></div>
<script>
function closeModal(id){ document.getElementById(id).style.display='none'; }
window.onclick = function(e) {
  if (e.target.classList.contains('modal')) e.target.style.display='none';
}

function updateBadge(name, val){
  var suffix = name==='servo'?'&deg;':name==='esc'?'%':'';
  document.getElementById('bd-'+name).textContent = val+suffix;
}

function updateLed(n, val){
  var bright = val/255;
  var col = val===0 ? '#111' : 'hsl(45,100%,'+(20+bright*0.6)+'%)';
  var glow = val===0 ? 'none' : '0 0 '+(6+bright/10)+'px hsl(45,100%,60%)';
  var dot = document.getElementById('dot-led'+n);
  dot.style.background = col;
  dot.style.boxShadow = glow;
  document.getElementById('bd-led'+n).textContent = val;
}

function setVal(name, val){
  var el = document.getElementById('sl-'+name);
  if(el){ el.value = val; updateBadge(name, val); }
}

function allLeds(val){
  for(var i=1;i<=3;i++){ setVal('led'+i, val); updateLed(i, val); }
}

var _ctrlTimer = null;
function sendControl(){
  if(_ctrlTimer) clearTimeout(_ctrlTimer);
  _ctrlTimer = setTimeout(function(){
    var data = {
      servo: parseInt(document.getElementById('sl-servo').value),
      esc: parseInt(document.getElementById('sl-esc').value),
      led1: parseInt(document.getElementById('sl-led1').value),
      led2: parseInt(document.getElementById('sl-led2').value),
      led3: parseInt(document.getElementById('sl-led3').value)
    };
    fetch('/control', {
      method: 'POST',
      headers: {'Content-Type':'application/json'},
      body: JSON.stringify(data)
    }).then(function(r){ return r.json(); })
    .then(function(j){ if(!j.ok) console.warn('Write error'); })
    .catch(function(e){ console.warn('Send error',e); });
  }, 150);
}

async function pollStatus(){
  try {
    var r = await fetch('/status');
    var s = await r.json();
    document.getElementById('st-wifi').textContent = s.wifi || '---';
    document.getElementById('st-ip').textContent = s.ip || '---';
    document.getElementById('st-uptime').textContent = s.uptime || '---';
    document.getElementById('device-name').textContent = s.device_name || 'ESP32';
    if(s.regs){
      var a = s.regs;
      if(a[0] !== undefined){ setVal('servo', a[0]); }
      if(a[1] !== undefined){ setVal('esc', a[1]); }
      if(a[2] !== undefined){ setVal('led1', a[2]); updateLed(1, a[2]); }
      if(a[3] !== undefined){ setVal('led2', a[3]); updateLed(2, a[3]); }
      if(a[4] !== undefined){ setVal('led3', a[4]); updateLed(3, a[4]); }
    }
  } catch(e){ console.warn('Status poll error',e); }
}

function fmt(s){ var h=Math.floor(s/3600),m=Math.floor(s%3600/60),sc=s%60; return (h?h+'h ':'')+(m?m+'m ':'')+sc+'s'; }

var _pendingAction = null;
function openEditModal(){
  document.getElementById('auth-err').textContent='';
  document.getElementById('au').value='';
  document.getElementById('ap').value='';
  _pendingAction = 'edit';
  document.getElementById('authModal').style.display='block';
}
function openResetModal(){
  document.getElementById('auth-err').textContent='';
  document.getElementById('au').value='';
  document.getElementById('ap').value='';
  _pendingAction = 'reset';
  document.getElementById('authModal').style.display='block';
}

async function doAuth(e){
  e.preventDefault();
  var u = document.getElementById('au').value;
  var p = document.getElementById('ap').value;
  try {
    var r = await fetch('/auth', {
      method: 'POST',
      headers: {'Content-Type':'application/json'},
      body: JSON.stringify({username:u, password:p})
    });
    var j = await r.json();
    if(j.success){
      document.getElementById('authModal').style.display='none';
      if(_pendingAction === 'edit'){
        var cr = await fetch('/config');
        var cfg = await cr.json();
        document.getElementById('f-dn').value = cfg.device_name||'';
        document.getElementById('f-s').value = cfg.ssid||'';
        document.getElementById('f-p').value = '';
        document.getElementById('save-err').textContent='';
        document.getElementById('editModal').style.display='block';
      } else if(_pendingAction === 'reset'){
        document.getElementById('ru').value='';
        document.getElementById('rp').value='';
        document.getElementById('rp2').value='';
        document.getElementById('reset-err').textContent='';
        document.getElementById('resetModal').style.display='block';
      }
    } else {
      document.getElementById('auth-err').textContent='Invalid credentials';
    }
  } catch(err) {
    document.getElementById('auth-err').textContent='Error: '+err.message;
  }
}

async function saveConfig(e){
  e.preventDefault();
  var data = {
    dn: document.getElementById('f-dn').value,
    s: document.getElementById('f-s').value,
    p: document.getElementById('f-p').value
  };
  try {
    var r = await fetch('/save', {
      method: 'POST',
      headers: {'Content-Type':'application/json'},
      body: JSON.stringify(data)
    });
    var j = await r.json();
    if(j.success){
      document.getElementById('save-err').textContent='Saved! Rebooting...';
      setTimeout(function(){ location.reload(); }, 3000);
    } else {
      document.getElementById('save-err').textContent='Save failed';
    }
  } catch(err) {
    document.getElementById('save-err').textContent='Error: '+err.message;
  }
}

async function doReset(e){
  e.preventDefault();
  var p1 = document.getElementById('rp').value;
  var p2 = document.getElementById('rp2').value;
  if(p1 !== p2){
    document.getElementById('reset-err').textContent='Passwords do not match';
    return;
  }
  try {
    var r = await fetch('/reset-device', {
      method: 'POST',
      headers: {'Content-Type':'application/json'},
      body: JSON.stringify({username: document.getElementById('ru').value, password: p1})
    });
    var j = await r.json();
    if(j.success){
      document.getElementById('reset-err').textContent='Reset OK! Rebooting...';
      setTimeout(function(){ location.reload(); }, 3000);
    } else {
      document.getElementById('reset-err').textContent='Reset failed';
    }
  } catch(err) {
    document.getElementById('reset-err').textContent='Error: '+err.message;
  }
}

pollStatus();
setInterval(pollStatus, 2000);
</script>
</body>
</html>
)rawliteral";


// ================= APPLY OUTPUTS =================
void applyOutputs() {
  // Clamp and apply servo (0–180°)
  int servoAngle = constrain(ctrlRegs[REG_SERVO], 0, 180);
  servo.write(servoAngle);

  // ESC throttle 0–100 → pulse 1000–2000 µs
  int escThrottle = constrain(ctrlRegs[REG_ESC], 0, 100);
  int escPulse = map(escThrottle, 0, 100, 1000, 2000);
  esc.writeMicroseconds(escPulse);

  // LEDs 0–255 PWM
  ledcWrite(PIN_LED1, constrain(ctrlRegs[REG_LED1], 0, 255));
  ledcWrite(PIN_LED2, constrain(ctrlRegs[REG_LED2], 0, 255));
  ledcWrite(PIN_LED3, constrain(ctrlRegs[REG_LED3], 0, 255));
}

// Called by Modbus library whenever a holding register is written by master.
// cbModbus signature required by this library version: uint16_t(TRegister*, uint16_t)
uint16_t onHregWrite(TRegister* reg, uint16_t val) {
  ctrlRegs[reg->address.address] = val;
  applyOutputs();
  Serial.printf("[MODBUS] Hreg[%d]=%d  servo=%d esc=%d led=%d,%d,%d\n",
    reg->address.address, val,
    ctrlRegs[REG_SERVO], ctrlRegs[REG_ESC],
    ctrlRegs[REG_LED1], ctrlRegs[REG_LED2], ctrlRegs[REG_LED3]);
  return val;   // return val to accept the write
}

// ================= UPDATE MODBUS REGISTERS =================
void syncModbusRegs() {
  for (int i = 0; i < NUM_REGS; i++) {
    mb.Hreg(i, ctrlRegs[i]);
  }
}

// ================= INITIALIZE MODBUS =================
void initModbus() {
  // Serial2 — GPIO 17 (RX), 16 (TX) — connect FTDI/RS485 here
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  mb.begin(&Serial2);
  mb.slave(SLAVE_ID);
  mb.addHreg(0, 0, NUM_REGS);
  mb.onSetHreg(0, onHregWrite, NUM_REGS);

  syncModbusRegs();   // write safe defaults

  modbusInitialized = true;
  Serial.printf("[MODBUS] Serial2 RX=GPIO%d TX=GPIO%d\n", RXD2, TXD2);
  Serial.printf("[MODBUS] Slave ID=%d  Baud=9600 8N1\n", SLAVE_ID);
  Serial.println("[MODBUS] Reg 0=Servo(0-180) Reg 1=ESC(0-100%) Reg 2-4=LED(0-255)");
}

// ================= INITIALIZE ACTUATORS =================
void initActuators() {
  // Servo
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  servo.setPeriodHertz(50);
  servo.attach(PIN_SERVO, 500, 2500);
  servo.write(90);   // centre

  // ESC — must arm with minimum pulse first
  esc.setPeriodHertz(50);
  esc.attach(PIN_ESC, 1000, 2000);
  esc.writeMicroseconds(1000);   // arm / minimum throttle
  delay(2000);                   // wait for ESC to arm
  Serial.println("[ESC] Armed at minimum throttle");

  // LEDs via ledc PWM
  // ESP32 Arduino core 3.x API — pin-based, no channel numbers
  ledcAttach(PIN_LED1, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_LED2, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_LED3, PWM_FREQ, PWM_RES);

  // Test flash on boot — if LEDs light up here, PWM init is working
  ledcWrite(PIN_LED1, 128); ledcWrite(PIN_LED2, 128); ledcWrite(PIN_LED3, 128);
  delay(500);
  ledcWrite(PIN_LED1, 0);   ledcWrite(PIN_LED2, 0);   ledcWrite(PIN_LED3, 0);
  Serial.println("[LED] Boot flash done — if LEDs did not flash, check wiring/polarity");

  Serial.printf("[ACTUATORS] Servo=GPIO%d  ESC=GPIO%d  LED1=GPIO%d  LED2=GPIO%d  LED3=GPIO%d\n",
    PIN_SERVO, PIN_ESC, PIN_LED1, PIN_LED2, PIN_LED3);
}

// ================= LOAD CONFIG =================
void loadConfig() {
  prefs.begin("config", true);
  ssid       = prefs.getString("ssid", "");
  password   = prefs.getString("pass", "");
  adminUser  = prefs.getString("adminUser", "admin");
  adminPass  = prefs.getString("adminPass", "admin123");
  deviceName = prefs.getString("deviceName", "ESP32-Actuator");
  prefs.end();
  Serial.println("[CONFIG] Device: " + deviceName);
  Serial.println("[CONFIG] SSID: " + (ssid.isEmpty() ? "(not set)" : ssid));
}

// ================= WEB ROUTES =================
void setupWebRoutes() {
  server.on("/", []() {
    server.send_P(200, "text/html", htmlPage);
  });

  // Auth
  server.on("/auth", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"No body\"}"); return; }
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }
    prefs.begin("config", true);
    String su = prefs.getString("adminUser", "admin");
    String sp = prefs.getString("adminPass", "admin123");
    prefs.end();
    if (doc["username"].as<String>() == su && doc["password"].as<String>() == sp)
      server.send(200, "application/json", "{\"success\":true}");
    else
      server.send(401, "application/json", "{\"error\":\"Invalid credentials\"}");
  });

  // Config (non-sensitive fields only)
  server.on("/config", []() {
    String json = "{";
    json += "\"device_name\":\"" + deviceName + "\",";
    json += "\"ssid\":\"" + ssid + "\"";
    json += "}";
    server.send(200, "application/json", json);
  });

  // Status + current register values (so web UI can sync with Modbus writes)
  server.on("/status", []() {
    String json = "{";
    json += "\"device_name\":\"" + deviceName + "\",";
    json += "\"wifi\":\""  + String(WiFi.status()==WL_CONNECTED?"connected":"disconnected") + "\",";
    json += "\"ip\":\""    + WiFi.localIP().toString() + "\",";
    json += "\"uptime\":\""  + String(millis()/1000) + "\",";
    json += "\"regs\":[";
    for (int i = 0; i < NUM_REGS; i++) {
      json += String(ctrlRegs[i]);
      if (i < NUM_REGS-1) json += ",";
    }
    json += "]}";
    server.send(200, "application/json", json);
  });

  // Control endpoint — web UI writes here
  server.on("/control", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"No body\"}"); return; }
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }

    if (doc.containsKey("servo")) ctrlRegs[REG_SERVO] = constrain((int)doc["servo"], 0, 180);
    if (doc.containsKey("esc"))   ctrlRegs[REG_ESC]   = constrain((int)doc["esc"],   0, 100);
    if (doc.containsKey("led1"))  ctrlRegs[REG_LED1]  = constrain((int)doc["led1"],  0, 255);
    if (doc.containsKey("led2"))  ctrlRegs[REG_LED2]  = constrain((int)doc["led2"],  0, 255);
    if (doc.containsKey("led3"))  ctrlRegs[REG_LED3]  = constrain((int)doc["led3"],  0, 255);

    applyOutputs();
    syncModbusRegs();   // keep Modbus registers in sync with web writes

    Serial.printf("[WEB] Control: servo=%d esc=%d led=%d,%d,%d\n",
      ctrlRegs[REG_SERVO], ctrlRegs[REG_ESC],
      ctrlRegs[REG_LED1], ctrlRegs[REG_LED2], ctrlRegs[REG_LED3]);

    server.send(200, "application/json", "{\"success\":true}");
  });

  // Save config
  server.on("/save", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"No body\"}"); return; }
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }
    prefs.begin("config", false);
    String dn = doc["dn"].as<String>();
    if (dn.length() > 0) { prefs.putString("deviceName", dn); deviceName = dn; }
    prefs.putString("ssid", doc["s"].as<String>());
    String np = doc["p"].as<String>();
    if (np.length() > 0) prefs.putString("pass", np);
    prefs.end();
    server.send(200, "application/json", "{\"success\":true}");
    delay(1000);
    ESP.restart();
  });

  // Reset device
  server.on("/reset-device", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"No body\"}"); return; }
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }
    String nu = doc["username"].as<String>();
    String np = doc["password"].as<String>();
    if (nu.isEmpty() || np.length() < 4) { server.send(400, "application/json", "{\"error\":\"Username and password (min 4 chars) required\"}"); return; }
    prefs.begin("config", false);
    prefs.clear();
    prefs.putString("adminUser", nu);
    prefs.putString("adminPass", np);
    prefs.end();
    server.send(200, "application/json", "{\"success\":true}");
    delay(1000);
    ESP.restart();
  });

  server.onNotFound([]() {
    server.send(404, "text/plain", "404 Not Found");
  });
}

// ================= WIFI =================
void startServer() {
  setupWebRoutes();
  server.begin();
}

bool connectWiFi() {
  loadConfig();
  if (ssid.isEmpty()) {
    Serial.println("[WIFI] No SSID → AP mode");
    WiFi.softAP(deviceName.c_str());
    startServer();
    Serial.println("[AP] http://192.168.4.1");
    return false;
  }
  Serial.println("[WIFI] Connecting to " + ssid + "...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500); Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WIFI] Connected  IP=" + WiFi.localIP().toString());
    startServer();
    if (MDNS.begin(deviceName.c_str()))
      Serial.println("[MDNS] http://" + deviceName + ".local");
    return true;
  }
  Serial.println("[WIFI] Failed → AP mode");
  WiFi.softAP(deviceName.c_str());
  startServer();
  Serial.println("[AP] http://192.168.4.1");
  return false;
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n====================");
  Serial.println("[BOOT] ESP32 Actuator Control v1.0");
  Serial.println("====================");

  initActuators();
  initModbus();
  connectWiFi();

  Serial.println("\n[SYSTEM] Ready!");
  Serial.println("====================\n");
}

// ================= LOOP =================
void loop() {
  if (modbusInitialized) {
    mb.task();
  }
  server.handleClient();
}
