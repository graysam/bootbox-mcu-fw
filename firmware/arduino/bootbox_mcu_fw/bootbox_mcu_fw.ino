#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <esp32-hal-ledc.h>
#include <esp_system.h>
#include <vector>
#include <deque>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <functional>

#include "config.h"
#include "settings.h"

// ---- WiFi AP config ----
static const char* AP_SSID = "BOOTBOXDSP"; // Open AP (SSID only)

// ---- Web server and WebSocket ----
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ---- State model (initial, expanded later) ----
struct RuntimeState {
  float temp1 = NAN;
  float temp2 = NAN;
  uint16_t fan_rpm = 0;         // combined/representative RPM
  uint8_t fan_target_pct = 0;   // 0-100
};

RuntimeState state;
Settings settings;
Preferences prefs;

static ThermistorParams thermistor_params[2] = {THERMISTOR1_PARAMS, THERMISTOR2_PARAMS};

static constexpr size_t FAN_SLOT_COUNT = sizeof(FAN_CTRL_PINS) / sizeof(FAN_CTRL_PINS[0]);
static const char FW_VERSION[] = "dev";
static const char FW_BUILD[] = __DATE__ " " __TIME__;
static char CHIP_LABEL[32] = "unknown";
static char RESET_REASON_LABEL[32] = "unknown";
static uint32_t boot_count = 0;
static size_t fs_total_bytes = 0;
static bool settings_dirty = false;
static uint32_t settings_dirty_since = 0;
static constexpr uint32_t SETTINGS_SAVE_DELAY_MS = 1500;
static bool littlefs_ready = false;
static esp_reset_reason_t last_reset_reason = ESP_RST_UNKNOWN;

// ---- DSP manager data ----
static const char DSP_DIR[] = "/dsp";
static const char DSP_PRESET_DIR[] = "/dsp-presets";
static constexpr size_t DSP_MAX_CONTROLS = 48;
static constexpr uint32_t DSP_SAVE_DELAY_MS = 1200;

struct DspControlSpec {
  String id;
  String label;
  String type;      // knob, slider, toggle, select
  String unit;
  float min_v = 0.0f;
  float max_v = 1.0f;
  float step = 0.1f;
  float default_v = NAN;
  uint32_t address = 0;
  uint8_t bytes = 4;
  String format = "float"; // float, fixed, etc.
};

struct DspValueEntry {
  String id;
  float value = NAN;
};

static std::vector<DspControlSpec> dsp_controls;
static std::vector<DspValueEntry> dsp_values;
static String dsp_active_bundle;
static bool dsp_schema_ready = false;
static bool dsp_values_dirty = false;
static uint32_t dsp_values_dirty_since = 0;

struct DspPresetInfo {
  String name;
  size_t size = 0;
};

static void ensureDspDirectories();
static void dspInit();
static bool loadDspBundle(const String& name, bool persist);
static void listDspBundles(JsonArray arr);
static void listDspPresets(const String& bundle, JsonArray arr);
static bool handleDspControlUpdate(JsonObject data);
static void markDspValuesDirty();
static void flushPendingDspSaves();
static bool savePreset(const String& bundle, const String& presetName);
static bool applyPreset(const String& bundle, const String& presetName);
static bool deletePreset(const String& bundle, const String& presetName);
static bool deleteBundle(const String& name);
static bool renameBundle(const String& oldName, const String& newName);
static bool pushBundleToDsp(const String& name);

static inline bool fanSlotEnabled(size_t idx) {
  return idx < FAN_SLOT_COUNT && FAN_CTRL_PINS[idx] >= 0 && FAN_PWM_CHANNELS[idx] >= 0;
}

static uint8_t activeFanCount() {
  uint8_t count = 0;
  for (size_t i = 0; i < FAN_SLOT_COUNT; ++i) {
    if (fanSlotEnabled(i)) ++count;
  }
  return count;
}

static inline float clampf(float value, float min_v, float max_v) {
  if (value < min_v) return min_v;
  if (value > max_v) return max_v;
  return value;
}

static inline void markSettingsDirty() {
  settings_dirty = true;
  settings_dirty_since = millis();
}

static void fillSystemInfo(JsonObject obj);
static const char* resetReasonToStr(esp_reset_reason_t reason);
static void populateDspState(JsonObject obj);
static void refreshThermistorParams();
static AsyncJsonResponse* createThermStatusResponse(bool ok, const String& message, int statusCode = 200);

struct ThermCalSession {
  bool active = false;
  uint8_t channel = 0; // 0-based
  bool has_low = false;
  bool has_high = false;
  float low_actual_c = NAN;
  float high_actual_c = NAN;
  int low_adc = 0;
  int high_adc = 0;
};

static ThermCalSession therm_cal_session;

static constexpr float THERMAL_CRITICAL_MARGIN_C = 12.0f;

