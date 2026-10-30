#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <math.h>
#include "GuardianXDsa.h"

const char* SSID = "GuardianAngel"; const char* PASS = "guardian123";
WebServer server(80);

MpuSensor mpu;
DhtSensor dhtSensor;
Mq135Sensor mqSensor;
GpsSensor gpsSensor;

Sensor* sensors[] = { &mpu, &dhtSensor, &mqSensor, &gpsSensor };

RingBuffer<float, 20> dynWindow;
RingBuffer<float, 20> gzWindow;
LinkedQueue<Alert, 12> alertQueue;
DoublyLinkedList<Location, 32> routeTrack;
double lastRouteLat = 0, lastRouteLon = 0;
bool haveRouteLast = false;

enum AlertId { A_SHOCK, A_CRASH, A_FLIP, A_TURN, A_OVER, A_TWARN, A_TURGENT, A_AIR, A_COUNT };
unsigned long lastFire[A_COUNT] = { 0 };
bool prevShock = false, prevFlip = false, prevTurn = false, prevOver = false, prevWarn = false, prevUrgent = false, prevAir = false;
bool alertsArmed = false;
float eventPeak = 0;
bool overspeed = false, sharpTurn = false;
unsigned long lastPrint = 0;
bool crashLatched = false;
unsigned long crashLatchedAt = 0;

bool crashActive(){
  return crashLatched && (millis() - crashLatchedAt < 15000);
}

bool arrivedSafe = false, sentBack = false, homeSafe = false;
uint32_t arrivedAt = 0, sentAt = 0, homeAt = 0;

void addAlert(const char* msg, uint8_t sev){
  Alert a; a.t = (uint32_t)(millis() / 1000); a.sev = sev;
  strncpy(a.msg, msg, sizeof(a.msg) - 1); a.msg[sizeof(a.msg) - 1] = 0;
  alertQueue.push(a);
}
void fireAlert(int id, const char* msg, uint8_t sev){
  if (lastFire[id] != 0 && millis() - lastFire[id] < ALERT_COOLDOWN_MS) return;
  addAlert(msg, sev);
  lastFire[id] = millis() ? millis() : 1;
}
float windowAvg(const RingBuffer<float, 20>& b){
  if (b.empty()) return 0;
  float s = 0; for (size_t i = 0; i < b.size(); i++) s += b[i];
  return s / b.size();
}
double metersBetween(double lat1, double lon1, double lat2, double lon2){
  const double R = 6371000.0, D2R = 0.017453292519943295;
  double dLat = (lat2 - lat1) * D2R, dLon = (lon2 - lon1) * D2R;
  double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * D2R) * cos(lat2 * D2R) * sin(dLon / 2) * sin(dLon / 2);
  return 2 * R * asin(sqrt(a));
}

void serveFile(const char* path, const char* type){
  File f = LittleFS.open(path, "r");
  if (!f) { server.send(404, "text/plain", "Not found"); return; }
  server.streamFile(f, type);
  f.close();
}
void root(){ serveFile("/index.html", "text/html"); }
void teacherPage(){ serveFile("/teacher.html", "text/html"); }
void demoPage(){ serveFile("/demo.html", "text/html"); }

