
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <math.h>
#include "GuardianXDsa.h"

const char* SSID = "GuardianX"; const char* PASS = "guardian123";
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

enum AlertId { A_SHOCK, A_TURN, A_OVER, A_TWARN, A_TURGENT, A_AIR, A_COUNT };
unsigned long lastFire[A_COUNT] = { 0 };
bool prevShock = false, prevTurn = false, prevOver = false, prevWarn = false, prevUrgent = false, prevAir = false;
float eventPeak = 0;                 // peak dynamicG within the current shock event
bool overspeed = false, sharpTurn = false;
unsigned long lastPrint = 0;

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
void adminPage(){ serveFile("/admin.html", "text/html"); }
void root(){ serveFile("/index.html", "text/html"); }

void data(){
  String s = "{";
  s += "\"temperature\":" + (dhtSensor.valid ? String(dhtSensor.tempC, 2) : String("null"));
  s += ",\"humidity\":" + (dhtSensor.valid ? String(dhtSensor.humidity, 2) : String("null"));
  s += ",\"ax\":" + String(mpu.ax, 4) + ",\"gx\":" + String(mpu.gx, 3);
  s += ",\"dynamicAcceleration\":" + String(mpu.dynamicG, 4) + ",\"dynamicAverage\":" + String(windowAvg(dynWindow), 4);
  s += ",\"motion\":\"" + String(mpu.shock ? "JOLT DETECTED" : "NORMAL") + "\"";
  s += ",\"shock\":" + String(mpu.shock ? "true" : "false") + ",\"impact\":" + String(mpu.impact ? "true" : "false");
  s += ",\"overspeed\":" + String(overspeed ? "true" : "false") + ",\"sharpTurn\":" + String(sharpTurn ? "true" : "false");
  s += ",\"mqVolt\":" + String(mqSensor.volt, 3) + ",\"mqTrendPct\":" + String(mqSensor.trendPct, 2) + ",\"mqBaselineReady\":" + String(mqSensor.baselineReady ? "true" : "false") + ",\"airQualityBad\":" + String(mqSensor.airQualityBad ? "true" : "false");
  s += ",\"gpsValid\":" + String(gpsSensor.valid ? "true" : "false") + ",\"latitude\":" + String(gpsSensor.lat, 6) + ",\"longitude\":" + String(gpsSensor.lon, 6);
  s += ",\"speedKmph\":" + String(gpsSensor.speed, 2) + ",\"satellites\":" + String(gpsSensor.sats);
  s += ",\"routeCount\":" + String(routeTrack.size());
  s += ",\"alerts\":[";
  const ListNode<Alert>* n = alertQueue.begin(); bool first = true;
  while (n) { if (!first) s += ","; first = false; s += "{\"t\":" + String(n->value.t) + ",\"sev\":" + String(n->value.sev) + ",\"msg\":\"" + String(n->value.msg) + "\"}"; n = n->next; }
  s += "]}";
  server.send(200, "application/json", s);
}