namespace StatusLed {

struct Step {
  uint16_t duration_ms;
  uint8_t level;
};

struct Pattern {
  const Step* steps;
  uint8_t count;
};

enum class Status : uint8_t {
  Boot,
  SystemOk,
  SystemRunningCheckLogs,
  GeneralError,
  ThermalError,
  NetworkError,
  BluetoothError,
  DspCommError,
  CheckDsp,
  CriticalError
};

static constexpr int PIN = STATUS_LED_PIN;
static constexpr bool ACTIVE_HIGH = STATUS_LED_ACTIVE_HIGH;

static const Step PATTERN_BOOT[] = {{100, 1}, {100, 0}};
static const Step PATTERN_OK[] = {{50, 1}, {1950, 0}};
static const Step PATTERN_CHECK_LOGS[] = {{100, 1}, {100, 0}, {100, 1}, {600, 0}};
static const Step PATTERN_GENERAL_ERROR[] = {{180, 1}, {120, 0}, {180, 1}, {900, 0}};
static const Step PATTERN_THERMAL[] = {{220, 1}, {120, 0}, {220, 1}, {120, 0}, {220, 1}, {800, 0}};
static const Step PATTERN_NETWORK[] = {{420, 1}, {160, 0}, {120, 1}, {160, 0}, {420, 1}, {900, 0}};
static const Step PATTERN_BLUETOOTH[] = {{100, 1}, {100, 0}, {100, 1}, {100, 0}, {100, 1}, {100, 0}, {100, 1}, {700, 0}};
static const Step PATTERN_DSP_COMM[] = {{360, 1}, {120, 0}, {360, 1}, {120, 0}, {120, 1}, {900, 0}};
static const Step PATTERN_CHECK_DSP[] = {{120, 1}, {200, 0}, {420, 1}, {1000, 0}};
static const Step PATTERN_CRITICAL[] = {{900, 1}, {150, 0}};

static const Pattern PATTERNS[] = {
  {PATTERN_BOOT, sizeof(PATTERN_BOOT) / sizeof(Step)},
  {PATTERN_OK, sizeof(PATTERN_OK) / sizeof(Step)},
  {PATTERN_CHECK_LOGS, sizeof(PATTERN_CHECK_LOGS) / sizeof(Step)},
  {PATTERN_GENERAL_ERROR, sizeof(PATTERN_GENERAL_ERROR) / sizeof(Step)},
  {PATTERN_THERMAL, sizeof(PATTERN_THERMAL) / sizeof(Step)},
  {PATTERN_NETWORK, sizeof(PATTERN_NETWORK) / sizeof(Step)},
  {PATTERN_BLUETOOTH, sizeof(PATTERN_BLUETOOTH) / sizeof(Step)},
  {PATTERN_DSP_COMM, sizeof(PATTERN_DSP_COMM) / sizeof(Step)},
  {PATTERN_CHECK_DSP, sizeof(PATTERN_CHECK_DSP) / sizeof(Step)},
  {PATTERN_CRITICAL, sizeof(PATTERN_CRITICAL) / sizeof(Step)}
};

static Status current = Status::Boot;
static Status defaultStatus = Status::SystemOk;
static const Pattern* activePattern = nullptr;
static uint8_t stepIndex = 0;
static uint32_t stepStartMs = 0;
static bool ready = false;

inline void write(uint8_t level) {
  if (!ready) return;
  const uint8_t high = ACTIVE_HIGH ? HIGH : LOW;
  const uint8_t low = ACTIVE_HIGH ? LOW : HIGH;
  digitalWrite(PIN, level ? high : low);
}

void apply(const Pattern& pattern) {
  activePattern = &pattern;
  stepIndex = 0;
  stepStartMs = millis();
  write(pattern.steps[stepIndex].level);
}

const Pattern& patternFor(Status status) {
  return PATTERNS[static_cast<uint8_t>(status)];
}

uint8_t severity(Status status) {
  switch (status) {
    case Status::SystemOk: return 1;
    case Status::SystemRunningCheckLogs: return 2;
    case Status::GeneralError: return 3;
    case Status::CheckDsp: return 3;
    case Status::NetworkError: return 4;
    case Status::BluetoothError: return 4;
    case Status::DspCommError: return 5;
    case Status::ThermalError: return 6;
    case Status::CriticalError: return 7;
    case Status::Boot: return 0;
  }
  return 1;
}

Status higher(Status currentStatus, Status candidate) {
  return (severity(candidate) > severity(currentStatus)) ? candidate : currentStatus;
}

void set(Status status, bool force = false) {
  if (!ready) return;
  if (!force && status == current) return;
  current = status;
  apply(patternFor(status));
}

void setDefault(Status status) {
  defaultStatus = status;
}

Status getDefault() {
  return defaultStatus;
}

void update() {
  if (!ready || !activePattern || activePattern->count == 0) return;
  const uint32_t now = millis();
  const Step& step = activePattern->steps[stepIndex];
  if (now - stepStartMs >= step.duration_ms) {
    stepIndex = (stepIndex + 1) % activePattern->count;
    stepStartMs = now;
    write(activePattern->steps[stepIndex].level);
  }
}

void init() {
  pinMode(PIN, OUTPUT);
  ready = true;
  set(Status::Boot, true);
}

Status higherSeverity(Status currentStatus, Status candidate) {
  return higher(currentStatus, candidate);
}

}

static bool wifiInitOk = false;

static bool thermalFaultActive() {
  const float limit = settings.setpoint2_c + THERMAL_CRITICAL_MARGIN_C;
  if (!std::isnan(state.temp1) && state.temp1 > limit) return true;
  if (!std::isnan(state.temp2) && state.temp2 > limit) return true;
  return false;
}

// Simple WS sequence counter for optional acks
static uint32_t msg_seq = 1;

// ---- Simple device log ring buffer ----
static std::deque<String> logs;
static const size_t LOG_MAX = 100;
static void addLog(const String& s) {
  if (logs.size() >= LOG_MAX) logs.pop_front();
  logs.push_back(s);
}

static void wsSendReliable(AsyncWebSocketClient* c, JsonDocument& doc) {
  if (!c) return;
  doc["id"] = msg_seq++;
  String out;
  serializeJson(doc, out);
  c->text(out);
}

static void wsBroadcastState() {
  StaticJsonDocument<1280> doc;
  doc["type"] = "state";
  auto data = doc.createNestedObject("data");
  if (isnan(state.temp1)) {
    data["temp1"] = nullptr;
  } else {
    data["temp1"] = state.temp1;
  }
  if (isnan(state.temp2)) {
    data["temp2"] = nullptr;
  } else {
    data["temp2"] = state.temp2;
  }
  data["fan_rpm"] = state.fan_rpm;
  data["fan_target_pct"] = state.fan_target_pct;
  data["fan_count"] = activeFanCount();
  data["pid_enabled"] = settings.pid_enabled;
  data["sp1"] = settings.setpoint1_c;
  data["sp2"] = settings.setpoint2_c;
  data["kp"] = settings.pid_kp;
  data["ki"] = settings.pid_ki;
  data["kd"] = settings.pid_kd;

  auto dsp = doc.createNestedObject("dsp");
  populateDspState(dsp);

  auto sys = doc.createNestedObject("sys");
  fillSystemInfo(sys);

  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}

static void handleAck(uint32_t /*id*/) {}

static void wsResendTick() {}

// ---- WebSocket event handler ----
static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    wsBroadcastState();
    return;
  }
  if (type == WS_EVT_DISCONNECT) {
    return;
  }
  if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (!info->final || info->opcode != WS_TEXT) return;
    // parse payload
    String msg;
    msg.reserve(len);
    for (size_t i = 0; i < len; i++) msg += (char)data[i];

    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, msg);
    if (err) {
      StaticJsonDocument<192> errDoc;
      errDoc["type"] = "error";
      errDoc["error"] = String("bad_json: ") + err.c_str();
      String out; serializeJson(errDoc, out);
      client->text(out);
      return;
    }

    const char* mtype = doc["type"] | "";
    if (strcmp(mtype, "ack") == 0) {
      handleAck(doc["id"].as<uint32_t>());
      return;
    }

    if (strcmp(mtype, "ping") == 0) {
      StaticJsonDocument<128> pong;
      pong["type"] = "pong";
      wsSendReliable(client, pong);
      return;
    }

    if (strcmp(mtype, "get_state") == 0) {
      wsBroadcastState();
      return;
    }

    if (strcmp(mtype, "set_settings") == 0) {
      JsonObject data = doc["data"];
      if (data.containsKey("pid_enabled")) settings.pid_enabled = data["pid_enabled"].as<bool>();
      if (data.containsKey("sp1")) settings.setpoint1_c = data["sp1"].as<float>();
      if (data.containsKey("sp2")) settings.setpoint2_c = data["sp2"].as<float>();
      if (data.containsKey("fan_manual_pct")) settings.fan_manual_pct = data["fan_manual_pct"].as<uint8_t>();
      if (data.containsKey("kp")) settings.pid_kp = data["kp"].as<float>();
      if (data.containsKey("ki")) settings.pid_ki = data["ki"].as<float>();
      if (data.containsKey("kd")) settings.pid_kd = data["kd"].as<float>();
      markSettingsDirty();
      addLog("thermal settings updated");
      wsBroadcastState();
      return;
    }

    if (strcmp(mtype, "set_dsp") == 0) {
      JsonObject data = doc["data"];
      if (!data.isNull() && handleDspControlUpdate(data)) {
        wsBroadcastState();
      }
      return;
    }

    if (strcmp(mtype, "get_logs") == 0) {
      StaticJsonDocument<2048> out;
      out["type"] = "logs";
      auto arr = out.createNestedArray("data");
      for (auto &l : logs) arr.add(l);
      String s; serializeJson(out, s);
      client->text(s);
      return;
    }

    // unknown -> error
    StaticJsonDocument<160> errDoc;
    errDoc["type"] = "error";
    errDoc["error"] = "unknown_type";
    errDoc["got"] = mtype;
    String out; serializeJson(errDoc, out);
    client->text(out);
  }
}