void data(){
  bool smoking = mqSensor.baselineReady && mqSensor.airQualityBad;
  bool hot = dhtSensor.valid && dhtSensor.tempC >= TEMP_HOT;
  bool cold = dhtSensor.valid && dhtSensor.tempC < TEMP_COLD;
  bool tooHot = dhtSensor.valid && dhtSensor.tempC >= TEMP_TOO_HOT;
  bool tooCold = dhtSensor.valid && dhtSensor.tempC <= TEMP_TOO_COLD;
  bool crash = mpu.crash || crashActive();
  bool problem = mpu.shock || crash || mpu.flipped || overspeed || sharpTurn || smoking || hot || cold;

  String s = "{";
  s += "\"temperature\":" + (dhtSensor.valid ? String(dhtSensor.tempC, 2) : String("null"));
  s += ",\"humidity\":" + (dhtSensor.valid ? String(dhtSensor.humidity, 2) : String("null"));
  s += ",\"tempTooCold\":" + String(TEMP_TOO_COLD, 1) + ",\"tempCold\":" + String(TEMP_COLD, 1);
  s += ",\"tempHot\":" + String(TEMP_HOT, 1) + ",\"tempTooHot\":" + String(TEMP_TOO_HOT, 1);
  s += ",\"smoking\":" + String(smoking ? "true" : "false") + ",\"smokingKnown\":" + String(mqSensor.baselineReady ? "true" : "false");
  s += ",\"harshDriving\":" + String(mpu.shock ? "true" : "false") + ",\"impact\":" + String(mpu.impact ? "true" : "false");
  s += ",\"flipped\":" + String(mpu.flipped ? "true" : "false") + ",\"crash\":" + String(crash ? "true" : "false");
  s += ",\"overspeed\":" + String(overspeed ? "true" : "false") + ",\"sharpTurn\":" + String(sharpTurn ? "true" : "false");
  s += ",\"problem\":" + String(problem ? "true" : "false") + ",\"problems\":[";
  bool firstP = true;
  auto addProblem = [&](const char* msg){
    if (!firstP) s += ","; firstP = false;
    s += "\""; s += msg; s += "\"";
  };
  if (mpu.flipped) addProblem("Car flipped - high crash risk");
  if (crash) addProblem("Crash detected");
  if (mpu.impact) addProblem("Sudden bump or hard impact");
  else if (mpu.shock) addProblem("Sudden braking or speeding up");
  if (overspeed) addProblem("Vehicle is going too fast");
  if (sharpTurn) addProblem("Sharp turn taken");
  if (smoking) addProblem("Smoke detected in the car");
  if (tooHot) addProblem("Car is too hot");
  else if (hot) addProblem("Car is hot");
  if (tooCold) addProblem("Car is too cold");
  else if (cold) addProblem("Car is cold");
  s += "]";
  s += ",\"gpsValid\":" + String(gpsSensor.valid ? "true" : "false") + ",\"latitude\":" + String(gpsSensor.lat, 6) + ",\"longitude\":" + String(gpsSensor.lon, 6);
  s += ",\"speedKmph\":" + String(gpsSensor.speed, 2) + ",\"satellites\":" + String(gpsSensor.sats);
  s += ",\"arrived\":" + String(arrivedSafe ? "true" : "false") + ",\"arrivedAt\":" + String(arrivedAt);
  s += ",\"sentBack\":" + String(sentBack ? "true" : "false") + ",\"sentAt\":" + String(sentAt);
  s += ",\"homeSafe\":" + String(homeSafe ? "true" : "false") + ",\"homeAt\":" + String(homeAt);
  s += ",\"alerts\":[";
  const ListNode<Alert>* n = alertQueue.begin(); bool first = true;
  while (n) { if (!first) s += ","; first = false; s += "{\"t\":" + String(n->value.t) + ",\"sev\":" + String(n->value.sev) + ",\"msg\":\"" + String(n->value.msg) + "\"}"; n = n->next; }
  s += "]}";
  server.send(200, "application/json", s);
}

void teacherData(){
  String s = "{";
  s += "\"arrived\":" + String(arrivedSafe ? "true" : "false");
  s += ",\"arrivedAt\":" + String(arrivedAt);
  s += ",\"sentBack\":" + String(sentBack ? "true" : "false");
  s += ",\"sentAt\":" + String(sentAt);
  s += ",\"homeSafe\":" + String(homeSafe ? "true" : "false");
  s += ",\"homeAt\":" + String(homeAt);
  s += "}";
  server.send(200, "application/json", s);
}

void markArrived(){
  if (!arrivedSafe) { arrivedSafe = true; arrivedAt = (uint32_t)(millis() / 1000); }
  server.send(200, "application/json", "{\"ok\":true}");
}

void markSentBack(){
  if (!arrivedSafe) { server.send(409, "application/json", "{\"ok\":false,\"error\":\"not arrived\"}"); return; }
  if (!sentBack) { sentBack = true; sentAt = (uint32_t)(millis() / 1000); }
  server.send(200, "application/json", "{\"ok\":true}");
}

void markHomeSafe(){
  if (!sentBack) { server.send(409, "application/json", "{\"ok\":false,\"error\":\"not sent back\"}"); return; }
  if (!homeSafe) { homeSafe = true; homeAt = (uint32_t)(millis() / 1000); }
  server.send(200, "application/json", "{\"ok\":true}");
}

void resetTrip(){
  arrivedSafe = sentBack = homeSafe = false;
  arrivedAt = sentAt = homeAt = 0;
  server.send(200, "application/json", "{\"ok\":true}");
}

