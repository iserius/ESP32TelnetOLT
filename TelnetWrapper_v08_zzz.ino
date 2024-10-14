/*    ESP32 telnet wraper for OLTC320

      main function is to config OLT C320 in CLI without using putty or auto telnet

      how to use : just open in web browser

      12 october 2024
      - v0.5  - add OTA function
      - v0.6  - added status bar header
      - v0.7  - add EEPROM save function
              - hide extra button
              - more responsive web
      13 october 2024
      - v0.8  - add ONU config script
              - fix long value result
              - fix status indicator (not fixed yet)
              - preset button is gone, preset buttton get wrong EEPROM address so it not showe, make sure erase all flash / EEPROM
              - fix .replace() calls to use a global regular expression
              - update Status time , heap, temperature, Wifi Signal, Voltage
            black themes
*/


#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>

const char* ssid = "RAHASIA";
const char* password = "";

WebServer server(80);
WiFiClient telnetClient;

#define EEPROM_SIZE 4096
#define MAX_BUTTON_NAME 20
#define MAX_BUTTON_COMMAND 50
#define NUM_BUTTONS 8
#define ONU_SCRIPT_SIZE 2048

// EEPROM layout
#define BUTTON_CONFIG_START 0
#define BUTTON_CONFIG_SIZE (NUM_BUTTONS * (MAX_BUTTON_NAME + MAX_BUTTON_COMMAND))
#define ONU_SCRIPT_START (EEPROM_SIZE - ONU_SCRIPT_SIZE)

struct ButtonConfig {
  char name[MAX_BUTTON_NAME];
  char command[MAX_BUTTON_COMMAND];
};


ButtonConfig buttons[NUM_BUTTONS];
char onuConfigScript[ONU_SCRIPT_SIZE];
const size_t JSON_CAPACITY = JSON_ARRAY_SIZE(50) + 50*JSON_STRING_SIZE(128);


void setup() {
  Serial.begin(115200);
  
  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadButtonConfigs();
  loadOnuScript();
  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  Serial.println("Connected to WiFi");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Hostname defaults to esp3232-[MAC]
  ArduinoOTA.setHostname("TelnetWrapper");

  // No authentication by default
  ArduinoOTA.setPassword("adnim");

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else {  // U_SPIFFS
        type = "filesystem";
      }

      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) {
        Serial.println("Auth Failed");
      } else if (error == OTA_BEGIN_ERROR) {
        Serial.println("Begin Failed");
      } else if (error == OTA_CONNECT_ERROR) {
        Serial.println("Connect Failed");
      } else if (error == OTA_RECEIVE_ERROR) {
        Serial.println("Receive Failed");
      } else if (error == OTA_END_ERROR) {
        Serial.println("End Failed");
      }
    });

  ArduinoOTA.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Set up web server routes
  server.on("/", handleRoot);
  server.on("/terminal", HTTP_POST, handleTerminal);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/setButton", HTTP_POST, handleSetButton);
  server.on("/getButtons", HTTP_GET, handleGetButtons);
  server.on("/TWconfig", HTTP_GET, handleDownloadEEPROM);
  server.on("/uploadEEPROM", HTTP_POST, handleUploadEEPROM);
  server.on("/saveOnuScript", HTTP_POST, handleSaveOnuScript);
  server.on("/loadOnuScript", HTTP_GET, handleLoadOnuScript);
  
  server.on("/status", HTTP_GET, []() {
  String json = "{";
  json += "\"wifi\":" + String(WiFi.RSSI()) + ",";
  //json += "\"battery\":" + String(analogRead(BATTERY_PIN) * VOLTAGE_MULTIPLIER) + ",";
  json += "\"battery\":" + String(analogRead(15) * 2) + ",";
  json += "\"temperature\":" + String(temperatureRead()) + ",";
  json += "\"heap\":" + String(ESP.getFreeHeap());
  json += "}";
  server.send(200, "application/json", json);
});

  // Start the web server
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
}


