#include "ui.h"
#include "config.h"
#include "state.h"
#include "sensors.h"          // rtcNow untuk jam
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "I2CKeyPad.h"
#include <stdarg.h>
#include <string.h>

static LiquidCrystal_I2C lcd(ADDR_LCD, 20, 4);
static I2CKeyPad         keypad(ADDR_KEYPAD);
static bool s_kpReady = false;

// index 0..15 = tombol, 16 = NoKey, 17 = Fail
static const char KEYS[18] = {
  '1','4','7','*', '2','5','8','0', '3','6','9','#', 'A','B','C','D', 'N','F'
};

// ── Navigasi UI ──────────────────────────────────────────────────────────────
enum Nav { NAV_HOME, NAV_START_FUZZY, NAV_PARAMS, NAV_INPUT, NAV_MONITOR };
static Nav s_nav = NAV_HOME;
static bool s_redraw = true;

static int  s_homeIdx  = 0;
static int  s_paramIdx = 0;
static int  s_page     = 0;
static String s_inBuf  = "";

struct InputTarget { const char* prompt; bool isFloat; CmdType cmd; int32_t paramId; };
static InputTarget s_in = { "", false, CMD_NONE, 0 };

static const char* HOME_ITEMS[] = { "1.Mulai FUZZY", "2.Mulai MANUAL", "3.Parameter", "4.Monitor" };
static const int   HOME_LEN = 4;
static const char* PARAM_LABELS[] = {
  "Kp", "Ki", "Kd", "Lambda", "Mu", "Beta", "SetPoint(C)", "Durasi(mnt)", "Servo(deg)"
};
static const int   PARAM_LEN = 9;

// ── Helper LCD (dipanggil saat I2C sudah di-lock) ────────────────────────────
static void lcdLine(int row, const char* fmt, ...) {
  char buf[21];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  int l = strlen(buf);
  for (int i = l; i < 20; i++) buf[i] = ' ';
  buf[20] = '\0';
  lcd.setCursor(0, row);
  lcd.print(buf);
}

static char getSingleKey() {
  static char last = 'N';
  if (!s_kpReady) return 'N';
  if (!I2C_TRYLOCK(20)) return 'N';
  uint8_t idx = keypad.getKey();
  I2C_UNLOCK();
  char k = (idx < 18) ? KEYS[idx] : 'N';
  if (k != 'N' && k != 'F' && k != last) { last = k; return k; }
  if (k == 'N' || k == 'F') last = 'N';
  return 'N';
}

void uiInit() {
  I2C_LOCK();
  lcd.init();
  lcd.backlight();
  s_kpReady = keypad.begin();
  I2C_UNLOCK();
  if (!s_kpReady) Serial.println("[KEYPAD] tidak ditemukan 0x20");
  I2C_LOCK();
  lcdLine(0, "Kopi Control");
  lcdLine(1, "Modular FreeRTOS");
  lcdLine(2, "Booting...");
  lcdLine(3, "");
  I2C_UNLOCK();
}

// ── float value helper untuk parameter ──────────────────────────────────────
static float paramValue(const SystemState& st, int idx) {
  switch (idx) {
    case 0: return st.Kp;    case 1: return st.Ki;  case 2: return st.Kd;
    case 3: return st.lambda;case 4: return st.mu;  case 5: return st.beta;
    case 6: return st.setPoint;
    case 7: return (float)st.durationMin;
    case 8: return (float)st.servoDeg;
  }
  return 0;
}
static void beginEdit(int idx) {
  s_in.prompt = PARAM_LABELS[idx];
  s_inBuf = "";
  if (idx <= 5)      { s_in.isFloat = true;  s_in.cmd = CMD_SET_PARAM; s_in.paramId = idx; }
  else if (idx == 6) { s_in.isFloat = true;  s_in.cmd = CMD_SET_SETPOINT; }
  else if (idx == 7) { s_in.isFloat = false; s_in.cmd = CMD_SET_DURATION; }
  else               { s_in.isFloat = false; s_in.cmd = CMD_SET_SERVO; }
  s_nav = NAV_INPUT; s_redraw = true;
}

// ── Render layar khusus operating-state ──────────────────────────────────────
static void renderFault(const SystemState& st) {
  const char* f = (st.fault == FLT_OVERTEMP) ? "OVER-TEMP" :
                  (st.fault == FLT_SENSOR)   ? "SENSOR GAGAL" :
                  (st.fault == FLT_ESTOP)    ? "EMERGENCY STOP" : "FAULT";
  I2C_LOCK();
  lcdLine(0, "!!! FAULT !!!");
  lcdLine(1, "%s", f);
  lcdLine(2, "T:%.1fC (max %d)", st.suhu, (int)SAFE_MAX_TEMP);
  lcdLine(3, "C = Reset (aman)");
  I2C_UNLOCK();
}
static void renderFinished(const SystemState& st) {
  I2C_LOCK();
  lcdLine(0, "** SELESAI **");
  lcdLine(1, "Blower & gas mati");
  lcdLine(2, "%s", st.logFile);
  lcdLine(3, "C = OK, ambil SD");
  I2C_UNLOCK();
}

