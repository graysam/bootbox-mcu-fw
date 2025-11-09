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
static bool handleDspUpdate(JsonObject data);
static const char* resetReasonToStr(esp_reset_reason_t reason);
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
  dsp["master_db"] = settings.dsp_master_db;
  dsp["stereo_db"] = settings.dsp_stereo_db;
  dsp["sub_lo_db"] = settings.dsp_sub_lo_db;
  dsp["sub_hi_db"] = settings.dsp_sub_hi_db;
  dsp["cross_mains_hz"] = settings.dsp_cross_mains_hz;
  dsp["cross_sub_hz"] = settings.dsp_cross_sub_hz;
  dsp["cross_linked"] = settings.dsp_cross_linked;
  dsp["sub_lo_hp_hz"] = settings.dsp_sub_lo_hp_hz;
  dsp["sub_lo_lp_hz"] = settings.dsp_sub_lo_lp_hz;
  dsp["sub_hi_hp_hz"] = settings.dsp_sub_hi_hp_hz;
  dsp["sub_hi_lp_hz"] = settings.dsp_sub_hi_lp_hz;

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
      bool changed = false;
      if (!data.isNull()) changed = handleDspUpdate(data);
      if (changed) {
        markSettingsDirty();
        addLog("dsp params updated");
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
    dsp["master_db"] = settings.dsp_master_db;
    dsp["stereo_db"] = settings.dsp_stereo_db;
    dsp["sub_lo_db"] = settings.dsp_sub_lo_db;
    dsp["sub_hi_db"] = settings.dsp_sub_hi_db;
    dsp["cross_mains_hz"] = settings.dsp_cross_mains_hz;
    dsp["cross_sub_hz"] = settings.dsp_cross_sub_hz;
    dsp["cross_linked"] = settings.dsp_cross_linked;
    dsp["sub_lo_hp_hz"] = settings.dsp_sub_lo_hp_hz;
    dsp["sub_lo_lp_hz"] = settings.dsp_sub_lo_lp_hz;
    dsp["sub_hi_hp_hz"] = settings.dsp_sub_hi_hp_hz;
    dsp["sub_hi_lp_hz"] = settings.dsp_sub_hi_lp_hz;
    auto sys = doc.createNestedObject("sys");
    fillSystemInfo(sys);
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Upload ADAU1701 binary/images to /dsp directory in LittleFS
  server.on("/api/upload/adau", HTTP_POST,
            [](AsyncWebServerRequest* req){
              // Unrestricted upload endpoint (no token, always enabled)
              req->send(200, "text/plain", "ok");
            },
            [](AsyncWebServerRequest* req, String filename, size_t index, uint8_t* data, size_t len, bool final){
              static File f;
              if (index == 0) {
                LittleFS.mkdir("/dsp");
                String path = String("/dsp/") + filename;
                f = LittleFS.open(path, FILE_WRITE);
                addLog(String("upload start ") + filename);
              }
              if (f) f.write(data, len);
              if (final && f) { f.close(); addLog(String("upload done ") + filename); }
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

static bool handleDspUpdate(JsonObject data) {
  bool changed = false;

  auto updateDb = [&](const char* key, float& target) {
    if (!data.containsKey(key)) return;
    float v = data[key].as<float>();
    if (isnan(v)) return;
    float clamped = clampf(v, -60.0f, 12.0f);
    if (fabsf(target - clamped) > 0.0001f) {
      target = clamped;
      changed = true;
    }
  };

  auto updateRange = [&](const char* key, float& target, float min_v, float max_v) {
    if (!data.containsKey(key)) return;
    float v = data[key].as<float>();
    if (isnan(v)) return;
    float clamped = clampf(v, min_v, max_v);
    if (fabsf(target - clamped) > 0.0001f) {
      target = clamped;
      changed = true;
    }
  };

  updateDb("master_db", settings.dsp_master_db);
  updateDb("stereo_db", settings.dsp_stereo_db);
  updateDb("sub_lo_db", settings.dsp_sub_lo_db);
  updateDb("sub_hi_db", settings.dsp_sub_hi_db);

  updateRange("sub_lo_hp_hz", settings.dsp_sub_lo_hp_hz, 15.0f, 180.0f);
  updateRange("sub_lo_lp_hz", settings.dsp_sub_lo_lp_hz, 30.0f, 220.0f);
  updateRange("sub_hi_hp_hz", settings.dsp_sub_hi_hp_hz, 40.0f, 240.0f);
  updateRange("sub_hi_lp_hz", settings.dsp_sub_hi_lp_hz, 60.0f, 260.0f);

  if (data.containsKey("cross_linked")) {
    bool v = data["cross_linked"].as<bool>();
    if (settings.dsp_cross_linked != v) {
      settings.dsp_cross_linked = v;
      changed = true;
    }
  }

  if (data.containsKey("cross_mains_hz")) {
    float v = data["cross_mains_hz"].as<float>();
    if (!isnan(v)) {
      float clamped = clampf(v, 40.0f, 300.0f);
      if (fabsf(settings.dsp_cross_mains_hz - clamped) > 0.0001f) {
        settings.dsp_cross_mains_hz = clamped;
        changed = true;
      }
      if (settings.dsp_cross_linked) {
        settings.dsp_cross_sub_hz = settings.dsp_cross_mains_hz;
      }
    }
  }

  if (data.containsKey("cross_sub_hz")) {
    float v = data["cross_sub_hz"].as<float>();
    if (!isnan(v)) {
      float clamped = clampf(v, 30.0f, 240.0f);
      if (fabsf(settings.dsp_cross_sub_hz - clamped) > 0.0001f) {
        settings.dsp_cross_sub_hz = clamped;
        changed = true;
      }
      if (settings.dsp_cross_linked) {
        settings.dsp_cross_mains_hz = settings.dsp_cross_sub_hz;
      }
    }
  }

  // Normalise linked crossover values if needed
  if (settings.dsp_cross_linked) {
    float linked = clampf(settings.dsp_cross_mains_hz, 40.0f, 240.0f);
    settings.dsp_cross_mains_hz = linked;
    settings.dsp_cross_sub_hz = linked;
  } else {
    settings.dsp_cross_mains_hz = clampf(settings.dsp_cross_mains_hz, 40.0f, 300.0f);
    settings.dsp_cross_sub_hz = clampf(settings.dsp_cross_sub_hz, 30.0f, 240.0f);
  }

  auto enforceRange = [&](float& hp, float& lp, float min_hp, float max_lp) {
    hp = clampf(hp, min_hp, max_lp - 5.0f);
    lp = clampf(lp, hp + 5.0f, max_lp);
  };

  enforceRange(settings.dsp_sub_lo_hp_hz, settings.dsp_sub_lo_lp_hz, 15.0f, 220.0f);
  enforceRange(settings.dsp_sub_hi_hp_hz, settings.dsp_sub_hi_lp_hz, 40.0f, 260.0f);

  return changed;
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