// ---- HTTP Handlers ----
static void registerHttpRoutes() {
  // Serve static files from LittleFS. Default file index.html.
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* req){
    StaticJsonDocument<1280> doc;
    if (isnan(state.temp1)) {
      doc["temp1"] = nullptr;
    } else {
      doc["temp1"] = state.temp1;
    }
    if (isnan(state.temp2)) {
      doc["temp2"] = nullptr;
    } else {
      doc["temp2"] = state.temp2;
    }
    doc["fan_rpm"] = state.fan_rpm;
    doc["fan_target_pct"] = state.fan_target_pct;
    doc["fan_count"] = activeFanCount();
    doc["pid_enabled"] = settings.pid_enabled;
    doc["sp1"] = settings.setpoint1_c;
    doc["sp2"] = settings.setpoint2_c;
    auto dsp = doc.createNestedObject("dsp");
    populateDspState(dsp);
    auto sys = doc.createNestedObject("sys");
    fillSystemInfo(sys);
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/dsp/schema", HTTP_GET, [](AsyncWebServerRequest* req){
    StaticJsonDocument<2048> doc;
    doc["active"] = dsp_active_bundle;
    auto controls = doc.createNestedArray("controls");
    for (const auto& spec : dsp_controls) {
      auto c = controls.createNestedObject();
      c["id"] = spec.id;
      c["label"] = spec.label;
      c["type"] = spec.type;
      c["unit"] = spec.unit;
      c["min"] = spec.min_v;
      c["max"] = spec.max_v;
      c["step"] = spec.step;
      if (!std::isnan(spec.default_v)) c["default"] = spec.default_v;
      c["format"] = spec.format;
    }
    auto values = doc.createNestedObject("values");
    for (const auto& entry : dsp_values) {
      values[entry.id] = entry.value;
    }
    auto presets = doc.createNestedArray("presets");
    listDspPresets(dsp_active_bundle, presets);
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/dsp/bundles", HTTP_GET, [](AsyncWebServerRequest* req){
    StaticJsonDocument<1536> doc;
    doc["active"] = dsp_active_bundle;
    auto arr = doc.createNestedArray("bundles");
    listDspBundles(arr);
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  auto* dspActionHandler = new AsyncCallbackJsonWebHandler("/api/dsp/action",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      auto sendResponse = [&](bool ok, const String& message, int status){
        StaticJsonDocument<256> resp;
        resp["ok"] = ok;
        if (message.length()) resp["message"] = message;
        String out; serializeJson(resp, out);
        req->send(status, "application/json", out);
      };

      if (!json.is<JsonObject>()) {
        sendResponse(false, "invalid_payload", 400);
        return;
      }

      JsonObject obj = json.as<JsonObject>();
      const char* action = obj["action"] | "";
      if (!action || !action[0]) {
        sendResponse(false, "missing_action", 400);
        return;
      }

      auto bundleFromPayload = [&](const char* key) -> String {
        if (!obj.containsKey(key)) return dsp_active_bundle;
        const char* raw = obj[key];
        if (!raw) return dsp_active_bundle;
        return String(raw);
      };

      String message;

      if (strcmp(action, "select_bundle") == 0) {
        String name = bundleFromPayload("name");
        if (!name.length()) {
          sendResponse(false, "bundle_required", 400);
          return;
        }
        if (loadDspBundle(name, true)) {
          addLog(String("dsp bundle set to ") + name);
          wsBroadcastState();
          sendResponse(true, "bundle_selected", 200);
        } else {
          sendResponse(false, "bundle_not_found", 404);
        }
        return;
      }

      if (strcmp(action, "delete_bundle") == 0) {
        String name = bundleFromPayload("name");
        if (!name.length()) {
          sendResponse(false, "bundle_required", 400);
          return;
        }
        if (deleteBundle(name)) {
          addLog(String("dsp bundle deleted: ") + name);
          if (name == dsp_active_bundle) {
            loadDspBundle("", true);
            wsBroadcastState();
          }
          sendResponse(true, "bundle_deleted", 200);
        } else {
          sendResponse(false, "delete_failed", 500);
        }
        return;
      }

      if (strcmp(action, "rename_bundle") == 0) {
        String name = bundleFromPayload("name");
        const char* rawNew = obj["new_name"] | "";
        String newName = rawNew;
        if (!name.length() || !newName.length()) {
          sendResponse(false, "name_required", 400);
          return;
        }
        if (renameBundle(name, newName)) {
          addLog(String("dsp bundle renamed: ") + name + " -> " + newName);
          if (name == dsp_active_bundle) {
            loadDspBundle(newName, true);
            wsBroadcastState();
          }
          sendResponse(true, "bundle_renamed", 200);
        } else {
          sendResponse(false, "rename_failed", 500);
        }
        return;
      }

      if (strcmp(action, "push_bundle") == 0) {
        String name = bundleFromPayload("name");
        if (!name.length()) {
          sendResponse(false, "bundle_required", 400);
          return;
        }
        if (pushBundleToDsp(name)) {
          sendResponse(true, "bundle_pushed", 200);
        } else {
          sendResponse(false, "push_failed", 500);
        }
        return;
      }

      if (strcmp(action, "save_preset") == 0) {
        String bundle = bundleFromPayload("bundle");
        const char* rawPreset = obj["preset"] | "";
        String preset = rawPreset;
        if (!bundle.length() || !preset.length()) {
          sendResponse(false, "preset_required", 400);
          return;
        }
        if (savePreset(bundle, preset)) {
          addLog(String("preset saved: ") + bundle + "/" + preset);
          sendResponse(true, "preset_saved", 200);
        } else {
          sendResponse(false, "preset_save_failed", 500);
        }
        return;
      }

      if (strcmp(action, "load_preset") == 0) {
        String bundle = bundleFromPayload("bundle");
        const char* rawPreset = obj["preset"] | "";
        String preset = rawPreset;
        if (!bundle.length() || !preset.length()) {
          sendResponse(false, "preset_required", 400);
          return;
        }
        if (applyPreset(bundle, preset)) {
          addLog(String("preset loaded: ") + bundle + "/" + preset);
          wsBroadcastState();
          sendResponse(true, "preset_loaded", 200);
        } else {
          sendResponse(false, "preset_load_failed", 500);
        }
        return;
      }

      if (strcmp(action, "delete_preset") == 0) {
        String bundle = bundleFromPayload("bundle");
        const char* rawPreset = obj["preset"] | "";
        String preset = rawPreset;
        if (!bundle.length() || !preset.length()) {
          sendResponse(false, "preset_required", 400);
          return;
        }
        if (deletePreset(bundle, preset)) {
          addLog(String("preset deleted: ") + bundle + "/" + preset);
          sendResponse(true, "preset_deleted", 200);
        } else {
          sendResponse(false, "preset_delete_failed", 500);
        }
        return;
      }

      sendResponse(false, "unknown_action", 400);
    });
  server.addHandler(dspActionHandler);

  // Upload SigmaStudio bundles (program/interface)
  server.on("/api/upload/adau", HTTP_POST,
            [](AsyncWebServerRequest* req){
              StaticJsonDocument<128> resp;
              resp["ok"] = true;
              String out; serializeJson(resp, out);
              req->send(200, "application/json", out);
            },
            [](AsyncWebServerRequest* req, String filename, size_t index, uint8_t* data, size_t len, bool final){
              struct UploadContext {
                File file;
                String bundle;
                String kind;
                String path;
              };

              auto* ctx = reinterpret_cast<UploadContext*>(req->_tempObject);
              if (index == 0) {
                ctx = new UploadContext();
                req->_tempObject = ctx;
                String bundle = "default";
                String kind = "program";
                if (req->hasParam("bundle")) bundle = req->getParam("bundle")->value();
                if (req->hasParam("kind")) kind = req->getParam("kind")->value();
                if (!bundle.length()) bundle = "default";
                if (!kind.length()) kind = "program";
                ctx->bundle = bundle;
                ctx->kind = kind;
                String dir = String(DSP_DIR) + "/" + bundle;
                LittleFS.mkdir(DSP_DIR);
                LittleFS.mkdir(dir);
                String target = dir + "/";
                if (kind == "interface") {
                  target += "interface.xml";
                } else {
                  target += "program.bin";
                }
                ctx->path = target;
                ctx->file = LittleFS.open(target, FILE_WRITE);
                addLog(String("upload start ") + target);
              }

              ctx = reinterpret_cast<UploadContext*>(req->_tempObject);
              if (ctx && ctx->file) {
                ctx->file.write(data, len);
              }

              if (final && ctx) {
                if (ctx->file) ctx->file.close();
                addLog(String("upload done ") + ctx->path);
                if (ctx->bundle == dsp_active_bundle && ctx->kind == "interface") {
                  loadDspBundle(dsp_active_bundle, false);
                  wsBroadcastState();
                }
                delete ctx;
                req->_tempObject = nullptr;
              }
  });

  server.on("/api/therm/calibration", HTTP_GET, [](AsyncWebServerRequest* req){
    auto* resp = createThermStatusResponse(true, "");
    req->send(resp);
  });

  auto* thermHandler = new AsyncCallbackJsonWebHandler("/api/therm/calibration",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!json.is<JsonObject>()) {
        auto* resp = createThermStatusResponse(false, "invalid_payload", 400);
        req->send(resp);
        return;
      }

      JsonObject obj = json.as<JsonObject>();
      const char* action = obj["action"] | "";
      if (!action || action[0] == '\0') {
        auto* resp = createThermStatusResponse(false, "missing_action", 400);
        req->send(resp);
        return;
      }

      int channel = obj.containsKey("channel") ? obj["channel"].as<int>() :
                    (therm_cal_session.active ? static_cast<int>(therm_cal_session.channel + 1) : 1);
      if (channel < 1 || channel > 2) {
        auto* resp = createThermStatusResponse(false, "invalid_channel", 400);
        req->send(resp);
        return;
      }
      const size_t idx = static_cast<size_t>(channel - 1);
      auto sendError = [&](const String& msg, int code = 400) {
        auto* resp = createThermStatusResponse(false, msg, code);
        req->send(resp);
      };

      if (strcmp(action, "start") == 0) {
        therm_cal_session = {};
        therm_cal_session.active = true;
        therm_cal_session.channel = static_cast<uint8_t>(idx);
        addLog(String("therm") + channel + " calibration start");
        auto* resp = createThermStatusResponse(true, String("Calibration started for channel ") + channel);
        req->send(resp);
        wsBroadcastState();
        return;
      }

      if (strcmp(action, "cancel") == 0) {
        if (therm_cal_session.active && therm_cal_session.channel == idx) {
          therm_cal_session = {};
        }
        auto* resp = createThermStatusResponse(true, "Calibration session cancelled");
        req->send(resp);
        wsBroadcastState();
        return;
      }

      if (strcmp(action, "status") == 0) {
        auto* resp = createThermStatusResponse(true, "");
        req->send(resp);
        return;
      }

      if (strcmp(action, "capture") == 0) {
        if (!(therm_cal_session.active && therm_cal_session.channel == idx)) {
          sendError("no_active_session");
          return;
        }
        const char* point = obj["point"] | "";
        if (!point || point[0] == '\0') {
          sendError("missing_point");
          return;
        }
        bool is_low = strcmp(point, "low") == 0;
        bool is_high = strcmp(point, "high") == 0;
        if (!is_low && !is_high) {
          sendError("point_must_be_low_or_high");
          return;
        }
        if (!obj.containsKey("actual_c")) {
          sendError("missing_actual_c");
          return;
        }
        float actual_c = obj["actual_c"].as<float>();
        if (!std::isfinite(actual_c)) {
          sendError("invalid_actual_c");
          return;
        }
        int adc = readThermAdc(static_cast<uint8_t>(idx));
        if (adc < 0) {
          sendError("adc_unavailable");
          return;
        }
        if (is_low) {
          therm_cal_session.has_low = true;
          therm_cal_session.low_actual_c = actual_c;
          therm_cal_session.low_adc = adc;
        } else {
          therm_cal_session.has_high = true;
          therm_cal_session.high_actual_c = actual_c;
          therm_cal_session.high_adc = adc;
        }
        auto* resp = createThermStatusResponse(true,
          String("Captured ") + point + " point for channel " + channel +
          String(" (ADC ") + adc + ")");
        req->send(resp);
        wsBroadcastState();
        return;
      }

      if (strcmp(action, "solve") == 0) {
        if (!(therm_cal_session.active && therm_cal_session.channel == idx)) {
          sendError("no_active_session");
          return;
        }
        if (!therm_cal_session.has_low || !therm_cal_session.has_high) {
          sendError("need_low_and_high_points");
          return;
        }
        const auto& base = baseThermParams(idx);
        float nominal_c = base.nominal_temperature_c;
        if (obj.containsKey("nominal_c")) {
          float candidate = obj["nominal_c"].as<float>();
          if (std::isfinite(candidate)) nominal_c = candidate;
        }
        const float t_low_k = therm_cal_session.low_actual_c + 273.15f;
        const float t_high_k = therm_cal_session.high_actual_c + 273.15f;
        const float t_nom_k = nominal_c + 273.15f;
        if (t_low_k <= 0.0f || t_high_k <= 0.0f || t_nom_k <= 0.0f) {
          sendError("invalid_temperature_values");
          return;
        }
        if (fabsf(t_low_k - t_high_k) < 0.05f) {
          sendError("points_too_close");
          return;
        }
        float r_low = adcToResistance(therm_cal_session.low_adc, base.series_resistance_ohms);
        float r_high = adcToResistance(therm_cal_session.high_adc, base.series_resistance_ohms);
        if (!std::isfinite(r_low) || !std::isfinite(r_high) || r_low <= 0.0f || r_high <= 0.0f) {
          sendError("invalid_adc_samples");
          return;
        }
        const float ln_ratio = logf(r_low / r_high);
        const float inv_delta = (1.0f / t_low_k) - (1.0f / t_high_k);
        if (!std::isfinite(ln_ratio) || fabsf(inv_delta) < 1e-8f) {
          sendError("calculation_error");
          return;
        }
        const float beta = ln_ratio / inv_delta;
        if (!std::isfinite(beta) || beta <= 0.0f) {
          sendError("beta_invalid");
          return;
        }
        const float exponent = beta * ((1.0f / t_low_k) - (1.0f / t_nom_k));
        const float r_nom = r_low * expf(exponent);
        if (!std::isfinite(r_nom) || r_nom <= 0.0f) {
          sendError("nominal_invalid");
          return;
        }

        settings.thermistors[idx].valid = true;
        settings.thermistors[idx].nominal_ohms = r_nom;
        settings.thermistors[idx].beta = beta;
        markSettingsDirty();
        refreshThermistorParams();
        therm_cal_session = {};
        addLog(String("therm") + channel + " calibration saved");
        wsBroadcastState();

        auto* resp = createThermStatusResponse(true, "Calibration solved");
        JsonObject root = resp->getRoot();
        auto cal = root.createNestedObject("calibration");
        cal["channel"] = channel;
        cal["nominal_ohms"] = r_nom;
        cal["beta"] = beta;
        cal["nominal_c"] = nominal_c;
        req->send(resp);
        return;
      }

      if (strcmp(action, "clear") == 0) {
        const auto& base = baseThermParams(idx);
        settings.thermistors[idx].valid = false;
        settings.thermistors[idx].nominal_ohms = base.nominal_resistance_ohms;
        settings.thermistors[idx].beta = base.beta_coefficient;
        refreshThermistorParams();
        markSettingsDirty();
        if (therm_cal_session.active && therm_cal_session.channel == idx) therm_cal_session = {};
        addLog(String("therm") + channel + " calibration cleared");
        wsBroadcastState();
        auto* resp = createThermStatusResponse(true, String("Calibration cleared for channel ") + channel);
        req->send(resp);
        return;
      }

      sendError("unknown_action");
      return;
    });
  server.addHandler(thermHandler);

  server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* req){
    StaticJsonDocument<2048> out;
    auto arr = out.createNestedArray("logs");
    for (auto &l : logs) arr.add(l);
    String s; serializeJson(out, s);
    req->send(200, "application/json", s);
  });
}