// ── Scrollable menu ──────────────────────────────────────────────────────────
static void renderMenu(const char* title, const char* const items[], int count, int sel) {
  int top = constrain(sel - 1, 0, max(0, count - 3));
  I2C_LOCK();
  lcdLine(0, "%s", title);
  for (int i = 0; i < 3; i++) {
    int idx = top + i;
    if (idx < count) lcdLine(i + 1, "%c %s", idx == sel ? '>' : ' ', items[idx]);
    else             lcdLine(i + 1, "");
  }
  I2C_UNLOCK();
}

// ── Handler per-nav ──────────────────────────────────────────────────────────
static void navHome(const SystemState& st, char key) {
  if      (key == 'A' && s_homeIdx > 0)            { s_homeIdx--; s_redraw = true; }
  else if (key == 'B' && s_homeIdx < HOME_LEN - 1) { s_homeIdx++; s_redraw = true; }
  else if (key == 'C') {
    switch (s_homeIdx) {
      case 0: s_inBuf = ""; s_nav = NAV_START_FUZZY; break;
      case 1: cmdSendT(CMD_START, 0, SUB_MANUAL); s_page = 0; s_nav = NAV_MONITOR; break;
      case 2: s_paramIdx = 0; s_nav = NAV_PARAMS; break;
      case 3: s_page = 0; s_nav = NAV_MONITOR; break;
    }
    s_redraw = true;
    return;
  }
  if (s_redraw) { renderMenu("Menu Utama", HOME_ITEMS, HOME_LEN, s_homeIdx); s_redraw = false; }
}

static void navStartFuzzy(const SystemState& st, char key) {
  if (key >= '0' && key <= '9') { if (s_inBuf.length() < 3) s_inBuf += key; s_redraw = true; }
  else if (key == 'A') { if (s_inBuf.length()) s_inBuf.remove(s_inBuf.length() - 1); s_redraw = true; }
  else if (key == 'C') {                       // set sudut gas
    if (s_inBuf.length()) { cmdSendT(CMD_SET_SERVO, 0, s_inBuf.toInt()); s_inBuf = ""; }
    s_redraw = true;
  }
  else if (key == '#') { cmdSendT(CMD_START, 0, SUB_FUZZY); s_page = 0; s_nav = NAV_MONITOR; s_redraw = true; return; }
  else if (key == 'D') { s_nav = NAV_HOME; s_redraw = true; return; }

  if (s_redraw) {
    I2C_LOCK();
    lcdLine(0, "Set Gas (servo)");
    lcdLine(1, "Sekarang: %d deg", st.servoDeg);
    lcdLine(2, "Input:%s  C=Set", s_inBuf.c_str());
    lcdLine(3, "#=MULAI  D=Batal");
    I2C_UNLOCK();
    s_redraw = false;
  }
}

static void navParams(const SystemState& st, char key) {
  if      (key == 'A' && s_paramIdx > 0)             { s_paramIdx--; s_redraw = true; }
  else if (key == 'B' && s_paramIdx < PARAM_LEN - 1) { s_paramIdx++; s_redraw = true; }
  else if (key == 'C') { beginEdit(s_paramIdx); return; }
  else if (key == 'D') { s_nav = NAV_HOME; s_redraw = true; return; }

  if (s_redraw) {
    I2C_LOCK();
    lcdLine(0, "Parameter");
    lcdLine(1, "> %s", PARAM_LABELS[s_paramIdx]);
    if (s_paramIdx <= 6) lcdLine(2, "Nilai: %.4f", paramValue(st, s_paramIdx));
    else                 lcdLine(2, "Nilai: %d", (int)paramValue(st, s_paramIdx));
    lcdLine(3, "A/B pilih C edit D<");
    I2C_UNLOCK();
    s_redraw = false;
  }
}

