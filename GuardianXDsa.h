#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <string.h>
#include <DHTesp.h>
#include <TinyGPSPlus.h>
#include "GuardianXCalibration.h"

static const uint8_t MPU_ADDR = 0x68;
static const int PIN_SDA = 8, PIN_SCL = 9, PIN_DHT = 4, PIN_MQ = 5, PIN_GPS_RX = 16, PIN_GPS_TX = 17;
static const float SPEED_LIMIT_KMPH = 50.0f, SHOCK_G = 0.50f, IMPACT_G = 0.90f, CRASH_G = 1.60f, TURN_DPS = 90.0f;
static const float FLIP_COS = 0.34f;
static const unsigned long ALERT_STARTUP_MS = 5000;
static const float TEMP_TOO_COLD = 8.0f, TEMP_COLD = 16.0f, TEMP_HOT = 30.0f, TEMP_TOO_HOT = 38.0f;
static const float TURN_MIN_KMPH = 5.0f;
static const unsigned long ALERT_COOLDOWN_MS = 10000;
static const unsigned long MQ_WARMUP_MS = 120000;
static const float MQ_ALERT_PCT = 25.0f;
static const double ROUTE_MIN_METERS = 10.0;

template <typename T, size_t N>
class RingBuffer {
public:
  void push(const T& v){ buf[head] = v; head = (head + 1) % N; if (count < N) count++; }
  size_t size() const { return count; }
  size_t capacity() const { return N; }
  bool empty() const { return count == 0; }
  void clear(){ head = 0; count = 0; }
  const T& operator[](size_t i) const { return buf[(head - count + i + N) % N]; }
private:
  T buf[N];
  size_t head = 0, count = 0;
};

template <typename T>
class ListNode {
public:
  T value;
  ListNode* next;
};

template <typename T, size_t MAXITEMS>
class LinkedQueue {
public:
  ~LinkedQueue(){ clear(); }
  void clear(){
    ListNode<T>* p = head;
    while (p) { ListNode<T>* n = p->next; delete p; p = n; }
    head = tail = nullptr; count = 0;
  }
  bool push(const T& v){
    ListNode<T>* n = new ListNode<T>{ v, nullptr };
    if (!n) return false;
    if (tail) tail->next = n; else head = n;
    tail = n; count++;
    if (count > MAXITEMS) { ListNode<T>* old = head; head = head->next; delete old; count--; }
    return true;
  }
  size_t size() const { return count; }
  const ListNode<T>* begin() const { return head; }
private:
  ListNode<T>* head = nullptr;
  ListNode<T>* tail = nullptr;
  size_t count = 0;
};

class Alert {
public:
  uint32_t t;
  uint8_t sev;
  char msg[44];
};

class Location {
public:
  uint32_t t;
  double lat, lon;
  const char* label;
};

template <typename T>
class DListNode {
public:
  T value;
  DListNode* prev;
  DListNode* next;
};

template <typename T, size_t MAXITEMS>
class DoublyLinkedList {
public:
  ~DoublyLinkedList(){ clear(); }
  void clear(){
    DListNode<T>* p = head;
    while (p) { DListNode<T>* n = p->next; delete p; p = n; }
    head = tail = nullptr; count = 0;
  }
  void push_back(const T& v){
    DListNode<T>* n = new DListNode<T>{ v, tail, nullptr };
    if (!n) return;
    if (tail) tail->next = n; else head = n;
    tail = n; count++;
    if (count > MAXITEMS) { DListNode<T>* old = head; head = head->next; head->prev = nullptr; delete old; count--; }
  }
  size_t size() const { return count; }
  const DListNode<T>* begin() const { return head; }
  const DListNode<T>* back() const { return tail; }
private:
  DListNode<T>* head = nullptr;
  DListNode<T>* tail = nullptr;
  size_t count = 0;
};

class Sensor {
public:
  virtual ~Sensor() {}
  virtual const char* name() const = 0;
  virtual bool begin() = 0;
  virtual void update() = 0;
  bool present = false;
};

