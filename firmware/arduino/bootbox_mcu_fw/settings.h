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

  // DSP controls (placeholder for ADAU1701 parameter mapping)
  float dsp_master_db = -6.0f;
  float dsp_stereo_db = 0.0f;
  float dsp_sub_lo_db = 0.0f;
  float dsp_sub_hi_db = 0.0f;
  float dsp_cross_mains_hz = 120.0f;
  float dsp_cross_sub_hz = 80.0f;
  bool dsp_cross_linked = true;
  float dsp_sub_lo_hp_hz = 25.0f;
  float dsp_sub_lo_lp_hz = 80.0f;
  float dsp_sub_hi_hp_hz = 70.0f;
  float dsp_sub_hi_lp_hz = 160.0f;

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

  s.dsp_master_db = prefs.getFloat("dspM", s.dsp_master_db);
  s.dsp_stereo_db = prefs.getFloat("dspS", s.dsp_stereo_db);
  s.dsp_sub_lo_db = prefs.getFloat("dspL", s.dsp_sub_lo_db);
  s.dsp_sub_hi_db = prefs.getFloat("dspH", s.dsp_sub_hi_db);
  s.dsp_cross_mains_hz = prefs.getFloat("dspXm", s.dsp_cross_mains_hz);
  s.dsp_cross_sub_hz = prefs.getFloat("dspXs", s.dsp_cross_sub_hz);
  s.dsp_cross_linked = prefs.getBool("dspXL", s.dsp_cross_linked);
  s.dsp_sub_lo_hp_hz = prefs.getFloat("dspLH", s.dsp_sub_lo_hp_hz);
  s.dsp_sub_lo_lp_hz = prefs.getFloat("dspLL", s.dsp_sub_lo_lp_hz);
  s.dsp_sub_hi_hp_hz = prefs.getFloat("dspHH", s.dsp_sub_hi_hp_hz);
  s.dsp_sub_hi_lp_hz = prefs.getFloat("dspHL", s.dsp_sub_hi_lp_hz);

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

  prefs.putFloat("dspM", s.dsp_master_db);
  prefs.putFloat("dspS", s.dsp_stereo_db);
  prefs.putFloat("dspL", s.dsp_sub_lo_db);
  prefs.putFloat("dspH", s.dsp_sub_hi_db);
  prefs.putFloat("dspXm", s.dsp_cross_mains_hz);
  prefs.putFloat("dspXs", s.dsp_cross_sub_hz);
  prefs.putBool("dspXL", s.dsp_cross_linked);
  prefs.putFloat("dspLH", s.dsp_sub_lo_hp_hz);
  prefs.putFloat("dspLL", s.dsp_sub_lo_lp_hz);
  prefs.putFloat("dspHH", s.dsp_sub_hi_hp_hz);
  prefs.putFloat("dspHL", s.dsp_sub_hi_lp_hz);

  const char* thermValidKeys[2] = {"tc0v", "tc1v"};
  const char* thermNomKeys[2] = {"tc0n", "tc1n"};
  const char* thermBetaKeys[2] = {"tc0b", "tc1b"};
  for (int i = 0; i < 2; ++i) {
    prefs.putBool(thermValidKeys[i], s.thermistors[i].valid);
    prefs.putFloat(thermNomKeys[i], s.thermistors[i].nominal_ohms);
    prefs.putFloat(thermBetaKeys[i], s.thermistors[i].beta);
  }
}