void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Telnet-Wrapper</title>
  <style>
    /* Base styles */
    body {
      font-family: monospace;
      background: #000;
      color: #0f0;
      margin: 0;
      padding: 0;
      box-sizing: border-box;
      font-size: 14px;
    }
    
    /* Responsive layout */
    @media (max-width: 768px) {
      body {
        font-size: 12px;
        padding: 5px;
      }
    }
    
    /* Status bar styles */
    #statusBar {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 10px;
      height: 20px;
      font-size: 14px;
      position: relative;
      background-color: #001f00;
      padding: 5px;
      border-radius: 5px;
    }
    #statusIndicator {
      width: 12px;
      height: 12px;
      border-radius: 50%;
      margin-right: 10px;
    }
    #statusText {
      font-size: 14px;
    }
    #clock {
      position: absolute;
      left: 50%;
      transform: translateX(-50%);
      font-size: 14px;
      font-weight: bold;
    }
    #phoneStatus {
      display: flex;
      align-items: center;
      gap: 10px;
    }
    .status-icon {
      display: flex;
      align-items: center;
      font-size: 12px;
    }
    .status-icon::before {
      margin-right: 2px;
    }
    #wifiSignal::before { content: '📶'; }
    #battery::before { content: '🔋'; }
    #temperature::before { content: '🌡️'; }
    #heap::before { content: '💾'; }

    /* Terminal styles */
    #terminal {
      width: 100%;
      height: 50vh;
      overflow-y: scroll;
      border: 1px solid #0f0;
      padding: 10px;
      margin-bottom: 10px;
      line-height: 1.4;
    }
    
    /* Input and button styles */
    #input {
      width: 100%;
      margin-bottom: 10px;
      padding: 5px;
      font-size: 14px;
    }
    .button-container {
      display: flex;
      flex-wrap: wrap;
      gap: 5px;
      margin-bottom: 10px;
    }
    .eeprom-button, .preset-button {
      flex-grow: 1;
      padding: 10px;
      font-size: 14px;
      background-color: #001f00;
      color: #0f0;
      border: 1px solid #0f0;
      cursor: pointer;
    }
    .preset-button:hover, .eeprom-button:hover {
      background-color: #003f00;
    }
    
    /* Status indicator colors */
    .connected { background-color: #00ff00; }
    .disconnected { background-color: #ff0000; }
    
    #loginForm input, #loginForm button, 
    #configTerminalBtn, #exitBtn, #showUnconfigOnuBtn, #cekSinyalBtn, #showPonPowerBtn {
      flex: 1;
      min-width: 0;
    }

    .preset-button {
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    /* Login form styles */
    #loginForm {
      margin-bottom: 10px;
      display: flex;
      flex-wrap: wrap;
      gap: 5px;
    }
    #loginForm input {
      flex-grow: 1;
      padding: 5px;
    }
    
    /* Overlay and selector styles */
    #selectorOverlay {
      display: none;
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background-color: rgba(0, 0, 0, 0.7);
      z-index: 1000;
    }
    #portSelector, #onuInput {
      display: none;
      position: fixed;
      bottom: 0;
      left: 50%;
      transform: translateX(-50%);
      width: 90%;
      max-width: 300px;
      max-height: 50%;
      overflow-y: auto;
      background-color: #001f00;
      border: 1px solid #0f0;
      border-radius: 10px 10px 0 0;
      z-index: 1001;
    }
    .ios-option {
      padding: 15px;
      text-align: center;
      border-bottom: 1px solid #0f0;
      cursor: pointer;
    }
    .ios-option:hover {
      background-color: #003f00;
    }
    .ios-option.selected {
      background-color: #005f00;
    }
    #onuInput {
      padding: 15px;
      text-align: center;
    }
    #onuInput input {
      width: 50px;
      margin-right: 10px;
      background-color: #001f00;
      color: #0f0;
      border: 1px solid #0f0;
      padding: 5px;
    }
    #onuInput button {
      background-color: #003f00;
      color: #0f0;
      border: 1px solid #0f0;
      padding: 5px 10px;
      cursor: pointer;
    }
    #onuConfigForm {
      display: none;
      margin-top: 10px;
    }
    #onuConfigForm input {
      width: 100%;
      margin-bottom: 5px;
      padding: 5px;
      font-size: 14px;
    }
    #configureOnuBtn, #extraBtn {
      width: 100%;
      padding: 10px;
      font-size: 14px;
      background-color: #001f00;
      color: #0f0;
      border: 1px solid #0f0;
      cursor: pointer;
    }
    #configureOnuBtn:hover, #extraBtn:hover {
      background-color: #003f00;
    }
    #scriptEditor {
      width: 100%;
      height: 300px;
      margin-top: 10px;
    }
    #editScriptBtn, #saveScriptBtn, #loadScriptBtn {
      margin-top: 10px;
    }
    .error-message {
      color: red;
      font-size: 12px;
      margin-top: 5px;
    }
    #extraBtn {
      margin-left: 5px;
    }
    #buttonContainer, .eeprom-buttons {
      display: none;
    }
    
    /* Dark theme login overlay styles */
    #darkThemeLoginOverlay {
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background-image: url('https://source.unsplash.com/1600x900/?mountain,night');
      background-size: cover;
      background-position: center;
      display: flex;
      justify-content: center;
      align-items: center;
      z-index: 9999;
    }
    #darkThemeLoginForm {
      background-color: rgba(0, 0, 0, 0.7);
      padding: 30px;
      border-radius: 10px;
      width: 80%;
      max-width: 300px;
    }
    #darkThemeLoginForm input {
      width: 100%;
      padding: 10px;
      margin-bottom: 15px;
      border: none;
      border-bottom: 1px solid #ffffff;
      background-color: transparent;
      color: #ffffff;
      font-size: 16px;
    }
    #darkThemeLoginForm input::placeholder {
      color: #cccccc;
    }
    #darkThemeLoginForm button {
      width: 100%;
      padding: 12px;
      background-color: #ccc;
      color: #000000;
      border: none;
      border-radius: 25px;
      font-size: 16px;
      font-weight: bold;
      cursor: pointer;
      margin-bottom: 15px;
    }
    #darkThemeLoginForm button:hover {
      background-color: #bcbcbc;
    }
    .darkThemeSocialLogin {
      display: flex;
      justify-content: center;
      margin-top: 20px;
    }
    .darkThemeSocialLogin button {
      background-color: transparent;
      border: 1px solid #ffffff;
      color: #ffffff;
      padding: 8px 15px;
      margin: 0 10px;
      border-radius: 5px;
      cursor: pointer;
    }
    #darkThemeLoginError {
      color: #ff4d4d;
      text-align: center;
      margin-top: 10px;
    }
  </style>
