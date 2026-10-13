/*
  QD020 Spider - corrected ESP8266 sketch + HC-SR04 ultrasonic sensor

  ULTRASONIC SENSOR (added):
    Plugged into the P7 header on the AceBott Spider Shield:
      Sensor VCC  -> P7 5V
      Sensor GND  -> P7 GND
      Sensor Trig -> P7 TX  (GPIO1)
      Sensor Echo -> 1k resistor -> P7 RX (GPIO3), and 2k resistor from RX to GND
                     (voltage divider: Echo is 5V, the ESP8266 pin is 3.3V)
    Because TX/RX are now used by the sensor, the Serial Monitor / serial
    command control has been REMOVED from this sketch. Use the browser page.
    - While moving forward, the spider stops if an obstacle is closer than
      STOP_DISTANCE_CM, and new "forward" commands are refused until the path
      is clear. Backward, turning, strafing and poses still work.
    - The browser page shows the live distance and the home-WiFi IP address.
    - Unplug the sensor (at least the Echo wire) before uploading code.
    - Keep the USB cable unplugged while using the sensor (run on battery):
      the USB chip also drives the RX line and will mask the Echo signal.

  Uses manufacturer-documented pin roles and standing angles. Actual wiring
  and mechanical assembly still require verification; gait/poses are unverified.
  All motion is cooperative: no animation delays or queued movement steps.
  Web/browser movement commands run once-clicked for WEB_BURST_MS then stop
  on their own (no renewal needed); TCP movement still needs renewed
  commands and stops after 700 ms silence (protocol unverified, unchanged).

  WiFi: QuadBot-E / 12345678   Browser: http://192.168.4.1/
  Also joins your home WiFi (dual AP+STA) - see STA_SSID/STA_PASS below.
  Once joined, its home-WiFi address is shown on the browser page at
  http://192.168.4.1/ (or check your router's client list).

  HTTP control API (local network only): GET or POST to
    http://<device-ip>/cmd?a=<command>&key=<API_KEY>
  or POST with header "X-API-Key: <API_KEY>" instead of the key= query arg.
  <command> is any of namedCommand()'s strings (forward, stop, dance1, ...).
  A request with a missing/wrong key gets HTTP 401. A "forward" command while
  an obstacle is detected gets HTTP 409.
  GET /status (same key) returns JSON: distance in cm (-1 = nothing in range),
  blocked flag, and home-WiFi IP.
  This is a shared static key over plain HTTP, not real auth - it stops
  casual/script access from other devices on the LAN, not a determined
  attacker sniffing traffic or reading the control page's source.

  Remote control over the internet: the ESP8266 also, once online, connects
  to Adafruit IO over MQTT and subscribes to AIO_COMMAND_FEED. Publish any
  of namedCommand()'s command strings to that feed. Movement commands from
  MQTT run for MQTT_BURST_MS then stop on their own. Fill in the AIO_*
  placeholders before flashing if you want this; leaving them as placeholders
  just means MQTT connect attempts fail harmlessly.

  TCP port 100 retains the ORIGINAL sketch's UNVERIFIED protocol assumption:
  FF 55 0B [six bytes] action device pad value pad (14 bytes total).
  Device must be 0x0C. Confirm this layout from your actual app before
  relying on it. Set TCP_HANDSOME_ACTION only when its official value is
  known; 0 disables that optional TCP mapping.

  Movement directions, especially strafing, require physical calibration.
  Per-joint limits below are electrical angle bounds, NOT certified safe
  mechanical limits. Set them for your assembly before ground testing.
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>
#include <Servo.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

const char *AP_SSID = "QuadBot-E";
const char *AP_PASS = "12345678";
const uint8_t TCP_DEVICE = 0x0C;
const uint8_t TCP_HANDSOME_ACTION = 0; // Unknown official code: do not guess.
const uint32_t MOVEMENT_TIMEOUT_MS = 700;
const uint32_t WEB_BURST_MS = 10000;
const uint32_t SERIAL_BURST_MS = 2500;
const uint32_t PACKET_TIMEOUT_MS = 300;
const uint32_t TCP_IDLE_MS = 10000;
const int HIP_SWING = 30;
const int KNEE_LIFT = 30;
const uint16_t LIFT_MS = 100, SWING_MS = 140, LOWER_MS = 100;

// ---------------- Ultrasonic sensor settings ----------------
const uint8_t TRIG_PIN = 1;               // P7 "TX" pin = GPIO1
const uint8_t ECHO_PIN = 3;               // P7 "RX" pin = GPIO3 (via divider)
const int STOP_DISTANCE_CM = 15;          // stop forward walking below this
const uint32_t PING_INTERVAL_MS = 60;     // time between measurements
const uint32_t ECHO_TIMEOUT_US = 25000;   // ~4 m; longer = "nothing in range"

// Home WiFi - the ESP8266 joins this alongside its own QuadBot-E access
// point (dual AP+STA, see setup()).
const char *STA_SSID = "YOUR_WIFI_NAME";
const char *STA_PASS = "YOUR_WIFI_PASSWORD";
const uint32_t STA_RETRY_MS = 15000;

// Shared secret required on the HTTP control API (see header comment above).
const char *API_KEY = "CHANGE_ME";

// Free Adafruit IO account: create a feed named "quadbot-command" and put
// its owner's username/key below.
const char *AIO_SERVER = "io.adafruit.com";
const uint16_t AIO_SERVERPORT = 1883;
const char *AIO_USERNAME = "YOUR_ADAFRUIT_IO_USERNAME";
const char *AIO_KEY = "YOUR_ADAFRUIT_IO_KEY";
const char *AIO_COMMAND_FEED = "YOUR_ADAFRUIT_IO_USERNAME/feeds/quadbot-command";
const uint32_t MQTT_RECONNECT_MS = 5000;
const uint32_t MQTT_BURST_MS = 3000;

enum { FL, FR, BL, BR };
enum { SRC_NONE, SRC_WEB, SRC_TCP, SRC_SERIAL, SRC_MQTT };
enum { STANDBY, FORWARD, BACKWARD, STRAFE_L, STRAFE_R, TURN_L, TURN_R,
       SLEEP, LIE, HELLO, PUSHUP, FIGHT, HANDSOME, DANCE1, DANCE2, DANCE3 };

// hip pin, knee pin, neutral angles, signs, hip min/max, knee min/max
struct Calibration {
  uint8_t hipPin, kneePin;
  int hipNeutral, kneeNeutral, hipSign, kneeSign;
  int hipMin, hipMax, kneeMin, kneeMax;
};
const Calibration CAL[4] = {
  /* FL */ {5,  16, 90, 110, -1, -1, 0, 180, 0, 180},
  /* FR */ {12, 14, 90,  70, +1, +1, 0, 180, 0, 180},
  /* BL */ {4,   2, 90,  70, -1, +1, 0, 180, 0, 180},
  /* BR */ {13, 15, 90, 110, +1, -1, 0, 180, 0, 180}
};

