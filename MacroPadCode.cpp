/*
  XIAO ESP32-C6 BLE Macropad - Smart Web-Config Edition
  ---------------------------
  - 3x3 keyboard matrix -> Launches Programs OR Types Text based on setting
  - Wi-Fi Access Point -> Hosts a Web GUI to remap keys, toggle mode, & change OS
  - Settings saved permanently to ESP32 Flash (Preferences)
  - Rotary encoder -> Volume Up / Volume Down (encoder push = Mute)
  - SSD1306 128x64 OLED (I2C) -> shows big text UI & Screensaver
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NimBleKeyboard.h> 
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// ================== OS CONFIGURATION ==================
enum OperatingSystem { OS_WINDOWS = 0, OS_MACOS = 1, OS_LINUX = 2 };
OperatingSystem targetOS = OS_WINDOWS; // Dynamic variable (loaded from flash)
// ======================================================

// ---------------- OLED CONFIG ----------------
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define OLED_SDA       D6
#define OLED_SCL       D7
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------------- BLE CONFIG ----------------
BleKeyboard bleKeyboard("XIAO Launcher", "DIY", 100);

// ---------------- WIFI & WEB SERVER ----------------
const char* AP_SSID = "XIAO_Macropad";
const char* AP_PASS = "12345678";
WebServer server(80);
Preferences prefs;

// Dynamic Bindings Arrays
String btnCmds[9];
bool btnIsText[9]; // True = Type Text, False = Launch App

// Default configuration (used on first boot)
const char* defCmds[9]  = {"notepad", "calc", "msedge", "mspaint", "cmd", "explorer", "taskmgr", "spotify", "LOCK"};

// ---------------- ROTARY ENCODER CONFIG ----------------
#define ENC_CLK  D8   
#define ENC_DT   D9   
#define ENC_SW   D10  

int lastClkState;
unsigned long lastEncTurnMs = 0;
unsigned long lastEncBtnMs  = 0;
const unsigned long ENC_DEBOUNCE_MS = 5;
const unsigned long BTN_DEBOUNCE_MS = 250;

// ---------------- MATRIX CONFIG ----------------
const int ROW_PINS[3] = {D3, D4, D5};   
const int COL_PINS[3] = {D0, D1, D2};   

const int BUTTON_ID[3][3] = {
  {1, 4, 7},
  {2, 5, 8},
  {3, 6, 9}
};

bool keyState[3][3]      = {false};
bool lastKeyState[3][3]  = {false};
unsigned long lastKeyDebounce[3][3] = {0};
const unsigned long KEY_DEBOUNCE_MS = 25;

// ---------------- STATUS / DISPLAY STATE ----------------
String statusLine  = "Booting...";
String actionLine  = "";
unsigned long actionShownAt = 0;
const unsigned long ACTION_DISPLAY_MS = 1000; 

// ---------------- SCREENSAVER CONFIG ----------------
#define GOL_WIDTH      64
#define GOL_HEIGHT     32
#define IDLE_TIMEOUT   15000 
#define GOL_FRAME_TIME 75    

unsigned long lastActivityMs = 0;
bool isScreenSaverMode = false;
uint8_t golGrid[GOL_WIDTH][GOL_HEIGHT];
uint8_t golNext[GOL_WIDTH][GOL_HEIGHT];
int golGenerations = 0;

// ---------------- HTML/JS WEB GUI ----------------
const char* index_html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Macropad Setup</title>
<style>
  :root { --bg: #0f172a; --panel: #1e293b; --text: #f8fafc; --accent: #3b82f6; --accent-hover: #2563eb; }
  body { background: var(--bg); color: var(--text); font-family: system-ui, sans-serif; padding: 20px; margin: 0; display: flex; justify-content: center; }
  .container { max-width: 500px; width: 100%; }
  h1 { text-align: center; margin-bottom: 24px; font-weight: 600; }
  .row { background: var(--panel); padding: 16px; border-radius: 12px; margin-bottom: 12px; display: grid; gap: 10px; }
  .row-header { font-weight: 600; font-size: 1.1rem; color: #94a3b8; }
  .input-group { display: flex; gap: 8px; }
  input, select { box-sizing: border-box; background: var(--bg); border: 1px solid #334155; color: var(--text); padding: 10px; border-radius: 8px; font-size: 1rem; }
  input { flex: 2; width: 100%; }
  select { flex: 1; min-width: 120px; cursor: pointer; }
  input:focus, select:focus { outline: none; border-color: var(--accent); }
  button { width: 100%; background: var(--accent); color: #fff; padding: 14px; border: none; border-radius: 8px; font-size: 1.1rem; font-weight: 600; cursor: pointer; margin-top: 10px; transition: 0.2s; }
  button:hover { background: var(--accent-hover); }
  #status { text-align: center; margin-top: 16px; min-height: 24px; color: #10b981; font-weight:bold; }
  .note { text-align: center; color: #64748b; font-size: 0.9rem; margin-top: 20px; }
</style>
</head>
<body>
<div class="container">
  <h1>Macropad Config</h1>
  
  <div class="row">
    <div class="row-header">Target Operating System</div>
    <select id="osSelect" style="width: 100%;">
      <option value="0">Windows</option>
      <option value="1">macOS</option>
      <option value="2">Linux</option>
    </select>
  </div>

  <div id="forms">Loading binds from pad...</div>
  <button onclick="save()">Save Settings</button>
  <div id="status"></div>
  <div class="note">Note: Type "LOCK" as a Launch App command to lock the OS screen.</div>
</div>
<script>
  fetch('/api/get').then(r=>r.json()).then(data => {
    document.getElementById('osSelect').value = data.os;
    let html = '';
    data.binds.forEach((b, i) => {
      let selApp = b.isText ? '' : 'selected';
      let selTxt = b.isText ? 'selected' : '';
      html += `<div class="row">
        <div class="row-header">Button ${i+1}</div>
        <div class="input-group">
          <input type="text" id="c${i}" placeholder="Command or Text" value="${b.cmd}">
          <select id="t${i}">
            <option value="0" ${selApp}>Launch App</option>
            <option value="1" ${selTxt}>Type Text</option>
          </select>
        </div>
      </div>`;
    });
    document.getElementById('forms').innerHTML = html;
  });

  function save() {
    let fd = new URLSearchParams();
    fd.append('os', document.getElementById('osSelect').value);
    for(let i=0; i<9; i++) {
      fd.append('c'+i, document.getElementById('c'+i).value);
      fd.append('t'+i, document.getElementById('t'+i).value);
    }
    document.getElementById('status').innerText = "Saving to hardware...";
    fetch('/api/save', {method: 'POST', body: fd})
      .then(()=> document.getElementById('status').innerText = "Saved successfully!");
  }
</script>
</body>
</html>
)rawliteral";

// ---------------- FUNCTION DECLARATIONS ----------------
void loadConfig();
void setupWebServer();
void updateDisplay();
void showAction(const String &text);
void scanMatrix();
void handleEncoder();
void executeMacro(int btnId);
void launchApp(const char* appCommand);
void lockOS();
void resetActivity();
void initGameOfLife();
void updateGameOfLife();

void setup() {
  Serial.begin(115200);
  
  // Display Init
  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setRotation(0);
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  
  // Show boot screen with Wi-Fi info
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("Wi-Fi AP:");
  display.setTextSize(1);
  display.setCursor(0, 24);
  display.println(AP_SSID);
  display.println("Pass: 12345678");
  display.println("IP: 192.168.4.1");
  display.display();

  // Load configs from flash memory
  loadConfig();

  // Hardware init
  for (int r = 0; r < 3; r++) { pinMode(ROW_PINS[r], OUTPUT); digitalWrite(ROW_PINS[r], HIGH); }
  for (int c = 0; c < 3; c++) { pinMode(COL_PINS[c], INPUT_PULLUP); }
  pinMode(ENC_CLK, INPUT_PULLUP); pinMode(ENC_DT, INPUT_PULLUP); pinMode(ENC_SW, INPUT_PULLUP);
  lastClkState = digitalRead(ENC_CLK);

  // Networking Init
  WiFi.softAP(AP_SSID, AP_PASS);
  setupWebServer();
  bleKeyboard.begin();

  // Serial Monitor Info
  Serial.println("\n==================================");
  Serial.println("   XIAO Macropad - Web Config");
  Serial.println("==================================");
  Serial.println("Wi-Fi Access Point Active!");
  Serial.print("SSID: "); Serial.println(AP_SSID);
  Serial.print("PASS: "); Serial.println(AP_PASS);
  Serial.print("Web GUI IP: "); Serial.println(WiFi.softAPIP());
  Serial.println("----------------------------------");
  Serial.println("BLE Keyboard Started.");
  Serial.println("Waiting for Bluetooth pairing...");
  Serial.println("==================================\n");

  delay(3000); 
  statusLine = "Waiting\nfor BLE...";
  updateDisplay();
  lastActivityMs = millis();
}

void loop() {
  server.handleClient(); 

  bool connected = bleKeyboard.isConnected();
  static bool prevConnected = false;
  
  if (connected != prevConnected) {
    statusLine = connected ? "Connected\n& Ready" : "Waiting\nfor BLE...";
    prevConnected = connected;
    resetActivity();
    updateDisplay();
  }

  if (connected) {
    scanMatrix();
    handleEncoder();
  }

  // Screensaver Logic
  if (millis() - lastActivityMs > IDLE_TIMEOUT) {
    if (!isScreenSaverMode) {
      isScreenSaverMode = true;
      initGameOfLife();
    }
    static unsigned long lastFrame = 0;
    if (millis() - lastFrame > GOL_FRAME_TIME) {
      lastFrame = millis();
      updateGameOfLife();
    }
  } else {
    if (actionLine != "" && millis() - actionShownAt > ACTION_DISPLAY_MS) {
      actionLine = "";
      updateDisplay();
    }
  }
  delay(2); 
}

// ================= PREFERENCES & WEB SERVER =================
void loadConfig() {
  prefs.begin("macropad", false);
  
  targetOS = (OperatingSystem)prefs.getInt("os", OS_WINDOWS);

  for (int i = 0; i < 9; i++) {
    btnCmds[i]   = prefs.getString(("c" + String(i)).c_str(), defCmds[i]);
    btnIsText[i] = prefs.getBool(("t" + String(i)).c_str(), false); // Default false (Launch App)
  }
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", index_html);
  });
  
  server.on("/api/get", HTTP_GET, []() {
    String json = "{\"os\":" + String((int)targetOS) + ",\"binds\":[";
    for (int i = 0; i < 9; i++) {
      json += "{\"cmd\":\"" + btnCmds[i] + "\", \"isText\":" + (btnIsText[i] ? "true" : "false") + "}";
      if (i < 8) json += ",";
    }
    json += "]}";
    server.send(200, "application/json", json);
  });

  server.on("/api/save", HTTP_POST, []() {
    if (server.hasArg("os")) {
      int osVal = server.arg("os").toInt();
      targetOS = (OperatingSystem)osVal;
      prefs.putInt("os", osVal);
    }

    for (int i = 0; i < 9; i++) {
      if (server.hasArg("c" + String(i))) {
        String c = server.arg("c" + String(i));
        btnCmds[i] = c;
        prefs.putString(("c" + String(i)).c_str(), c);
      }
      if (server.hasArg("t" + String(i))) {
        bool isTxt = (server.arg("t" + String(i)) == "1");
        btnIsText[i] = isTxt;
        prefs.putBool(("t" + String(i)).c_str(), isTxt);
      }
    }
    server.send(200, "text/plain", "OK");
    
    resetActivity();
    showAction("Settings\nSaved!");
  });

  server.begin();
}

// ================= ACTIVITY TRACKER =================
void resetActivity() {
  lastActivityMs = millis();
  if (isScreenSaverMode) {
    isScreenSaverMode = false;
    updateDisplay(); 
  }
}

// ================= MACRO EXECUTION =================
void launchApp(const char* appCommand) {
  showAction(appCommand);
  
  if (targetOS == OS_WINDOWS) {
    bleKeyboard.press(KEY_LEFT_GUI); bleKeyboard.press('r');
  } else if (targetOS == OS_MACOS) {
    bleKeyboard.press(KEY_LEFT_GUI); bleKeyboard.press(' ');
  } else if (targetOS == OS_LINUX) {
    bleKeyboard.press(KEY_LEFT_ALT); bleKeyboard.press(KEY_F2);
  }

  delay(100);
  bleKeyboard.releaseAll();
  delay(500); 
  bleKeyboard.print(appCommand);
  delay(300); 
  bleKeyboard.write(KEY_RETURN);
}

void lockOS() {
  showAction("Lock Screen");
  if (targetOS == OS_WINDOWS || targetOS == OS_LINUX) {
    bleKeyboard.press(KEY_LEFT_GUI); bleKeyboard.press('l');
  } else if (targetOS == OS_MACOS) {
    bleKeyboard.press(KEY_LEFT_CTRL); bleKeyboard.press(KEY_LEFT_GUI); bleKeyboard.press('q');
  }
  delay(100);
  bleKeyboard.releaseAll();
}

void executeMacro(int btnId) {
  int index = btnId - 1; // Array is 0-8, IDs are 1-9
  
  if (btnIsText[index]) {
    // Behavior 1: Type the string as text
    showAction(btnCmds[index]);
    bleKeyboard.print(btnCmds[index]);
  } else {
    // Behavior 2: Launch application via OS shortcut
    if (btnCmds[index] == "LOCK") {
      lockOS();
    } else {
      launchApp(btnCmds[index].c_str());
    }
  }
}

// ================= MATRIX SCANNING =================
void scanMatrix() {
  for (int r = 0; r < 3; r++) {
    digitalWrite(ROW_PINS[r], LOW);
    delayMicroseconds(20); 

    for (int c = 0; c < 3; c++) {
      bool pressed = (digitalRead(COL_PINS[c]) == LOW);
      if (pressed != lastKeyState[r][c]) {
        lastKeyDebounce[r][c] = millis();
        lastKeyState[r][c] = pressed;
      }
      if (millis() - lastKeyDebounce[r][c] > KEY_DEBOUNCE_MS) {
        if (pressed != keyState[r][c]) {
          keyState[r][c] = pressed;
          if (pressed) {
            resetActivity(); 
            executeMacro(BUTTON_ID[r][c]);
          }
        }
      }
    }
    digitalWrite(ROW_PINS[r], HIGH); 
  }
}

// ================= ROTARY ENCODER =================
void handleEncoder() {
  int clkState = digitalRead(ENC_CLK);
  if (clkState != lastClkState && millis() - lastEncTurnMs > ENC_DEBOUNCE_MS) {
    lastEncTurnMs = millis();
    if (clkState == LOW) { 
      resetActivity(); 
      if (digitalRead(ENC_DT) != clkState) {
        bleKeyboard.write(KEY_MEDIA_VOLUME_UP); showAction("Volume +");
      } else {
        bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN); showAction("Volume -");
      }
    }
  }
  lastClkState = clkState;

  if (digitalRead(ENC_SW) == LOW && millis() - lastEncBtnMs > BTN_DEBOUNCE_MS) {
    lastEncBtnMs = millis();
    resetActivity(); 
    bleKeyboard.write(KEY_MEDIA_MUTE);
    showAction("Mute");
  }
}

// ================= GAME OF LIFE ANIMATION =================
void initGameOfLife() {
  for (int x = 0; x < GOL_WIDTH; x++) {
    for (int y = 0; y < GOL_HEIGHT; y++) { golGrid[x][y] = random(2); }
  }
  golGenerations = 0;
}

void updateGameOfLife() {
  display.clearDisplay();
  bool changed = false;

  for (int x = 0; x < GOL_WIDTH; x++) {
    for (int y = 0; y < GOL_HEIGHT; y++) {
      int neighbors = 0;
      for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
          if (i == 0 && j == 0) continue;
          neighbors += golGrid[(x + i + GOL_WIDTH) % GOL_WIDTH][(y + j + GOL_HEIGHT) % GOL_HEIGHT];
        }
      }
      
      golNext[x][y] = (golGrid[x][y] == 1) ? ((neighbors == 2 || neighbors == 3) ? 1 : 0) : ((neighbors == 3) ? 1 : 0);
      if (golGrid[x][y] != golNext[x][y]) changed = true;
      if (golNext[x][y] == 1) display.fillRect(x * 2, y * 2, 2, 2, SSD1306_WHITE);
    }
  }
  
  for (int x = 0; x < GOL_WIDTH; x++) {
    for (int y = 0; y < GOL_HEIGHT; y++) { golGrid[x][y] = golNext[x][y]; }
  }
  
  display.display();
  golGenerations++;

  if (!changed || golGenerations > 250) initGameOfLife();
}

// ================= DISPLAY HELPERS =================
void showAction(const String &text) {
  actionLine = text;
  actionShownAt = millis();
  updateDisplay();
}

void updateDisplay() {
  if (isScreenSaverMode) return; 

  display.clearDisplay();
  display.setTextSize(2); 
  display.setCursor(0, 16); 

  if (actionLine != "") {
    display.println(actionLine);
  } else {
    display.println(statusLine);
  }

  display.display();
}