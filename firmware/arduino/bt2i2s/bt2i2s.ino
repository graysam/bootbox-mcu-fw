#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#ifndef A2DP_LEGACY_I2S_SUPPORT
#define A2DP_LEGACY_I2S_SUPPORT true
#endif
#include <BluetoothA2DPSink.h>
#include <driver/i2s.h>
#include <esp_a2dp_api.h>
#include <esp_avrc_api.h>
#include <esp_bt_device.h>
#include <esp_gap_bt_api.h>
#include <algorithm>
#include <vector>

#include "config.h"

BluetoothA2DPSink a2dp_sink;
static constexpr size_t LINK_RX_CAPACITY = 640;
static constexpr size_t LINK_TX_CAPACITY = 1024;
static constexpr uint32_t PAIRING_DEFAULT_MS = 120000;
static constexpr uint32_t AUTOCONNECT_BACKOFF_MS = 5000;
static constexpr const char* PREF_NAMESPACE = "bt2i2s";
static constexpr const char* PREF_DEVICES = "devices";

struct TransportState {
  bool a2dp_connected = false;
  bool avrcp_connected = false;
  bool audio_active = false;
  bool playing = false;
  uint8_t volume_pct = BT_DEFAULT_VOLUME_PERCENT;
  uint16_t sample_rate_hz = 44100;
  String device_addr;
  String device_name;
  String title;
  String artist;
  String album;
  esp_a2d_audio_state_t audio_state = ESP_A2D_AUDIO_STATE_STOPPED;
};

struct PairedDevice {
  String addr;
  String name;
  int priority = 0;
  bool connected = false;
  uint32_t last_seen_ms = 0;
};

static TransportState g_state;
static std::vector<PairedDevice> g_devices;
static HardwareSerial* g_link = nullptr;
static bool g_link_ready = false;
static uint32_t g_last_status_ms = 0;
static bool g_pairing_active = false;
static uint32_t g_pairing_until_ms = 0;
static uint32_t g_last_connect_attempt_ms = 0;
static uint32_t g_last_hello_tx_ms = 0;
static Preferences g_prefs;

static String sanitize_label(const String& in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in.charAt(i);
    if (c >= 32 && c < 127) {
      out += c;
    }
  }
  return out;
}

static uint8_t clamp_pct(int value) {
  if (value < 0) return 0;
  if (value > 100) return 100;
  return static_cast<uint8_t>(value);
}

static uint8_t pct_to_a2dp(uint8_t pct) {
  pct = clamp_pct(pct);
  // Map 0-100% into the 0-127 AVRCP volume range (rounded).
  return static_cast<uint8_t>((pct * 127 + 50) / 100);
}

static uint8_t a2dp_to_pct(int raw) {
  if (raw < 0) raw = 0;
  if (raw > 127) raw = 127;
  return static_cast<uint8_t>((raw * 100 + 63) / 127);
}