class Spider {
  struct Frame { int hip[4], knee[4]; uint16_t duration; };
  Servo hips[4], knees[4];
  int hipOffset[4] = {0, 0, 0, 0};
  int kneeOffset[4] = {0, 0, 0, 0};
  Frame frames[16];
  uint8_t frameCount = 0, frameIndex = 0;
  int move = STANDBY, owner = SRC_NONE;
  uint8_t gaitPhase = 0;
  bool pairA = true;
  uint32_t nextAt = 0, lastMoveAt = 0, moveTimeout = MOVEMENT_TIMEOUT_MS;

  static int limit(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
  }
  void leg(int i, int h, int k) {
    const Calibration &c = CAL[i];
    hipOffset[i] = h;
    kneeOffset[i] = k;
    hips[i].write(limit(c.hipNeutral + c.hipSign * h, c.hipMin, c.hipMax));
    knees[i].write(limit(c.kneeNeutral + c.kneeSign * k, c.kneeMin, c.kneeMax));
  }
  void neutral() { for (int i = 0; i < 4; ++i) leg(i, 0, 0); }
  void add(uint16_t ms, int h0, int k0, int h1, int k1,
           int h2, int k2, int h3, int k3) {
    if (frameCount >= 16) return;
    Frame &f = frames[frameCount++];
    f.hip[0] = h0; f.knee[0] = k0; f.hip[1] = h1; f.knee[1] = k1;
    f.hip[2] = h2; f.knee[2] = k2; f.hip[3] = h3; f.knee[3] = k3;
    f.duration = ms;
  }
  void uniform(uint16_t ms, int h, int k) { add(ms,h,k,h,k,h,k,h,k); }
  void showFrame(uint32_t now) {
    const Frame &f = frames[frameIndex];
    for (int i = 0; i < 4; ++i) leg(i, f.hip[i], f.knee[i]);
    nextAt = now + f.duration;
  }
  int travel(int i) const {
    switch (move) {
      case FORWARD: return +1;
      case BACKWARD: return -1;
      case TURN_R: return (i == FL || i == BL) ? +1 : -1;
      case TURN_L: return (i == FL || i == BL) ? -1 : +1;
      case STRAFE_R: return (i == FL || i == FR) ? +1 : -1;
      case STRAFE_L: return (i == FL || i == FR) ? -1 : +1;
      default: return 0;
    }
  }
  void advanceGait(uint32_t now) {
    for (int i = 0; i < 4; ++i) {
      bool lifting = ((i == FL || i == BR) == pairA);
      if (gaitPhase == 0) {
        leg(i, hipOffset[i], lifting ? KNEE_LIFT : 0);
      } else if (gaitPhase == 1) {
        leg(i, (lifting ? +1 : -1) * travel(i) * HIP_SWING,
            lifting ? KNEE_LIFT : 0);
      } else {
        leg(i, hipOffset[i], 0);
      }
    }
    nextAt = now + (gaitPhase == 0 ? LIFT_MS : gaitPhase == 1 ? SWING_MS : LOWER_MS);
    if (++gaitPhase == 3) { gaitPhase = 0; pairA = !pairA; }
  }

public:
  void begin() {
    for (int i = 0; i < 4; ++i) {
      hips[i].attach(CAL[i].hipPin);
      hips[i].write(CAL[i].hipNeutral);
      knees[i].attach(CAL[i].kneePin);
      knees[i].write(CAL[i].kneeNeutral);
    }
    stop();
  }
  void stop() {
    move = STANDBY; owner = SRC_NONE; frameCount = frameIndex = 0;
    gaitPhase = 0; pairA = true; neutral();
  }
  void disconnected(int source) { if (owner == source) stop(); }
  bool busy() const { return move != STANDBY; }
  int currentMove() const { return move; }
  bool command(int c, int source) {
    if (c < STANDBY || c > DANCE3) return false;
    const uint32_t now = millis();
    if (c >= FORWARD && c <= TURN_R) {
      lastMoveAt = now;
      moveTimeout = source == SRC_SERIAL ? SERIAL_BURST_MS
                  : source == SRC_MQTT ? MQTT_BURST_MS
                  : source == SRC_WEB ? WEB_BURST_MS : MOVEMENT_TIMEOUT_MS;
      if (move != c || owner != source) {
        stop(); move = c; owner = source; nextAt = now;
      }
      return true;
    }
    stop();
    if (c == STANDBY) return true;
    owner = source;
    switch (c) {
      case LIE: uniform(0,0,55); break;
      case SLEEP: uniform(250,0,55); uniform(0,10,60); break;
      case HELLO:
        uniform(200,0,0);
        for (int r=0; r<3; ++r) {
          add(220,0,0,30,45,0,0,0,0); add(220,0,0,-10,45,0,0,0,0);
        }
        uniform(0,0,0); break;
      case PUSHUP:
        for (int r=0; r<4; ++r) { uniform(280,0,35); uniform(280,0,0); }
        break;
      case FIGHT: add(0,15,30,15,30,-5,10,-5,10); break;
      case HANDSOME: add(0,-10,20,10,40,5,5,5,5); break;
      case DANCE1:
        for (int r=0; r<3; ++r) {
          add(250,20,15,-20,0,20,15,-20,0);
          add(250,-20,0,20,15,-20,0,20,15);
        }
        uniform(0,0,0); break;
      case DANCE2:
        for (int r=0; r<3; ++r) {
          add(220,25,20,-25,20,25,20,-25,20);
          add(220,-25,20,25,20,-25,20,25,20);
        }
        uniform(0,0,0); break;
      case DANCE3:
        for (int r=0; r<4; ++r) { uniform(160,0,40); uniform(160,0,-10); }
        uniform(0,0,0); break;
    }
    if (frameCount) showFrame(now);
    return true;
  }
  void tick() {
    const uint32_t now = millis();
    if (move != STANDBY && uint32_t(now-lastMoveAt) >= moveTimeout) { stop(); return; }
    if (int32_t(now-nextAt) < 0) return;
    if (move != STANDBY) { advanceGait(now); return; }
    if (frameCount && ++frameIndex < frameCount) showFrame(now);
    else { frameCount = 0; frameIndex = 0; }
  }
};