</head>
<body>

    <div id="darkThemeLoginOverlay">
        <form id="darkThemeLoginForm">
            <input type="text" id="darkThemeLoginUsername" placeholder="Username" required>
            <input type="password" id="darkThemeLoginPassword" placeholder="Password" required>
            <button type="submit">LOG IN</button>
            <p id="darkThemeLoginError"></p>
        </form>
    </div>


  <div id="statusBar">
    <div>
      <div id="statusIndicator" class="disconnected"></div>
      <span id="statusText">Disconnected</span>
    </div>
    <div id="clock"></div>
    <div id="phoneStatus">
      <span id="wifiSignal" class="status-icon">-</span>
      <span id="battery" class="status-icon">-V</span>
      <span id="temperature" class="status-icon">-°C</span>
      <span id="heap" class="status-icon">-</span>
    </div>
  </div>

  <div id="terminal"></div>
  <input type="text" id="input" placeholder="Enter command...">
<div id="loginForm" style="display: flex; gap: 10px; margin-bottom: 10px;">
  <input type="text" id="serverAddress" placeholder="Server Address" value="10.10.10.3" style="flex-grow: 1;">
  <input type="text" id="username" placeholder="Username" value="admin" style="flex-grow: 1;">
  <input type="password" id="password" placeholder="Password" value="chArlize24" style="flex-grow: 1;">
  <button id="autoLoginBtn" class="preset-button">Login</button>
  <button id="disconnectBtn" class="preset-button">Disconnect</button>
</div>

<div style="display: flex; gap: 10px; margin-bottom: 10px;">
  <button id="configTerminalBtn" class="preset-button">Config Terminal</button>
  <button id="exitBtn" class="preset-button">Exit</button>
  <button id="showUnconfigOnuBtn" class="preset-button">Show Unconfig ONU</button>
  <button id="cekSinyalBtn" class="preset-button">Cek Sinyal</button>
  <button id="showPonPowerBtn" class="preset-button">Show pon Status</button>