static String addr_to_string(const esp_bd_addr_t addr) {
  char out[18];
  snprintf(out, sizeof(out), "%02x:%02x:%02x:%02x:%02x:%02x",
           addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
  return String(out);
}

static bool addr_from_string(const String& s, esp_bd_addr_t out) {
  if (s.length() != 17) return false;
  int values[6];
  if (sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x",
             &values[0], &values[1], &values[2],
             &values[3], &values[4], &values[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; ++i) out[i] = static_cast<uint8_t>(values[i]);
  return true;
}

static void publish_hello(const char* reason, JsonVariantConst reply_id = JsonVariantConst());
static void publish_link_state(const char* reason, JsonVariantConst reply_id = JsonVariantConst());
static void send_ack(const char* cmd, JsonVariantConst reply_id = JsonVariantConst());
static void send_error(const char* reason, const char* detail, JsonVariantConst reply_id = JsonVariantConst());
static void handle_link_packet(const JsonDocument& doc);
static void poll_link_rx();
static void init_link_bus();
static void init_a2dp();
static void sync_paired_devices(const char* reason);
static void persist_devices();
static void publish_devices(const char* reason, JsonVariantConst reply_id = JsonVariantConst());
static PairedDevice* find_device(const String& addr);
static void ensure_device_entry(const esp_bd_addr_t addr, const String& name);
static void set_pairing_mode(bool enable, uint32_t timeout_ms = PAIRING_DEFAULT_MS);
static void pairing_tick();
static void autoconnect_tick(bool force = false);
static void apply_volume_pct(uint8_t pct, bool broadcast, JsonVariantConst reply_id = JsonVariantConst());
static void on_metadata(uint8_t attr_id, const uint8_t* text);
static void on_connection_state(esp_a2d_connection_state_t state, void*);
static void on_audio_state(esp_a2d_audio_state_t state, void*);
static void on_avrc_connection(bool connected);
static void on_play_status(esp_avrc_playback_stat_t playback);
static void on_sample_rate(uint16_t rate);
static void on_volume_change(int volume);
static void attach_id(JsonDocument& dest, JsonVariantConst id);
static void link_send(const JsonDocument& doc, bool log);
static void log_state(const char* label);

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println("\n[bt2i2s] booting");

  g_prefs.begin(PREF_NAMESPACE, false);
  init_link_bus();
  publish_hello("boot");
  init_a2dp();
  sync_paired_devices("boot");
  autoconnect_tick(true);
  publish_link_state("boot");
}

void loop() {
  poll_link_rx();
  pairing_tick();
  autoconnect_tick();

  const uint32_t now = millis();
  if (now - g_last_hello_tx_ms >= LINK_STATUS_INTERVAL_MS * 4) {
    g_last_hello_tx_ms = now;
    publish_hello("heartbeat");
  }
  if (now - g_last_status_ms >= LINK_STATUS_INTERVAL_MS) {
    g_last_status_ms = now;
    publish_link_state("heartbeat");
  }

  delay(5);
}

static void init_link_bus() {
  if (PIN_LINK_TX < 0 || PIN_LINK_RX < 0) {
    Serial.println("[link] UART disabled (pins set to -1)");
    return;
  }
  if (LINK_UART_NUM == 1) {
    g_link = &Serial1;
  } else {
    g_link = &Serial2;
  }
  g_link->begin(LINK_UART_BAUD, SERIAL_8N1, PIN_LINK_RX, PIN_LINK_TX);
  g_link->setTimeout(5);
  g_link_ready = true;
  Serial.printf("[link] UART%d ready TX=%d RX=%d @ %lu\n", LINK_UART_NUM, PIN_LINK_TX, PIN_LINK_RX, static_cast<unsigned long>(LINK_UART_BAUD));
}

static void init_a2dp() {
#if A2DP_LEGACY_I2S_SUPPORT
  // Ensure the sink has a concrete output instance before we touch pin config.
  // Some ESP-IDF/Arduino builds have been observed to leave the default
  // output pointer unset, which would null-deref inside set_pin_config().
  static BluetoothA2DPOutputDefault s_out_default;
  if (!a2dp_sink.get_output()) {
    Serial.println("[bt] A2DP output missing; rebinding default output");
    a2dp_sink.set_output(s_out_default);
  }
  if (!a2dp_sink.get_output()) {
    Serial.println("[bt] A2DP output unavailable; aborting init");
    return;
  }
#endif

  i2s_pin_config_t pin_config = {
      .bck_io_num = PIN_I2S_BCLK,
      .ws_io_num = PIN_I2S_LRCLK,
      .data_out_num = PIN_I2S_DOUT,
      .data_in_num = I2S_PIN_NO_CHANGE,
  };

  i2s_config_t i2s_config = {
      .mode = static_cast<i2s_mode_t>((I2S_MASTER_MODE ? I2S_MODE_MASTER : I2S_MODE_SLAVE) | I2S_MODE_TX),
      .sample_rate = g_state.sample_rate_hz,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = static_cast<int>(I2S_DMA_BUF_COUNT),
      .dma_buf_len = static_cast<int>(I2S_DMA_BUF_LEN),
      .use_apll = I2S_USE_APLL,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0,
#ifdef I2S_MCLK_MULTIPLE_DEFAULT
      .mclk_multiple = I2S_MCLK_MULTIPLE_DEFAULT,
#else
      .mclk_multiple = I2S_MCLK_MULTIPLE_256,
#endif
      .bits_per_chan = I2S_BITS_PER_CHAN_16BIT,
  };

  a2dp_sink.set_pin_config(pin_config);
#if A2DP_LEGACY_I2S_SUPPORT
  // Legacy API does not require explicit port/config; use defaults after pin mapping.
#else
  a2dp_sink.set_i2s_config(i2s_config);
#endif
  if (BT_AUTO_RECONNECT) {
    a2dp_sink.set_auto_reconnect(true);
  }

  a2dp_sink.set_avrc_metadata_callback(on_metadata);
  a2dp_sink.set_on_connection_state_changed(on_connection_state, nullptr);
  a2dp_sink.set_on_audio_state_changed(on_audio_state, nullptr);
  a2dp_sink.set_avrc_connection_state_callback(on_avrc_connection);
  a2dp_sink.set_avrc_rn_playstatus_callback(on_play_status);
  a2dp_sink.set_sample_rate_callback(on_sample_rate);
  a2dp_sink.set_avrc_rn_volumechange(on_volume_change);
  a2dp_sink.set_on_volumechange(on_volume_change);

  Serial.printf("[bt] Advertising as '%s'\n", BT_DEVICE_NAME);
  a2dp_sink.start(BT_DEVICE_NAME);
  apply_volume_pct(BT_DEFAULT_VOLUME_PERCENT, false);
}

static void persist_devices() {
  StaticJsonDocument<1024> doc;
  JsonArray arr = doc.createNestedArray("devices");
  for (const auto& d : g_devices) {
    if (!d.addr.length()) continue;
    JsonObject obj = arr.createNestedObject();
    obj["addr"] = d.addr;
    if (d.name.length()) obj["name"] = d.name;
    obj["priority"] = d.priority;
  }
  String out;
  serializeJson(doc, out);
  g_prefs.putString(PREF_DEVICES, out);
}

static PairedDevice* find_device(const String& addr) {
  if (!addr.length()) return nullptr;
  for (auto& d : g_devices) {
    if (d.addr.equalsIgnoreCase(addr)) return &d;
  }
  return nullptr;
}

static void ensure_device_entry(const esp_bd_addr_t addr, const String& name) {
  const String addr_str = addr_to_string(addr);
  PairedDevice* existing = find_device(addr_str);
  if (!existing) {
    PairedDevice dev;
    dev.addr = addr_str;
    dev.priority = static_cast<int>(g_devices.size()) + 1;
    g_devices.push_back(dev);
    existing = &g_devices.back();
  }
  if (name.length()) existing->name = name;
  existing->last_seen_ms = millis();
  existing->connected = true;
}

static void sync_paired_devices(const char* reason) {
  // Load persisted metadata first.
  g_devices.clear();
  String raw = g_prefs.getString(PREF_DEVICES, "");
  if (raw.length()) {
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, raw) == DeserializationError::Ok) {
      JsonArray arr = doc["devices"].as<JsonArray>();
      for (JsonObject obj : arr) {
        PairedDevice d;
        d.addr = obj["addr"] | "";
        d.name = obj["name"] | "";
        d.priority = obj["priority"] | 0;
        if (d.addr.length()) g_devices.push_back(d);
      }
    }
  }

  // Merge with the live bonded device list.
  int bonded_count = esp_bt_gap_get_bond_device_num();
  std::vector<String> bonded;
  if (bonded_count > 0) {
    std::vector<esp_bd_addr_t> list(static_cast<size_t>(bonded_count));
    if (esp_bt_gap_get_bond_device_list(&bonded_count, list.data()) == ESP_OK) {
      for (int i = 0; i < bonded_count; ++i) {
        bonded.push_back(addr_to_string(list[static_cast<size_t>(i)]));
      }
    }
  }

  // Drop entries that are no longer bonded.
  g_devices.erase(std::remove_if(g_devices.begin(), g_devices.end(),
    [&](const PairedDevice& d) {
      return std::find_if(bonded.begin(), bonded.end(),
        [&](const String& addr){ return addr.equalsIgnoreCase(d.addr); }) == bonded.end();
    }), g_devices.end());

  // Add newly bonded devices.
  for (const auto& addr : bonded) {
    if (!find_device(addr)) {
      PairedDevice d;
      d.addr = addr;
      d.priority = static_cast<int>(g_devices.size()) + 1;
      g_devices.push_back(d);
    }
  }

  // Normalize priorities.
  std::sort(g_devices.begin(), g_devices.end(), [](const PairedDevice& a, const PairedDevice& b){
    return a.priority < b.priority;
  });
  for (size_t i = 0; i < g_devices.size(); ++i) {
    g_devices[i].priority = static_cast<int>(i + 1);
  }
  persist_devices();
  publish_devices(reason);
}

