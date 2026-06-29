#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ModbusRTU.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

// ================= STORAGE =================
Preferences prefs;

// ================= WIFI =================
WebServer server(80);

String ssid, password, apiKey;
String cities[3];
bool cityValid[3] = {false, false, false};
String deviceName = "ESP32-Weather";

// ================= AUTHENTICATION =================
String adminUser = "admin";
String adminPass = "admin123";

// ================= UART PINS =================
// Serial1 - GPIO 32 (RX), 33 (TX)
#define RXD1 32
#define TXD1 33

// Serial2 - GPIO 16 (RX), 17 (TX)  [note: ESP32 default is RX=16, TX=17]
#define RXD2 17
#define TXD2 16

// ================= MODBUS =================
ModbusRTU mb1;  // Serial1
ModbusRTU mb2;  // Serial2

#define SLAVE_ID 10
#define NUM_REGS 24

// FIX: use int16_t so negative temperatures are stored correctly,
// then cast to uint16_t only when writing to Modbus registers.
int16_t holdingRegs[NUM_REGS];
bool modbusInitialized = false;

// ================= HTML (PROGMEM) =================
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Weather Station</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial;margin:20px;background:#f0f0f0}
.container{max-width:600px;margin:auto;background:#fff;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,.1)}
h2{color:#333;margin-bottom:4px}
.subtitle{color:#888;font-size:.85rem;margin-bottom:20px}

/* Status Bar */
.status-bar{background:#f9f9f9;padding:12px 16px;border-radius:5px;margin-bottom:20px;font-size:.85rem;display:flex;gap:20px;flex-wrap:wrap;border-left:3px solid #2196F3}
.status-item span{color:#2196F3;font-weight:700}

/* City Selector */
.city-selector{display:flex;gap:10px;margin-bottom:20px;flex-wrap:wrap}
.city-selector select{flex:1;padding:10px;border:1px solid #ddd;border-radius:5px;font-size:14px;background:#fff;min-width:150px}
.city-selector button{padding:10px 20px;background:#2196F3;color:#fff;border:none;border-radius:5px;cursor:pointer;font-weight:700}
.city-selector button:hover{background:#0b7dda}

/* Weather Table */
.weather-table{width:100%;border-collapse:collapse;margin-top:10px}
.weather-table td{padding:10px 12px;border-bottom:1px solid #eee}
.weather-table tr:last-child td{border-bottom:none}
.weather-table .label{font-weight:700;color:#555;width:40%;background:#f9f9f9;white-space:nowrap}
.weather-table .value{color:#333;width:60%}
.weather-card{background:#f9f9f9;border-radius:8px;padding:10px 0;border-left:3px solid #4CAF50}

/* Buttons */
.edit-btn,.reset-btn{color:#fff;border:none;padding:12px;font-size:16px;cursor:pointer;border-radius:5px;margin:5px 0}
.edit-btn{background:#2196F3}
.edit-btn:hover{background:#0b7dda}
.reset-btn{background:#f44336}
.reset-btn:hover{background:#d32f2f}
.button-group{display:flex;gap:10px;flex-wrap:wrap;margin-top:20px}

/* Modal */
.modal{display:none;position:fixed;z-index:1000;left:0;top:0;width:100%;height:100%;background:rgba(0,0,0,.5);animation:fadeIn .3s}
.modal-content{background:#fff;margin:10% auto;padding:30px;border-radius:10px;width:90%;max-width:500px;box-shadow:0 4px 20px rgba(0,0,0,.3);animation:slideIn .3s}
@keyframes fadeIn{from{opacity:0}to{opacity:1}}
@keyframes slideIn{from{transform:translateY(-50px);opacity:0}to{transform:translateY(0);opacity:1}}
.close{float:right;font-size:28px;font-weight:700;cursor:pointer;color:#999}
.close:hover{color:#333}
.modal-title{margin-top:0;color:#333;font-weight:700}
.error-msg{color:#f44336;font-size:14px;margin:5px 0;min-height:20px}
.warning-text{color:#f44336;font-weight:700}
input{width:100%;padding:10px;margin:5px 0 15px;border:1px solid #ddd;border-radius:5px;box-sizing:border-box;font-size:14px}
input:focus{border-color:#2196F3;outline:none}
input[type=submit]{width:100%;color:#fff;border:none;padding:12px;font-size:16px;cursor:pointer;border-radius:5px;margin:5px 0;background:#2196F3}
input[type=submit]:hover{background:#0b7dda}
.label{font-weight:700;color:#555;margin-top:10px;display:block;font-size:.85rem}
</style>
</head>
<body>
<div class="container">
<h2>ESP32 Weather Station</h2>
<p class="subtitle" id="sub">Connected to <span id="device-name">Loading...</span></p>

<!-- Status Bar -->
<div class="status-bar">
  <div class="status-item">WiFi: <span id="st-wifi">---</span></div>
  <div class="status-item">IP: <span id="st-ip">---</span></div>
  <div class="status-item">Uptime: <span id="st-uptime">---</span></div>
  <div class="status-item">Modbus: <span id="st-modbus">Slave 10 | 9600 8N1</span></div>
</div>

<!-- City Selector -->
<div class="city-selector">
  <select id="city-select">
    <option value="-1">Select a city...</option>
  </select>
  <button onclick="loadWeather()">Show Weather</button>
</div>

<!-- Weather Display -->
<div id="weather-display" style="display:none">
  <div class="weather-card">
    <table class="weather-table">
      <tr><td class="label">City</td><td class="value" id="w-city">---</td></tr>
      <tr><td class="label">Temperature</td><td class="value" id="w-temp">---</td></tr>
      <tr><td class="label">Feels Like</td><td class="value" id="w-feels">---</td></tr>
      <tr><td class="label">Humidity</td><td class="value" id="w-humidity">---</td></tr>
      <tr><td class="label">Pressure</td><td class="value" id="w-pressure">---</td></tr>
      <tr><td class="label">Wind Speed</td><td class="value" id="w-wind">---</td></tr>
      <tr><td class="label">Wind Direction</td><td class="value" id="w-dir">---</td></tr>
      <tr><td class="label">Visibility</td><td class="value" id="w-visibility">---</td></tr>
      <tr><td class="label">Cloud Cover</td><td class="value" id="w-clouds">---</td></tr>
    </table>
  </div>
</div>

<!-- Buttons -->
<div class="button-group">
  <button class="edit-btn" onclick="openEditModal()">Edit Configuration</button>
  <button class="reset-btn" onclick="openResetModal()">Reset Device</button>
</div>
</div>

<!-- Auth Modal -->
<div id="editModal" class="modal">
<div class="modal-content">
<span class="close" onclick="closeModal('editModal')">&times;</span>
<h3 class="modal-title">Authentication Required</h3>
<form onsubmit="authenticateEdit(event)">
  <div class="label">Username:</div><input type="text" id="edit-username" required>
  <div class="label">Password:</div><input type="password" id="edit-password-input" required>
  <div id="edit-error" class="error-msg"></div>
  <input type="submit" value="Login">
</form>
</div></div>

<!-- Edit Form Modal -->
<div id="editFormModal" class="modal">
<div class="modal-content">
<span class="close" onclick="closeModal('editFormModal')">&times;</span>
<h3 class="modal-title">Edit Configuration</h3>
<form onsubmit="saveConfig(event)">
  <div class="label">Device Name:</div><input id="f-device" required>
  <div class="label">WiFi SSID:</div><input id="f-ssid" required>
  <div class="label">WiFi Password:</div><input type="password" id="f-pass">
  <div class="label">OpenWeatherMap API Key:</div><input id="f-api" required>
  <div class="label">City 1 (Required):</div><input id="f-c1" required>
  <div class="label">City 2:</div><input id="f-c2">
  <div class="label">City 3:</div><input id="f-c3">
  <div id="save-msg" class="error-msg"></div>
  <input type="submit" value="Save & Reboot">
</form>
</div></div>

<!-- Reset Modal -->
<div id="resetModal" class="modal">
<div class="modal-content">
<span class="close" onclick="closeModal('resetModal')">&times;</span>
<h3 class="modal-title">Reset Device</h3>
<p class="warning-text">WARNING: This will delete ALL configuration data!</p>
<form onsubmit="resetDevice(event)">
  <div class="label">New Username:</div><input type="text" id="new-username" required>
  <div class="label">New Password:</div><input type="password" id="new-password" required minlength="4">
  <div class="label">Confirm Password:</div><input type="password" id="confirm-password" required>
  <div id="reset-error" class="error-msg"></div>
  <input type="submit" value="Reset Device" style="background:#f44336">
</form>
</div></div>

<script>
// ---- helpers ----
function closeModal(id){ document.getElementById(id).style.display='none'; }
window.onclick = function(e) {
  if (e.target.classList.contains('modal')) e.target.style.display='none';
}

// ---- load status ----
async function loadStatus(){
  try{
    const d = await fetch('/status').then(r=>r.json());
    document.getElementById('device-name').textContent = d.device_name||'ESP32-Weather';
    document.getElementById('st-wifi').textContent = d.wifi||'---';
    document.getElementById('st-ip').textContent = d.ip||'---';
    document.getElementById('st-uptime').textContent = d.uptime||'---';
    
    // Populate city dropdown - only show valid cities without symbols
    var sel = document.getElementById('city-select');
    sel.innerHTML = '<option value="-1">Select a city...</option>';
    if(d.cities){
      for(var i=0;i<d.cities.length;i++){
        if(d.cities[i].valid){
          var opt = document.createElement('option');
          opt.value = i;
          opt.textContent = d.cities[i].name;
          sel.appendChild(opt);
        }
      }
    }
  }catch(e){ console.error('Status error:',e); }
}

// ---- load weather ----
async function loadWeather(){
  var id = document.getElementById('city-select').value;
  if(id == -1){ 
    document.getElementById('weather-display').style.display='none';
    return; 
  }
  try{
    var d = await fetch('/api?city='+id).then(r=>r.json());
    document.getElementById('weather-display').style.display='block';
    document.getElementById('w-city').textContent = d.city||'---';
    document.getElementById('w-temp').textContent = d.temp+' degC'||'---';
    document.getElementById('w-feels').textContent = d.feels_like+' degC'||'---';
    document.getElementById('w-humidity').textContent = d.humidity+' %'||'---';
    document.getElementById('w-pressure').textContent = d.pressure+' hPa'||'---';
    document.getElementById('w-wind').textContent = d.wind_speed+' m/s'||'---';
    document.getElementById('w-dir').textContent = d.wind_dir+' deg'||'---';
    document.getElementById('w-visibility').textContent = d.visibility+' m'||'---';
    document.getElementById('w-clouds').textContent = d.clouds+' %'||'---';
  }catch(e){ 
    alert('Error loading weather data'); 
    console.error(e);
  }
}

// ---- edit flow ----
function openEditModal(){
  document.getElementById('edit-error').textContent='';
  document.getElementById('edit-username').value='';
  document.getElementById('edit-password-input').value='';
  document.getElementById('editModal').style.display='block';
}

async function authenticateEdit(e){
  e.preventDefault();
  document.getElementById('edit-error').textContent='';
  var body = JSON.stringify({
    username: document.getElementById('edit-username').value,
    password: document.getElementById('edit-password-input').value
  });
  try{
    var r = await fetch('/auth',{method:'POST',headers:{'Content-Type':'application/json'},body:body});
    if(r.ok){
      closeModal('editModal');
      var cfg = await fetch('/config').then(x=>x.json());
      document.getElementById('f-device').value = cfg.device_name||'';
      document.getElementById('f-ssid').value   = cfg.ssid||'';
      document.getElementById('f-pass').value   = '';
      document.getElementById('f-api').value    = '';
      document.getElementById('f-c1').value     = cfg.city1||'';
      document.getElementById('f-c2').value     = cfg.city2||'';
      document.getElementById('f-c3').value     = cfg.city3||'';
      document.getElementById('save-msg').textContent='';
      document.getElementById('editFormModal').style.display='block';
    } else {
      var err = await r.json();
      document.getElementById('edit-error').textContent = err.error||'Invalid credentials';
    }
  }catch(err){ document.getElementById('edit-error').textContent='Network error: '+err.message; }
}

async function saveConfig(e){
  e.preventDefault();
  var msg = document.getElementById('save-msg');
  msg.style.color='blue'; msg.textContent='Saving...';
  var body = JSON.stringify({
    dn:  document.getElementById('f-device').value,
    s:   document.getElementById('f-ssid').value,
    p:   document.getElementById('f-pass').value,
    a:   document.getElementById('f-api').value,
    c1:  document.getElementById('f-c1').value,
    c2:  document.getElementById('f-c2').value,
    c3:  document.getElementById('f-c3').value
  });
  try{
    var r = await fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:body});
    if(r.ok){ msg.style.color='green'; msg.textContent='Saved! Rebooting...'; }
    else { msg.style.color='red'; msg.textContent='Save failed'; }
  }catch(err){ msg.style.color='red'; msg.textContent='Error: '+err.message; }
}

// ---- reset flow ----
function openResetModal(){
  document.getElementById('reset-error').textContent='';
  document.getElementById('new-username').value='';
  document.getElementById('new-password').value='';
  document.getElementById('confirm-password').value='';
  document.getElementById('resetModal').style.display='block';
}

async function resetDevice(e){
  e.preventDefault();
  var errEl = document.getElementById('reset-error');
  var nu = document.getElementById('new-username').value;
  var np = document.getElementById('new-password').value;
  var cp = document.getElementById('confirm-password').value;
  if(np!==cp){ errEl.textContent='Passwords do not match!'; return; }
  if(!confirm('Are you sure? ALL data will be erased!')) return;
  try{
    var r = await fetch('/reset-device',{
      method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({username:nu,password:np})
    });
    if(r.ok){
      alert('Device resetting with new credentials. Reconnect in a moment.');
      closeModal('resetModal');
      setTimeout(function(){ window.location.reload(); }, 4000);
    } else {
      var err = await r.json();
      errEl.textContent = err.error||'Reset failed';
    }
  }catch(err){ errEl.textContent='Error: '+err.message; }
}

// ---- start ----
loadStatus();
setInterval(loadStatus, 5000);
</script>
</body>
</html>
)rawliteral";

// ================= HELPER: signed temp to uint16 =================
inline uint16_t signedToReg(int16_t v) { return (uint16_t)v; }

// ================= WEB ROUTES =================
void setupWebRoutes() {
  server.on("/", []() {
    server.send_P(200, "text/html", htmlPage);
  });

  server.on("/auth", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"No body\"}");
      return;
    }
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    prefs.begin("config", true);
    String su = prefs.getString("adminUser", "admin");
    String sp = prefs.getString("adminPass", "admin123");
    prefs.end();

    if (doc["username"].as<String>() == su && doc["password"].as<String>() == sp)
      server.send(200, "application/json", "{\"success\":true}");
    else
      server.send(401, "application/json", "{\"error\":\"Invalid credentials\"}");
  });

  server.on("/config", []() {
    String json = "{";
    json += "\"device_name\":\"" + deviceName + "\",";
    json += "\"ssid\":\"" + ssid + "\",";
    json += "\"city1\":\"" + cities[0] + "\",";
    json += "\"city2\":\"" + cities[1] + "\",";
    json += "\"city3\":\"" + cities[2] + "\"";
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/save", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"No body\"}");
      return;
    }
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    prefs.begin("config", false);
    String dn = doc["dn"].as<String>();
    if (dn.length() > 0) { prefs.putString("deviceName", dn); deviceName = dn; }
    prefs.putString("ssid", doc["s"].as<String>());
    String newPass = doc["p"].as<String>();
    if (newPass.length() > 0) prefs.putString("pass", newPass);
    String newApi = doc["a"].as<String>();
    if (newApi.length() > 0) prefs.putString("api", newApi);
    prefs.putString("c1", doc["c1"].as<String>());
    prefs.putString("c2", doc["c2"].as<String>());
    prefs.putString("c3", doc["c3"].as<String>());
    prefs.end();

    Serial.println("[CONFIG] Saved. Rebooting...");
    server.send(200, "application/json", "{\"success\":true}");
    delay(1000);
    ESP.restart();
  });

  server.on("/reset-device", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"No body\"}");
      return;
    }
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    String nu = doc["username"].as<String>();
    String np = doc["password"].as<String>();
    if (nu.isEmpty() || np.length() < 4) {
      server.send(400, "application/json", "{\"error\":\"Username and password (min 4 chars) required\"}");
      return;
    }
    prefs.begin("config", false);
    prefs.clear();
    prefs.putString("adminUser", nu);
    prefs.putString("adminPass", np);
    prefs.end();
    Serial.println("[SYSTEM] Device reset. New user: " + nu);
    server.send(200, "application/json", "{\"success\":true}");
    delay(1000);
    ESP.restart();
  });

  server.onNotFound([]() {
    server.send(404, "text/plain", "404 Not Found");
  });
}

// ================= API ROUTES =================
void setupApiRoutes() {
  server.on("/api", []() {
    if (!server.hasArg("city")) {
      server.send(400, "application/json", "{\"error\":\"missing city\"}");
      return;
    }
    int id = server.arg("city").toInt();
    if (id < 0 || id > 2) {
      server.send(400, "application/json", "{\"error\":\"city must be 0-2\"}");
      return;
    }
    if (!cityValid[id]) {
      server.send(404, "application/json", "{\"error\":\"city not configured or invalid\"}");
      return;
    }
    int b = id * 8;
    String json = "{";
    json += "\"city\":\"" + cities[id] + "\",";
    json += "\"temp\":"       + String((int16_t)holdingRegs[b+0] / 10.0, 1) + ",";
    json += "\"feels_like\":" + String((int16_t)holdingRegs[b+1] / 10.0, 1) + ",";
    json += "\"humidity\":"   + String(holdingRegs[b+2]) + ",";
    json += "\"pressure\":"   + String(holdingRegs[b+3]) + ",";
    json += "\"wind_speed\":" + String(holdingRegs[b+4] / 10.0, 1) + ",";
    json += "\"wind_dir\":"   + String(holdingRegs[b+5]) + ",";
    json += "\"visibility\":" + String(holdingRegs[b+6]) + ",";
    json += "\"clouds\":"     + String(holdingRegs[b+7]);
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/status", []() {
    String json = "{";
    json += "\"device_name\":\"" + deviceName + "\",";
    json += "\"wifi\":\""  + String(WiFi.status()==WL_CONNECTED ? "Connected" : "Disconnected") + "\",";
    json += "\"ip\":\""    + WiFi.localIP().toString() + "\",";
    json += "\"uptime\":"  + String(millis() / 1000) + ",";
    json += "\"cities\":[";
    for (int i = 0; i < 3; i++) {
      json += "{\"name\":\"" + cities[i] + "\",\"valid\":" + (cityValid[i]?"true":"false") + "}";
      if (i < 2) json += ",";
    }
    json += "]}";
    server.send(200, "application/json", json);
  });

  server.on("/all", []() {
    String json = "[";
    for (int id = 0; id < 3; id++) {
      if (id > 0) json += ",";
      if (!cityValid[id]) { json += "null"; continue; }
      int b = id * 8;
      json += "{";
      json += "\"city\":\""      + cities[id] + "\",";
      json += "\"temp\":"        + String((int16_t)holdingRegs[b+0] / 10.0, 1) + ",";
      json += "\"feels_like\":"  + String((int16_t)holdingRegs[b+1] / 10.0, 1) + ",";
      json += "\"humidity\":"    + String(holdingRegs[b+2]) + ",";
      json += "\"pressure\":"    + String(holdingRegs[b+3]) + ",";
      json += "\"wind_speed\":"  + String(holdingRegs[b+4] / 10.0, 1) + ",";
      json += "\"wind_dir\":"    + String(holdingRegs[b+5]) + ",";
      json += "\"visibility\":"  + String(holdingRegs[b+6]) + ",";
      json += "\"clouds\":"      + String(holdingRegs[b+7]);
      json += "}";
    }
    json += "]";
    server.send(200, "application/json", json);
  });
}

// ================= LOAD CONFIG =================
void loadConfig() {
  Serial.println("[CONFIG] Loading from NVS...");
  prefs.begin("config", true);
  ssid       = prefs.getString("ssid", "");
  password   = prefs.getString("pass", "");
  apiKey     = prefs.getString("api",  "");
  cities[0]  = prefs.getString("c1",  "");
  cities[1]  = prefs.getString("c2",  "");
  cities[2]  = prefs.getString("c3",  "");
  adminUser  = prefs.getString("adminUser", "admin");
  adminPass  = prefs.getString("adminPass", "admin123");
  deviceName = prefs.getString("deviceName", "ESP32-Weather");
  prefs.end();

  Serial.println("[CONFIG] Device: " + deviceName);
  Serial.println("[CONFIG] SSID: "   + (ssid.isEmpty() ? "(not set)" : ssid));
  Serial.println("[CONFIG] City1: "  + (cities[0].isEmpty() ? "(not set)" : cities[0]));
  Serial.println("[CONFIG] City2: "  + (cities[1].isEmpty() ? "(not set)" : cities[1]));
  Serial.println("[CONFIG] City3: "  + (cities[2].isEmpty() ? "(not set)" : cities[2]));
}

// ================= VALIDATE CITIES =================
void validateCities() {
  Serial.println("[API] Validating cities...");
  for (int i = 0; i < 3; i++) {
    if (cities[i].isEmpty()) { cityValid[i] = false; continue; }
    HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/weather?q=" + cities[i] + "&appid=" + apiKey;
    http.begin(url);
    http.setTimeout(5000);
    int code = http.GET();
    http.end();
    cityValid[i] = (code == 200);
    Serial.println("[CITY] " + cities[i] + " → " + (cityValid[i] ? "VALID" : "INVALID (HTTP " + String(code) + ")"));
  }
}

// ================= FETCH WEATHER =================
bool fetchWeatherForCity(int index) {
  if (!cityValid[index] || cities[index].isEmpty()) return false;

  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" +
               cities[index] + "&appid=" + apiKey + "&units=metric";
  http.begin(url);
  http.setTimeout(5000);
  int code = http.GET();

  if (code != 200) {
    Serial.println("[HTTP] Error " + String(code) + " for " + cities[index]);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, payload)) {
    Serial.println("[JSON] Parse error for " + cities[index]);
    return false;
  }

  JsonObject main = doc["main"];
  JsonObject wind = doc["wind"];

  int b = index * 8;
  holdingRegs[b+0] = (int16_t)(main["temp"].as<float>()       * 10);
  holdingRegs[b+1] = (int16_t)(main["feels_like"].as<float>() * 10);
  holdingRegs[b+2] = (int16_t) main["humidity"].as<int>();
  holdingRegs[b+3] = (int16_t) main["pressure"].as<int>();
  holdingRegs[b+4] = (int16_t)(wind["speed"].as<float>()      * 10);
  holdingRegs[b+5] = (int16_t) wind["deg"].as<int>();
  holdingRegs[b+6] = (int16_t) doc["visibility"].as<int>();
  holdingRegs[b+7] = (int16_t) doc["clouds"]["all"].as<int>();

  Serial.printf("[WEATHER] %s → T=%.1f°C H=%d%% P=%dhPa W=%.1fm/s\n",
    cities[index].c_str(),
    holdingRegs[b+0] / 10.0,
    holdingRegs[b+2],
    holdingRegs[b+3],
    holdingRegs[b+4] / 10.0);
  return true;
}

void fetchWeatherAll() {
  Serial.println("[WEATHER] Updating all cities...");
  for (int i = 0; i < 3; i++) {
    fetchWeatherForCity(i);
    if (i < 2) delay(300);
  }
  updateModbusRegisters();
}

// ================= UPDATE MODBUS REGISTERS =================
void updateModbusRegisters() {
  for (int i = 0; i < NUM_REGS; i++) {
    mb1.Hreg(i, (uint16_t)holdingRegs[i]);
    mb2.Hreg(i, (uint16_t)holdingRegs[i]);
  }
  Serial.println("[MODBUS] Registers updated on both ports");
}

// ================= INITIALIZE MODBUS =================
void initModbus() {
  for (int i = 0; i < NUM_REGS; i++) holdingRegs[i] = 0;

  Serial1.begin(9600, SERIAL_8N1, RXD1, TXD1);
  mb1.begin(&Serial1);
  mb1.addHreg(0, 0, NUM_REGS);
  mb1.slave(SLAVE_ID);

  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  mb2.begin(&Serial2);
  mb2.addHreg(0, 0, NUM_REGS);
  mb2.slave(SLAVE_ID);

  modbusInitialized = true;

  Serial.printf("[MODBUS] Serial1 RX=GPIO%d TX=GPIO%d\n", RXD1, TXD1);
  Serial.printf("[MODBUS] Serial2 RX=GPIO%d TX=GPIO%d\n", RXD2, TXD2);
  Serial.printf("[MODBUS] Slave ID=%d  Baud=9600 8N1  Registers=0-%d\n", SLAVE_ID, NUM_REGS-1);
}

// ================= WIFI / SERVER =================
void startServer() {
  setupWebRoutes();
  setupApiRoutes();
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
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WIFI] Connected  IP=" + WiFi.localIP().toString() +
                   "  RSSI=" + String(WiFi.RSSI()) + "dBm");
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
  Serial.println("[BOOT] ESP32 Weather Station v6.0");
  Serial.println("====================");

  initModbus();

  bool wifiOk = connectWiFi();

  if (wifiOk) {
    validateCities();
    fetchWeatherAll();
  }

  Serial.println("\n[SYSTEM] Ready!");
  Serial.println("[AUTH] User=" + adminUser);
  Serial.println("[DEVICE] " + deviceName);
  Serial.println("====================\n");
}

// ================= LOOP =================
void loop() {
  if (modbusInitialized) {
    mb1.task();
    mb2.task();
  }

  server.handleClient();

  static unsigned long lastUpdate = millis();
  if (millis() - lastUpdate >= 600000UL) {
    lastUpdate = millis();
    Serial.println("[CYCLE] 10-minute update...");
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WIFI] Lost connection, reconnecting...");
      WiFi.reconnect();
      unsigned long t = millis();
      while (WiFi.status() != WL_CONNECTED && millis()-t < 10000) delay(500);
    }
    if (WiFi.status() == WL_CONNECTED) {
      fetchWeatherAll();
      Serial.println("[CYCLE] Done");
    } else {
      Serial.println("[WIFI] Reconnect failed — skipping update");
    }
  }
}
