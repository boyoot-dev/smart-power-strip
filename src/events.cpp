#include "events.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "time_sync.h"

namespace {

constexpr size_t kRingSize = 100;
constexpr size_t kTbQueueSize = 16;

Event ring[kRingSize];
size_t head = 0;   // next write slot
size_t count = 0;
volatile uint32_t total = 0;
SemaphoreHandle_t lock;
QueueHandle_t tbQueue;

}  // namespace

const char* sourceName(Source s) {
  switch (s) {
    case Source::kBoot: return "boot";
    case Source::kButton: return "button";
    case Source::kWeb: return "web";
    case Source::kThingsBoard: return "thingsboard";
    case Source::kRule: return "rule";
    case Source::kOvercurrent: return "overcurrent";
    case Source::kVoltage: return "voltage";
    case Source::kCompressorRelease: return "compressor_delay_release";
    case Source::kOverheat: return "overheat";
    case Source::kSensorFault: return "sensor_fault";
  }
  return "?";
}

namespace Events {

void begin() {
  lock = xSemaphoreCreateMutex();
  tbQueue = xQueueCreate(kTbQueueSize, sizeof(Event));
}

void record(int outlet, bool on, Source source) {
  Event e;
  e.synced = TimeSync::synced();
  e.time = e.synced ? time(nullptr) : static_cast<time_t>(millis() / 1000);
  e.outlet = static_cast<uint8_t>(outlet);
  e.on = on;
  e.source = source;

  xSemaphoreTake(lock, portMAX_DELAY);
  ring[head] = e;
  head = (head + 1) % kRingSize;
  if (count < kRingSize) count++;
  total++;
  xSemaphoreGive(lock);

  // Drop rather than block if ThingsBoard is offline and the queue is full; the
  // local ring still has the event.
  xQueueSend(tbQueue, &e, 0);
  Serial.printf("Event: out%d %s (%s)\n", outlet + 1, on ? "ON" : "OFF", sourceName(source));
}

size_t latest(Event* out, size_t max) {
  xSemaphoreTake(lock, portMAX_DELAY);
  size_t n = count < max ? count : max;
  for (size_t i = 0; i < n; i++) {
    out[i] = ring[(head + kRingSize - 1 - i) % kRingSize];
  }
  xSemaphoreGive(lock);
  return n;
}

uint32_t seq() { return total; }

bool nextUnsent(Event* out) { return xQueueReceive(tbQueue, out, 0) == pdTRUE; }

}  // namespace Events