static void set_pairing_mode(bool enable, uint32_t timeout_ms) {
  if (enable) {
    g_pairing_active = true;
    g_pairing_until_ms = millis() + timeout_ms;
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
  } else {
    g_pairing_active = false;
    g_pairing_until_ms = 0;
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
  }
}

static void pairing_tick() {
  if (!g_pairing_active) return;
  const uint32_t now = millis();
  if (now >= g_pairing_until_ms) {
    set_pairing_mode(false);
    publish_devices("pair_timeout");
  }
}

static void autoconnect_tick(bool force) {
  if (g_pairing_active || g_state.a2dp_connected || g_devices.empty()) return;
  const uint32_t now = millis();
  if (!force && now - g_last_connect_attempt_ms < AUTOCONNECT_BACKOFF_MS) return;
  esp_bd_addr_t addr = {0};
  if (!addr_from_string(g_devices.front().addr, addr)) return;
  esp_err_t err = esp_a2d_sink_connect(addr);
  g_last_connect_attempt_ms = now;
  if (err == ESP_OK) {
    Serial.printf("[bt] autoconnect -> %s\n", g_devices.front().addr.c_str());
  } else {
    Serial.printf("[bt] autoconnect failed (%d)\n", static_cast<int>(err));
  }
}

static void apply_volume_pct(uint8_t pct, bool broadcast, JsonVariantConst reply_id) {
  const uint8_t mapped = pct_to_a2dp(pct);
  a2dp_sink.set_volume(mapped);
  g_state.volume_pct = pct;
  if (broadcast) {
    publish_link_state("set_volume", reply_id);
  }
}