// ---- Setup helpers ----
static bool initWiFiAP() {
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID); // Open AP (no password)
  if (!ok) {
    // Retry once quickly
    delay(500);
    ok = WiFi.softAP(AP_SSID);
  }
  return ok;
}

// ---- Sensors & Control ----
static const ThermistorParams& baseThermParams(size_t idx) {
  return (idx == 0) ? THERMISTOR1_PARAMS : THERMISTOR2_PARAMS;
}

static void refreshThermistorParams() {
  for (size_t i = 0; i < 2; ++i) {
    thermistor_params[i] = baseThermParams(i);
    if (settings.thermistors[i].valid) {
      thermistor_params[i].nominal_resistance_ohms = settings.thermistors[i].nominal_ohms;
      thermistor_params[i].beta_coefficient = settings.thermistors[i].beta;
    }
  }
}

static float adcToResistance(int adc, float series_resistance) {
  if (adc <= 0 || adc >= ADC_FULL_SCALE) return NAN;
  const float adc_f = static_cast<float>(adc);
  const float denom = (ADC_FULL_SCALE - adc_f);
  if (denom <= 0.0f) return NAN;
  const float ratio = adc_f / denom;
  if (ratio <= 0.0f) return NAN;
  return series_resistance * ratio;
}

static int readThermAdc(uint8_t channel) {
  const int pin = (channel == 0) ? PIN_THERM1 : PIN_THERM2;
  if (pin < 0) return -1;
  constexpr uint8_t samples = 12;
  uint32_t acc = 0;
  for (uint8_t i = 0; i < samples; ++i) {
    acc += analogRead(pin);
    delayMicroseconds(150);
  }
  return static_cast<int>(acc / samples);
}

