// =============================================================================
//  state.h — SystemState global + command queue + mutex (FreeRTOS)
//  Hanya control/realtime task yang menulis state proses; reader pakai stateGet().
// =============================================================================
#ifndef STATE_H
#define STATE_H

#include "types.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

extern SystemState       g_state;
extern SemaphoreHandle_t g_stateMux;
extern SemaphoreHandle_t g_i2cMux;
extern SemaphoreHandle_t g_sdMux;
extern QueueHandle_t     g_cmdQueue;

void stateInit();
void stateGet(SystemState& dst);              // salinan ter-lock (untuk reader)

bool cmdSend(const Command& c);               // dari task mana pun (non-blok)
bool cmdSendT(CmdType t, float f = 0, int32_t i = 0);
bool cmdRecv(Command& c);                      // non-blok (untuk realtime task)

// ── Helper lock ──────────────────────────────────────────────────────────────
#define I2C_LOCK()        xSemaphoreTake(g_i2cMux, portMAX_DELAY)
#define I2C_TRYLOCK(ms)  (xSemaphoreTake(g_i2cMux, pdMS_TO_TICKS(ms)) == pdTRUE)
#define I2C_UNLOCK()      xSemaphoreGive(g_i2cMux)
#define SD_LOCK()         xSemaphoreTake(g_sdMux, portMAX_DELAY)
#define SD_UNLOCK()       xSemaphoreGive(g_sdMux)
#define STATE_LOCK()      xSemaphoreTake(g_stateMux, portMAX_DELAY)
#define STATE_UNLOCK()    xSemaphoreGive(g_stateMux)

#endif // STATE_H