static void on_metadata(uint8_t attr_id, const uint8_t* text) {
  if (text == nullptr) {
    return;
  }
  const String value(reinterpret_cast<const char*>(text));
  switch (attr_id) {
    case ESP_AVRC_MD_ATTR_TITLE:
      g_state.title = value;
      break;
    case ESP_AVRC_MD_ATTR_ARTIST:
      g_state.artist = value;
      break;
    case ESP_AVRC_MD_ATTR_ALBUM:
      g_state.album = value;
      break;
    default:
      break;
  }
  publish_link_state("metadata");
}

static void on_connection_state(esp_a2d_connection_state_t state, void*) {
  g_state.a2dp_connected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
  if (g_state.a2dp_connected) {
    esp_bd_addr_t* peer = a2dp_sink.get_current_peer_address();
    if (peer != nullptr) {
      g_state.device_addr = addr_to_string(*peer);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0)
      g_state.device_name = String(a2dp_sink.get_peer_name());
#endif
      if (g_pairing_active) {
        set_pairing_mode(false);
      }
      ensure_device_entry(*peer, g_state.device_name);
      if (PairedDevice* d = find_device(g_state.device_addr)) {
        d->connected = true;
        d->last_seen_ms = millis();
      if (!g_state.device_name.isEmpty()) d->name = g_state.device_name;
      }
      persist_devices();
      publish_devices("connection");
      Serial.printf("[bt] conn: devices=%u (addr=%s name=%s)\n",
        static_cast<unsigned>(g_devices.size()),
        g_state.device_addr.c_str(),
        g_state.device_name.c_str());
    }
  }
  if (!g_state.a2dp_connected) {
    g_state.audio_active = false;
    g_state.playing = false;
    g_state.title = "";
    g_state.artist = "";
    g_state.album = "";
    if (g_state.device_addr.length()) {
      PairedDevice* d = find_device(g_state.device_addr);
      if (d) {
        d->connected = false;
        d->last_seen_ms = millis();
      }
      g_state.device_addr = "";
      g_state.device_name = "";
      persist_devices();
      publish_devices("disconnect");
    }
  }
  publish_link_state("connection");
  log_state("conn");
}

