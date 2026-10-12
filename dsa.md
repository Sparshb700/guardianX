# GuardianX DSA

1. Class inheritance & polymorphism
2. Linked list — alert queue
3. Doubly linked list — GPS route tracking

## 1. Class inheritance

One abstract base class, four sensor subclasses, driven through base-class pointers.

```cpp
// GuardianXDsa.h:123
class Sensor {
public:
  virtual ~Sensor() {}
  virtual const char* name() const = 0;
  virtual bool begin() = 0;
  virtual void update() = 0;
  bool present = false;
};

class MpuSensor : public Sensor {   // same pattern: DhtSensor, Mq135Sensor, GpsSensor
public:
  const char* name() const override { return "MPU-6050"; }
  bool begin() override { /* I2C setup */ }
  void update() override { /* read accel/gyro */ }
};
```

```cpp
// GuardianX.ino:28  -- only base pointers are used afterwards
Sensor* sensors[] = { &mpu, &dhtSensor, &mqSensor, &gpsSensor };

// GuardianX.ino:168  -- virtual dispatch calls the right update() per subclass
for (Sensor* s : sensors) s->update();
```

## 2. Linked list — alert queue

A singly linked list as a bounded FIFO: append at the tail, drop the oldest from the head.

```cpp
// GuardianXDsa.h:58
template <typename T>
struct ListNode { T value; ListNode* next; };

// GuardianXDsa.h:61
template <typename T, size_t MAXITEMS>
class LinkedQueue {
  void push(const T& v){
    ListNode<T>* n = new ListNode<T>{ v, nullptr };
    if (tail) tail->next = n; else head = n;
    tail = n; count++;
    if (count > MAXITEMS) { ListNode<T>* old = head; head = head->next; delete old; count--; }
  }
  const ListNode<T>* begin() const { return head; }
};
```

the alert history

```cpp
// GuardianXDsa.h:85
struct Alert { uint32_t t; uint8_t sev; char msg[44]; };

// GuardianX.ino:33
LinkedQueue<Alert, 12> alertQueue;

// GuardianX.ino:45  -- add an alert
void addAlert(const char* msg, uint8_t sev){
  Alert a; a.t = millis() / 1000; a.sev = sev;
  strncpy(a.msg, msg, sizeof(a.msg) - 1);
  alertQueue.push(a);
}

// GuardianX.ino:92  -- walk the list to serialise it
const ListNode<Alert>* n = alertQueue.begin();
while (n) { /* n->value.msg ... */ n = n->next; }
```

---

## 3. Doubly linked list — GPS route tracking

Each `prev`/`next` node is one distinct NEO-6M location, so the route can be walked in both
directions. A new node is stored only after the unit moves ≥ `ROUTE_MIN_METERS` (10 m).

```cpp
// GuardianXDsa.h:90
struct Location { uint32_t t; double lat, lon; const char* label; };

// GuardianXDsa.h:93
template <typename T>
struct DListNode { T value; DListNode* prev; DListNode* next; };

// GuardianXDsa.h:96
template <typename T, size_t MAXITEMS>
class DoublyLinkedList {
  void push_back(const T& v){
    DListNode<T>* n = new DListNode<T>{ v, tail, nullptr };
    if (tail) tail->next = n; else head = n;
    tail = n; count++;
    if (count > MAXITEMS) { DListNode<T>* old = head; head = head->next; head->prev = nullptr; delete old; count--; }
  }
  const DListNode<T>* begin() const { return head; }  // forward
  const DListNode<T>* back() const { return tail; }   // backward
};
```

Usage:

```cpp
// GuardianX.ino:34
DoublyLinkedList<Location, 32> routeTrack;


// GuardianX.ino:171  -- real waypoint, added only after moving >= ROUTE_MIN_METERS
routeTrack.push_back(Location{ millis() / 1000, gpsSensor.lat, gpsSensor.lon, "GPS" });

// GuardianX.ino:116  -- walk the route to build the admin JSON
const DListNode<Location>* rn = routeTrack.begin();
while (rn) { /* rn->value.lat, rn->value.lon, rn->value.label */ rn = rn->next; }
```

Shown on the admin dashboard (`http://192.168.4.1/admin`)
