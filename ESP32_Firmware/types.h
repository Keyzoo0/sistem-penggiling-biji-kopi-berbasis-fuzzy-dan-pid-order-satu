// =============================================================================
//  types.h — Enum & struct inti (SystemState, Command). Lihat ARCHITECTURE.md §12
// =============================================================================
#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>

// ── Operating state machine ──────────────────────────────────────────────────
enum OpState    : uint8_t { ST_BOOT, ST_IDLE, ST_RUNNING, ST_FINISHED, ST_FAULT };
enum RunSubMode : uint8_t { SUB_NONE, SUB_FUZZY, SUB_MANUAL };
enum FaultCode  : uint8_t { FLT_NONE, FLT_ESTOP, FLT_SENSOR, FLT_OVERTEMP };
enum RpmStatus  : uint8_t { RPMS_STARTUP, RPMS_NORMAL, RPMS_WARN_LOW,
                            RPMS_WARN_HIGH, RPMS_ERR_LOW, RPMS_ERR_HIGH };

// ── Command (input dari keypad & web → antrian) ──────────────────────────────
enum Profile : uint8_t { PROF_WAFI, PROF_FADEL };   // Wafi=suhu, Fadel=kecepatan
enum CmdType : uint8_t {
  CMD_NONE, CMD_START, CMD_STOP, CMD_ESTOP, CMD_RESET,
  CMD_SET_SETPOINT, CMD_SET_SERVO, CMD_SET_BLOWER,
  CMD_SET_DURATION, CMD_SET_PARAM, CMD_SET_FREQ,
  CMD_SET_PROFILE, CMD_SET_SPEED_SP, CMD_SET_SPARAM
};
enum ParamId  : uint8_t { P_KP, P_KI, P_KD, P_LAMBDA, P_MU, P_BETA };
enum SParamId : uint8_t { SP_KP, SP_KI, SP_KD };

struct Command {
  CmdType type;
  float   fval;   // nilai float (setpoint, param)
  int32_t ival;   // nilai int (servo, blower, sub-mode, paramId, durasi)
};

// ── Sumber kebenaran tunggal ─────────────────────────────────────────────────
struct SystemState {
  OpState     opState;
  RunSubMode  subMode;
  FaultCode   fault;

  // proses
  float suhu, setPoint, error, dError;
  float uFopid, integral, derivative, fisOut;
  int   blowerPct;
  int   servoDeg;

  // sensor lain
  float rpm, voltage, current, power, pf, energy, frequency;
  RpmStatus rpmStatus;
  bool  mlxOk;

  // sesi
  bool      logging;
  uint32_t  runStartMs;
  uint32_t  durationMin;
  char      logFile[32];

  // tunable (di-persist ke NVS via params.*)
  float Kp, Ki, Kd, lambda, mu, beta;
  float freqMotor;        // Wafi: kecepatan motor konstan (Hz, via VFD)
  // profil & kontrol kecepatan (Fadel)
  Profile profile;
  float   speedSP;        // target RPM drum (Fadel)
  float   sKp, sKi, sKd;  // PID kecepatan
  int     blowerConst;    // Fadel: blower konstan (%)
  float   vfdFreq;        // output frekuensi ke VFD (Hz)
};

#endif // TYPES_H