// ---------------- Ultrasonic sensor (non-blocking) ----------------
// The Echo pulse is timed with an interrupt, so measuring distance never
// pauses walking, WiFi or the web server (no pulseIn()).
volatile uint32_t echoStart = 0, echoWidth = 0;
volatile bool echoDone = false;

void IRAM_ATTR echoISR() {
  uint32_t t = micros();
  if (GPIP(ECHO_PIN)) {
    echoStart = t;
  } else {
    echoWidth = t - echoStart;
    echoDone = true;
  }
}

class Ultrasonic {
  uint32_t lastPing = 0;
  bool waiting = false;
  uint8_t closeCount = 0;
  int distance = -1;   // cm; -1 means nothing in range / no reading
public:
  void begin() {
    pinMode(TRIG_PIN, OUTPUT);
    digitalWrite(TRIG_PIN, LOW);
    pinMode(ECHO_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(ECHO_PIN), echoISR, CHANGE);
  }
  void service() {
    const uint32_t now = millis();
    if (echoDone) {
      noInterrupts();
      uint32_t width = echoWidth;
      echoDone = false;
      interrupts();
      waiting = false;
      distance = (width > 0 && width < ECHO_TIMEOUT_US) ? int(width / 58) : -1;
      // Require 2 close readings in a row so one noisy echo doesn't stop it.
      if (distance >= 0 && distance < STOP_DISTANCE_CM) {
        if (closeCount < 3) ++closeCount;
      } else {
        closeCount = 0;
      }
    }
    if (uint32_t(now - lastPing) >= PING_INTERVAL_MS) {
      if (waiting) { distance = -1; closeCount = 0; } // previous ping got no echo
      lastPing = now;
      waiting = true;
      digitalWrite(TRIG_PIN, HIGH);
      delayMicroseconds(10);
      digitalWrite(TRIG_PIN, LOW);
    }
  }
  int cm() const { return distance; }
  bool blocked() const { return closeCount >= 2; }
};