class MpuSensor : public Sensor {
public:
  const char* name() const override { return "MPU-6050"; }
  bool begin() override {
    Wire.begin(PIN_SDA, PIN_SCL); Wire.setClock(100000);
    Wire.beginTransmission(MPU_ADDR); Wire.write(0x75);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(MPU_ADDR, (uint8_t)1) != 1 || Wire.read() != 0x68) return present = false;
    Wire.beginTransmission(MPU_ADDR); Wire.write(0x6B); Wire.write(0); if (Wire.endTransmission() != 0) return present = false;
    Wire.beginTransmission(MPU_ADDR); Wire.write(0x1C); Wire.write(0); Wire.endTransmission();
    Wire.beginTransmission(MPU_ADDR); Wire.write(0x1B); Wire.write(0); Wire.endTransmission();
    return present = true;
  }
  void update() override {
    ax = (rd(0x3B) - ACCEL_X_OFFSET) / ACCEL_LSB_PER_G;
    ay = (rd(0x3D) - ACCEL_Y_OFFSET) / ACCEL_LSB_PER_G;
    az = (rd(0x3F) - ACCEL_Z_OFFSET) / ACCEL_LSB_PER_G;
    gx = (rd(0x43) - GYRO_X_OFFSET) / GYRO_LSB_PER_DPS;
    gy = (rd(0x45) - GYRO_Y_OFFSET) / GYRO_LSB_PER_DPS;
    gz = (rd(0x47) - GYRO_Z_OFFSET) / GYRO_LSB_PER_DPS;
    totalG = sqrtf(ax * ax + ay * ay + az * az);
    dynamicG = fabsf(totalG - 1.0f);
    shockFrames = dynamicG > SHOCK_G ? (shockFrames < 5 ? shockFrames + 1 : 5) : 0;
    shock = shockFrames >= 2;
    impact = dynamicG > IMPACT_G;
    crash = dynamicG > CRASH_G;
    bool rawFlip = false;
    if (hasUpright) {
      float dot = ax * uprightAx + ay * uprightAy + az * uprightAz;
      float mag = sqrtf((ax * ax + ay * ay + az * az) *
                        (uprightAx * uprightAx + uprightAy * uprightAy + uprightAz * uprightAz));
      float cosDev = mag > 0.01f ? dot / mag : 1.0f;
      rawFlip = cosDev < FLIP_COS;
    }
    flipFrames = rawFlip ? (flipFrames < 60 ? flipFrames + 1 : 60) : 0;
    flipped = flipFrames >= 50;
  }
  void captureUpright(){
    uprightAx = ax; uprightAy = ay; uprightAz = az;
    hasUpright = true; flipFrames = 0; flipped = false;
  }
  float ax = 0, ay = 0, az = 1, gx = 0, gy = 0, gz = 0, totalG = 1, dynamicG = 0;
  bool shock = false, impact = false, crash = false, flipped = false;
private:
  uint8_t shockFrames = 0, flipFrames = 0;
  float uprightAx = 0, uprightAy = 0, uprightAz = 1;
  bool hasUpright = false;
  int16_t rd(uint8_t r){
    Wire.beginTransmission(MPU_ADDR); Wire.write(r);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(MPU_ADDR, (uint8_t)2) != 2) return 0;
    return (int16_t)(((uint16_t)Wire.read() << 8) | Wire.read());
  }
};

class DhtSensor : public Sensor {
public:
  const char* name() const override { return "DHT22"; }
  bool begin() override { dht.setup(PIN_DHT, DHTesp::DHT22); return present = true; }
  void update() override {
    if (last && millis() - last < 2000) return;
    last = millis();
    TempAndHumidity x = dht.getTempAndHumidity();
    valid = dht.getStatus() == 0;
    tempC = valid ? x.temperature : NAN;
    humidity = valid ? x.humidity : NAN;
  }
  float tempC = NAN, humidity = NAN;
  bool valid = false;
private:
  DHTesp dht; unsigned long last = 0;
};

class Mq135Sensor : public Sensor {
public:
  const char* name() const override { return "MQ-135"; }
  bool begin() override {
    analogReadResolution(12); analogSetPinAttenuation(PIN_MQ, ADC_11db); pinMode(PIN_MQ, INPUT);
    start = millis(); return present = true;
  }
  void update() override {
    raw = analogRead(PIN_MQ); volt = raw * 3.3f / 4095.0f;
    if (!ready) {
      if (millis() - start < MQ_WARMUP_MS) { trendPct = 0; airQualityBad = false; return; }
      long s = 0; for (int i = 0; i < 10; i++) { s += analogRead(PIN_MQ); delay(2); }
      base = s / 10.0f; if (base < 1.0f) base = 1.0f; ready = true;
      baseline = base; baselineReady = true; trendPct = 0; airQualityBad = false; return;
    }
    baseline = base; baselineReady = true;
    trendPct = (raw - base) / base * 100.0f;
    airQualityBad = trendPct > MQ_ALERT_PCT;
  }
  int raw = 0;
  float volt = 0, baseline = 0, trendPct = 0;
  bool baselineReady = false, airQualityBad = false;
private:
  unsigned long start = 0; float base = 0; bool ready = false;
};

class GpsSensor : public Sensor {
public:
  const char* name() const override { return "NEO-6M"; }
  bool begin() override { GPS.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX); return present = true; }
  void update() override {
    while (GPS.available()) gps.encode(GPS.read());
    if (gps.location.isValid()) { valid = true; lat = gps.location.lat(); lon = gps.location.lng(); }
    if (gps.speed.isValid()) speed = gps.speed.kmph();
    if (gps.satellites.isValid()) sats = gps.satellites.value();
  }
  bool valid = false;
  double lat = 0, lon = 0, speed = 0;
  uint32_t sats = 0;
private:
  TinyGPSPlus gps; HardwareSerial GPS{ 1 };
};