static float thermistorAdcToC(int adc, const ThermistorParams& params) {
  if (adc <= 0 || adc >= ADC_FULL_SCALE) return NAN;
  const float adc_f = static_cast<float>(adc);
  const float ratio = adc_f / (ADC_FULL_SCALE - adc_f);
  if (ratio <= 0.0f) return NAN;
  const float resistance = params.series_resistance_ohms * ratio;
  if (resistance <= 0.0f || params.nominal_resistance_ohms <= 0.0f || params.beta_coefficient <= 0.0f) return NAN;

  const float ln_ratio = std::log(resistance / params.nominal_resistance_ohms);
  const float inverse_T = (1.0f / (params.nominal_temperature_c + 273.15f)) + (ln_ratio / params.beta_coefficient);
  if (inverse_T <= 0.0f) return NAN;
  const float temperature_k = 1.0f / inverse_T;
  return temperature_k - 273.15f;
}

static void sampleSensors() {
  // Read analog temps; if pins unconnected, values may float
  int a1 = analogRead(PIN_THERM1);
  int a2 = analogRead(PIN_THERM2);
  state.temp1 = thermistorAdcToC(a1, thermistor_params[0]);
  state.temp2 = thermistorAdcToC(a2, thermistor_params[1]);
  // TODO: tach read via PCNT or RMT; set to 0 for now
  if (THERMISTOR_DEBUG_LOG) {
    static uint32_t last_report = 0;
    const uint32_t now = millis();
    if (now - last_report > THERMISTOR_DEBUG_INTERVAL_MS) {
      last_report = now;
      Serial.printf("Therm ADC: ch1=%d temp=%.2fC, ch2=%d temp=%.2fC\n", a1, state.temp1, a2, state.temp2);
    }
  }
}

static void populateThermStatus(JsonObject obj) {
  auto therms = obj.createNestedArray("thermistors");
  for (size_t i = 0; i < 2; ++i) {
    const auto& base = baseThermParams(i);
    const bool calibrated = settings.thermistors[i].valid;
    auto therm = therms.createNestedObject();
    therm["channel"] = static_cast<uint8_t>(i + 1);
    therm["series_ohms"] = base.series_resistance_ohms;
    therm["nominal_ohms"] = calibrated ? settings.thermistors[i].nominal_ohms : base.nominal_resistance_ohms;
    therm["beta"] = calibrated ? settings.thermistors[i].beta : base.beta_coefficient;
    therm["default_nominal_ohms"] = base.nominal_resistance_ohms;
    therm["default_beta"] = base.beta_coefficient;
    therm["calibrated"] = calibrated;
  }

  auto session = obj.createNestedObject("therm_cal_session");
  session["active"] = therm_cal_session.active;
  if (therm_cal_session.active) {
    session["channel"] = static_cast<uint8_t>(therm_cal_session.channel + 1);
    session["has_low"] = therm_cal_session.has_low;
    session["has_high"] = therm_cal_session.has_high;
    if (therm_cal_session.has_low) {
      session["low_actual_c"] = therm_cal_session.low_actual_c;
      session["low_adc"] = therm_cal_session.low_adc;
    } else {
      session["low_actual_c"] = nullptr;
      session["low_adc"] = nullptr;
    }
    if (therm_cal_session.has_high) {
      session["high_actual_c"] = therm_cal_session.high_actual_c;
      session["high_adc"] = therm_cal_session.high_adc;
    } else {
      session["high_actual_c"] = nullptr;
      session["high_adc"] = nullptr;
    }
  }
}