static void on_audio_state(esp_a2d_audio_state_t state, void*) {
  g_state.audio_state = state;
  g_state.audio_active = (state == ESP_A2D_AUDIO_STATE_STARTED);
  if (state == ESP_A2D_AUDIO_STATE_STOPPED || state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
    g_state.playing = false;
  }
  publish_link_state("audio");
  log_state("audio");
}

static void on_avrc_connection(bool connected) {
  g_state.avrcp_connected = connected;
  publish_link_state("avrcp");
}

static void on_play_status(esp_avrc_playback_stat_t playback) {
  switch (playback) {
    case ESP_AVRC_PLAYBACK_PLAYING:
      g_state.playing = true;
      break;
    case ESP_AVRC_PLAYBACK_PAUSED:
    case ESP_AVRC_PLAYBACK_STOPPED:
    case ESP_AVRC_PLAYBACK_FWD_SEEK:
    case ESP_AVRC_PLAYBACK_REV_SEEK:
    default:
      g_state.playing = false;
      break;
  }
  publish_link_state("playback");
}

static void on_sample_rate(uint16_t rate) {
  if (rate == 0) {
    return;
  }
  g_state.sample_rate_hz = rate;
  publish_link_state("sample_rate");
}

static void on_volume_change(int volume) {
  g_state.volume_pct = a2dp_to_pct(volume);
  publish_link_state("volume_change");
}

static void poll_link_rx() {
  if (!g_link_ready || g_link == nullptr) {
    return;
  }
  while (g_link->available()) {
    String line = g_link->readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) {
      continue;
    }
    Serial.printf("[link-rx] %s\n", line.c_str());
    StaticJsonDocument<LINK_RX_CAPACITY> doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
      Serial.printf("[link] JSON parse error: %s\n", err.c_str());
      continue;
    }
    handle_link_packet(doc);
  }
}

