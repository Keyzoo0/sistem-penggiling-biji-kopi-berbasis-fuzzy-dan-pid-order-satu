#include "webserver.h"
#include "config.h"
#include "state.h"
#include "logging.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "FS.h"
#include "SD.h"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

static const char* opStr(OpState s) {
  switch (s) { case ST_IDLE:return "IDLE"; case ST_RUNNING:return "RUNNING";
    case ST_FINISHED:return "FINISHED"; case ST_FAULT:return "FAULT"; default:return "BOOT"; }
}
static const char* subStr(RunSubMode m) {
  switch (m) { case SUB_FUZZY:return "FUZZY"; case SUB_MANUAL:return "MANUAL"; default:return "NONE"; }
}
static const char* faultStr(FaultCode f) {
  switch (f) { case FLT_ESTOP:return "ESTOP"; case FLT_SENSOR:return "SENSOR";
    case FLT_OVERTEMP:return "OVERTEMP"; default:return "NONE"; }
}

static String buildJson() {
  SystemState st; stateGet(st);
  JsonDocument doc;
  doc["opState"]    = opStr(st.opState);
  doc["subMode"]    = subStr(st.subMode);
  doc["fault"]      = faultStr(st.fault);
  doc["temp"]       = st.suhu;
  doc["setpoint"]   = st.setPoint;
  doc["error"]      = st.error;
  doc["blower"]     = st.blowerPct;
  doc["servo"]      = st.servoDeg;
  doc["rpm"]        = st.rpm;
  doc["rpmStatus"]  = (int)st.rpmStatus;
  doc["power"]      = st.power;
  doc["voltage"]    = st.voltage;
  doc["current"]    = st.current;
  doc["pf"]         = st.pf;
  doc["fisOut"]     = st.fisOut;
  doc["u_fopid"]    = st.uFopid;
  doc["integral"]   = st.integral;
  doc["derivative"] = st.derivative;
  doc["mlxOk"]      = st.mlxOk;
  doc["logging"]    = st.logging;
  long remaining = 0;
  if (st.opState == ST_RUNNING) {
    long total = (long)st.durationMin * 60L;
    long elapsed = (long)((millis() - st.runStartMs) / 1000UL);
    remaining = total - elapsed;
    if (remaining < 0) remaining = 0;
  }
  doc["remaining"]  = remaining;
  String out; serializeJson(doc, out); return out;
}

static String buildParamsJson() {
  SystemState st; stateGet(st);
  JsonDocument doc;
  doc["Kp"] = st.Kp; doc["Ki"] = st.Ki; doc["Kd"] = st.Kd;
  doc["lambda"] = st.lambda; doc["mu"] = st.mu; doc["beta"] = st.beta;
  doc["setpoint"] = st.setPoint; doc["duration"] = (long)st.durationMin;
  doc["servo"] = st.servoDeg; doc["freq"] = st.freqMotor;
  String o; serializeJson(doc, o); return o;
}

static void handleWsText(uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, (const char*)data, len)) return;

  if (!doc["start"].isNull()) {
    String m = doc["start"].as<String>();
    cmdSendT(CMD_START, 0, (m == "MANUAL") ? SUB_MANUAL : SUB_FUZZY);
  }
  if (!doc["stop"].isNull())     cmdSendT(CMD_STOP);
  if (!doc["estop"].isNull())    cmdSendT(CMD_ESTOP);
  if (!doc["reset"].isNull())    cmdSendT(CMD_RESET);
  if (!doc["setpoint"].isNull()) cmdSendT(CMD_SET_SETPOINT, doc["setpoint"].as<float>());
  if (!doc["servo"].isNull())    cmdSendT(CMD_SET_SERVO, 0, doc["servo"].as<int>());
  if (!doc["blower"].isNull())   cmdSendT(CMD_SET_BLOWER, 0, doc["blower"].as<int>());
  if (!doc["duration"].isNull()) cmdSendT(CMD_SET_DURATION, 0, doc["duration"].as<int>());
  if (!doc["kp"].isNull())     cmdSendT(CMD_SET_PARAM, doc["kp"].as<float>(),     P_KP);
  if (!doc["ki"].isNull())     cmdSendT(CMD_SET_PARAM, doc["ki"].as<float>(),     P_KI);
  if (!doc["kd"].isNull())     cmdSendT(CMD_SET_PARAM, doc["kd"].as<float>(),     P_KD);
  if (!doc["lambda"].isNull()) cmdSendT(CMD_SET_PARAM, doc["lambda"].as<float>(), P_LAMBDA);
  if (!doc["mu"].isNull())     cmdSendT(CMD_SET_PARAM, doc["mu"].as<float>(),     P_MU);
  if (!doc["beta"].isNull())   cmdSendT(CMD_SET_PARAM, doc["beta"].as<float>(),   P_BETA);
  if (!doc["freq"].isNull())   cmdSendT(CMD_SET_FREQ,  doc["freq"].as<float>());
}

static void onWsEvent(AsyncWebSocket* s, AsyncWebSocketClient* c, AwsEventType type,
                      void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS] connect #%u\n", c->id());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      handleWsText(data, len);
    }
  }
}

void webInit() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WiFi] connecting to %s", WIFI_SSID);
  int n = 0;
  while (WiFi.status() != WL_CONNECTED && n < 20) { delay(250); Serial.print("."); n++; }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("[WiFi] STA IP: %s\n", WiFi.localIP().toString().c_str());
  else
    Serial.println("[WiFi] STA gagal — pakai AP");

  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("[WiFi] AP %s IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);

  if (!LittleFS.begin(true)) Serial.println("[LittleFS] mount gagal");

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "application/json", buildJson());
  });
  server.on("/api/params", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "application/json", buildParamsJson());
  });
  server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "application/json", loggingListJson());
  });
  server.on("/api/download", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!r->hasParam("file")) { r->send(400, "text/plain", "missing file"); return; }
    SystemState st; stateGet(st);
    if (st.logging) { r->send(503, "text/plain", "sedang merekam; unduh setelah selesai"); return; }
    String fn = "/" + r->getParam("file")->value();
    if (!loggingFileExists(fn)) { r->send(404, "text/plain", "not found"); return; }
    r->send(SD, fn, "text/csv");
  });

  server.begin();
  Serial.println("[HTTP] server started :80");
}

void webBroadcast() {
  ws.cleanupClients();
  if (ws.count() > 0) ws.textAll(buildJson());
}