</div>


  <div id="loginForm">
    <button id="extraBtn" class="preset-button">Extra</button>
      <button id="configureOnuBtn" class="preset-button">Configure ONU</button>
  </div>

  <div class="button-container" id="buttonContainer"></div>
    <div class="eeprom-buttons">
    <button id="downloadEEPROMBtn" class="eeprom-button">Download Config File</button>
    <input type="file" id="uploadEEPROMFile" style="display:none;" />
    <button id="uploadEEPROMBtn" class="eeprom-button">Upload Config File</button>
  </div>



  <div id="onuConfigForm">
    <input type="text" id="serialNumber" placeholder="Modem Serial Number (12 characters)" maxlength="12">
    <div id="serialNumberError" class="error-message"></div>
    <input type="number" id="rackNumber" placeholder="Rack Number">
    <input type="number" id="portNumber" placeholder="Port Number">
    <input type="number" id="oonuNumber" placeholder="ONU Number">
    <input type="number" id="vlanNumber" placeholder="VLAN Number">
    <input type="text" id="Ousername" placeholder="Username">
    <input type="password" id="Opassword" placeholder="Password">
    <input type="text" id="description" placeholder="Description">
    <button id="submitOnuConfig" class="preset-button">Submit ONU Configuration</button>
    <button id="editScriptBtn" class="preset-button">Edit ONU Config Script</button>
    <textarea id="scriptEditor" style="display: none;"></textarea>
    <button id="saveScriptBtn" class="preset-button" style="display: none;">Save Script</button>
    <button id="loadScriptBtn" class="preset-button" style="display: none;">Load Script</button>
    <button id="submitOnuConfig" class="preset-button">Submit ONU Configuration</button>
  </div>
  </div>

 

  <div id="selectorOverlay"></div>
  <div id="portSelector"></div>
  <div id="onuInput">
    <label for="onuNumber">ONU number (1-99):</label>
    <input type="number" id="onuNumber" min="1" max="99">
    <button onclick="sendCekSinyalCommand()">Send</button>
  </div>

  <script>
          const darkThemeLoginOverlay = document.getElementById('darkThemeLoginOverlay');
        const darkThemeLoginForm = document.getElementById('darkThemeLoginForm');
        const darkThemeLoginUsername = document.getElementById('darkThemeLoginUsername');
        const darkThemeLoginPassword = document.getElementById('darkThemeLoginPassword');
        const darkThemeLoginError = document.getElementById('darkThemeLoginError');


    const terminal = document.getElementById('terminal');
    const input = document.getElementById('input');
    const buttonContainer = document.getElementById('buttonContainer');
    const showUnconfigOnuBtn = document.getElementById('showUnconfigOnuBtn');
    const cekSinyalBtn = document.getElementById('cekSinyalBtn');
    const showPonPowerBtn = document.getElementById('showPonPowerBtn');
    const autoLoginBtn = document.getElementById('autoLoginBtn');
    const disconnectBtn = document.getElementById('disconnectBtn');
    const configTerminalBtn = document.getElementById('configTerminalBtn');
    const exitBtn = document.getElementById('exitBtn');
    const selectorOverlay = document.getElementById('selectorOverlay');
    const portSelector = document.getElementById('portSelector');
    const onuInput = document.getElementById('onuInput');
    const oonuNumber = document.getElementById('oonuNumber');
    const statusIndicator = document.getElementById('statusIndicator');
    const serverAddressInput = document.getElementById('serverAddress');
    const usernameInput = document.getElementById('username');
    const passwordInput = document.getElementById('password');
    const downloadEEPROMBtn = document.getElementById('downloadEEPROMBtn');
    const uploadEEPROMFile = document.getElementById('uploadEEPROMFile');
    const uploadEEPROMBtn = document.getElementById('uploadEEPROMBtn');
    const extraBtn = document.getElementById('extraBtn');
    const configureOnuBtn = document.getElementById('configureOnuBtn');
    const onuConfigForm = document.getElementById('onuConfigForm');
    const serialNumberInput = document.getElementById('serialNumber');
    const serialNumberError = document.getElementById('serialNumberError');
    const submitOnuConfig = document.getElementById('submitOnuConfig');
    const editScriptBtn = document.getElementById('editScriptBtn');
    const scriptEditor = document.getElementById('scriptEditor');
    const saveScriptBtn = document.getElementById('saveScriptBtn');
    const loadScriptBtn = document.getElementById('loadScriptBtn');



        darkThemeLoginForm.addEventListener('submit', function(e) {
            e.preventDefault();
            if (darkThemeLoginUsername.value === 'adnim' && darkThemeLoginPassword.value === 'minda') {
                darkThemeLoginOverlay.style.display = 'none';
            } else {
                darkThemeLoginError.textContent = 'Invalid username or password';
            }
        });


    let extraVisible = false;
    let selectedPort = null;
    let currentMode = null;
    let onuConfigScript = [
      'con ter',
      'int gpon-olt_1/${rackNumber}/${portNumber}',
      'onu ${oonuNumber} type ALL sn ${serialNumber}',
      'exit',
      'int gpon-onu_1/${rackNumber}/${portNumber}:${oonuNumber}',
      'name WaiFai_${oonuNumber}',
      'description ${description} - U: ${Ousername} - P: ${Opassword}',
      'tcont 1 profile 100M',
      'gemport 1 tcont 1',
      'gemport 2 tcont 1',
      'service-port 1 vport 1 user-vlan ${vlanNumber} vlan ${vlanNumber}',
      'service-port 2 vport 2 user-vlan 177 vlan 177',
      'exit',
      'pon-onu-mng gpon-onu_1/${rackNumber}/${portNumber}:${oonuNumber}',
      'service PPPoE gemport 1 vlan ${vlanNumber}',
      'service BRIDGE gemport 2 vlan 177',
      'wan-ip 1 mode pppoe username ${Ousername} password ${Opassword} vlan-profile ${vlanNumber} host 1',
      'wan-ip 1 ping-response enable traceroute-response enable',
      'vlan port eth_0/3 mode tag vlan 177',
      'vlan port eth_0/4 mode tag vlan 177',
      'vlan port wifi_0/3 mode tag vlan 177',
      'vlan port wifi_0/4 mode tag vlan 177',
      'interface wifi wifi_0/1 state unlock',
      'interface wifi wifi_0/2 state lock',
      'interface wifi wifi_0/3 state lock',
      'interface wifi wifi_0/4 state unlock',
      'security-mgmt 1 state enable mode forward protocol web',
      'ssid ctrl wifi_0/2 name bismillah',
      'ssid ctrl wifi_0/3 name bismillah',
      'ssid ctrl wifi_0/4 name WaiFai',
      'ssid auth wpa wifi_0/2 wpa2-psk key bismillah24',
      'ssid auth wpa wifi_0/3 wpa2-psk key bismillah24',
      'ssid auth wpa wifi_0/4 wpa2-psk key bismillah',
      'wan 1 ethuni 1,2 ssid 1 service internet host 1',
      'exit',
      'exit',
      'wr'
    ];

      editScriptBtn.onclick = () => {
      scriptEditor.value = onuConfigScript.join('\n');
      scriptEditor.style.display = 'block';
      saveScriptBtn.style.display = 'inline-block';
      loadScriptBtn.style.display = 'inline-block';
    };


      saveScriptBtn.onclick = () => {
      const scriptLines = scriptEditor.value.split('\n').filter(line => line.trim() !== '');
      fetch('/saveOnuScript', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({script: scriptLines})
      })
      .then(response => response.text())
      .then(data => {
        alert(data);
        onuConfigScript = scriptLines; // Update the local script array
      })
      .catch(error => {
        console.error('Error:', error);
        alert('Failed to save the script. Check the console for details.');
      });
    };

    loadScriptBtn.onclick = () => {
      fetch('/loadOnuScript')
      .then(response => response.json())
      .then(data => {
        onuConfigScript = data.script;
        scriptEditor.value = onuConfigScript.join('\n');
      })
      .catch(error => {
        console.error('Error:', error);
        alert('Failed to load the script. Check the console for details.');
      });
    };

    configureOnuBtn.onclick = () => {
      onuConfigForm.style.display = onuConfigForm.style.display === 'none' ? 'block' : 'none';
    };



      extraBtn.onclick = () => {
      extraVisible = !extraVisible;
      buttonContainer.style.display = extraVisible ? 'flex' : 'none';
      document.querySelector('.eeprom-buttons').style.display = extraVisible ? 'block' : 'none';
      extraBtn.textContent = extraVisible ? 'Hide Extra' : 'Extra';
      
      // If extra buttons are visible, ensure they are loaded
      if (extraVisible) {
        loadButtons();
      }
    };
    



    downloadEEPROMBtn.onclick = () => {
      window.location.href = '/TWconfig';
    };

    uploadEEPROMBtn.onclick = () => {
      uploadEEPROMFile.click();
    };

    uploadEEPROMFile.onchange = () => {
      const file = uploadEEPROMFile.files[0];
      if (file) {
        const reader = new FileReader();
        reader.onload = (e) => {
          const contents = e.target.result;
          fetch('/uploadEEPROM', {
            method: 'POST',
            headers: {'Content-Type': 'application/octet-stream'},
            body: contents
          })
          .then(response => response.text())
          .then(data => {
            alert(data);
            loadButtons();
          });
        };
        reader.readAsArrayBuffer(file);
      }
    };


    let isConnected = false;
    let connectedServer = '';

 function updateClock() {
      const now = new Date();
      const days = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
      const months = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];
      
      const dayName = days[now.getDay()];
      const day = now.getDate();
      const month = months[now.getMonth()];
      const year = now.getFullYear();
      const hours = now.getHours().toString().padStart(2, '0');
      const minutes = now.getMinutes().toString().padStart(2, '0');

      const timeString = `${dayName}, ${day} ${month} ${year}   ${hours}:${minutes}`;
      document.getElementById('clock').textContent = timeString;
    }