static void handle_link_packet(const JsonDocument& doc) {
  JsonVariantConst reply_id = doc["id"];
  String type = doc["type"] | "";

  if (!type.isEmpty()) {
    if (type.equalsIgnoreCase("hello")) {
      publish_hello("rx", reply_id);
      return;
    }
    if (type.equalsIgnoreCase("get")) {
      String what = doc["what"] | "";
      if (what.equalsIgnoreCase("state")) {
        publish_link_state("get", reply_id);
      } else if (what.equalsIgnoreCase("hello")) {
        publish_hello("get", reply_id);
      } else if (what.equalsIgnoreCase("devices")) {
        publish_devices("get", reply_id);
      } else {
        send_error("unknown_get", what.length() ? what.c_str() : "missing", reply_id);
      }
      return;
    }
    if (type.equalsIgnoreCase("cmd") || type.equalsIgnoreCase("control")) {
      // fall through to cmd handling
    } else {
      send_error("unknown_type", type.c_str(), reply_id);
      return;
    }
  }
  String cmd = doc["cmd"] | "";
  if (cmd.isEmpty()) {
    if (type.equalsIgnoreCase("hello") || type.equalsIgnoreCase("get")) return;
    send_error("missing_cmd", "cmd field required", reply_id);
    return;
  }
  if (cmd.equalsIgnoreCase("play")) {
    a2dp_sink.play();
    send_ack(cmd.c_str(), reply_id);
  } else if (cmd.equalsIgnoreCase("pause")) {
    a2dp_sink.pause();
    send_ack(cmd.c_str(), reply_id);
  } else if (cmd.equalsIgnoreCase("toggle")) {
    if (g_state.playing) {
      a2dp_sink.pause();
    } else {
      a2dp_sink.play();
    }
    send_ack(cmd.c_str(), reply_id);
  } else if (cmd.equalsIgnoreCase("next")) {
    a2dp_sink.next();
    send_ack(cmd.c_str(), reply_id);
  } else if (cmd.equalsIgnoreCase("prev") || cmd.equalsIgnoreCase("previous")) {
    a2dp_sink.previous();
    send_ack(cmd.c_str(), reply_id);
  } else if (cmd.equalsIgnoreCase("volume")) {
    if (doc["pct"].is<int>()) {
      apply_volume_pct(doc["pct"].as<int>(), true, reply_id);
    } else {
      send_error("missing_pct", "pct required", reply_id);
    }
  } else if (cmd.equalsIgnoreCase("state")) {
    publish_link_state("cmd_state", reply_id);
  } else if (cmd.equalsIgnoreCase("hello")) {
    publish_hello("cmd", reply_id);
  } else if (cmd.equalsIgnoreCase("pair_start")) {
    uint32_t timeout_ms = doc["timeout_ms"] | PAIRING_DEFAULT_MS;
    set_pairing_mode(true, timeout_ms);
    send_ack(cmd.c_str(), reply_id);
    publish_devices("pair_start");
  } else if (cmd.equalsIgnoreCase("pair_stop") || cmd.equalsIgnoreCase("pair_cancel")) {
    set_pairing_mode(false);
    send_ack(cmd.c_str(), reply_id);
    publish_devices("pair_stop");
  } else if (cmd.equalsIgnoreCase("forget")) {
    const char* addr = doc["addr"] | nullptr;
    if (!addr || strlen(addr) == 0) {
      send_error("missing_addr", "addr required", reply_id);
      return;
    }
    esp_bd_addr_t target = {0};
    if (!addr_from_string(String(addr), target)) {
      send_error("bad_addr", addr, reply_id);
      return;
    }
    if (g_state.a2dp_connected && g_state.device_addr.equalsIgnoreCase(addr)) {
      esp_a2d_sink_disconnect(target);
    }
    esp_err_t err = esp_bt_gap_remove_bond_device(target);
    if (err != ESP_OK) {
      send_error("forget_failed", String(err).c_str(), reply_id);
      return;
    }
    g_devices.erase(std::remove_if(g_devices.begin(), g_devices.end(),
      [&](const PairedDevice& d){ return d.addr.equalsIgnoreCase(addr); }), g_devices.end());
    persist_devices();
    send_ack(cmd.c_str(), reply_id);
    publish_devices("forget");
  } else if (cmd.equalsIgnoreCase("priority")) {
    if (!doc["order"].is<JsonArray>()) {
      send_error("missing_order", "order array required", reply_id);
      return;
    }
    JsonArrayConst order = doc["order"].as<JsonArrayConst>();
    int next_pri = 1;
    for (JsonVariantConst v : order) {
      const char* addr = v.as<const char*>();
      if (!addr) continue;
      if (PairedDevice* d = find_device(String(addr))) {
        d->priority = next_pri++;
      }
    }
    // Unmentioned devices follow.
    std::sort(g_devices.begin(), g_devices.end(), [](const PairedDevice& a, const PairedDevice& b){
      return a.priority < b.priority;
    });
    for (size_t i = 0; i < g_devices.size(); ++i) {
      g_devices[i].priority = static_cast<int>(i + 1);
    }
    persist_devices();
    send_ack(cmd.c_str(), reply_id);
    publish_devices("priority");
  } else if (cmd.equalsIgnoreCase("connect")) {
    const char* addr = doc["addr"] | nullptr;
    if (!addr || strlen(addr) == 0) {
      send_error("missing_addr", "addr required", reply_id);
      return;
    }
    esp_bd_addr_t target = {0};
    if (!addr_from_string(String(addr), target)) {
      send_error("bad_addr", addr, reply_id);
      return;
    }
    esp_err_t err = esp_a2d_sink_connect(target);
    if (err == ESP_OK) {
      g_last_connect_attempt_ms = millis();
      send_ack(cmd.c_str(), reply_id);
    } else {
      send_error("connect_failed", String(err).c_str(), reply_id);
    }
  } else if (cmd.equalsIgnoreCase("disconnect")) {
    if (!g_state.a2dp_connected) {
      send_error("not_connected", "no active link", reply_id);
      return;
    }
    esp_bd_addr_t* peer = a2dp_sink.get_current_peer_address();
    if (peer == nullptr) {
      send_error("missing_peer", "no peer address", reply_id);
      return;
    }
    esp_err_t err = esp_a2d_sink_disconnect(*peer);
    if (err == ESP_OK) {
      send_ack(cmd.c_str(), reply_id);
    } else {
      send_error("disconnect_failed", String(err).c_str(), reply_id);
    }
  } else {
    send_error("unknown_cmd", cmd.c_str(), reply_id);
  }
}

