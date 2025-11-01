#pragma once
#include <Preferences.h>

struct Settings {
  bool pid_enabled = true;
  float setpoint1_c = 45.0f;
  float setpoint2_c = 55.0f;
  uint8_t fan_manual_pct = 30; // used when pid_enabled == false
  // PID tuning (basic defaults; adjust per hardware)
  float pid_kp = 4.0f;
  float pid_ki = 0.1f;
  float pid_kd = 0.0f;
  // Upload controls
  bool uploads_enabled = false;  // if false, reject upload endpoints
  String upload_token = "";     // if non-empty, require Authorization: Bearer <token>
};

inline void settingsLoad(Preferences& prefs, Settings& s) {
  s.pid_enabled = prefs.getBool("pid", s.pid_enabled);
  s.setpoint1_c = prefs.getFloat("sp1", s.setpoint1_c);
  s.setpoint2_c = prefs.getFloat("sp2", s.setpoint2_c);
  s.fan_manual_pct = prefs.getUChar("fman", s.fan_manual_pct);
  s.pid_kp = prefs.getFloat("kp", s.pid_kp);
  s.pid_ki = prefs.getFloat("ki", s.pid_ki);
  s.pid_kd = prefs.getFloat("kd", s.pid_kd);
  s.uploads_enabled = prefs.getBool("upl", s.uploads_enabled);
  s.upload_token = prefs.getString("utok", s.upload_token);
}

inline void settingsSave(Preferences& prefs, const Settings& s) {
  prefs.putBool("pid", s.pid_enabled);
  prefs.putFloat("sp1", s.setpoint1_c);
  prefs.putFloat("sp2", s.setpoint2_c);
  prefs.putUChar("fman", s.fan_manual_pct);
  prefs.putFloat("kp", s.pid_kp);
  prefs.putFloat("ki", s.pid_ki);
  prefs.putFloat("kd", s.pid_kd);
  prefs.putBool("upl", s.uploads_enabled);
  prefs.putString("utok", s.upload_token);
}