function updatePhoneStatus() {
  fetch('/status')
    .then(response => response.json())
    .then(data => {
      const wifiStrength = Math.min(Math.max(0, Math.floor(data.wifi / -20)), 4); // Convert RSSI to 0-4 bars
      document.getElementById('wifiSignal').textContent = '📶'.repeat(wifiStrength + 1);
      document.getElementById('battery').textContent = `${data.battery.toFixed(2)}V`;
      document.getElementById('temperature').textContent = `${data.temperature.toFixed(1)}°C`;
      document.getElementById('heap').textContent = `${Math.floor(data.heap / 1024)}KB`;
    });
}

    function updateStatusIndicator() {
      const statusIndicator = document.getElementById('statusIndicator');
      const statusText = document.getElementById('statusText');
      if (isConnected) {
        statusIndicator.className = 'connected';
        statusText.textContent = `Connected to ${connectedServer}`;
      } else {
        statusIndicator.className = 'disconnected';
        statusText.textContent = connectedServer ? `Disconnected from ${connectedServer}` : 'Disconnected';
      }
    }

    updateClock();
    updatePhoneStatus();
    setInterval(() => {
      updateClock();
      updatePhoneStatus();
    }, 1000);

    function scrollToBottom() {
      terminal.scrollTop = terminal.scrollHeight;
    }

    function appendToTerminal(text) {
      terminal.innerHTML += text;
      scrollToBottom();
    }

    function sendCommand(command, isLongOutput = false) {
  appendToTerminal('> ' + command + '<br>');
  fetch('/terminal', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: 'command=' + encodeURIComponent(command)
  })
  .then(response => response.text())
  .then(data => {
    if (isLongOutput) {
      handleLongOutput(data);
    } else {
      appendToTerminal(data.split('\n').join('<br>'));
      if (data.includes('Connected to') || data.includes('Login successful')) {
        isConnected = true;
      } else if (data.includes('Connection closed') || data.includes('Login failed')) {
        isConnected = false;
      }
      updateStatusIndicator();
    }
  });
}

