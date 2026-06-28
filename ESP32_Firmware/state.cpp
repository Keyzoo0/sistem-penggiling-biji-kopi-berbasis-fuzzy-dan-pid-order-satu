#include "state.h"
#include "config.h"
#include <string.h>

SystemState       g_state;
SemaphoreHandle_t g_stateMux = nullptr;
SemaphoreHandle_t g_i2cMux   = nullptr;
SemaphoreHandle_t g_sdMux    = nullptr;
QueueHandle_t     g_cmdQueue = nullptr;

void stateInit() {
  g_stateMux = xSemaphoreCreateMutex();
  g_i2cMux   = xSemaphoreCreateMutex();
  g_sdMux    = xSemaphoreCreateMutex();
  g_cmdQueue = xQueueCreate(16, sizeof(Command));

  memset(&g_state, 0, sizeof(g_state));
  g_state.opState     = ST_BOOT;
  g_state.subMode     = SUB_NONE;
  g_state.fault       = FLT_NONE;
  g_state.setPoint    = DEF_SETPOINT;
  g_state.durationMin = DEF_DURATION_MIN;
  g_state.servoDeg    = SERVO_MAX;     // ditimpa oleh NVS saat actuatorsInit()
  g_state.blowerPct   = 0;
  g_state.Kp = DEF_KP; g_state.Ki = DEF_KI; g_state.Kd = DEF_KD;
  g_state.lambda = DEF_LAMBDA; g_state.mu = DEF_MU; g_state.beta = DEF_BETA;
  g_state.freqMotor   = DEF_FREQ_MOTOR;
  g_state.profile     = PROF_WAFI;
  g_state.speedSP     = DEF_SPEED_SP;
  g_state.sKp = DEF_SKP; g_state.sKi = DEF_SKI; g_state.sKd = DEF_SKD;
  g_state.blowerConst = DEF_BLOWER_CONST;
  g_state.vfdFreq     = 0.0f;
  g_state.rpmStatus   = RPMS_STARTUP;
  g_state.logFile[0]  = '\0';
}

void stateGet(SystemState& dst) {
  STATE_LOCK();
  dst = g_state;
  STATE_UNLOCK();
}

bool cmdSend(const Command& c) {
  if (!g_cmdQueue) return false;
  return xQueueSend(g_cmdQueue, &c, 0) == pdTRUE;
}

bool cmdSendT(CmdType t, float f, int32_t i) {
  Command c; c.type = t; c.fval = f; c.ival = i;
  return cmdSend(c);
}

bool cmdRecv(Command& c) {
  if (!g_cmdQueue) return false;
  return xQueueReceive(g_cmdQueue, &c, 0) == pdTRUE;
}