void updateAlerts(){
  bool turnNow = sharpTurn;
  bool flipNow = mpu.flipped;
  bool warnNow = dhtSensor.valid && dhtSensor.tempC >= TEMP_HOT;
  bool urgentNow = dhtSensor.valid && dhtSensor.tempC >= TEMP_TOO_HOT;

  if (!alertsArmed) {
    prevShock = mpu.shock; prevFlip = flipNow; prevTurn = turnNow; prevOver = overspeed;
    prevWarn = warnNow; prevUrgent = urgentNow; prevAir = mqSensor.airQualityBad;
    eventPeak = 0;
    if (millis() >= ALERT_STARTUP_MS && !mpu.shock && !flipNow && !turnNow && !overspeed) {
      mpu.captureUpright();
      alertsArmed = true;
    }
    return;
  }

  if (mpu.shock) eventPeak = max(eventPeak, mpu.dynamicG);
  if (!mpu.shock && prevShock) {
    if (eventPeak > CRASH_G) {
      fireAlert(A_CRASH, "Crash detected", 3);
      crashLatched = true; crashLatchedAt = millis();
    } else if (eventPeak > IMPACT_G) {
      fireAlert(A_SHOCK, "Sudden bump or hard impact", 2);
    } else {
      fireAlert(A_SHOCK, "Sudden braking or speeding up", 1);
    }
    eventPeak = 0;
  }
  if (flipNow && !prevFlip) fireAlert(A_FLIP, "Car flipped - high crash risk", 2);
  if (overspeed && !prevOver) fireAlert(A_OVER, "Vehicle is going too fast", 2);
  if (turnNow && !prevTurn) fireAlert(A_TURN, "Sharp turn taken", 1);
  if (warnNow && !prevWarn) fireAlert(A_TWARN, "Car is too hot", 1);
  if (urgentNow && !prevUrgent) fireAlert(A_TURGENT, "Car is dangerously hot", 2);
  if (mqSensor.airQualityBad && !prevAir) fireAlert(A_AIR, "Smoke detected in the car", 1);
  prevShock = mpu.shock; prevFlip = flipNow; prevTurn = turnNow; prevOver = overspeed;
  prevWarn = warnNow; prevUrgent = urgentNow;
  prevAir = mqSensor.airQualityBad;
}

void setup(){
  Serial.begin(115200); delay(500);
  for (Sensor* s : sensors) Serial.printf("%s: %s\n", s->name(), s->begin() ? "OK" : "NOT DETECTED");
  routeTrack.push_back(Location{ 0, 13.352500, 74.793200, "test1loc" });
  routeTrack.push_back(Location{ 0, 13.353100, 74.794000, "test2loc" });
  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  else if (!LittleFS.exists("/index.html")) Serial.println("WARNING: /index.html missing - flash the data/ folder (see README).");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(SSID, PASS);

  server.on("/", HTTP_GET, root);
  server.on("/teacher", HTTP_GET, teacherPage);
  server.on("/demo", HTTP_GET, demoPage);
  server.on("/data", HTTP_GET, data);
  server.on("/teacher/data", HTTP_GET, teacherData);
  server.on("/teacher/arrive", HTTP_GET, markArrived);
  server.on("/teacher/sent", HTTP_GET, markSentBack);
  server.on("/parent/home", HTTP_GET, markHomeSafe);
  server.on("/parent/reset", HTTP_GET, resetTrip);
  server.serveStatic("/", LittleFS, "/");
  server.begin();
  Serial.print("Wi-Fi GuardianAngel; parent http://"); Serial.print(WiFi.softAPIP());
  Serial.println("  teacher /teacher");
  Serial.printf("MQ-135 on GPIO%d; warming up %lus before baseline.\n", PIN_MQ, MQ_WARMUP_MS / 1000);
}

void loop(){
  server.handleClient();
  for (Sensor* s : sensors) s->update();
  if (mpu.present) { dynWindow.push(mpu.dynamicG); gzWindow.push(mpu.gz); }
  if (gpsSensor.valid && (!haveRouteLast || metersBetween(lastRouteLat, lastRouteLon, gpsSensor.lat, gpsSensor.lon) >= ROUTE_MIN_METERS)) {
    routeTrack.push_back(Location{ (uint32_t)(millis() / 1000), gpsSensor.lat, gpsSensor.lon, "GPS" });
    lastRouteLat = gpsSensor.lat; lastRouteLon = gpsSensor.lon; haveRouteLast = true;
  }
  float yawAvg = windowAvg(gzWindow);
  sharpTurn = gpsSensor.valid && gpsSensor.speed > TURN_MIN_KMPH && fabsf(yawAvg) > TURN_DPS;
  overspeed = gpsSensor.valid && gpsSensor.speed > SPEED_LIMIT_KMPH;
  updateAlerts();
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    Serial.printf("AX %.3f total %.3f dyn %.3f avg %.3f | GX %.2f yaw %.1f | ", mpu.ax, mpu.totalG, mpu.dynamicG, windowAvg(dynWindow), mpu.gx, yawAvg);
    if (dhtSensor.valid) Serial.printf("T %.1fC H %.1f%% | ", dhtSensor.tempC, dhtSensor.humidity); else Serial.print("DHT ERROR | ");
    Serial.printf("MQ %d(%.2fV) trend %.1f%% %s | ", mqSensor.raw, mqSensor.volt, mqSensor.trendPct, !mqSensor.baselineReady ? "WARM" : (mqSensor.airQualityBad ? "RISING" : "OK"));
    if (gpsSensor.valid) Serial.printf("GPS %.6f,%.6f speed %.1f sats %u | ", gpsSensor.lat, gpsSensor.lon, gpsSensor.speed, gpsSensor.sats); else Serial.print("GPS NO FIX | ");
    if (overspeed) Serial.print("OVERSPEED ");
    if (mpu.shock) Serial.print("SHOCK ");
    if (sharpTurn) Serial.print("TURN ");
    Serial.println(mpu.shock ? "JOLT" : "NORMAL");
  }
  delay(5);
}