Spider spider;
Ultrasonic sonar;
WiFiServer tcpServer(100);
ESP8266WebServer webServer(80);
WiFiClient tcpClient;

WiFiClient mqttClient;
Adafruit_MQTT_Client mqtt(&mqttClient, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);
Adafruit_MQTT_Subscribe commandFeed(&mqtt, AIO_COMMAND_FEED);

int namedCommand(const char *name) {
  const char *names[] = {"standby","forward","backward","strafe_left","strafe_right",
    "turn_left","turn_right","sleep","lie","hello","pushup","fight","handsome",
    "dance1","dance2","dance3"};
  for (int i=0; i<16; ++i) if (!strcmp(name,names[i])) return i;
  if (!strcmp(name,"stop")) return STANDBY;
  if (!strcmp(name,"back")) return BACKWARD;
  if (!strcmp(name,"left")) return STRAFE_L;
  if (!strcmp(name,"right")) return STRAFE_R;
  if (!strcmp(name,"cw")) return TURN_R;
  if (!strcmp(name,"ccw")) return TURN_L;
  return -1;
}

class PacketParser {
  uint8_t data[14], used = 0;
  uint32_t lastByte = 0;
  void resync(uint8_t b) { used = b == 0xFF ? 1 : 0; if (used) data[0] = b; }
public:
  void reset() { used = 0; lastByte = 0; }
  void expire(uint32_t now) {
    if (used && uint32_t(now-lastByte) >= PACKET_TIMEOUT_MS) reset();
  }
  void feed(uint8_t b) {
    expire(millis()); lastByte = millis();
    if (used == 0) { resync(b); return; }
    if (used == 1 && b != 0x55) { resync(b); return; }
    if (used == 2 && b != 0x0B) { resync(b); return; }
    data[used++] = b;
    if (used != sizeof(data)) return;
    used = 0;
    if (data[10] != TCP_DEVICE) return;
    int c = -1;
    switch (data[9]) {
      case 1:
        if (data[12] <= 6) c = data[12];
        break;
      case 3: c = STANDBY; break;
      case 5: c = SLEEP; break;
      case 6: c = LIE; break;
      case 7: c = HELLO; break;
      case 8: c = PUSHUP; break;
      case 9: c = FIGHT; break;
      case 10: c = DANCE1; break;
      case 11: c = DANCE2; break;
      case 12: c = DANCE3; break;
      default:
        if (TCP_HANDSOME_ACTION != 0 && data[9] == TCP_HANDSOME_ACTION) c = HANDSOME;
        break;
    }
    if (c >= 0) spider.command(c,SRC_TCP);
  }
} parser;

