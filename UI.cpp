#include "Globals.h"
#include "UI.h"

/**
 * @file UI.cpp
 * @brief UI 관련 함수들(화면 그리기, 입력 처리)의 실제 구현을 포함합니다.
 */


//==============================================================================
// UI 화면 그리기 구현
//==============================================================================

void drawCurrentScreen() {
  switch (g_currentScreen) {
    case SCREEN_DASHBOARD:  drawDashboard();   break;
    case SCREEN_TANK:       drawTankScreen();  break;
    case SCREEN_GROW:       drawGrowScreen();  break;
    case SCREEN_NUTRIENT:   drawNutrientScreen(); break;
    case SCREEN_FEEDER:     drawFeederScreen();   break;
    case SCREEN_LOG:        drawLogScreen();      break;
    case SCREEN_SETTINGS:   drawSettingsScreen(); break;
    default:                drawDashboard();   break;
  }
}

void drawDashboard() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextSize(2);
  tft.println("[Dashboard]");

  if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    tft.printf("Tank: st=%d temp=%.1fC lvl=%.1f%%\n",
               g_state.tank.status,
               g_state.tank.tempC,
               g_state.tank.levelPercent);
    tft.printf("Grow: st=%d temp=%.1fC hum=%.1f%%\n",
               g_state.grow.status,
               g_state.grow.tempC,
               g_state.grow.humidity);
    tft.printf("Nutr: st=%d\n", g_state.nutrient.status);
    tft.printf("Feed: st=%d\n", g_state.feeder.status);

    tft.printf("Server: %s\n", g_state.serverConnected ? "ON" : "OFF");
    tft.printf("Warn: %d Err: %d\n", g_state.hasWarning, g_state.hasError);

    xSemaphoreGive(g_stateMutex);
  }

  tft.println();
  tft.println("Rotary: change screen");
  tft.println("Btn:   select/confirm");
}

void drawTankScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextSize(2);
  tft.println("[Tank]");

  if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    tft.printf("Temp: %.1fC\n", g_state.tank.tempC);
    tft.printf("Level: %.1f%%\n", g_state.tank.levelPercent);
    tft.printf("pH: %.2f\n", g_state.tank.pH);
    tft.printf("TDS: %.0f\n", g_state.tank.tds);
    tft.printf("DO: %.1f mg/L\n", g_state.tank.do_mgL);
    tft.printf("Pump: %s\n", g_state.tank.pumpOn ? "ON" : "OFF");
    tft.printf("Light: %s\n", g_state.tank.lightOn ? "ON" : "OFF");
    xSemaphoreGive(g_stateMutex);
  }

  tft.println();
  tft.println("Short Btn: Pump ON/OFF");
  tft.println("Long  Btn: Light ON/OFF");
}

void drawGrowScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextSize(2);
  tft.println("[Grow]");

  if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    tft.printf("Temp: %.1fC\n", g_state.grow.tempC);
    tft.printf("Hum:  %.1f%%\n", g_state.grow.humidity);
    tft.printf("Leak: %d%d%d%d\n",
               g_state.grow.leak[0], g_state.grow.leak[1],
               g_state.grow.leak[2], g_state.grow.leak[3]);
    tft.printf("LED:  %d%%\n", g_state.grow.ledBrightness);
    xSemaphoreGive(g_stateMutex);
  }

  tft.println();
  tft.println("Short Btn: LED 0/50/100%");
  tft.println("Long  Btn: Reset leaks");
}

void drawNutrientScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextSize(2);
  tft.println("[Nutrient] (TODO)");
  tft.println("Show channel ratio, motor, level");
}

void drawFeederScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextSize(2);
  tft.println("[Feeder] (TODO)");
  tft.println("Show remain, history, schedule");
}

void drawLogScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextSize(2);
  tft.println("[Logs]");
  int idx = g_logHead;
  for (int i = 0; i < g_logCount && i < 6; ++i) {
    idx = (idx - 1 + LOG_BUFFER_SIZE) % LOG_BUFFER_SIZE;
    // TFT 라이브러리가 String 객체를 직접 처리하지 못할 수 있으므로 c_str() 사용
    tft.println(g_logBuffer[idx].c_str());
  }
}

void drawSettingsScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextSize(2);
  tft.println("[Settings]");
  tft.printf("DisplayOff: %d min\n", g_settings.displayOffMinutes);
  tft.printf("Feeder: %02d:%02d amt=%d%%\n",
             g_settings.feederHour,
             g_settings.feederMinute,
             g_settings.feederAmountPercent);
  tft.printf("GrowLED: %d%%\n", g_settings.growLedBrightness);
  tft.printf("FW: 0x%08lx\n", g_settings.fwVersion);
  tft.printf("FactoryInit: %d\n", g_settings.factoryInitialized);

  tft.println();
  tft.println("Short: DispOff cycle");
  tft.println("Long : FactoryInit toggle");
}


//==============================================================================
// UI 화면별 클릭 이벤트 처리 구현
//==============================================================================

void handleTankClick(bool shortClick, bool longClick) {
  if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
    logEvent("handleTankClick: mutex timeout");
    return;
  }

  if (shortClick) {
    // 펌프 토글
    g_state.tank.pumpOn = !g_state.tank.pumpOn;
    bool on = g_state.tank.pumpOn;
    xSemaphoreGive(g_stateMutex);

    requestTankPump(on);
  } else if (longClick) {
    // 조명 토글
    g_state.tank.lightOn = !g_state.tank.lightOn;
    bool on = g_state.tank.lightOn;
    xSemaphoreGive(g_stateMutex);

    requestTankLight(on);
  } else {
    xSemaphoreGive(g_stateMutex);
  }
}

void handleGrowClick(bool shortClick, bool longClick) {
  if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
    logEvent("handleGrowClick: mutex timeout");
    return;
  }

  if (shortClick) {
    // LED 밝기 0 → 50 → 100 → 0 순환
    uint8_t b = g_state.grow.ledBrightness;
    if (b == 0)      b = 50;
    else if (b == 50) b = 100;
    else             b = 0;

    g_state.grow.ledBrightness   = b;
    g_settings.growLedBrightness = b;
    xSemaphoreGive(g_stateMutex);

    // 설정 저장 및 CAN 전송
    saveSettings();
    requestGrowLedBrightness(b);
  } else if (longClick) {
    // 누수 플래그 리셋 + 에러 해제
    g_state.grow.leak[0] = false;
    g_state.grow.leak[1] = false;
    g_state.grow.leak[2] = false;
    g_state.grow.leak[3] = false;
    g_state.hasError     = false;
    xSemaphoreGive(g_stateMutex);

    logEvent("Grow leaks reset (long click)");
  } else {
    xSemaphoreGive(g_stateMutex);
  }
}

void handleLogClick(bool shortClick, bool longClick) {
  if (longClick) {
    clearLogs();
  } else if (shortClick) {
    logEvent("Log screen short click");
  }
}

void handleSettingsClick(bool shortClick, bool longClick) {
  if (shortClick) {
    // 화면 끄기 시간 0 → 5 → 10 → 30 → 0 순환
    uint8_t v = g_settings.displayOffMinutes;
    if      (v == 0)  v = 5;
    else if (v == 5)  v = 10;
    else if (v == 10) v = 30;
    else              v = 0;

    g_settings.displayOffMinutes = v;
    saveSettings();

    logEvent(("Settings: displayOffMinutes = " + String(v)).c_str());
  }
  else if (longClick) {
    // 공장 초기화 플래그 토글 (예시)
    g_settings.factoryInitialized = !g_settings.factoryInitialized;
    saveSettings();

    logEvent(g_settings.factoryInitialized
             ? "Settings: factoryInitialized = 1"
             : "Settings: factoryInitialized = 0");
  }
}

