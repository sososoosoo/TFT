#include "Globals.h"
#include "Communication.h"

/**
 * @file Communication.cpp
 * @brief CAN, UART 통신 관련 함수의 실제 구현을 포함합니다.
 */


//==============================================================================
// CAN 버스 관련 함수 구현
//==============================================================================

void enqueueCanCommand(uint8_t moduleId, uint8_t cmd, int32_t param) {
  CanTxItem item;
  // 예시 CAN ID: 0x100 | moduleId
  item.canId = 0x100 | moduleId;
  item.dlc   = 8;
  item.data[0] = cmd;
  item.data[1] = (param >> 24) & 0xFF;
  item.data[2] = (param >> 16) & 0xFF;
  item.data[3] = (param >>  8) & 0xFF;
  item.data[4] = (param      ) & 0xFF;
  item.data[5] = 0;
  item.data[6] = 0;
  item.data[7] = 0;

  if (g_canTxQueue) {
    xQueueSend(g_canTxQueue, &item, 0);
  }
}

void handleCanFrame(const twai_message_t &msg) {
  uint32_t id = msg.identifier;

  if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    uint32_t now = millis();

    switch (id) {
      case 0x010: { // 예: 수조 모듈 상태
        g_state.tank.status = MODULE_OK;
        // payload 예시: temp(0), level(1), pH(2), TDS(3), turbidity(4), DO(5)
        // 실제 포맷에 맞게 디코딩 필요
        g_state.tank.tempC        = msg.data[0];
        g_state.tank.levelPercent = msg.data[1];
        g_state.tank.pH           = msg.data[2] / 10.0;
        g_state.tank.tds          = msg.data[3] * 10;
        g_state.tank.turbidity    = msg.data[4];
        g_state.tank.do_mgL       = msg.data[5] / 10.0;
        g_state.tank.lastUpdateMs = now;
        break;
      }
      case 0x020: { // 재배기 모듈 상태
        g_state.grow.status     = MODULE_OK;
        g_state.grow.tempC      = msg.data[0];
        g_state.grow.humidity   = msg.data[1];
        g_state.grow.leak[0]    = msg.data[2] & 0x01;
        g_state.grow.leak[1]    = msg.data[2] & 0x02;
        g_state.grow.leak[2]    = msg.data[2] & 0x04;
        g_state.grow.leak[3]    = msg.data[2] & 0x08;
        g_state.grow.lastUpdateMs = now;
        break;
      }
      // TODO: 양액기(0x030), 급여기(0x040) 등 실제 CAN ID에 맞게 구현
      default:
        break;
    }

    xSemaphoreGive(g_stateMutex);
  }
}


//==============================================================================
// 모듈 제어 요청 헬퍼 함수 구현
//==============================================================================

void requestTankPump(bool on) {
  enqueueCanCommand(MODULE_TANK, TANK_CMD_SET_PUMP, on ? 1 : 0);
  logEvent(on ? "Tank pump ON requested" : "Tank pump OFF requested");
}

void requestTankLight(bool on) {
  enqueueCanCommand(MODULE_TANK, TANK_CMD_SET_LIGHT, on ? 1 : 0);
  logEvent(on ? "Tank light ON requested" : "Tank light OFF requested");
}

void requestGrowLedBrightness(uint8_t brightness) {
  if (brightness > 100) brightness = 100;
  enqueueCanCommand(MODULE_GROW, GROW_CMD_SET_LED_BRIGHTNESS, (int32_t)brightness);
  logEvent(("Grow LED brightness set to " + String(brightness) + "%" ).c_str());
}

void requestFeederOnce(uint8_t amountPercent) {
  if (amountPercent > 100) amountPercent = 100;
  enqueueCanCommand(MODULE_FEEDER, FEEDER_CMD_FEED_ONCE, (int32_t)amountPercent);
  logEvent(("Feeder once, amount " + String(amountPercent) + "% requested" ).c_str());
}


//==============================================================================
// UART (서버) 통신 관련 함수 구현
//==============================================================================

String buildStatusJson() {
  String s = "{";

  if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    s += "\"tank\":{\"st\":