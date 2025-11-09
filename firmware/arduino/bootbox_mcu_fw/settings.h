#pragma once
#include <Preferences.h>

#include "config.h"

struct Settings {
  bool pid_enabled = true;
  float setpoint1_c = 45.0f;
  float setpoint2_c = 55.0f;
  uint8_t fan_manual_pct = 30; // used when pid_enabled == false
  // PID tuning (basic defaults; adjust per hardware)
  float pid_kp = 4.0f;
  float pid_ki = 0.1f;
  float pid_kd = 0.0f;

  struct ThermCal {
    bool valid = false;
    float nominal_ohms = 0.0f;
    float beta = 0.0f;
  };
  ThermCal thermistors[2] = {
    {false, THERMISTOR1_PARAMS.nominal_resistance_ohms, THERMISTOR1_PARAMS.beta_coefficient},
    {false, THERMISTOR2_PARAMS.nominal_resistance_ohms, THERMISTOR2_PARAMS.beta_coefficient}
  };
};

inline void settingsLoad(Preferences& prefs, Settings& s) {
  s.pid_enabled = prefs.getBool("pid", s.pid_enabled);
  s.setpoint1_c = prefs.getFloat("sp1", s.setpoint1_c);
  s.setpoint2_c = prefs.getFloat("sp2", s.setpoint2_c);
  s.fan_manual_pct = prefs.getUChar("fman", s.fan_manual_pct);
  s.pid_kp = prefs.getFloat("kp", s.pid_kp);
  s.pid_ki = prefs.getFloat("ki", s.pid_ki);
  s.pid_kd = prefs.getFloat("kd", s.pid_kd);

  const char* thermValidKeys[2] = {"tc0v", "tc1v"};
  const char* thermNomKeys[2] = {"tc0n", "tc1n"};
  const char* thermBetaKeys[2] = {"tc0b", "tc1b"};
  for (int i = 0; i < 2; ++i) {
    s.thermistors[i].valid = prefs.getBool(thermValidKeys[i], s.thermistors[i].valid);
    s.thermistors[i].nominal_ohms = prefs.getFloat(thermNomKeys[i], s.thermistors[i].nominal_ohms);
    s.thermistors[i].beta = prefs.getFloat(thermBetaKeys[i], s.thermistors[i].beta);
  }
}

inline void settingsSave(Preferences& prefs, const Settings& s) {
  prefs.putBool("pid", s.pid_enabled);
  prefs.putFloat("sp1", s.setpoint1_c);
  prefs.putFloat("sp2", s.setpoint2_c);
  prefs.putUChar("fman", s.fan_manual_pct);
  prefs.putFloat("kp", s.pid_kp);
  prefs.putFloat("ki", s.pid_ki);
  prefs.putFloat("kd", s.pid_kd);

  const char* thermValidKeys[2] = {"tc0v", "tc1v"};
  const char* thermNomKeys[2] = {"tc0n", "tc1n"};
  const char* thermBetaKeys[2] = {"tc0b", "tc1b"};
  for (int i = 0; i < 2; ++i) {
    prefs.putBool(thermValidKeys[i], s.thermistors[i].valid);
    prefs.putFloat(thermNomKeys[i], s.thermistors[i].nominal_ohms);
    prefs.putFloat(thermBetaKeys[i], s.thermistors[i].beta);
  }
}
