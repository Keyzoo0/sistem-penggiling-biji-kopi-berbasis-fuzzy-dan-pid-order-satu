// =============================================================================
//  Kopi Control — Sistem Kontrol Suhu Biji Kopi (ESP32)
//  Arsitektur MODULAR · dual-core FreeRTOS · non-blocking · command-queue.
//  Lihat ARCHITECTURE.md untuk rancangan lengkap.
//
//  Core 1 : realtimeTask  → sensor suhu, drain command, safety, kontrol, aktuator
//  Core 0 : pzemTask, uiTask, logTask, wsTask + AsyncTCP (web)
//  Sumber kebenaran tunggal: g_state (state.*). Hanya realtimeTask penulis proses.
// =============================================================================
#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "types.h"
#include "state.h"
#include "sensors.h"
#include "actuators.h"
#include "control.h"
#include "safety.h"
#include "logging.h"
#include "ui.h"
#include "webserver.h"
#include "params.h"
#include "vfd.h"
#include "control_speed.h"

// =============================================================================
//  Transisi state (dipanggil hanya dari realtimeTask)
// =============================================================================
static void doEnterIdle(SystemState& st) {
  st.opState = ST_IDLE; st.subMode = SUB_NONE; st.fault = FLT_NONE;
}
static void doEnterFault(SystemState& st, FaultCode f) {
  if (st.logging) loggingStop(st);
  st.opState = ST_FAULT; st.fault = f; st.subMode = SUB_NONE;
  actuatorsSafeState(); st.blowerPct = 0; st.servoDeg = 0;
  Serial.printf("[FAULT] code=%d T=%.1f\n", (int)f, st.suhu);
}
static void doEnterFinished(SystemState& st) {
  if (st.logging) loggingStop(st);
  st.opState = ST_FINISHED; st.subMode = SUB_NONE;
  actuatorsSafeState(); st.blowerPct = 0; st.servoDeg = 0;
  Serial.println("[FINISHED] durasi tercapai");
}
static void applyParam(SystemState& st, ParamId id, float v) {
  switch (id) {
    case P_KP: st.Kp = v; break; case P_KI: st.Ki = v; break; case P_KD: st.Kd = v; break;
    case P_LAMBDA: st.lambda = v; break; case P_MU: st.mu = v; break; case P_BETA: st.beta = v; break;
  }
}
static void applySParam(SystemState& st, SParamId id, float v) {
  switch (id) { case SP_KP: st.sKp = v; break; case SP_KI: st.sKi = v; break; case SP_KD: st.sKd = v; break; }
}

// =============================================================================
//  Pemroses command (dari keypad & web, via antrian)
// =============================================================================
static void applyCommand(SystemState& st, const Command& c, uint32_t now) {
  switch (c.type) {
    case CMD_START:
      if (st.opState == ST_IDLE) {
        st.subMode    = (c.ival == SUB_MANUAL) ? SUB_MANUAL : SUB_FUZZY;
        st.opState    = ST_RUNNING;
        st.fault      = FLT_NONE;
        st.runStartMs = now;
        controlReset(st);
        controlSpeedReset(st);
        safetyNoteValidTemp(now);
        if (st.subMode == SUB_MANUAL)
          st.blowerPct = constrain(st.blowerPct, (int)DIMMER_MIN, (int)DIMMER_MAX);
        loggingStart(st);
        Serial.printf("[START] %s | servo=%d | dur=%lumnt | file=%s\n",
                      st.subMode == SUB_FUZZY ? "FUZZY" : "MANUAL",
                      st.servoDeg, (unsigned long)st.durationMin, st.logFile);
      }
      break;

    case CMD_STOP:
      if (st.opState == ST_RUNNING) {
        loggingStop(st);
        actuatorsSafeState(); st.blowerPct = 0; st.servoDeg = 0;
        doEnterIdle(st);
        Serial.println("[STOP] operator");
      }
      break;

    case CMD_ESTOP:
      doEnterFault(st, FLT_ESTOP);
      break;

    case CMD_RESET:
      if (st.opState == ST_FAULT) {
        if (st.suhu < SAFE_MAX_TEMP) { doEnterIdle(st); Serial.println("[RESET] fault cleared"); }
        else Serial.println("[RESET] ditolak: suhu masih tinggi");
      } else if (st.opState == ST_FINISHED) {
        doEnterIdle(st);
      }
      break;

    case CMD_SET_SETPOINT:
      st.setPoint = c.fval; paramsSave(st);
      break;

    case CMD_SET_SERVO:
      st.servoDeg = constrain((int)c.ival, 0, SERVO_MAX);
      actuatorSetServo(st.servoDeg); paramsSave(st);
      break;

    case CMD_SET_BLOWER:
      st.blowerConst = constrain((int)c.ival, (int)DIMMER_MIN, (int)DIMMER_MAX);  // Fadel: blower konstan
      if (st.subMode == SUB_MANUAL) st.blowerPct = st.blowerConst;                // Wafi manual (live)
      if (st.opState == ST_IDLE) paramsSave(st);  // konfig sengaja (web/keypad saat siaga) → persist
      break;   // saat RUNNING (slider manual sering) tak di-NVS

    case CMD_SET_PROFILE:
      if (st.opState == ST_IDLE) { st.profile = (c.ival == PROF_FADEL) ? PROF_FADEL : PROF_WAFI; paramsSave(st); }
      break;

    case CMD_SET_SPEED_SP:
      st.speedSP = constrain(c.fval, 0.0f, SPEED_SP_MAX); paramsSave(st);
      break;

    case CMD_SET_SPARAM:
      applySParam(st, (SParamId)c.ival, c.fval); paramsSave(st);
      break;

    case CMD_SET_DURATION:
      st.durationMin = constrain((uint32_t)c.ival, DURATION_MIN_LIMIT, DURATION_MAX_LIMIT);
      paramsSave(st);
      break;

    case CMD_SET_PARAM:
      applyParam(st, (ParamId)c.ival, c.fval); paramsSave(st);
      break;

    case CMD_SET_FREQ:
      st.freqMotor = constrain(c.fval, 0.0f, FREQ_MOTOR_MAX); paramsSave(st);
      break;

    case CMD_SET_VFD:
      st.vfdFreq = constrain(c.fval, 0.0f, VFD_FREQ_MAX_HZ);   // Fadel manual: freq langsung (live, tak di-NVS)
      break;

    default: break;
  }
}