static void fillSystemInfo(JsonObject obj) {
  obj["uptime_ms"] = millis();
  obj["free_heap"] = ESP.getFreeHeap();
  obj["heap_size"] = ESP.getHeapSize();
  obj["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
  obj["fw_version"] = FW_VERSION;
  obj["fw_build"] = FW_BUILD;
  obj["sdk"] = ESP.getSdkVersion();
  obj["chip"] = CHIP_LABEL;
  obj["chip_revision"] = ESP.getChipRevision();
  obj["reset_reason"] = RESET_REASON_LABEL;
  obj["boot_count"] = boot_count;
  obj["wifi_clients"] = WiFi.softAPgetStationNum();
  obj["ap_ip"] = WiFi.softAPIP().toString();
  if (littlefs_ready) {
    obj["fs_total"] = fs_total_bytes;
    obj["fs_used"] = LittleFS.usedBytes();
  } else {
    obj["fs_total"] = 0;
    obj["fs_used"] = 0;
  }
  populateThermStatus(obj);
}

// ---- DSP interface helpers ----

static DspControlSpec* findDspControl(const String& id) {
  for (auto& spec : dsp_controls) {
    if (spec.id == id) return &spec;
  }
  return nullptr;
}

static DspValueEntry* findDspValue(const String& id) {
  for (auto& entry : dsp_values) {
    if (entry.id == id) return &entry;
  }
  return nullptr;
}

static float getDspValue(const String& id) {
  auto* entry = findDspValue(id);
  if (!entry) return NAN;
  return entry->value;
}

static void setDspValueInternal(const String& id, float value, bool markDirtyFlag) {
  auto* entry = findDspValue(id);
  if (!entry) {
    DspValueEntry e;
    e.id = id;
    e.value = value;
    dsp_values.push_back(e);
  } else {
    entry->value = value;
  }
  if (markDirtyFlag) markDspValuesDirty();
}

static void loadDspValuesFromPrefs() {
  dsp_values.clear();
  String stored = prefs.getString("dspVals", "");
  if (!stored.length()) return;
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, stored) != DeserializationError::Ok) return;
  JsonObject obj = doc.as<JsonObject>();
  for (JsonPair kv : obj) {
    DspValueEntry entry;
    entry.id = kv.key().c_str();
    entry.value = kv.value().as<float>();
    dsp_values.push_back(entry);
  }
}

static void saveDspValuesNow() {
  DynamicJsonDocument doc(4096);
  for (const auto& entry : dsp_values) {
    doc[entry.id] = entry.value;
  }
  String out;
  serializeJson(doc, out);
  prefs.putString("dspVals", out);
  dsp_values_dirty = false;
}

static void markDspValuesDirty() {
  dsp_values_dirty = true;
  dsp_values_dirty_since = millis();
}

static void flushPendingDspSaves() {
  if (!dsp_values_dirty) return;
  if (millis() - dsp_values_dirty_since < DSP_SAVE_DELAY_MS) return;
  saveDspValuesNow();
}

static void ensureDspDirectories() {
  if (!LittleFS.exists(DSP_DIR)) LittleFS.mkdir(DSP_DIR);
  if (!LittleFS.exists(DSP_PRESET_DIR)) LittleFS.mkdir(DSP_PRESET_DIR);
}

static String pickFirstBundleName() {
  File root = LittleFS.open(DSP_DIR);
  if (!root) return "";
  File entry = root.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      String full = entry.name(); // e.g. /dsp/bundle
      entry.close();
      root.close();
      if (full.startsWith(DSP_DIR)) {
        String name = full.substring(strlen(DSP_DIR) + 1);
        return name;
      }
      return full;
    }
    entry = root.openNextFile();
  }
  root.close();
  return "";
}

static void syncDspValuesWithControls() {
  // remove stale entries
  dsp_values.erase(
    std::remove_if(dsp_values.begin(), dsp_values.end(),
      [](const DspValueEntry& entry){
        return findDspControl(entry.id) == nullptr;
      }),
    dsp_values.end());

  for (const auto& spec : dsp_controls) {
    if (findDspValue(spec.id)) continue;
    float initial = std::isnan(spec.default_v) ? ((spec.min_v + spec.max_v) * 0.5f) : spec.default_v;
    initial = clampf(initial, spec.min_v, spec.max_v);
    setDspValueInternal(spec.id, initial, false);
  }
}

static bool parseDspInterface(const String& path, std::vector<DspControlSpec>& out) {
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return false;
  String xml = f.readString();
  f.close();
  out.clear();
  int idx = 0;
  while ((idx = xml.indexOf("<control", idx)) != -1) {
    int end = xml.indexOf("/>", idx);
    if (end == -1) break;
    String segment = xml.substring(idx, end);
    idx = end + 2;

    DspControlSpec spec;
    spec.type = "slider";
    spec.unit = "";

    int pos = segment.indexOf(' ');
    while (pos != -1) {
      while (pos < segment.length() && isspace(segment[pos])) ++pos;
      if (pos >= segment.length()) break;
      int eq = segment.indexOf('=', pos);
      if (eq == -1) break;
      String key = segment.substring(pos, eq);
      key.trim();
      int quoteStart = segment.indexOf('"', eq);
      if (quoteStart == -1) break;
      int quoteEnd = segment.indexOf('"', quoteStart + 1);
      if (quoteEnd == -1) break;
      String value = segment.substring(quoteStart + 1, quoteEnd);
      pos = quoteEnd + 1;

      if (key == "id") spec.id = value;
      else if (key == "label") spec.label = value;
      else if (key == "type") spec.type = value;
      else if (key == "unit") spec.unit = value;
      else if (key == "min") spec.min_v = value.toFloat();
      else if (key == "max") spec.max_v = value.toFloat();
      else if (key == "step") spec.step = value.toFloat();
      else if (key == "default") spec.default_v = value.toFloat();
      else if (key == "address") spec.address = strtoul(value.c_str(), nullptr, 0);
      else if (key == "bytes") spec.bytes = static_cast<uint8_t>(value.toInt());
      else if (key == "format") spec.format = value;
    }

    if (!spec.id.length()) continue;
    if (!spec.label.length()) spec.label = spec.id;
    if (spec.step <= 0.0f) spec.step = 0.1f;
    if (spec.max_v <= spec.min_v) spec.max_v = spec.min_v + spec.step;
    if (spec.bytes == 0) spec.bytes = 4;
    out.push_back(spec);
    if (out.size() >= DSP_MAX_CONTROLS) break;
  }
  return !out.empty();
}

static void dspInit() {
  ensureDspDirectories();
  loadDspValuesFromPrefs();
  String saved = prefs.getString("dspBundle", "");
  if (!saved.length()) saved = pickFirstBundleName();
  if (saved.length()) {
    loadDspBundle(saved, false);
  } else {
    dsp_controls.clear();
    dsp_schema_ready = false;
  }
}

static bool loadDspBundle(const String& name, bool persist) {
  String target = name;
  if (!target.length()) target = pickFirstBundleName();
  if (!target.length()) {
    dsp_controls.clear();
    dsp_active_bundle = "";
    dsp_schema_ready = false;
    if (persist) prefs.putString("dspBundle", "");
    return false;
  }
  String dir = String(DSP_DIR) + "/" + target;
  String iface = dir + "/interface.xml";
  if (!LittleFS.exists(iface)) {
    return false;
  }
  std::vector<DspControlSpec> parsed;
  if (!parseDspInterface(iface, parsed)) {
    return false;
  }
  dsp_controls = parsed;
  syncDspValuesWithControls();
  dsp_active_bundle = target;
  dsp_schema_ready = true;
  if (persist) prefs.putString("dspBundle", target);
  return true;
}