static void publish_link_state(const char* reason, JsonVariantConst reply_id) {
  if (g_state.a2dp_connected) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0)
    String remote = String(a2dp_sink.get_peer_name());
    if (!remote.isEmpty() && !remote.equalsIgnoreCase("unknown")) {
      g_state.device_name = remote;
      if (PairedDevice* d = find_device(g_state.device_addr)) {
        d->name = remote;
      }
    }
#endif
    if (PairedDevice* d = find_device(g_state.device_addr)) {
      d->connected = true;
      d->last_seen_ms = millis();
    }
  }
  DynamicJsonDocument doc(LINK_TX_CAPACITY);
  doc["type"] = "bt_state";
  doc["reason"] = reason;
  attach_id(doc, reply_id);
  doc["link_proto"] = LINK_PROTO_VERSION;
  doc["connected"] = g_state.a2dp_connected;
  doc["avrcp"] = g_state.avrcp_connected;
  doc["audio_active"] = g_state.audio_active;
  doc["playing"] = g_state.playing;
  doc["volume_pct"] = g_state.volume_pct;
  doc["sample_rate_hz"] = g_state.sample_rate_hz;
  if (g_state.device_addr.length()) doc["peer_addr"] = g_state.device_addr;
  if (g_state.device_name.length()) doc["peer_name"] = g_state.device_name;
  if (!g_state.title.isEmpty()) doc["title"] = g_state.title;
  if (!g_state.artist.isEmpty()) doc["artist"] = g_state.artist;
  if (!g_state.album.isEmpty()) doc["album"] = g_state.album;
  doc["paired_count"] = static_cast<uint32_t>(g_devices.size());
  doc["pairing_supported"] = true;
  if (!g_devices.empty()) {
    Serial.printf("[link] include %u devices in state\n", static_cast<unsigned>(g_devices.size()));
    JsonArray devices = doc.createNestedArray("devices");
    std::vector<PairedDevice> sorted = g_devices;
    std::sort(sorted.begin(), sorted.end(), [](const PairedDevice& a, const PairedDevice& b){
      return a.priority < b.priority;
    });
    for (const auto& d : sorted) {
      if (!d.addr.length()) continue;
      JsonObject obj = devices.createNestedObject();
      obj["addr"] = d.addr;
      if (d.name.length()) obj["name"] = sanitize_label(d.name);
      obj["priority"] = d.priority;
      obj["connected"] = d.connected;
      obj["last_seen_ms"] = d.last_seen_ms;
    }
  }
  JsonObject pairing = doc.createNestedObject("pairing");
  pairing["active"] = g_pairing_active;
  if (g_pairing_active && g_pairing_until_ms > millis()) {
    pairing["remaining_ms"] = g_pairing_until_ms - millis();
  } else {
    pairing["remaining_ms"] = 0;
  }

  link_send(doc, DEBUG_LOG_STATE);
}