// =============================================================================
//  Publikasi working-copy → g_state (field milik realtimeTask; pzem ditulis pzemTask)
// =============================================================================
static void publishState(const SystemState& st) {
  STATE_LOCK();
  g_state.opState = st.opState; g_state.subMode = st.subMode; g_state.fault = st.fault;
  g_state.suhu = st.suhu; g_state.setPoint = st.setPoint; g_state.error = st.error; g_state.dError = st.dError;
  g_state.uFopid = st.uFopid; g_state.integral = st.integral; g_state.derivative = st.derivative; g_state.fisOut = st.fisOut;
  g_state.blowerPct = st.blowerPct; g_state.servoDeg = st.servoDeg;
  g_state.rpm = st.rpm; g_state.rpmStatus = st.rpmStatus; g_state.mlxOk = st.mlxOk;
  g_state.logging = st.logging; g_state.runStartMs = st.runStartMs; g_state.durationMin = st.durationMin;
  g_state.Kp = st.Kp; g_state.Ki = st.Ki; g_state.Kd = st.Kd;
  g_state.lambda = st.lambda; g_state.mu = st.mu; g_state.beta = st.beta;
  g_state.freqMotor = st.freqMotor;
  g_state.profile = st.profile; g_state.speedSP = st.speedSP;
  g_state.sKp = st.sKp; g_state.sKi = st.sKi; g_state.sKd = st.sKd;
  g_state.blowerConst = st.blowerConst; g_state.vfdFreq = st.vfdFreq;
  memcpy(g_state.logFile, st.logFile, sizeof(g_state.logFile));
  STATE_UNLOCK();
}