static void navInput(const SystemState& st, char key) {
  if      (key >= '0' && key <= '9') { s_inBuf += key; s_redraw = true; }
  else if (key == '*' && s_in.isFloat && s_inBuf.indexOf('.') < 0) { s_inBuf += '.'; s_redraw = true; }
  else if (key == 'D') { if (s_inBuf.length()) s_inBuf.remove(s_inBuf.length() - 1); s_redraw = true; }
  else if (key == 'C' && s_inBuf.length()) {
    float fv = s_inBuf.toFloat();
    int32_t iv = s_inBuf.toInt();
    switch (s_in.cmd) {
      case CMD_SET_PARAM:    cmdSendT(CMD_SET_PARAM, fv, s_in.paramId); break;
      case CMD_SET_SETPOINT: cmdSendT(CMD_SET_SETPOINT, fv); break;
      case CMD_SET_DURATION: cmdSendT(CMD_SET_DURATION, 0, iv); break;
      case CMD_SET_SERVO:    cmdSendT(CMD_SET_SERVO, 0, iv); break;
      default: break;
    }
    s_nav = NAV_PARAMS; s_redraw = true; return;
  }
  if (s_redraw) {
    I2C_LOCK();
    lcdLine(0, "Edit %s", s_in.prompt);
    lcdLine(1, "Nilai: %s", s_inBuf.c_str());
    lcdLine(2, s_in.isFloat ? "*=titik  D=hapus" : "D=hapus");
    lcdLine(3, "C=OK");
    I2C_UNLOCK();
    s_redraw = false;
  }
}

static void navMonitor(const SystemState& st, char key) {
  int maxPage = (st.subMode == SUB_FUZZY) ? 2 : 1;
  if      (key == 'C') { s_page = (s_page < maxPage) ? s_page + 1 : 0; s_redraw = true; }
  else if (key == 'D') { s_nav = NAV_HOME; s_redraw = true; return; }
  else if (key == '*') { cmdSendT(CMD_STOP); }
  else if (st.subMode == SUB_MANUAL && key == 'A') { cmdSendT(CMD_SET_BLOWER, 0, constrain(st.blowerPct + 5, (int)DIMMER_MIN, (int)DIMMER_MAX)); }
  else if (st.subMode == SUB_MANUAL && key == 'B') { cmdSendT(CMD_SET_BLOWER, 0, constrain(st.blowerPct - 5, (int)DIMMER_MIN, (int)DIMMER_MAX)); }

  // Monitor selalu refresh (data live).
  // PENTING: rtcNow() mengambil I2C mutex → panggil SEBELUM I2C_LOCK() (mutex non-rekursif).
  DateTime now = rtcNow();
  I2C_LOCK();
  if (s_page == 0) {
    lcdLine(0, "T:%.1fC  SP:%.1f", st.suhu, st.setPoint);
    lcdLine(1, "Blower:%d%%  RPM:%.1f", st.blowerPct, st.rpm);
    lcdLine(2, "Daya:%.0fW Gas:%d", st.power, st.servoDeg);
    lcdLine(3, "%02d:%02d:%02d  %s", now.hour(), now.minute(), now.second(),
            st.subMode == SUB_FUZZY ? "FUZZY" : st.subMode == SUB_MANUAL ? "MANUAL" : "IDLE");
  } else if (s_page == 1) {
    long rem = 0;
    if (st.opState == ST_RUNNING) {
      long tot = (long)st.durationMin * 60L;
      long el  = (long)((millis() - st.runStartMs) / 1000UL);
      rem = tot - el; if (rem < 0) rem = 0;
    }
    lcdLine(0, "Sisa: %02ld:%02ld", rem / 60, rem % 60);
    lcdLine(1, "Log:%s", st.logging ? "REC" : "OFF");
    lcdLine(2, st.subMode == SUB_MANUAL ? "A/B atur blower" : "C=page  *=STOP");
    lcdLine(3, "%s  D=Menu", st.logging ? (char*)st.logFile : (char*)"");
  } else {
    lcdLine(0, "E:%.1fC  u:%.2f", st.error, st.uFopid);
    lcdLine(1, "FIS:%.1f%%", st.fisOut);
    lcdLine(2, "I:%.2f D:%.2f", st.integral, st.derivative);
    lcdLine(3, "C=page  *=STOP");
  }
  I2C_UNLOCK();
  s_redraw = false;
}

void uiTick() {
  SystemState st; stateGet(st);
  char key = getSingleKey();

  // Override layar untuk FAULT / FINISHED (apa pun nav-nya)
  if (st.opState == ST_FAULT)    { renderFault(st);    if (key == 'C') cmdSendT(CMD_RESET); s_nav = NAV_HOME; s_redraw = true; return; }
  if (st.opState == ST_FINISHED) { renderFinished(st); if (key == 'C') cmdSendT(CMD_RESET); s_nav = NAV_HOME; s_redraw = true; return; }

  switch (s_nav) {
    case NAV_HOME:        navHome(st, key);       break;
    case NAV_START_FUZZY: navStartFuzzy(st, key); break;
    case NAV_PARAMS:      navParams(st, key);     break;
    case NAV_INPUT:       navInput(st, key);      break;
    case NAV_MONITOR:     navMonitor(st, key);    break;
  }
}
