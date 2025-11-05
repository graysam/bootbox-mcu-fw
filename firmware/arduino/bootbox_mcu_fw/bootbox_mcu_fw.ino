#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <esp32-hal-ledc.h>
#include <vector>
#include <deque>
#include <cstddef>
#include <cmath>

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

static constexpr size_t FAN_SLOT_COUNT = sizeof(FAN_CTRL_PINS) / sizeof(FAN_CTRL_PINS[0]);
static bool settings_dirty = false;
static uint32_t settings_dirty_since = 0;
static constexpr uint32_t SETTINGS_SAVE_DELAY_MS = 1500;

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

static bool handleDspUpdate(JsonObject data);

// Reliable WS: basic ack tracking
struct PendingMsg {
  uint32_t id;
  uint32_t sent_at_ms;
  uint8_t attempts;
  String payload;
  AsyncWebSocketClient* client;
};

static uint32_t msg_seq = 1;
static constexpr uint32_t WS_ACK_TIMEOUT_MS = 2000;
static constexpr uint8_t WS_MAX_RETRIES = 3;
static std::vector<PendingMsg> pending;

// ---- Simple device log ring buffer ----
static std::deque<String> logs;
static const size_t LOG_MAX = 100;
static void addLog(const String& s) {
  if (logs.size() >= LOG_MAX) logs.pop_front();
  logs.push_back(s);
}

static void wsSendReliable(AsyncWebSocketClient* c, JsonDocument& doc) {
  doc["id"] = msg_seq++;
  String out;
  serializeJson(doc, out);
  c->text(out);
  pending.push_back({(uint32_t)doc["id"].as<uint32_t>(), millis(), 1, out, c});
}

static void wsBroadcastState() {
  StaticJsonDocument<768> doc;
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

  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}

static void handleAck(uint32_t id) {
  for (auto it = pending.begin(); it != pending.end(); ++it) {
    if (it->id == id) { pending.erase(it); return; }
  }
}

static void wsResendTick() {
  const uint32_t now = millis();
  for (auto it = pending.begin(); it != pending.end();) {
    if (!it->client || !it->client->canSend()) { it = pending.erase(it); continue; }
    if (now - it->sent_at_ms > WS_ACK_TIMEOUT_MS) {
      if (it->attempts >= WS_MAX_RETRIES) {
        // give up on this message
        it = pending.erase(it);
        continue;
      }
      it->attempts++;
      it->sent_at_ms = now;
      it->client->text(it->payload);
      ++it;
    } else {
      ++it;
    }
  }
}

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
    StaticJsonDocument<768> doc;
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

  server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* req){
    StaticJsonDocument<2048> out;
    auto arr = out.createNestedArray("logs");
    for (auto &l : logs) arr.add(l);
    String s; serializeJson(out, s);
    req->send(200, "application/json", s);
  });
}

// ---- Setup helpers ----
static void initWiFiAP() {
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID); // Open AP (no password)
  if (!ok) {
    // Retry once quickly
    delay(500);
    WiFi.softAP(AP_SSID);
  }
}

// ---- Sensors & Control ----
static float adcToTempC(int adc) {
  // Placeholder: map raw ADC to degrees C. Replace with NTC curve.
  // For now, simulate ~25-80C range over 0..4095
  return TEMP_MIN_C + (adc / 4095.0f) * TEMP_SPAN_C;
}

static void sampleSensors() {
  // Read analog temps; if pins unconnected, values may float
  int a1 = analogRead(PIN_THERM1);
  int a2 = analogRead(PIN_THERM2);
  state.temp1 = adcToTempC(a1);
  state.temp2 = adcToTempC(a2);
  // TODO: tach read via PCNT or RMT; set to 0 for now
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

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }

  prefs.begin("bootbox", false);
  settingsLoad(prefs, settings);

  initWiFiAP();

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
}