// =============================================================================
//  TASKS
// =============================================================================
static void realtimeTask(void* pv) {
  TickType_t last = xTaskGetTickCount();
  uint32_t tick = 0;
  const uint32_t encEvery  = ENC_PERIOD_MS / RT_TICK_MS;
  const uint32_t ctrlEvery = CONTROL_PERIOD_MS / RT_TICK_MS;

  SystemState st; stateGet(st);
  controlReset(st);
  safetyInit();

  for (;;) {
    uint32_t now = millis();

    // suhu (I2C-locked)
    bool ok; float t = sensorsReadTemp(ok);
    if (ok) { st.suhu = t; st.mlxOk = true; safetyNoteValidTemp(now); }
    else    { st.mlxOk = false; }

    // RPM
    if (tick % encEvery == 0) { float rd; sensorsServiceRPM(ENC_PERIOD_MS, rd); st.rpm = rd; }

    // command queue (keypad + web)
    Command c;
    while (cmdRecv(c)) applyCommand(st, c, now);

    // safety supervisor
    if (st.opState == ST_RUNNING) {
      FaultCode f = safetyEvaluate(st, now);
      if (f != FLT_NONE) {
        doEnterFault(st, f);
      } else if ((uint32_t)(now - st.runStartMs) >= (uint32_t)st.durationMin * 60UL * 1000UL) {
        doEnterFinished(st);
      }
    }
    st.rpmStatus = safetyRpmStatus(st, now);

    // kontrol tiap CONTROL_PERIOD — per profil (PID/fuzzy hanya di mode FUZZY;
    //   mode MANUAL: blower (Wafi) / vfdFreq (Fadel) di-set langsung operator)
    if (st.opState == ST_RUNNING && st.subMode == SUB_FUZZY && (tick % ctrlEvery == 0)) {
      if (st.profile == PROF_WAFI) controlCompute(st);        // Wafi: suhu → blower
      else                         controlSpeedCompute(st);   // Fadel: RPM → vfdFreq
    }

    // aktuator
    if (st.opState == ST_RUNNING) {
      actuatorSetBlower(st.profile == PROF_FADEL ? st.blowerConst : st.blowerPct);
      actuatorSetServo(st.servoDeg);
    } else if (st.opState == ST_FAULT || st.opState == ST_FINISHED) {
      actuatorsSafeState(); st.blowerPct = 0; st.servoDeg = 0;
    }
    actuatorLed(st.opState);

    publishState(st);
    tick++;
    vTaskDelayUntil(&last, pdMS_TO_TICKS(RT_TICK_MS));
  }
}

static void pzemTask(void* pv) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    float v, i, p, e, f, pf;
    sensorsReadPzem(v, i, p, e, f, pf);
    STATE_LOCK();
    g_state.voltage = v; g_state.current = i; g_state.power = p;
    g_state.energy = e; g_state.frequency = f; g_state.pf = pf;
    STATE_UNLOCK();
    vTaskDelayUntil(&last, pdMS_TO_TICKS(PZEM_PERIOD_MS));
  }
}

static void uiTaskFn(void* pv) {
  TickType_t last = xTaskGetTickCount();
  for (;;) { uiTick(); vTaskDelayUntil(&last, pdMS_TO_TICKS(UI_PERIOD_MS)); }
}

static void logTaskFn(void* pv) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    SystemState st; stateGet(st);
    if (st.logging && st.opState == ST_RUNNING) loggingAppend(st);
    vTaskDelayUntil(&last, pdMS_TO_TICKS(LOG_PERIOD_MS));
  }
}

static void wsTaskFn(void* pv) {
  TickType_t last = xTaskGetTickCount();
  for (;;) { webBroadcast(); vTaskDelayUntil(&last, pdMS_TO_TICKS(WS_PERIOD_MS)); }
}

// VFD via Modbus (core 0) — Modbus lambat, dipisah dari loop kontrol.
//  Target freq: Fadel = hasil PID (vfdFreq); Wafi = freqMotor (konstan).
static void vfdTask(void* pv) {
  TickType_t last = xTaskGetTickCount();
  bool wasRun = false;
  for (;;) {
    SystemState st; stateGet(st);
    bool run = (st.opState == ST_RUNNING);
    float hz = (st.profile == PROF_FADEL) ? st.vfdFreq : st.freqMotor;
    if (run) {
      if (!wasRun) { vfdRun(); wasRun = true; }
      vfdSetFrequency(hz);
    } else if (wasRun) {
      vfdStop(); wasRun = false;
    }
    vTaskDelayUntil(&last, pdMS_TO_TICKS(400));
  }
}

// =============================================================================
//  setup() / loop()
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Kopi Control — Modular FreeRTOS ===");

  stateInit();

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(100000);

  sensorsInit();
  actuatorsInit();
  vfdInit();
  loggingInit();
  uiInit();
  safetyInit();

  paramsLoad(g_state);                  // tunables + servo + freq dari NVS (default config bila kosong)
  actuatorSetServo(g_state.servoDeg);   // pasang servo ke posisi tersimpan
  STATE_LOCK();
  g_state.opState  = ST_IDLE;
  STATE_UNLOCK();

  webInit();

  xTaskCreatePinnedToCore(realtimeTask, "rt",   8192, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(pzemTask,     "pzem", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(uiTaskFn,     "ui",   6144, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(logTaskFn,    "log",  8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(wsTaskFn,     "ws",   8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(vfdTask,      "vfd",  4096, NULL, 1, NULL, 0);

  Serial.println("[SETUP] selesai, tasks berjalan");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));   // semua kerja di task; loop idle
}