void serviceTcp() {
  static bool hadClient = false;
  static uint32_t lastTraffic = 0;
  uint32_t now = millis();
  if (hadClient && (!tcpClient.connected() || uint32_t(now-lastTraffic) >= TCP_IDLE_MS)) {
    tcpClient.stop(); parser.reset(); spider.disconnected(SRC_TCP); hadClient = false;
  }
  if (!hadClient) {
    tcpClient = tcpServer.available();
    if (tcpClient) { parser.reset(); hadClient = true; lastTraffic = now; }
  }
  parser.expire(now);
  for (int budget=0; hadClient && budget<64 && tcpClient.available(); ++budget) {
    int b = tcpClient.read();
    if (b < 0) break;
    lastTraffic = millis(); parser.feed(uint8_t(b));
  }
}

void serviceStation() {
  static uint32_t lastAttempt = 0;
  if (WiFi.status() == WL_CONNECTED) return;
  uint32_t now = millis();
  if (uint32_t(now - lastAttempt) < STA_RETRY_MS) return;
  lastAttempt = now;
  WiFi.begin(STA_SSID, STA_PASS);
}

void serviceMqtt() {
  static uint32_t lastAttempt = 0;
  if (WiFi.status() != WL_CONNECTED) return;
  if (!mqtt.connected()) {
    if (spider.busy()) return;
    uint32_t now = millis();
    if (uint32_t(now - lastAttempt) < MQTT_RECONNECT_MS) return;
    lastAttempt = now;
    if (mqtt.connect() != 0) return;
  }
  Adafruit_MQTT_Subscribe *sub;
  while ((sub = mqtt.readSubscription(0))) {
    if (sub == &commandFeed) {
      int c = namedCommand((const char *)commandFeed.lastread);
      if (c >= 0) spider.command(c, SRC_MQTT);
    }
  }
}

// Obstacle guard: called every loop, just before the servos are updated,
// so a forward command from ANY source (web, TCP, MQTT) is stopped before
// the spider takes a step toward the obstacle.
void serviceObstacle() {
  sonar.service();
  if (sonar.blocked() && spider.currentMove() == FORWARD) spider.stop();
}

