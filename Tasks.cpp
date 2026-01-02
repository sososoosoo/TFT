#include "Globals.h"
#include "Tasks.h"

// twai.h는 C 라이브러리이므로 extern "C"로 감싸야 합니다.
extern "C" {
  #include "driver/twai.h"
}

/**
 * @file Tasks.cpp
 * @brief FreeRTOS 태스크 함수들의 실제 구현을 포함합니다.
 */


//==============================================================================
// FreeRTOS 태스크 구현
//==============================================================================

void taskCan(void *pvParameters) {
  uint32_t lastPollMs = 0;
  for (;;) {
    uint32_t now = millis();

    // Rx (non-blocking or 짧은 timeout)
    twai_message_t rxMsg;
    if (twai_receive(&rxMsg, pdMS_TO_TICKS(10)) == ESP_OK) {
      handleCanFrame(rxMsg);
    }

    // Tx 큐 처리
    if (g_canTxQueue) {
      CanTxItem item;
      if (xQueueReceive(g_canTxQueue, &item, 0) == pdTRUE) {
        twai_message_t txMsg;
        memset(&txMsg, 0, sizeof(txMsg));
        txMsg.identifier = item.canId;
        txMsg.data_length_code = item.dlc;
        memcpy(txMsg.data, item.data, item.dlc);
        twai_transmit(&txMsg, pdMS_TO_TICKS(10));
      }
    }

    // 필요 시 100ms 간격으로 Heartbeat 등 송신
    if (now - lastPollMs >= PERIOD_CAN_COLLECT_MS) {
      lastPollMs = now;
      // TODO: 필요 시 CAN 폴링 프레임 송신
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void taskUart(void *pvParameters) {
  uint32_t lastTxMs = 0;
  String rxBuf;

  for (;;) {
    uint32_t now = millis();

    // 200ms 주기 상태 전송
    if (now - lastTxMs >= PERIOD_UART_TX_MS) {
      lastTxMs = now;
      String json = buildStatusJson();
      Serial2.println(json);
    }

    // Rx 수신 및 파싱
    while (Serial2.available()) {
      char c = Serial2.read();
      if (c == '\n' || c == '\r') {
        if (rxBuf.length() > 0) {
          parseServerLine(rxBuf);
          rxBuf = "";
        }
      } else {
        rxBuf += c;
      }
    }

    // 서버 명령 큐 → CAN 라우팅
    if (g_serverCmdQueue) {
      ServerCommand cmd;
      if (xQueueReceive(g_serverCmdQueue, &cmd, 0) == pdTRUE) {
        handleServerCommand(cmd);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void taskUi(void *pvParameters) {
  uint32_t lastUpdateMs   = 0;
  int16_t  lastScreenIndex = (int16_t)g_currentScreen;

  for (;;) {
    uint32_t now = millis();

    // 로터리 읽기
    updateRotary();

    // 로터리 회전량으로 화면 전환 (한 스텝당 화면 1칸 이동)
    int16_t pos  = g_encoderPos;
    int16_t diff = pos - g_lastScreenEncPos;

    if (diff >= 1) {
      g_lastScreenEncPos = pos;
      int16_t idx = (int16_t)g_currentScreen + 1;
      if (idx >= SCREEN_COUNT) idx = 0;
      g_currentScreen = (ScreenId)idx;

    } else if (diff <= -1) {
      g_lastScreenEncPos = pos;
      int16_t idx = (int16_t)g_currentScreen - 1;
      if (idx < 0) idx = SCREEN_COUNT - 1;
      g_currentScreen = (ScreenId)idx;
    }

    // 버튼 클릭 처리 (짧은 / 긴 클릭)
    bool shortClick = fetchShortClick();
    bool longClick  = fetchLongClick();

    if (shortClick || longClick) {
      playClickBuzzer();

      switch (g_currentScreen) {
        case SCREEN_TANK:
          handleTankClick(shortClick, longClick);
          break;
        case SCREEN_GROW:
          handleGrowClick(shortClick, longClick);
          break;
        case SCREEN_SETTINGS:
          handleSettingsClick(shortClick, longClick);
          break;
        case SCREEN_LOG:
          handleLogClick(shortClick, longClick);
          break;
        default:
          logEvent("Button clicked (no special action)");
          break;
      }

      // 버튼 눌린 직후, 바로 화면 다시 그림
      lastUpdateMs    = now;
      lastScreenIndex = (int16_t)g_currentScreen;
      drawCurrentScreen();
    }

    // UI 갱신
    if (now - lastUpdateMs >= PERIOD_UI_UPDATE_MS ||
        lastScreenIndex != (int16_t)g_currentScreen) {
      lastUpdateMs    = now;
      lastScreenIndex = (int16_t)g_currentScreen;
      drawCurrentScreen();
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}


void taskLogic(void *pvParameters) {
  for (;;) {
    uint32_t now = millis();

    // ===== 간단 소프트웨어 시계 (부팅 기준) =====
    if (now - g_lastClockUpdateMs >= 1000) {  // 1초마다
      g_lastClockUpdateMs = now;
      g_uptimeSeconds++;

      g_timeMinute = (g_uptimeSeconds / 60)   % 60; // 0~59
      g_timeHour   = (g_uptimeSeconds / 3600) % 24; // 0~23
    }

    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      // 서버 연결 상태 (Fail-safe 판단)
      bool connected = (now - g_state.lastServerRxMs) < SERVER_TIMEOUT_MS;
      g_state.serverConnected = connected;

      // 모듈 Offline 검사 (1000ms 이상 업데이트 없으면 OFFLINE)
      if (now - g_state.tank.lastUpdateMs     > 1000) g_state.tank.status     = MODULE_OFFLINE;
      if (now - g_state.grow.lastUpdateMs     > 1000) g_state.grow.status     = MODULE_OFFLINE;
      if (now - g_state.nutrient.lastUpdateMs > 1000) g_state.nutrient.status = MODULE_OFFLINE;
      if (now - g_state.feeder.lastUpdateMs   > 1000) g_state.feeder.status   = MODULE_OFFLINE;

      // 경고/오류 플래그 (예시: 누수 감지 → ERROR)
      bool hasLeak = ( g_state.grow.leak[0] || g_state.grow.leak[1]
                    || g_state.grow.leak[2] || g_state.grow.leak[3] );
      g_state.hasError = hasLeak;

      bool anyOffline = (g_state.tank.status     == MODULE_OFFLINE ||
                         g_state.grow.status     == MODULE_OFFLINE ||
                         g_state.nutrient.status == MODULE_OFFLINE ||
                         g_state.feeder.status   == MODULE_OFFLINE);
      g_state.hasWarning = anyOffline && !g_state.hasError;

      // Fail-safe: 서버 미연결 시 급여 스케줄 로컬 실행
      if (!connected) {
        int currentMinuteOfDay = (int)g_timeHour * 60 + (int)g_timeMinute;
        int schedMinuteOfDay   = (int)g_settings.feederHour * 60 +
                                 (int)g_settings.feederMinute;

        if (currentMinuteOfDay == schedMinuteOfDay &&
            g_lastFeederScheduleMinute != currentMinuteOfDay) {

          uint8_t amt = g_settings.feederAmountPercent;
          if (amt > 0) {
            xSemaphoreGive(g_stateMutex);  // CAN 명령 보내는 동안 잠깐 풀고
            requestFeederOnce(amt);
            g_lastFeederScheduleMinute = currentMinuteOfDay;
            logEvent("Fail-safe feeder schedule triggered");

            if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
              vTaskDelay(pdMS_TO_TICKS(100));
              continue;
            }
          }
        }
      } else {
        // 서버가 다시 연결되면 스케줄 플래그 리셋
        g_lastFeederScheduleMinute = -1;
      }

      // AlarmLevel 업데이트
      if (g_state.hasError) {
        g_alarmLevel = ALARM_ERROR;
      } else if (g_state.hasWarning) {
        g_alarmLevel = ALARM_WARNING;
      } else {
        g_alarmLevel = ALARM_NONE;
      }

      xSemaphoreGive(g_stateMutex);
    }

    // LED 상태 표시 (mutex 없이 읽어도 무방한 수준)
    bool allOk = (g_state.tank.status     == MODULE_OK &&
                  g_state.grow.status     == MODULE_OK &&
                  g_state.nutrient.status == MODULE_OK &&
                  g_state.feeder.status   == MODULE_OK);

    digitalWrite(PIN_LED_BLUE,  g_state.serverConnected ? HIGH : LOW);
    digitalWrite(PIN_LED_GREEN, allOk ? HIGH : LOW);
    digitalWrite(PIN_LED_RED,   (g_state.hasWarning || g_state.hasError) ? HIGH : LOW);

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}



void taskAlarm(void *pvParameters) {
  uint32_t lastToggleMs = 0;
  bool buzOn = false;

  for (;;) {
    uint32_t now = millis();
    uint32_t onMs = 0;
    uint32_t offMs = 0;

    if (g_alarmLevel == ALARM_WARNING) {
      // 경고: 500ms ON, 500ms OFF 반복
      onMs  = 500;
      offMs = 500;
    } else if (g_alarmLevel == ALARM_ERROR) {
      // 오류/누수: 1000ms ON, 500ms OFF 반복
      onMs  = 1000;
      offMs = 500;
    } else {
      // 알람 없음
      digitalWrite(PIN_BUZZER, LOW);
      buzOn = false;
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    if (!buzOn) {
      // OFF → ON 전환
      digitalWrite(PIN_BUZZER, HIGH);
      buzOn = true;
      lastToggleMs = now;
    } else {
      // ON 상태에서 onMs 지나면 OFF, offMs 지나면 다시 ON
      uint32_t diff = now - lastToggleMs;
      if ((g_alarmLevel == ALARM_WARNING && diff >= onMs) ||
          (g_alarmLevel == ALARM_ERROR   && diff >= onMs)) {
        digitalWrite(PIN_BUZZER, LOW);
        buzOn = false;
        lastToggleMs = now;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
