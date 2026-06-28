#include "logging.h"
#include "config.h"
#include "state.h"
#include "sensors.h"      // rtcNow()
#include "FS.h"
#include "SD.h"
#include "SPI.h"

// Folder log per profil
#define LOG_DIR_WAFI  "/log/wafi"
#define LOG_DIR_FADEL "/log/fadel"

static File s_file;
static bool s_open = false;
static bool s_sdOk = false;

bool loggingReady() { return s_sdOk; }

void loggingInit() {
  SD_LOCK();
  s_sdOk = SD.begin(PIN_SD_CS);
  if (s_sdOk) {
    if (!SD.exists("/log"))        SD.mkdir("/log");
    if (!SD.exists(LOG_DIR_WAFI))  SD.mkdir(LOG_DIR_WAFI);
    if (!SD.exists(LOG_DIR_FADEL)) SD.mkdir(LOG_DIR_FADEL);
  }
  SD_UNLOCK();
  Serial.println(s_sdOk ? "[SD] OK" : "[SD] FAIL");
}

static void tsNow(char* buf, size_t n) {
  DateTime now = rtcNow();
  snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
}

static void genName(char* buf, size_t n, const char* dir, const char* pre) {
  for (int i = 1; i <= 999; i++) {
    snprintf(buf, n, "%s/%s_%03d.csv", dir, pre, i);
    if (!SD.exists(buf)) break;
  }
}

bool loggingStart(SystemState& st) {
  if (!s_sdOk) { st.logging = false; return false; }
  SD_LOCK();
  if (s_open && s_file) { s_file.close(); s_open = false; }
  const char* dir = (st.profile == PROF_FADEL) ? LOG_DIR_FADEL : LOG_DIR_WAFI;
  const char* pre = (st.profile == PROF_FADEL) ? "fadel" : "wafi";
  genName(st.logFile, sizeof(st.logFile), dir, pre);
  s_file = SD.open(st.logFile, FILE_WRITE);
  bool ok = (bool)s_file;
  if (ok) {
    char tb[24]; tsNow(tb, sizeof(tb));
    s_file.printf("# Kopi Control (%s) | Start:%s | SP:%.1f Kp:%.3f Ki:%.3f Kd:%.3f | Servo:%d | Dur:%lumnt\n",
                  pre, tb, (st.profile == PROF_FADEL) ? st.speedSP : st.setPoint,
                  (st.profile == PROF_FADEL) ? st.sKp : st.Kp,
                  (st.profile == PROF_FADEL) ? st.sKi : st.Ki,
                  (st.profile == PROF_FADEL) ? st.sKd : st.Kd,
                  st.servoDeg, (unsigned long)st.durationMin);
    s_file.println("DateTime,Suhu_C,SetPoint_C,Error_C,dError_C,U_FoPID,Integral,Derivative,"
                   "FIS_Out,Blower_pct,Servo,RPM,Voltage,Current,Power,PF,OpState,SubMode,RpmStatus");
    s_file.flush();
    s_open = true;
  } else {
    Serial.printf("[SD] gagal buka %s\n", st.logFile);
  }
  SD_UNLOCK();
  st.logging = ok;
  return ok;
}

void loggingStop(SystemState& st) {
  SD_LOCK();
  if (s_open && s_file) { s_file.flush(); s_file.close(); s_open = false; }
  SD_UNLOCK();
  st.logging = false;
}

void loggingAppend(const SystemState& st) {
  if (!s_sdOk) return;
  SD_LOCK();
  if (s_open && s_file) {
    char tb[24]; tsNow(tb, sizeof(tb));
    s_file.printf("%s,%.2f,%.1f,%.2f,%.2f,%.3f,%.3f,%.3f,%.2f,%d,%d,%.2f,%.1f,%.2f,%.1f,%.2f,%d,%d,%d\n",
                  tb, st.suhu, st.setPoint, st.error, st.dError,
                  st.uFopid, st.integral, st.derivative, st.fisOut,
                  st.blowerPct, st.servoDeg, st.rpm,
                  st.voltage, st.current, st.power, st.pf,
                  (int)st.opState, (int)st.subMode, (int)st.rpmStatus);
    s_file.flush();
  }
  SD_UNLOCK();
}

// Daftar CSV (path lengkap) dari sebuah folder
static void listDir(const char* path, String& json, bool& first) {
  File root = SD.open(path);
  if (!root) return;
  File f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String n = f.name();                 // bisa basename atau full path tergantung core
      if (n.endsWith(".csv")) {
        String full = n.startsWith("/") ? n : (String(path) + "/" + n);
        if (!first) json += ",";
        json += "\"" + full + "\"";
        first = false;
      }
    }
    f = root.openNextFile();
  }
  root.close();
}

String loggingListJson() {
  String json = "[";
  SD_LOCK();
  if (s_sdOk) {
    bool first = true;
    listDir(LOG_DIR_WAFI, json, first);
    listDir(LOG_DIR_FADEL, json, first);
  }
  SD_UNLOCK();
  json += "]";
  return json;
}

bool loggingFileExists(const String& path) {
  SD_LOCK();
  bool e = s_sdOk && SD.exists(path);
  SD_UNLOCK();
  return e;
}