function handleLongOutput(initialData) {
  let fullResponse = initialData;
  appendToTerminal(initialData.split('\n').join('<br>'));

  function getMoreData() {
    if (fullResponse.includes('--More--')) {
      sendCommand(' ', true);
    } else {
      appendToTerminal('<br>Command completed.<br>');
      appendToTerminal('<br>Full Output:<br>' + fullResponse.split('\n').join('<br>'));
    }
  }

  setTimeout(getMoreData, 900);
}

    input.addEventListener('keypress', function(e) {
      if (e.key === 'Enter') {
        sendCommand(input.value);
        input.value = '';
      }
    });

    function loadButtons() {
      fetch('/getButtons')
        .then(response => response.json())
        .then(buttons => {
          buttonContainer.innerHTML = '';
          buttons.forEach((button, index) => {
            const btn = document.createElement('button');
            btn.textContent = button.name || 'Button ' + (index + 1);
            btn.className = 'preset-button';
            btn.onclick = () => sendCommand(button.command);
            btn.ondblclick = () => {
              const newName = prompt('Enter button name:', button.name);
              const newCommand = prompt('Enter button command:', button.command);
              if (newName !== null && newCommand !== null) {
                fetch('/setButton', {
                  method: 'POST',
                  headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                  body: 'index=' + index + '&name=' + encodeURIComponent(newName) + '&command=' + encodeURIComponent(newCommand)
                }).then(() => loadButtons());
              }
            };
            buttonContainer.appendChild(btn);
          });
        });
    }
    
    loadButtons();

    // Create port selector options
    for (let i = 1; i <= 16; i++) {
      const option = document.createElement('div');
      option.className = 'ios-option';
      option.textContent = 'Port ' + i;
      option.onclick = () => {
        selectedPort = i;
        document.querySelectorAll('.ios-option').forEach(opt => opt.classList.remove('selected'));
        option.classList.add('selected');
        if (currentMode === 'cekSinyal') {
          showOnuInput();
        } else if (currentMode === 'showPonPower') {
          sendShowPonPowerCommand();
        }
      };
      portSelector.appendChild(option);
    }

    cekSinyalBtn.onclick = () => {
      currentMode = 'cekSinyal';
      showSelector();
    };

    showPonPowerBtn.onclick = () => {
      currentMode = 'showPonPower';
      showSelector();
    };

    showUnconfigOnuBtn.onclick = () => {
      sendCommand('sho gpon onu un');
    };

    function showSelector() {
      selectorOverlay.style.display = 'block';
      portSelector.style.display = 'block';
      onuInput.style.display = 'none';
    }

    function showOnuInput() {
      portSelector.style.display = 'none';
      onuInput.style.display = 'block';
    }

    function closeSelector() {
      selectorOverlay.style.display = 'none';
      portSelector.style.display = 'none';
      onuInput.style.display = 'none';
      selectedPort = null;
      currentMode = null;
    }

    selectorOverlay.onclick = closeSelector;

    function sendCekSinyalCommand() {
      const onu = onuNumber.value;
      if (selectedPort && onu && onu >= 1 && onu <= 99) {
        const command = 'sho pon power attenuation gpon-onu_1/2/' + selectedPort + ':' + onu;
        sendCommand(command);
        closeSelector();
        onuNumber.value = '';
      } else {
        alert('Please select a valid port and enter a valid ONU number (1-99).');
      }
    }