void adminData(){
  String s = "{";
  s += "\"ax\":" + String(mpu.ax, 4) + ",\"ay\":" + String(mpu.ay, 4) + ",\"az\":" + String(mpu.az, 4);
  s += ",\"gx\":" + String(mpu.gx, 3) + ",\"gy\":" + String(mpu.gy, 3) + ",\"gz\":" + String(mpu.gz, 3);
  s += ",\"totalAcceleration\":" + String(mpu.totalG, 4) + ",\"dynamicAcceleration\":" + String(mpu.dynamicG, 4) + ",\"dynamicAverage\":" + String(windowAvg(dynWindow), 4) + ",\"yawAverage\":" + String(windowAvg(gzWindow), 2);
  s += ",\"temperature\":" + (dhtSensor.valid ? String(dhtSensor.tempC, 2) : String("null"));
  s += ",\"humidity\":" + (dhtSensor.valid ? String(dhtSensor.humidity, 2) : String("null"));
  s += ",\"mqRaw\":" + String(mqSensor.raw) + ",\"mqVolt\":" + String(mqSensor.volt, 4) + ",\"mqBaseline\":" + String(mqSensor.baseline, 1) + ",\"mqTrendPct\":" + String(mqSensor.trendPct, 2) + ",\"mqBaselineReady\":" + String(mqSensor.baselineReady ? "true" : "false") + ",\"airQualityBad\":" + String(mqSensor.airQualityBad ? "true" : "false");
  s += ",\"gpsValid\":" + String(gpsSensor.valid ? "true" : "false") + ",\"latitude\":" + String(gpsSensor.lat, 6) + ",\"longitude\":" + String(gpsSensor.lon, 6) + ",\"speedKmph\":" + String(gpsSensor.speed, 2) + ",\"satellites\":" + String(gpsSensor.sats);
  s += ",\"overspeed\":" + String(overspeed ? "true" : "false") + ",\"shock\":" + String(mpu.shock ? "true" : "false") + ",\"impact\":" + String(mpu.impact ? "true" : "false") + ",\"sharpTurn\":" + String(sharpTurn ? "true" : "false");
  s += ",\"uptimeMs\":" + String(millis()) + ",\"freeHeap\":" + String(ESP.getFreeHeap()) + ",\"wifiClients\":" + String(WiFi.softAPgetStationNum());
  s += ",\"sensors\":{\"mpu\":" + String(mpu.present ? "true" : "false") + ",\"dht\":" + String(dhtSensor.present ? "true" : "false") + ",\"mq\":" + String(mqSensor.present ? "true" : "false") + ",\"gps\":" + String(gpsSensor.present ? "true" : "false") + "}";
  s += ",\"thresholds\":{\"speed\":" + String(SPEED_LIMIT_KMPH, 1) + ",\"shock\":" + String(SHOCK_G, 2) + ",\"impact\":" + String(IMPACT_G, 2) + ",\"turn\":" + String(TURN_DPS, 0) + ",\"tempWarn\":" + String(TEMP_WARN, 0) + ",\"tempUrgent\":" + String(TEMP_URGENT, 0) + ",\"mqPct\":" + String(MQ_ALERT_PCT, 0) + "}";
  s += ",\"dynWindow\":[";
  for (size_t i = 0; i < dynWindow.size(); i++) { if (i) s += ","; s += String(dynWindow[i], 3); }
  s += "],\"gzWindow\":[";
  for (size_t i = 0; i < gzWindow.size(); i++) { if (i) s += ","; s += String(gzWindow[i], 1); }
  s += "],\"routeCount\":" + String(routeTrack.size()) + ",\"route\":[";
  const DListNode<Location>* rn = routeTrack.begin(); bool firstR = true;
  while (rn) { if (!firstR) s += ","; firstR = false; s += "{\"t\":" + String(rn->value.t) + ",\"lat\":" + String(rn->value.lat, 6) + ",\"lon\":" + String(rn->value.lon, 6) + ",\"label\":\"" + String(rn->value.label) + "\"}"; rn = rn->next; }
  s += "],\"alerts\":[";
  const ListNode<Alert>* n = alertQueue.begin(); bool first = true;
  while (n) { if (!first) s += ","; first = false; s += "{\"t\":" + String(n->value.t) + ",\"sev\":" + String(n->value.sev) + ",\"msg\":\"" + String(n->value.msg) + "\"}"; n = n->next; }
  s += "]}";
  server.send(200, "application/json", s);
}

// One physical jolt -> exactly one alert, classified by the peak it reached.
void updateAlerts(){
  bool turnNow = sharpTurn;
  if (mpu.shock) eventPeak = max(eventPeak, mpu.dynamicG);
  if (!mpu.shock && prevShock) {
    if (eventPeak > IMPACT_G) fireAlert(A_SHOCK, "Impact / jolt detected", 2);
    else fireAlert(A_SHOCK, "Harsh braking / acceleration", 1);
    eventPeak = 0;
  }
  if (overspeed && !prevOver) fireAlert(A_OVER, "Overspeed above limit", 2);
  if (turnNow && !prevTurn) fireAlert(A_TURN, "Sharp turn detected", 1);
  if (dhtSensor.valid && dhtSensor.tempC >= TEMP_WARN && !prevWarn) fireAlert(A_TWARN, "Cabin temperature warning", 1);
  if (dhtSensor.valid && dhtSensor.tempC >= TEMP_URGENT && !prevUrgent) fireAlert(A_TURGENT, "Cabin temperature urgent", 2);
  if (mqSensor.airQualityBad && !prevAir) fireAlert(A_AIR, "Air quality rising", 1);
  prevShock = mpu.shock; prevTurn = turnNow; prevOver = overspeed;
  prevWarn = dhtSensor.valid && dhtSensor.tempC >= TEMP_WARN;
  prevUrgent = dhtSensor.valid && dhtSensor.tempC >= TEMP_URGENT;
  prevAir = mqSensor.airQualityBad;
}

void setup(){
  Serial.begin(115200); delay(500);
  for (Sensor* s : sensors) Serial.printf("%s: %s\n", s->name(), s->begin() ? "OK" : "NOT DETECTED");
  routeTrack.push_back(Location{ 0, 13.352500, 74.793200, "test1loc" });
  routeTrack.push_back(Location{ 0, 13.353100, 74.794000, "test2loc" });
  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  else if (!LittleFS.exists("/index.html")) Serial.println("WARNING: /index.html missing - flash the data/ folder (see README).");
  WiFi.mode(WIFI_AP); WiFi.softAP(SSID, PASS);
  server.on("/", HTTP_GET, root);
  server.on("/admin", HTTP_GET, adminPage);
  server.on("/data", HTTP_GET, data);
  server.on("/admin/data", HTTP_GET, adminData);
  server.serveStatic("/", LittleFS, "/");
  server.begin();
  Serial.print("Wi-Fi GuardianX; dashboard http://"); Serial.print(WiFi.softAPIP());
  Serial.println("  admin /admin");
  Serial.printf("MQ-135 on GPIO%d; warming up %lus before baseline.\n", PIN_MQ, MQ_WARMUP_MS / 1000);
}

void loop(){
  server.handleClient();
  for (Sensor* s : sensors) s->update();           // polymorphic update of every sensor
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
