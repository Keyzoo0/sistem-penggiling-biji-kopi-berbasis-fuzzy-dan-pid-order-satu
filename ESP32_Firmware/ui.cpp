// =============================================================================
//  ui.cpp — LCD 20x4 + Keypad 4x4 (profil-aware, page-based)
//
//  PRINSIP: g_state = sumber kebenaran tunggal. LCD merender dari snapshot,
//  keypad mengirim Command ke antrian yang sama dengan web → "last control wins"
//  (siapa pun mengirim perintah terakhir, itu yang berlaku). Profil aktif (WAFI/
//  FADEL) ikut g_state.profile, jadi LCD & web selalu menampilkan profil yang sama;
//  ganti profil dari keypad → web ikut pindah tab, dan sebaliknya.
//
//  LAYAR (s_scr):
//    MON   monitor live (home), multi-halaman      → '#' Menu · 'C' ganti halaman
//    MENU  aksi kontekstual (start/stop/reset/...)  → A/B pilih · C OK · D kembali
//    PARAM daftar parameter profil aktif            → A/B pilih · C edit · D kembali
//    EDIT  input angka                              → 0-9 · '*' titik · D hapus · C OK
//
//  Tombol global di MON: '#'=Menu · 'C'=ganti halaman · '*'=Berhenti (saat jalan)
//                        A/B=atur aktuator (hanya mode MANUAL)
//  Lihat ARCHITECTURE.md §7.
// =============================================================================
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

// ── Layar UI ─────────────────────────────────────────────────────────────────
enum UiScreen { SCR_MON, SCR_MENU, SCR_PARAM, SCR_EDIT };
static UiScreen s_scr     = SCR_MON;
static bool     s_redraw  = true;
static int      s_monPage = 0;
static int      s_menuIdx = 0;
static int      s_paramIdx = 0;
static bool     s_editFloat = false;
static String   s_inBuf  = "";
static uint32_t s_lastMon = 0;
static const uint32_t MON_REFRESH_MS = 400;   // monitor live di-refresh tiap ~400ms

// ── Deskriptor parameter per profil ──────────────────────────────────────────
struct PItem { const char* name; bool isFloat; CmdType cmd; int32_t pid; };

static const PItem PWAFI[] = {
  { "Kp",       true,  CMD_SET_PARAM,    P_KP     },
  { "Ki",       true,  CMD_SET_PARAM,    P_KI     },
  { "Kd",       true,  CMD_SET_PARAM,    P_KD     },
  { "Lambda",   true,  CMD_SET_PARAM,    P_LAMBDA },
  { "Mu",       true,  CMD_SET_PARAM,    P_MU     },
  { "Beta",     true,  CMD_SET_PARAM,    P_BETA   },
  { "SetPoint", true,  CMD_SET_SETPOINT, 0        },
  { "Gas(deg)", false, CMD_SET_SERVO,    0        },
  { "Freq(Hz)", true,  CMD_SET_FREQ,     0        },
  { "Durasi",   false, CMD_SET_DURATION, 0        },
};
static const PItem PFADEL[] = {
  { "Kp",       true,  CMD_SET_SPARAM,   SP_KP },
  { "Ki",       true,  CMD_SET_SPARAM,   SP_KI },
  { "Kd",       true,  CMD_SET_SPARAM,   SP_KD },
  { "SetRPM",   true,  CMD_SET_SPEED_SP, 0     },
  { "Gas(deg)", false, CMD_SET_SERVO,    0     },
  { "Blower%",  false, CMD_SET_BLOWER,   0     },
  { "Durasi",   false, CMD_SET_DURATION, 0     },
};
static const PItem* plist(const SystemState& st, int& n) {
  if (st.profile == PROF_FADEL) { n = (int)(sizeof(PFADEL) / sizeof(PFADEL[0])); return PFADEL; }
  n = (int)(sizeof(PWAFI) / sizeof(PWAFI[0])); return PWAFI;
}
static float pval(const SystemState& st, const PItem& it) {
  switch (it.cmd) {
    case CMD_SET_PARAM:
      switch (it.pid) { case P_KP:return st.Kp; case P_KI:return st.Ki; case P_KD:return st.Kd;
        case P_LAMBDA:return st.lambda; case P_MU:return st.mu; case P_BETA:return st.beta; } break;
    case CMD_SET_SPARAM:
      switch (it.pid) { case SP_KP:return st.sKp; case SP_KI:return st.sKi; case SP_KD:return st.sKd; } break;
    case CMD_SET_SETPOINT: return st.setPoint;
    case CMD_SET_SERVO:    return (float)st.servoDeg;
    case CMD_SET_FREQ:     return st.freqMotor;
    case CMD_SET_DURATION: return (float)st.durationMin;
    case CMD_SET_SPEED_SP: return st.speedSP;
    case CMD_SET_BLOWER:   return (float)st.blowerConst;
    default: break;
  }
  return 0;
}