function sendShowPonPowerCommand() {
  if (selectedPort) {
    const command = 'sho gpon onu stat gpon-olt_1/2/' + selectedPort;
    appendToTerminal('<br>Retrieving Port Status... Please wait.<br>');
    sendCommand(command, true);
    closeSelector();
  } else {
    alert('Please select a valid port.');
  }
}

    autoLoginBtn.onclick = () => {
      const serverAddress = serverAddressInput.value;
      const username = usernameInput.value;
      const password = passwordInput.value;
      
      fetch('/connect', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'command=connect ' + encodeURIComponent(serverAddress)
      })
      .then(response => response.text())
      .then(data => {
        appendToTerminal(data.split('\n').join('<br>'));
        if (data.includes('Connected to')) {
          connectedServer = serverAddress;
          setTimeout(() => {
            sendCommand(username);
            setTimeout(() => {
              // Send password without displaying it in the terminal
              fetch('/terminal', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'command=' + encodeURIComponent(password)
              })
              .then(response => response.text())
              .then(data => {
                // Display a placeholder instead of the actual password
                appendToTerminal('> ********<br>');
                appendToTerminal(data.split('\n').join('<br>'));
                if (data.includes('Login successful')) {
                  isConnected = true;
                } else {
                  isConnected = false;
                  connectedServer = '';
                }
                updateStatusIndicator();
              });
            }, 1000);
          }, 1000);
        } else {
          isConnected = false;
        }
        updateStatusIndicator();
      });
    };


      disconnectBtn.onclick = () => {
        sendCommand('exit');
        isConnected = false;
        updateStatusIndicator();
      };
      configTerminalBtn.onclick = () => {
        sendCommand('con ter');
      };

      exitBtn.onclick = () => {
        sendCommand('exit');
      };

    serialNumberInput.addEventListener('input', function() {
      if (this.value.length !== 12) {
        serialNumberError.textContent = 'Serial Number must be exactly 12 characters.';
        submitOnuConfig.disabled = true;
      } else {
        serialNumberError.textContent = '';
        submitOnuConfig.disabled = false;
      }
    });

 submitOnuConfig.onclick = () => {
  const serialNumber = document.getElementById('serialNumber').value;
  if (serialNumber.length !== 12) {
    alert('Modem Serial Number must be exactly 12 characters.');
    return;
  }

  const rackNumber = document.getElementById('rackNumber').value;
  const portNumber = document.getElementById('portNumber').value;
  const oonuNumber = document.getElementById('oonuNumber').value;
  const vlanNumber = document.getElementById('vlanNumber').value;
  const Ousername = document.getElementById('Ousername').value;
  const Opassword = document.getElementById('Opassword').value;
  const description = document.getElementById('description').value;

  const commands = onuConfigScript.map(command => {
    return command
      .replace(/\${rackNumber}/g, rackNumber)
      .replace(/\${portNumber}/g, portNumber)
      .replace(/\${oonuNumber}/g, oonuNumber)
      .replace(/\${serialNumber}/g, serialNumber)
      .replace(/\${vlanNumber}/g, vlanNumber)
      .replace(/\${Ousername}/g, Ousername)
      .replace(/\${Opassword}/g, Opassword)
      .replace(/\${description}/g, description);
  });

  function executeCommands(index = 0) {
    if (index >= commands.length) {
      appendToTerminal('ONU configuration completed.<br>');
      return;
    }

    sendCommand(commands[index]);
    setTimeout(() => executeCommands(index + 1), 1000);
  }

  executeCommands();
        };

    // Initial status update
    updateStatusIndicator();
  </script>
</body>
</html>
  )rawliteral";
  server.send(200, "text/html", html);
}

void handleTerminal() {
  if (server.hasArg("command")) {
    String command = server.arg("command");
    String response = executeTelnetCommand(command);
    server.send(200, "text/plain", response);
  } else {
    server.send(400, "text/plain", "No command provided");
  }
}

void handleConnect() {
  if (server.hasArg("command")) {
    String command = server.arg("command");
    if (command.startsWith("connect ")) {
      String serverAddress = command.substring(8);
      if (telnetClient.connect(serverAddress.c_str(), 23)) {
        server.send(200, "text/plain", "Connected to " + serverAddress + "\n");
      } else {
        server.send(200, "text/plain", "Failed to connect to " + serverAddress + "\n");
      }
    } else {
      server.send(400, "text/plain", "Invalid connect command");
    }
  } else {
    server.send(400, "text/plain", "No command provided");
  }
}

