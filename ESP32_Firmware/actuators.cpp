#include "actuators.h"
#include "config.h"
#include "types.h"
#include <RBDdimmer.h>
#include <ESP32Servo.h>
#include <Preferences.h>

static dimmerLamp dimmer(PIN_DIMMER, PIN_ZEROCROSS);
static Servo      servo1;
static Preferences prefs;
static int s_blower = 0;
static int s_servo  = SERVO_MAX;

void actuatorsInit() {
  dimmer.begin(NORMAL_MODE, ON);
  dimmer.setPower(0);
  s_blower = 0;

  servoLoadNVS();
  servo1.setPeriodHertz(50);
  servo1.attach(PIN_SERVO, 500, 2400);
  servo1.write(s_servo);

  ledcAttach(PIN_LED, 2000, 8);
  ledcWrite(PIN_LED, 0);
}

void actuatorSetBlower(int pct) {
  pct = constrain(pct, (int)DIMMER_MIN, (int)DIMMER_MAX);
  s_blower = pct;
  dimmer.setPower(pct);
}
int actuatorGetBlower() { return s_blower; }

void actuatorSetServo(int deg, bool persist) {
  deg = constrain(deg, 0, SERVO_MAX);
  s_servo = deg;
  servo1.write(deg);
  if (persist) servoSaveNVS();
}
int actuatorGetServo() { return s_servo; }

void actuatorsSafeState() {
  s_blower = 0; dimmer.setPower(0);
  s_servo  = 0; servo1.write(0);
}

void actuatorLed(uint8_t opState) {
  switch (opState) {
    case ST_RUNNING:  ledcWrite(PIN_LED, 255); break;
    case ST_FAULT:    ledcWrite(PIN_LED, ((millis() / 150) % 2) ? 255 : 0); break;
    case ST_FINISHED: ledcWrite(PIN_LED, ((millis() / 600) % 2) ? 180 : 0); break;
    default:          ledcWrite(PIN_LED, 20);  break;   // IDLE: redup
  }
}

void servoSaveNVS() {
  prefs.begin("servocal", false);
  prefs.putInt("angle", s_servo);
  prefs.end();
}
void servoLoadNVS() {
  prefs.begin("servocal", false);
  s_servo = prefs.getInt("angle", SERVO_MAX);
  prefs.end();
  s_servo = constrain(s_servo, 0, SERVO_MAX);
}
