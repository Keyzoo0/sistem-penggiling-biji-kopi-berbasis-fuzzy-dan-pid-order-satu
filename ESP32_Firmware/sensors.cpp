#include "sensors.h"
#include "config.h"
#include "state.h"
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <PZEM004Tv30.h>

static Adafruit_MLX90614 mlx = Adafruit_MLX90614();
static RTC_DS3231 rtc;
static bool s_mlxReady = false;
static bool s_rtcReady = false;

static PZEM004Tv30 pzem(Serial2, PIN_PZEM_RX, PIN_PZEM_TX);

// ── Encoder ──────────────────────────────────────────────────────────────────
static volatile int32_t s_pulseCount = 0;
static portMUX_TYPE     s_encMux = portMUX_INITIALIZER_UNLOCKED;
#define RPM_SAMPLES 10
static float s_rpmBuf[RPM_SAMPLES];
static int   s_rpmIdx = 0;
static bool  s_rpmBufReady = false;

static void IRAM_ATTR encoderISR() {
  portENTER_CRITICAL_ISR(&s_encMux);
  s_pulseCount += (digitalRead(PIN_ENC_B) != digitalRead(PIN_ENC_A)) ? -1 : 1;
  portEXIT_CRITICAL_ISR(&s_encMux);
}

void sensorsInit() {
  if (mlx.begin()) { s_mlxReady = true;  Serial.println("[MLX] OK"); }
  else             { s_mlxReady = false; Serial.println("[MLX] NOT FOUND"); }

  if (rtc.begin()) {
    s_rtcReady = true;
    if (rtc.lostPower()) rtcSyncFromCompile();
  } else {
    s_rtcReady = false; Serial.println("[RTC] NOT FOUND");
  }

  Serial2.begin(9600, SERIAL_8N1, PIN_PZEM_RX, PIN_PZEM_TX);

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encoderISR, CHANGE);
  for (int i = 0; i < RPM_SAMPLES; i++) s_rpmBuf[i] = 0.0f;
}

float sensorsReadTemp(bool& ok) {
  ok = false;
  if (!s_mlxReady) return 0.0f;
  if (!I2C_TRYLOCK(50)) return 0.0f;        // bus sibuk → lewati siklus ini
  float t = mlx.readObjectTempC();
  I2C_UNLOCK();
  if (isnan(t) || t < SENSOR_MIN_TEMP || t > SENSOR_MAX_TEMP) return t;  // ok=false
  ok = true;
  return t;
}

void sensorsServiceRPM(uint32_t dtMs, float& rpmDryerOut) {
  portENTER_CRITICAL(&s_encMux);
  int32_t ticks = s_pulseCount;
  s_pulseCount = 0;
  portEXIT_CRITICAL(&s_encMux);

  float revs = (float)ticks / (float)ENC_EDGES_PER_REV;
  float rpm  = revs * (60000.0f / (float)dtMs);
  if (rpm < 0) rpm = -rpm;

  if (!s_rpmBufReady && rpm > 0.5f) {
    for (int i = 0; i < RPM_SAMPLES; i++) s_rpmBuf[i] = rpm;
    s_rpmBufReady = true;
  }
  s_rpmBuf[s_rpmIdx] = rpm;
  s_rpmIdx = (s_rpmIdx + 1) % RPM_SAMPLES;
  float sum = 0;
  for (int i = 0; i < RPM_SAMPLES; i++) sum += s_rpmBuf[i];
  rpmDryerOut = (sum / RPM_SAMPLES) / RPM_GEAR_RATIO;
}

void sensorsReadPzem(float& v, float& i, float& p, float& e, float& f, float& pfOut) {
  v = pzem.voltage();   i = pzem.current();   p = pzem.power();
  e = pzem.energy();    f = pzem.frequency(); pfOut = pzem.pf();
  if (isnan(v)) v = 0;  if (isnan(i)) i = 0;  if (isnan(p)) p = 0;
  if (isnan(e)) e = 0;  if (isnan(f)) f = 0;  if (isnan(pfOut)) pfOut = 0;
}

DateTime rtcNow() {
  if (!s_rtcReady) return DateTime((uint32_t)0);
  I2C_LOCK();
  DateTime n = rtc.now();
  I2C_UNLOCK();
  return n;
}

void rtcSyncFromCompile() {
  if (!s_rtcReady) return;
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
}