static void publish_devices(const char* reason, JsonVariantConst reply_id) {
  DynamicJsonDocument doc(LINK_TX_CAPACITY);
  doc["type"] = "bt_devices";
  doc["reason"] = reason;
  doc["link_proto"] = LINK_PROTO_VERSION;
  attach_id(doc, reply_id);
  JsonObject pairing = doc.createNestedObject("pairing");
  pairing["active"] = g_pairing_active;
  if (g_pairing_active && g_pairing_until_ms > millis()) {
    pairing["remaining_ms"] = g_pairing_until_ms - millis();
  } else {
    pairing["remaining_ms"] = 0;
  }
  doc["pairing_supported"] = true;
  Serial.printf("[link] publish_devices count=%u\n", static_cast<unsigned>(g_devices.size()));
  JsonArray arr = doc.createNestedArray("devices");
  std::vector<PairedDevice> sorted = g_devices;
  std::sort(sorted.begin(), sorted.end(), [](const PairedDevice& a, const PairedDevice& b){
    return a.priority < b.priority;
  });
  for (const auto& d : sorted) {
    if (!d.addr.length()) continue;
    JsonObject obj = arr.createNestedObject();
    obj["addr"] = d.addr;
    if (d.name.length()) obj["name"] = sanitize_label(d.name);
    obj["priority"] = d.priority;
    obj["connected"] = d.connected;
    obj["last_seen_ms"] = d.last_seen_ms;
  }
  link_send(doc, true);
}

static void publish_hello(const char* reason, JsonVariantConst reply_id) {
  DynamicJsonDocument doc(LINK_TX_CAPACITY);
  doc["type"] = "hello";
  doc["reason"] = reason;
  doc["fw"] = "bt2i2s";
  doc["fw_version"] = FW_VERSION;
  doc["fw_build"] = FW_BUILD;
  doc["bt_name"] = BT_DEVICE_NAME;
  doc["link_proto"] = LINK_PROTO_VERSION;
  doc["uart_baud"] = LINK_UART_BAUD;
  doc["i2s_master"] = I2S_MASTER_MODE;
  doc["volume_default_pct"] = BT_DEFAULT_VOLUME_PERCENT;
  doc["pairing_supported"] = true;
  attach_id(doc, reply_id);
  JsonArray cmds = doc.createNestedArray("cmds");
  cmds.add("hello");
  cmds.add("state");
  cmds.add("play");
  cmds.add("pause");
  cmds.add("toggle");
  cmds.add("next");
  cmds.add("prev");
  cmds.add("volume");
  cmds.add("pair_start");
  cmds.add("pair_stop");
  cmds.add("forget");
  cmds.add("priority");
  cmds.add("connect");
  cmds.add("disconnect");
  cmds.add("devices");
  link_send(doc, true);
  g_last_hello_tx_ms = millis();
}

static void send_ack(const char* cmd, JsonVariantConst reply_id) {
  StaticJsonDocument<192> doc;
  doc["type"] = "ack";
  doc["status"] = "ok";
  doc["cmd"] = cmd;
  attach_id(doc, reply_id);
  link_send(doc, DEBUG_LOG_STATE);
}

static void send_error(const char* reason, const char* detail, JsonVariantConst reply_id) {
  StaticJsonDocument<256> doc;
  doc["type"] = "error";
  doc["reason"] = reason;
  doc["detail"] = detail;
  attach_id(doc, reply_id);
  link_send(doc, true);
}

static void attach_id(JsonDocument& dest, JsonVariantConst id) {
  if (!id.isNull()) {
    dest["id"] = id;
  }
}

static void link_send(const JsonDocument& doc, bool log) {
  if (g_link_ready && g_link != nullptr) {
    serializeJson(doc, *g_link);
    g_link->println();
  }
  if (log) {
    Serial.print("[link-tx] ");
    serializeJson(doc, Serial);
    Serial.println();
  }
}

static void log_state(const char* label) {
  if (!DEBUG_LOG_STATE) {
    return;
  }
  Serial.printf("[state:%s] conn=%d avrcp=%d playing=%d audio=%d vol=%u sr=%u\n",
                label,
                g_state.a2dp_connected,
                g_state.avrcp_connected,
                g_state.playing,
                g_state.audio_active,
                g_state.volume_pct,
                g_state.sample_rate_hz);
}