const char PAGE_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>QD020 Spider</title><style>
body{font:18px system-ui;text-align:center;background:#111;color:#eee;margin:24px auto;max-width:520px}
button{font:inherit;touch-action:none;user-select:none;margin:5px;padding:16px;border:0;border-radius:9px;background:#2876d7;color:white}
.pose{background:#733ac0}#stop{background:#bd3434}#status{min-height:2em}
#dist{font-size:22px;margin:12px 0}#dist.warn{color:#ff6b6b}#ip{font-size:14px;color:#aaa}
</style></head><body><h2>QD020 Spider</h2>
<p id="dist">Distance: waiting...</p>
<p>Click once to run for the command's configured duration. Stop ends the command immediately.</p>
<div><button data-move="turn_left">Turn left</button><button data-move="forward">Forward</button><button data-move="turn_right">Turn right</button></div>
<div><button data-move="strafe_left">Left</button><button id="stop" data-pose="standby">Stop</button><button data-move="strafe_right">Right</button></div>
<div><button data-move="backward">Backward</button></div><hr>
<div id="poses"></div><p id="status">Ready</p><p id="ip"></p>
<script>
const API_KEY='%API_KEY%';
const status=document.getElementById('status');
const dist=document.getElementById('dist');
const ip=document.getElementById('ip');
const poses={hello:'Hello',sleep:'Sleep',lie:'Lie down',pushup:'Push-up',fight:'Fighting',handsome:'Handsome',dance1:'Dance 1',dance2:'Dance 2',dance3:'Dance 3'};
for(const [name,label] of Object.entries(poses)){
  const b=document.createElement('button');b.className='pose';b.dataset.pose=name;b.textContent=label;
  document.getElementById('poses').appendChild(b);
}
let pending=null, sending=false;
function queue(command){ pending=command; pump(); }
async function pump(){
  if(sending || pending===null)return;
  sending=true;const command=pending;pending=null;
  const controller=new AbortController();const timeout=setTimeout(()=>controller.abort(),3000);
  try{
    const r=await fetch('/cmd?a='+encodeURIComponent(command),{method:'POST',cache:'no-store',signal:controller.signal,headers:{'X-API-Key':API_KEY}});
    if(r.status===409){status.textContent=await r.text();}
    else if(!r.ok){throw new Error(await r.text());}
    else{status.textContent=command==='standby'?'Stopped':command.replaceAll('_',' ')+' accepted for its configured duration';}
  }catch(e){
    status.textContent='Request could not be confirmed. Any accepted command stops after its configured timer. Use Stop to interrupt.';
  }finally{clearTimeout(timeout);sending=false;}
  if(pending!==null)pump();
}
async function poll(){
  try{
    const r=await fetch('/status',{cache:'no-store',headers:{'X-API-Key':API_KEY}});
    if(r.ok){
      const d=await r.json();
      if(d.distance<0){dist.textContent='Distance: clear (nothing in range)';dist.className='';}
      else{dist.textContent='Distance: '+d.distance+' cm'+(d.blocked?' - obstacle! Forward blocked':'');dist.className=d.blocked?'warn':'';}
      ip.textContent=d.ip?'Home WiFi IP: '+d.ip:'Home WiFi: not connected';
    }
  }catch(e){}
  setTimeout(poll,500);
}
document.querySelectorAll('[data-move]').forEach(b=>b.addEventListener('click',()=>queue(b.dataset.move)));
document.querySelectorAll('[data-pose]').forEach(b=>b.addEventListener('click',()=>queue(b.dataset.pose)));
poll();
</script></body></html>
)HTML";

void handleRoot() {
  String page;
  page.reserve(sizeof(PAGE_HTML) + 32);
  page = FPSTR(PAGE_HTML);
  page.replace("%API_KEY%", API_KEY);
  webServer.send(200, "text/html", page);
}

bool apiKeyOk() {
  return webServer.header("X-API-Key") == API_KEY || webServer.arg("key") == API_KEY;
}
void handleCmd() {
  webServer.sendHeader("Cache-Control","no-store");
  if (!apiKeyOk()) { webServer.send(401,"text/plain","Unauthorized"); return; }
  int c = namedCommand(webServer.arg("a").c_str());
  if (c < 0) { webServer.send(400,"text/plain","Unknown or missing command"); return; }
  if (c == FORWARD && sonar.blocked()) {
    webServer.send(409,"text/plain","Obstacle ahead - forward blocked. Back up or turn.");
    return;
  }
  spider.command(c,SRC_WEB);
  webServer.send(200,"text/plain","ok");
}
void handleStatus() {
  webServer.sendHeader("Cache-Control","no-store");
  if (!apiKeyOk()) { webServer.send(401,"text/plain","Unauthorized"); return; }
  String json = "{\"distance\":";
  json += sonar.cm();
  json += ",\"blocked\":";
  json += sonar.blocked() ? "true" : "false";
  json += ",\"ip\":\"";
  if (WiFi.status() == WL_CONNECTED) json += WiFi.localIP().toString();
  json += "\"}";
  webServer.send(200,"application/json",json);
}

void setup() {
  // NOTE: no Serial.begin() - TX/RX pins belong to the ultrasonic sensor.
  spider.begin();
  sonar.begin();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID,AP_PASS);
  WiFi.begin(STA_SSID,STA_PASS);
  mqtt.subscribe(&commandFeed);
  tcpServer.begin(); tcpServer.setNoDelay(true);
  webServer.collectHeaders("X-API-Key");
  webServer.on("/",HTTP_GET,handleRoot);
  webServer.on("/cmd",HTTP_POST,handleCmd);
  webServer.on("/cmd",HTTP_GET,handleCmd);
  webServer.on("/status",HTTP_GET,handleStatus);
  webServer.onNotFound([](){ webServer.send(404,"text/plain","Not found"); });
  webServer.begin();
}
void loop() {
  serviceTcp();
  serviceStation();
  serviceMqtt();
  webServer.handleClient();
  serviceObstacle();
  spider.tick();
  yield();
}