// ── Menu aksi kontekstual ────────────────────────────────────────────────────
enum MenuAct { MA_FUZZY, MA_MANUAL, MA_PARAM, MA_PROFILE, MA_STOP, MA_RESET, MA_ESTOP };
static int buildMenu(const SystemState& st, const char* labels[], MenuAct acts[]) {
  int n = 0; bool fadel = (st.profile == PROF_FADEL);
  if (st.opState == ST_IDLE) {
    labels[n] = fadel ? "Mulai PID"    : "Mulai FUZZY";   acts[n++] = MA_FUZZY;
    labels[n] = "Mulai MANUAL";                            acts[n++] = MA_MANUAL;
    labels[n] = "Parameter";                              acts[n++] = MA_PARAM;
    labels[n] = fadel ? "Ganti ke WAFI": "Ganti ke FADEL"; acts[n++] = MA_PROFILE;
    labels[n] = "BERHENTI DARURAT";                       acts[n++] = MA_ESTOP;
  } else if (st.opState == ST_RUNNING) {
    labels[n] = "BERHENTI";                               acts[n++] = MA_STOP;
    labels[n] = "Parameter";                              acts[n++] = MA_PARAM;
    labels[n] = "BERHENTI DARURAT";                       acts[n++] = MA_ESTOP;
  } else {  // FAULT / FINISHED (umumnya sudah di-override, ini fallback)
    labels[n] = "RESET";                                  acts[n++] = MA_RESET;
  }
  return n;
}

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
static const char* profTag(const SystemState& st) { return st.profile == PROF_FADEL ? "FADEL" : "WAFI"; }
static const char* stTag(OpState s) {
  switch (s) { case ST_RUNNING:return "JALAN"; case ST_FINISHED:return "SELESAI";
    case ST_FAULT:return "FAULT"; case ST_IDLE:return "SIAGA"; default:return "BOOT"; }
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

// ── Override layar FAULT / FINISHED ──────────────────────────────────────────
static void renderFault(const SystemState& st) {
  const char* f = (st.fault == FLT_OVERTEMP) ? "OVER-TEMP" :
                  (st.fault == FLT_SENSOR)   ? "SENSOR GAGAL" :
                  (st.fault == FLT_ESTOP)    ? "BERHENTI DARURAT" : "FAULT";
  I2C_LOCK();
  lcdLine(0, "!!! FAULT (%s) !!!", profTag(st));
  lcdLine(1, "%s", f);
  lcdLine(2, "T:%.1fC (max %d)", st.suhu, (int)SAFE_MAX_TEMP);
  lcdLine(3, "C=Reset (jika aman)");
  I2C_UNLOCK();
}
static void renderFinished(const SystemState& st) {
  I2C_LOCK();
  lcdLine(0, "** SELESAI (%s) **", profTag(st));
  lcdLine(1, "Gas tutup, blower 0");
  lcdLine(2, "Durasi tercapai");
  lcdLine(3, "C=OK (kembali siaga)");
  I2C_UNLOCK();
}

// ── Daftar (menu/param): item terpilih di baris 1 + preview baris 2 + legenda ─
static void renderMenu(const SystemState& st) {
  const char* labels[6]; MenuAct acts[6];
  int n = buildMenu(st, labels, acts);
  if (s_menuIdx >= n) s_menuIdx = n - 1; if (s_menuIdx < 0) s_menuIdx = 0;
  I2C_LOCK();
  lcdLine(0, "AKSI %-5s   %d/%d", profTag(st), s_menuIdx + 1, n);
  lcdLine(1, "> %s", labels[s_menuIdx]);
  if (s_menuIdx + 1 < n) lcdLine(2, "  %s", labels[s_menuIdx + 1]);
  else                   lcdLine(2, "");
  lcdLine(3, "A/B pilih C:OK D:<");
  I2C_UNLOCK();
}
static void renderParam(const SystemState& st) {
  int n; const PItem* L = plist(st, n);
  if (s_paramIdx >= n) s_paramIdx = n - 1; if (s_paramIdx < 0) s_paramIdx = 0;
  I2C_LOCK();
  lcdLine(0, "PARAM %-5s %2d/%d", profTag(st), s_paramIdx + 1, n);
  const PItem& a = L[s_paramIdx];
  if (a.isFloat) lcdLine(1, ">%-9s%9.3f", a.name, pval(st, a));
  else           lcdLine(1, ">%-9s%9d", a.name, (int)pval(st, a));
  if (s_paramIdx + 1 < n) {
    const PItem& b = L[s_paramIdx + 1];
    if (b.isFloat) lcdLine(2, " %-9s%9.3f", b.name, pval(st, b));
    else           lcdLine(2, " %-9s%9d", b.name, (int)pval(st, b));
  } else lcdLine(2, "");
  lcdLine(3, "A/B  C:edit  D:<");
  I2C_UNLOCK();
}
static void renderEdit(const SystemState& st) {
  int n; const PItem* L = plist(st, n);
  if (s_paramIdx >= n) s_paramIdx = n - 1; if (s_paramIdx < 0) s_paramIdx = 0;
  const PItem& a = L[s_paramIdx];
  I2C_LOCK();
  lcdLine(0, "SET %s (%s)", a.name, profTag(st));
  if (a.isFloat) lcdLine(1, "Lama: %.3f", pval(st, a));
  else           lcdLine(1, "Lama: %d", (int)pval(st, a));
  lcdLine(2, "Baru: %s_", s_inBuf.c_str());
  lcdLine(3, s_editFloat ? "C:OK *=. D:hapus" : "C:OK    D:hapus");
  I2C_UNLOCK();
}

// ── Monitor live (home) ──────────────────────────────────────────────────────
static void renderMonitor(const SystemState& st) {
  bool fadel   = (st.profile == PROF_FADEL);
  bool running = (st.opState == ST_RUNNING);
  bool manual  = (st.subMode == SUB_MANUAL);
  int  maxPage = fadel ? 1 : ((running && st.subMode == SUB_FUZZY) ? 2 : 1);
  if (s_monPage > maxPage) s_monPage = 0;

  long rem = 0;
  if (running) { long tot = (long)st.durationMin * 60L;
    long el = (long)((millis() - st.runStartMs) / 1000UL); rem = tot - el; if (rem < 0) rem = 0; }

  DateTime now = rtcNow();   // PENTING: ambil I2C sebelum I2C_LOCK (mutex non-rekursif)
  I2C_LOCK();
  lcdLine(0, "%-5s %-7s  %02d:%02d", profTag(st), stTag(st.opState), now.hour(), now.minute());

  if (!fadel) {
    // ── WAFI ──
    if (s_monPage == 0) {
      lcdLine(1, "T%.1f SP%.1f E%+.1f", st.suhu, st.setPoint, st.error);
      const char* m = (st.blowerPct == 0) ? "mati" :
                      (st.blowerPct >= 20 && st.blowerPct <= 30) ? "panas" : "dingin";
      lcdLine(2, "Blower %d%% %s", st.blowerPct, m);
    } else if (s_monPage == 1) {
      lcdLine(1, "RPM%.0f Gas%d Frq%.0f", st.rpm, st.servoDeg, st.freqMotor);
      lcdLine(2, "Daya%.0fW Sisa%02ld:%02ld", st.power, rem / 60, rem % 60);
    } else {
      lcdLine(1, "E%+.1f u%.2f FIS%.0f", st.error, st.uFopid, st.fisOut);
      lcdLine(2, "I%.2f D%.2f %s", st.integral, st.derivative, st.mlxOk ? "senOK" : "senX");
    }
  } else {
    // ── FADEL ──
    if (s_monPage == 0) {
      lcdLine(1, "RPM%.1f / %.1f", st.rpm, st.speedSP);
      lcdLine(2, "VFD%.1fHz E%+.1f", st.vfdFreq, st.speedSP - st.rpm);
    } else {
      lcdLine(1, "Daya%.0fW Gas%d", st.power, st.servoDeg);
      lcdLine(2, "Blow%d%% Sisa%02ld:%02ld", st.blowerConst, rem / 60, rem % 60);
    }
  }

  if (running && manual)      lcdLine(3, fadel ? "A/B:Hz C:hal #Menu" : "A/B:%% C:hal #Menu");
  else if (running)           lcdLine(3, "*:Stop C:hal #Menu");
  else                        lcdLine(3, "C:halaman   #:Menu");
  I2C_UNLOCK();
}

// ── Handler per-layar ────────────────────────────────────────────────────────
static void onMenu(const SystemState& st, char key) {
  const char* labels[6]; MenuAct acts[6];
  int n = buildMenu(st, labels, acts);
  if (s_menuIdx >= n) s_menuIdx = n - 1;
  if      (key == 'A') { s_menuIdx = (s_menuIdx > 0) ? s_menuIdx - 1 : n - 1; s_redraw = true; }
  else if (key == 'B') { s_menuIdx = (s_menuIdx < n - 1) ? s_menuIdx + 1 : 0; s_redraw = true; }
  else if (key == 'D') { s_scr = SCR_MON; s_redraw = true; }
  else if (key == 'C') {
    switch (acts[s_menuIdx]) {
      case MA_FUZZY:   cmdSendT(CMD_START, 0, SUB_FUZZY);  s_monPage = 0; s_scr = SCR_MON; break;
      case MA_MANUAL:  cmdSendT(CMD_START, 0, SUB_MANUAL); s_monPage = 0; s_scr = SCR_MON; break;
      case MA_PARAM:   s_paramIdx = 0; s_scr = SCR_PARAM; break;
      case MA_PROFILE: cmdSendT(CMD_SET_PROFILE, 0, st.profile == PROF_FADEL ? PROF_WAFI : PROF_FADEL);
                       s_menuIdx = 0; break;   // tetap di menu; daftar dibangun ulang tick berikut
      case MA_STOP:    cmdSendT(CMD_STOP);  s_scr = SCR_MON; break;
      case MA_RESET:   cmdSendT(CMD_RESET); s_scr = SCR_MON; break;
      case MA_ESTOP:   cmdSendT(CMD_ESTOP); s_scr = SCR_MON; break;
    }
    s_redraw = true;
  }
}

static void onParam(const SystemState& st, char key) {
  int n; plist(st, n);
  if (s_paramIdx >= n) s_paramIdx = n - 1;
  if      (key == 'A') { s_paramIdx = (s_paramIdx > 0) ? s_paramIdx - 1 : n - 1; s_redraw = true; }
  else if (key == 'B') { s_paramIdx = (s_paramIdx < n - 1) ? s_paramIdx + 1 : 0; s_redraw = true; }
  else if (key == 'D') { s_scr = SCR_MON; s_redraw = true; }
  else if (key == 'C') { int m; const PItem* L = plist(st, m); s_editFloat = L[s_paramIdx].isFloat;
                         s_inBuf = ""; s_scr = SCR_EDIT; s_redraw = true; }
}

static void onEdit(const SystemState& st, char key) {
  if      (key >= '0' && key <= '9')                              { s_inBuf += key; s_redraw = true; }
  else if (key == '*' && s_editFloat && s_inBuf.indexOf('.') < 0) { s_inBuf += '.'; s_redraw = true; }
  else if (key == 'D') {
    if (s_inBuf.length()) { s_inBuf.remove(s_inBuf.length() - 1); s_redraw = true; }
    else                  { s_scr = SCR_PARAM; s_redraw = true; }   // backspace di buffer kosong = batal
  }
  else if (key == 'C' && s_inBuf.length()) {
    int n; const PItem* L = plist(st, n);
    if (s_paramIdx >= n) s_paramIdx = n - 1;
    const PItem& it = L[s_paramIdx];
    float fv = s_inBuf.toFloat(); int32_t iv = s_inBuf.toInt();
    switch (it.cmd) {
      case CMD_SET_PARAM:    cmdSendT(CMD_SET_PARAM, fv, it.pid); break;
      case CMD_SET_SPARAM:   cmdSendT(CMD_SET_SPARAM, fv, it.pid); break;
      case CMD_SET_SETPOINT: cmdSendT(CMD_SET_SETPOINT, fv); break;
      case CMD_SET_SPEED_SP: cmdSendT(CMD_SET_SPEED_SP, fv); break;
      case CMD_SET_FREQ:     cmdSendT(CMD_SET_FREQ, fv); break;
      case CMD_SET_SERVO:    cmdSendT(CMD_SET_SERVO, 0, iv); break;
      case CMD_SET_DURATION: cmdSendT(CMD_SET_DURATION, 0, iv); break;
      case CMD_SET_BLOWER:   cmdSendT(CMD_SET_BLOWER, 0, iv); break;
      default: break;
    }
    s_scr = SCR_PARAM; s_redraw = true;
  }
}

static void onMonitor(const SystemState& st, char key) {
  bool fadel   = (st.profile == PROF_FADEL);
  bool running = (st.opState == ST_RUNNING);
  int  maxPage = fadel ? 1 : ((running && st.subMode == SUB_FUZZY) ? 2 : 1);
  if      (key == '#') { s_menuIdx = 0; s_scr = SCR_MENU; s_redraw = true; return; }
  else if (key == 'C') { s_monPage = (s_monPage < maxPage) ? s_monPage + 1 : 0; s_redraw = true; }
  else if (key == '*' && running) { cmdSendT(CMD_STOP); }
  else if (running && st.subMode == SUB_MANUAL && (key == 'A' || key == 'B')) {
    if (!fadel) {   // Wafi manual → blower ±5%
      int v = constrain(st.blowerPct + (key == 'A' ? 5 : -5), (int)DIMMER_MIN, (int)DIMMER_MAX);
      cmdSendT(CMD_SET_BLOWER, 0, v);
    } else {        // Fadel manual → freq VFD ±1 Hz
      float v = constrain(st.vfdFreq + (key == 'A' ? 1.0f : -1.0f), 0.0f, VFD_FREQ_MAX_HZ);
      cmdSendT(CMD_SET_VFD, v);
    }
  }
}

void uiTick() {
  SystemState st; stateGet(st);
  char key = getSingleKey();

  // Override layar untuk FAULT / FINISHED (apa pun layar-nya)
  if (st.opState == ST_FAULT)    { renderFault(st);    if (key == 'C') cmdSendT(CMD_RESET); s_scr = SCR_MON; s_redraw = true; return; }
  if (st.opState == ST_FINISHED) { renderFinished(st); if (key == 'C') cmdSendT(CMD_RESET); s_scr = SCR_MON; s_redraw = true; return; }

  switch (s_scr) {
    case SCR_MENU:  onMenu(st, key);  break;
    case SCR_PARAM: onParam(st, key); break;
    case SCR_EDIT:  onEdit(st, key);  break;
    case SCR_MON:   onMonitor(st, key); break;
  }

  // Monitor = live: refresh berkala walau tanpa tombol; layar lain = on-change.
  if (s_scr == SCR_MON) {
    uint32_t nowMs = millis();
    if (s_redraw || (nowMs - s_lastMon) >= MON_REFRESH_MS) { renderMonitor(st); s_lastMon = nowMs; s_redraw = false; }
  } else if (s_redraw) {
    switch (s_scr) { case SCR_MENU: renderMenu(st); break; case SCR_PARAM: renderParam(st); break;
                     case SCR_EDIT: renderEdit(st); break; default: break; }
    s_redraw = false;
  }
}