void handleSetButton() {
  if (server.hasArg("index") && server.hasArg("name") && server.hasArg("command")) {
    int index = server.arg("index").toInt();
    String name = server.arg("name");
    String command = server.arg("command");
    
    if (index >= 0 && index < NUM_BUTTONS) {
      strncpy(buttons[index].name, name.c_str(), MAX_BUTTON_NAME - 1);
      strncpy(buttons[index].command, command.c_str(), MAX_BUTTON_COMMAND - 1);
      buttons[index].name[MAX_BUTTON_NAME - 1] = '\0';
      buttons[index].command[MAX_BUTTON_COMMAND - 1] = '\0';
      saveButtonConfigs();
      server.send(200, "text/plain", "Button updated");
    } else {
      server.send(400, "text/plain", "Invalid button index");
    }
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void handleGetButtons() {
  String json = "[";
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (i > 0) json += ",";
    json += "{\"name\":\"" + String(buttons[i].name) + "\",\"command\":\"" + String(buttons[i].command) + "\"}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

String executeTelnetCommand(String command) {
  if (!telnetClient.connected()) {
    return "Not connected to server\n";
  }

  // Clear any existing data in the buffer
  while (telnetClient.available()) {
    telnetClient.read();
  }

  telnetClient.println(command);

  String response = "";
  unsigned long startTime = millis();
  bool commandCompleted = false;
  int noDataCount = 0;
  bool isLongOutput = command.startsWith("sho gpon onu stat");

  while (millis() - startTime < (isLongOutput ? 10000 : 5000) && !commandCompleted) {
    if (telnetClient.available()) {
      char c = telnetClient.read();
      response += c;
      if (response.endsWith("ZXAN(config)#") || 
          (!isLongOutput && (response.endsWith("Password:") || response.endsWith("login:")))) {
        commandCompleted = true;
      }
      noDataCount = 0;
    } else {
      delay(50);
      noDataCount++;
      if (noDataCount > (isLongOutput ? 20 : 10)) {
        commandCompleted = true;
      }
    }
  }

  // For long output commands, ensure we've captured any remaining data
  if (isLongOutput) {
    unsigned long endTime = millis();
    while (millis() - endTime < 500) {
      if (telnetClient.available()) {
        char c = telnetClient.read();
        response += c;
        endTime = millis();
      }
    }
  }

  return response;
}

void loadButtonConfigs() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    int addr = BUTTON_CONFIG_START + i * (MAX_BUTTON_NAME + MAX_BUTTON_COMMAND);
    EEPROM.get(addr, buttons[i]);
  }
}

void saveButtonConfigs() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    int addr = BUTTON_CONFIG_START + i * (MAX_BUTTON_NAME + MAX_BUTTON_COMMAND);
    EEPROM.put(addr, buttons[i]);
  }
  EEPROM.commit();
}

void loadOnuScript() {
  for (int i = 0; i < ONU_SCRIPT_SIZE; i++) {
    onuConfigScript[i] = EEPROM.read(ONU_SCRIPT_START + i);
  }
  onuConfigScript[ONU_SCRIPT_SIZE - 1] = '\0';
}

void saveOnuScript() {
  for (int i = 0; i < ONU_SCRIPT_SIZE; i++) {
    EEPROM.write(ONU_SCRIPT_START + i, onuConfigScript[i]);
  }
  EEPROM.commit();
}



void handleDownloadEEPROM() {
  String eepromData;
  for (int i = 0; i < EEPROM_SIZE; i++) {
    eepromData += char(EEPROM.read(i));
  }
  server.send(200, "application/octet-stream", eepromData);
}

void handleUploadEEPROM() {
  if (server.hasArg("plain") == false) {
    server.send(400, "text/plain", "No data received");
    return;
  }
  
  String data = server.arg("plain");
  if (data.length() > EEPROM_SIZE) {
    server.send(400, "text/plain", "Data size exceeds EEPROM size");
    return;
  }

  for (int i = 0; i < data.length(); i++) {
    EEPROM.write(i, data[i]);
  }
  EEPROM.commit();
  loadButtonConfigs();
  loadOnuScript();
  server.send(200, "text/plain", "EEPROM updated with uploaded data");
}
void handleSaveOnuScript() {
  if (server.hasArg("plain")) {
    String scriptJson = server.arg("plain");
    
    DynamicJsonDocument doc(JSON_CAPACITY);
    DeserializationError error = deserializeJson(doc, scriptJson);
    
    if (error) {
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }

    JsonArray scriptArray = doc["script"];
    String script = "";
    for (JsonVariant v : scriptArray) {
      script += v.as<String>() + "\n";
    }

    // Clear the existing script
    memset(onuConfigScript, 0, ONU_SCRIPT_SIZE);

    // Copy the new script
    strncpy(onuConfigScript, script.c_str(), ONU_SCRIPT_SIZE - 1);
    onuConfigScript[ONU_SCRIPT_SIZE - 1] = '\0';

    saveOnuScript();

    server.send(200, "text/plain", "ONU Configuration script saved successfully");
  } else {
    server.send(400, "text/plain", "No data received");
  }
}

void handleLoadOnuScript() {
  DynamicJsonDocument doc(JSON_CAPACITY);
  JsonArray scriptArray = doc.createNestedArray("script");

  char* line = strtok(onuConfigScript, "\n");
  while (line != NULL) {
    scriptArray.add(String(line));
    line = strtok(NULL, "\n");
  }

  String scriptJson;
  serializeJson(doc, scriptJson);
  server.send(200, "application/json", scriptJson);
}