static void populateDspState(JsonObject obj) {
  obj["bundle"] = dsp_active_bundle;
  obj["schema_ready"] = dsp_schema_ready;
  auto values = obj.createNestedObject("values");
  for (const auto& entry : dsp_values) {
    values[entry.id] = entry.value;
  }
}

static void listDspBundles(JsonArray arr) {
  File root = LittleFS.open(DSP_DIR);
  if (!root) return;
  File entry = root.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      String full = entry.name();
      String name = full.substring(strlen(DSP_DIR) + 1);
      JsonObject obj = arr.createNestedObject();
      obj["name"] = name;
      obj["active"] = (name == dsp_active_bundle);
      String base = String(DSP_DIR) + "/" + name + "/";
      obj["has_program"] = LittleFS.exists(base + "program.bin");
      obj["has_interface"] = LittleFS.exists(base + "interface.xml");
    }
    entry = root.openNextFile();
  }
  root.close();
}

static String presetPath(const String& bundle, const String& preset) {
  return String(DSP_PRESET_DIR) + "/" + bundle + "/" + preset + ".json";
}

static void listDspPresets(const String& bundle, JsonArray arr) {
  if (!bundle.length()) return;
  String dirPath = String(DSP_PRESET_DIR) + "/" + bundle;
  File dir = LittleFS.open(dirPath);
  if (!dir) return;
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String full = entry.name();
      int slash = full.lastIndexOf('/');
      String name = slash >= 0 ? full.substring(slash + 1) : full;
      if (name.endsWith(".json")) {
        name.remove(name.length() - 5);
        arr.add(name);
      }
    }
    entry = dir.openNextFile();
  }
  dir.close();
}

static bool savePreset(const String& bundle, const String& presetName) {
  if (!bundle.length() || !presetName.length()) return false;
  String dir = String(DSP_PRESET_DIR) + "/" + bundle;
  LittleFS.mkdir(DSP_PRESET_DIR);
  LittleFS.mkdir(dir);
  String path = presetPath(bundle, presetName);
  File f = LittleFS.open(path, FILE_WRITE);
  if (!f) return false;
  DynamicJsonDocument doc(2048);
  for (const auto& entry : dsp_values) {
    doc[entry.id] = entry.value;
  }
  if (serializeJson(doc, f) == 0) {
    f.close();
    LittleFS.remove(path);
    return false;
  }
  f.close();
  return true;
}

static bool applyPreset(const String& bundle, const String& presetName) {
  if (!bundle.length() || !presetName.length()) return false;
  String path = presetPath(bundle, presetName);
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return false;
  DynamicJsonDocument doc(2048);
  auto err = deserializeJson(doc, f);
  f.close();
  if (err != DeserializationError::Ok) return false;
  JsonObject obj = doc.as<JsonObject>();
  bool changed = false;
  for (JsonPair kv : obj) {
    const char* id = kv.key().c_str();
    float val = kv.value().as<float>();
    DspControlSpec* spec = findDspControl(String(id));
    if (!spec) continue;
    float clamped = clampf(val, spec->min_v, spec->max_v);
    float existing = getDspValue(spec->id);
    if (std::isnan(existing) || fabsf(existing - clamped) > 0.0001f) {
      setDspValueInternal(spec->id, clamped, false);
      changed = true;
    }
  }
  if (changed) markDspValuesDirty();
  return changed;
}

static bool deletePreset(const String& bundle, const String& presetName) {
  if (!bundle.length() || !presetName.length()) return false;
  String path = presetPath(bundle, presetName);
  return LittleFS.exists(path) ? LittleFS.remove(path) : false;
}

static bool deleteBundle(const String& name) {
  if (!name.length()) return false;
  String dir = String(DSP_DIR) + "/" + name;
  if (!LittleFS.exists(dir)) return false;
  File root = LittleFS.open(dir);
  if (root) {
    File entry = root.openNextFile();
    while (entry) {
      String path = entry.name();
      entry.close();
      LittleFS.remove(path);
      entry = root.openNextFile();
    }
    root.close();
  }
  LittleFS.rmdir(dir);
  String presetDir = String(DSP_PRESET_DIR) + "/" + name;
  File pd = LittleFS.open(presetDir);
  if (pd) {
    File entry = pd.openNextFile();
    while (entry) {
      String path = entry.name();
      entry.close();
      LittleFS.remove(path);
      entry = pd.openNextFile();
    }
    pd.close();
  }
  if (LittleFS.exists(presetDir)) {
    LittleFS.rmdir(presetDir);
  }
  return true;
}

static bool renameBundle(const String& oldName, const String& newName) {
  if (!oldName.length() || !newName.length()) return false;
  String oldDir = String(DSP_DIR) + "/" + oldName;
  String newDir = String(DSP_DIR) + "/" + newName;
  if (!LittleFS.exists(oldDir) || LittleFS.exists(newDir)) return false;
  LittleFS.mkdir(newDir);
  auto copyFile = [](const String& srcPath, const String& dstPath) {
    File src = LittleFS.open(srcPath, FILE_READ);
    if (!src) return false;
    File dst = LittleFS.open(dstPath, FILE_WRITE);
    if (!dst) { src.close(); return false; }
    uint8_t buf[256];
    while (src.available()) {
      size_t n = src.read(buf, sizeof(buf));
      dst.write(buf, n);
    }
    src.close();
    dst.close();
    LittleFS.remove(srcPath);
    return true;
  };

  if (LittleFS.exists(oldDir + "/program.bin")) {
    if (!copyFile(oldDir + "/program.bin", newDir + "/program.bin")) {
      LittleFS.rmdir(newDir);
      return false;
    }
  }
  if (LittleFS.exists(oldDir + "/interface.xml")) {
    if (!copyFile(oldDir + "/interface.xml", newDir + "/interface.xml")) {
      LittleFS.rmdir(newDir);
      return false;
    }
  }
  LittleFS.rmdir(oldDir);

  String oldPresetDir = String(DSP_PRESET_DIR) + "/" + oldName;
  String newPresetDir = String(DSP_PRESET_DIR) + "/" + newName;
  if (LittleFS.exists(oldPresetDir)) {
    LittleFS.mkdir(newPresetDir);
    File entry = LittleFS.open(oldPresetDir);
    File file = entry.openNextFile();
    while (file) {
      String srcPath = file.name();
      String filename = srcPath.substring(srcPath.lastIndexOf('/') + 1);
      file.close();
      if (!copyFile(oldPresetDir + "/" + filename, newPresetDir + "/" + filename)) {
        entry.close();
        return false;
      }
      file = entry.openNextFile();
    }
    entry.close();
    LittleFS.rmdir(oldPresetDir);
  }
  return true;
}

static bool pushBundleToDsp(const String& name) {
  if (!name.length()) return false;
  String program = String(DSP_DIR) + "/" + name + "/program.bin";
  if (!LittleFS.exists(program)) return false;
  addLog(String("push dsp bundle requested: ") + name + " (self-boot stub)");
  return true;
}

