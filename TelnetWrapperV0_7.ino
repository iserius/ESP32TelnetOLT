/*    ESP32 telnet wraper for OLTC320
      12 october 2024
      - v0.5  - add OTA function
      - v0.6  - added status bar header
      - v0.7  - add EEPROM save function
*/


#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>

const char* ssid = "RAHASIA";
const char* password = "";

WebServer server(80);
WiFiClient telnetClient;

#define EEPROM_SIZE 1024
#define MAX_BUTTON_NAME 20
#define MAX_BUTTON_COMMAND 50
#define NUM_BUTTONS 8

struct ButtonConfig {
  char name[MAX_BUTTON_NAME];
  char command[MAX_BUTTON_COMMAND];
};

ButtonConfig buttons[NUM_BUTTONS];

void setup() {
  Serial.begin(115200);
  
  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadButtonConfigs();

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
    server.on("/downloadEEPROM", HTTP_GET, handleDownloadEEPROM);
  server.on("/uploadEEPROM", HTTP_POST, handleUploadEEPROM);
  
  
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
      padding: 10px;
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
      margin-bottom: 10px;
      height: 20px;
    }
    #statusIndicator {
      width: 20px;
      height: 20px;
      border-radius: 50%;
      margin-right: 10px;
    }
    #statusText {
      font-size: 14px;
    }
    
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
    .preset-button, .eeprom-button {
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
  </style>
</head>
<body>
<div id="statusBar">
  <div id="statusIndicator" class="disconnected"></div>
  <div id="statusText">Disconnected</div>
</div>
  <div id="terminal"></div>
  <input type="text" id="input" placeholder="Enter command...">
  <div id="loginForm">
    <input type="text" id="serverAddress" placeholder="Server Address" value="10.10.10.3">
    <input type="text" id="username" placeholder="Username" value="admin">
    <input type="password" id="password" placeholder="Password" value="YourTelnetPassword">
    <button id="autoLoginBtn" class="preset-button">Login</button>
    <button id="disconnectBtn" class="preset-button">Disconnect</button>
    <button id="configTerminalBtn" class="preset-button">Config Terminal</button>
  <button id="exitBtn" class="preset-button">Exit</button>
  </div>
  <div class="button-container" id="buttonContainer"></div>
    <div class="eeprom-buttons">
    <button id="downloadEEPROMBtn" class="eeprom-button">Download EEPROM</button>
    <input type="file" id="uploadEEPROMFile" style="display:none;" />
    <button id="uploadEEPROMBtn" class="eeprom-button">Upload EEPROM</button>
  </div>

  <button id="showUnconfigOnuBtn" class="preset-button">Show Unconfig ONU</button>
  <button id="cekSinyalBtn" class="preset-button">Cek Sinyal</button>
  <button id="showPonPowerBtn" class="preset-button">Show pon Status</button>
 

  <div id="selectorOverlay"></div>
  <div id="portSelector"></div>
  <div id="onuInput">
    <label for="onuNumber">ONU number (1-99):</label>
    <input type="number" id="onuNumber" min="1" max="99">
    <button onclick="sendCekSinyalCommand()">Send</button>
  </div>

  <script>
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
    const onuNumber = document.getElementById('onuNumber');
    const statusIndicator = document.getElementById('statusIndicator');
    const serverAddressInput = document.getElementById('serverAddress');
    const usernameInput = document.getElementById('username');
    const passwordInput = document.getElementById('password');
        const downloadEEPROMBtn = document.getElementById('downloadEEPROMBtn');
    const uploadEEPROMFile = document.getElementById('uploadEEPROMFile');
    const uploadEEPROMBtn = document.getElementById('uploadEEPROMBtn');
    let selectedPort = null;
    let isConnected = false;
    let currentMode = null;
    
 function updateStatusIndicator() {
  const statusIndicator = document.getElementById('statusIndicator');
  const statusText = document.getElementById('statusText');
  if (isConnected) {
    statusIndicator.className = 'connected';
    statusText.textContent = 'Connected';
  } else {
    statusIndicator.className = 'disconnected';
    statusText.textContent = 'Disconnected';
  }
}


    downloadEEPROMBtn.onclick = () => {
      window.location.href = '/downloadEEPROM';
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



    function scrollToBottom() {
      terminal.scrollTop = terminal.scrollHeight;
    }

    function appendToTerminal(text) {
      terminal.innerHTML += text;
      scrollToBottom();
    }

    function sendCommand(command, hidePassword = false) {
      if (hidePassword) {
        appendToTerminal('> ' + command.replace(passwordInput.value, '********') + '<br>');
      } else {
        appendToTerminal('> ' + command + '<br>');
      }
      fetch('/terminal', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'command=' + encodeURIComponent(command)
      })
      .then(response => response.text())
      .then(data => {
        // Split the response into lines
        const lines = data.split('\n');
        let formattedResponse = '';
        
        // Process each line
        for (let line of lines) {
          // Replace spaces with non-breaking spaces to preserve formatting
          line = line.replace(/ /g, '&nbsp;');
          formattedResponse += line + '<br>';
        }
        
        // Append the formatted response to the terminal
        appendToTerminal(formattedResponse);
        
        if (data.includes('Connected to') || data.includes('Login successful')) {
          isConnected = true;
        } else if (data.includes('Connection closed') || data.includes('Login failed')) {
          isConnected = false;
        }
        updateStatusIndicator();
        
        // Check if the last line ends with "ZXAN(config)#"
        if (lines[lines.length - 1].trim().endsWith('ZXAN(config)#')) {
          appendToTerminal('<br>done.<br>');
        }
      });
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
        sendCommand(command);
        closeSelector();
      } else {
        alert('Please select a valid port.');
      }
    }

    autoLoginBtn.onclick = () => {
      const serverAddress = serverAddressInput.value;
      const username = usernameInput.value;
      const password = passwordInput.value;
      
      // First, connect to the server
      fetch('/connect', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'command=connect ' + encodeURIComponent(serverAddress)
      })
      .then(response => response.text())
      .then(data => {
        appendToTerminal(data.split('\n').join('<br>'));
        
        // If connection successful, send username and password
        if (data.includes('Connected to')) {
          setTimeout(() => {
            sendCommand(username, true);
            setTimeout(() => sendCommand(password, true), 1000);
          }, 1000);
          isConnected = true;
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

  telnetClient.println(command);

  String response = "";
  unsigned long startTime = millis();
  bool commandCompleted = false;

  while (millis() - startTime < 2200 && !commandCompleted) {  // Wait for up to 5 seconds
    while (telnetClient.available()) {
      char c = telnetClient.read();
      response += c;
      if (response.endsWith("#")) {
        commandCompleted = true;
        break;
      }
    }
    if (commandCompleted) break;
    delay(20);  // Small delay to prevent tight looping
  }

  return response;
}

void loadButtonConfigs() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    int addr = i * (MAX_BUTTON_NAME + MAX_BUTTON_COMMAND);
    EEPROM.get(addr, buttons[i]);
  }
}

void saveButtonConfigs() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    int addr = i * (MAX_BUTTON_NAME + MAX_BUTTON_COMMAND);
    EEPROM.put(addr, buttons[i]);
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
  server.send(200, "text/plain", "EEPROM updated with uploaded data");
}
