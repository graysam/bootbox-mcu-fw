#ifndef ASYNCWEBSYNCHRONIZATION_H_
#define ASYNCWEBSYNCHRONIZATION_H_

// Synchronisation is only available on ESP32, as the ESP8266 isn't using FreeRTOS by default

#include <ESPAsyncWebServer.h>

#if defined(ESP32) || (defined(LIBRETINY) && LT_HAS_FREERTOS)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// This is the ESP32 version of the Sync Lock, using the FreeRTOS Semaphore
class AsyncWebLock
{
private:
  SemaphoreHandle_t _lock;
  mutable TaskHandle_t _lockedBy;

public:
  AsyncWebLock() {
    _lock = xSemaphoreCreateBinary();
    _lockedBy = nullptr;
    xSemaphoreGive(_lock);
  }

  ~AsyncWebLock() {
    vSemaphoreDelete(_lock);
  }

  bool lock() const {
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (_lockedBy != current) {
      xSemaphoreTake(_lock, portMAX_DELAY);
      _lockedBy = current;
      return true;
    }
    return false;
  }

  void unlock() const {
    _lockedBy = nullptr;
    xSemaphoreGive(_lock);
  }
};

#else

// This is the 8266 version of the Sync Lock which is currently unimplemented
class AsyncWebLock
{

public:
  AsyncWebLock() {
  }

  ~AsyncWebLock() {
  }

  bool lock() const {
    return false;
  }

  void unlock() const {
  }
};
#endif

class AsyncWebLockGuard
{
private:
  const AsyncWebLock *_lock;

public:
  AsyncWebLockGuard(const AsyncWebLock &l) {
    if (l.lock()) {
      _lock = &l;
    } else {
      _lock = NULL;
    }
  }

  ~AsyncWebLockGuard() {
    if (_lock) {
      _lock->unlock();
    }
  }
};

#endif // ASYNCWEBSYNCHRONIZATION_H_