static bool handleDspControlUpdate(JsonObject data) {
  const char* idStr = data["id"] | "";
  if (!idStr || !idStr[0]) return false;
  if (!data.containsKey("value")) return false;
  float raw = data["value"].as<float>();
  if (!std::isfinite(raw)) return false;
  String id(idStr);
  DspControlSpec* spec = findDspControl(id);
  if (!spec) return false;
  float clamped = clampf(raw, spec->min_v, spec->max_v);
  float current = getDspValue(spec->id);
  if (!std::isfinite(current) || fabsf(current - clamped) > 0.0001f) {
    setDspValueInternal(spec->id, clamped, true);
    addLog(String("dsp control ") + spec->id + " -> " + String(clamped, 3));
    return true;
  }
  return false;
}

static AsyncJsonResponse* createThermStatusResponse(bool ok, const String& message, int statusCode) {
  auto* resp = new AsyncJsonResponse(false, 1024);
  resp->setCode(statusCode);
  JsonObject root = resp->getRoot();
  root["ok"] = ok;
  if (message.length() > 0) root["message"] = message;
  populateThermStatus(root);
  resp->setLength();
  return resp;
}


// PID controller state
static float pid_i = 0.0f;
static float pid_prev_err = 0.0f;
static uint32_t pid_prev_ms = 0;

static void applyFanOutput(uint8_t pct) {
  pct = (pct > 100) ? 100 : pct;
  state.fan_target_pct = pct;
  // 3-wire fans: enforce a minimum start duty to avoid stall.
  if (kFanType == FanType::Fan3Wire && pct > 0 && pct < FAN3_MIN_START_PCT) pct = FAN3_MIN_START_PCT;
  uint32_t duty = map(pct, 0, 100, 0, (1 << FAN_PWM_RES_BITS) - 1);
  for (size_t i = 0; i < FAN_SLOT_COUNT; ++i) {
    if (!fanSlotEnabled(i)) continue;
    ledcWriteChannel(FAN_PWM_CHANNELS[i], duty);
  }
  // Tach handling note: at low duty on 3-wire, tach pulses may be intermittent.
}

static void controlLoop() {
  const uint32_t now = millis();
  const float dt = (pid_prev_ms == 0) ? 0.1f : (now - pid_prev_ms) / 1000.0f;
  pid_prev_ms = now;

  // choose hottest sensor as control input
  float temp = isnan(state.temp1) ? state.temp2 : isnan(state.temp2) ? state.temp1 : max(state.temp1, state.temp2);
  if (isnan(temp)) return;

  if (settings.pid_enabled) {
    float sp = settings.setpoint1_c; // primary setpoint
    float err = temp - sp;           // positive = too hot
    pid_i += err * dt;
    // anti-windup
    pid_i = constrain(pid_i, -100.0f, 100.0f);
    float d = (dt > 0.0f) ? (err - pid_prev_err) / dt : 0.0f;
    pid_prev_err = err;
    float out = settings.pid_kp * err + settings.pid_ki * pid_i + settings.pid_kd * d;
    // Map PID output to 0..100% range with floor
    uint8_t pct = (uint8_t)constrain((int)round(out + 20.0f), 0, 100);
    applyFanOutput(pct);
  } else {
    applyFanOutput(settings.fan_manual_pct);
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("Booting BOOTBOXDSP controller...");

  StatusLed::init();

  littlefs_ready = LittleFS.begin(true);
  if (!littlefs_ready) {
    Serial.println("LittleFS mount failed");
  } else {
    fs_total_bytes = LittleFS.totalBytes();
  }

  prefs.begin("bootbox", false);
  last_reset_reason = esp_reset_reason();
  const char* rr = resetReasonToStr(last_reset_reason);
  snprintf(RESET_REASON_LABEL, sizeof(RESET_REASON_LABEL), "%s", rr);
  boot_count = prefs.getUInt("boot_cnt", 0) + 1;
  prefs.putUInt("boot_cnt", boot_count);

  const char* chip_model = ESP.getChipModel();
  if (strncmp(chip_model, "ESP32", 5) == 0) {
    snprintf(CHIP_LABEL, sizeof(CHIP_LABEL), "MCU%s", chip_model + 5);
  } else {
    snprintf(CHIP_LABEL, sizeof(CHIP_LABEL), "%s", chip_model);
  }

  addLog(String("reset reason: ") + RESET_REASON_LABEL);
  addLog(String("boot count: ") + boot_count);

  settingsLoad(prefs, settings);
  refreshThermistorParams();
  if (littlefs_ready) {
    dspInit();
  } else {
    dsp_controls.clear();
    dsp_active_bundle = "";
  }

  StatusLed::setDefault(StatusLed::Status::SystemOk);
  if (!littlefs_ready) {
    StatusLed::setDefault(StatusLed::higherSeverity(StatusLed::getDefault(), StatusLed::Status::GeneralError));
  }

  wifiInitOk = initWiFiAP();
  if (!wifiInitOk) {
    Serial.println("WiFi AP startup failed");
    StatusLed::setDefault(StatusLed::higherSeverity(StatusLed::getDefault(), StatusLed::Status::NetworkError));
  }

  switch (last_reset_reason) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      StatusLed::setDefault(StatusLed::higherSeverity(StatusLed::getDefault(), StatusLed::Status::SystemRunningCheckLogs));
      break;
    case ESP_RST_BROWNOUT:
      StatusLed::setDefault(StatusLed::higherSeverity(StatusLed::getDefault(), StatusLed::Status::CriticalError));
      break;
    default:
      break;
  }

  // Fan PWM setup
  const int pwm_freq = (kFanType == FanType::Fan4Wire) ? FAN_PWM_FREQ_4WIRE : FAN_PWM_FREQ_3WIRE;
  for (size_t i = 0; i < FAN_SLOT_COUNT; ++i) {
    if (!fanSlotEnabled(i)) continue;
    ledcAttachChannel(FAN_CTRL_PINS[i], pwm_freq, FAN_PWM_RES_BITS, FAN_PWM_CHANNELS[i]);
  }
  applyFanOutput(settings.pid_enabled ? 20 : settings.fan_manual_pct);

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  registerHttpRoutes();
  server.begin();
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
  addLog(String("fans active: ") + activeFanCount());
  addLog("system boot");
  StatusLed::set(StatusLed::getDefault(), true);
}

void loop() {
  if (settings_dirty && millis() - settings_dirty_since > SETTINGS_SAVE_DELAY_MS) {
    settings_dirty = false;
    settingsSave(prefs, settings);
    addLog("settings persisted");
  }
  // Handle reliable WS retransmission
  wsResendTick();
  // Sample sensors and control
  static uint32_t last_sense = 0;
  uint32_t now = millis();
  if (now - last_sense > 200) {
    sampleSensors();
    controlLoop();
    last_sense = now;
  }
  static uint32_t last_bcast = 0;
  if (now - last_bcast > 1000) {
    wsBroadcastState();
    last_bcast = now;
  }

  flushPendingDspSaves();

  StatusLed::Status desired = StatusLed::getDefault();
  if (!littlefs_ready) desired = StatusLed::higherSeverity(desired, StatusLed::Status::GeneralError);
  if (!wifiInitOk) desired = StatusLed::higherSeverity(desired, StatusLed::Status::NetworkError);
  if (thermalFaultActive()) desired = StatusLed::higherSeverity(desired, StatusLed::Status::ThermalError);
  StatusLed::set(desired);
  StatusLed::update();
}

static const char* resetReasonToStr(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "ext_reset";
    case ESP_RST_SW: return "sw_reset";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brown_out";
    case ESP_RST_SDIO: return "sdio";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    default: return "unknown";
  }
}
