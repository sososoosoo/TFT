#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
extern "C" {
  #include "driver/twai.h"   // ESP32 CAN(TWAI) 드라이버
}

#include "Config.h"

#include "DataTypes.h"

#include "Globals.h"

#include "UI.h"

#include "Tasks.h"

#include "Communication.h"

#include "Input.h"

// ======================== 전역 인스턴스 ==========================
TFT_eSPI tft = TFT_eSPI();
Preferences prefs;       // NVS
SystemState g_state;
SystemSettings g_settings;

// ======================== 소프트웨어 시계 및 스케줄 ===================
// 부팅 기준 uptime 기반의 간단한 시계 (실제 RTC/서버 시간 동기 전 단계)
uint32_t g_uptimeSeconds     = 0;
uint32_t g_lastClockUpdateMs = 0;
uint8_t  g_timeHour          = 0;   // 0~23
uint8_t  g_timeMinute        = 0;   // 0~59

// Fail-safe 모드에서 하루에 한 번 급여를 실행하기 위한 마지막 실행 시각(분 단위)
int16_t  g_lastFeederScheduleMinute = -1;

// RTOS 동기화 객체
SemaphoreHandle_t g_stateMutex;

// 큐/태스크 핸들
QueueHandle_t g_canTxQueue = nullptr;

QueueHandle_t g_serverCmdQueue = nullptr;

TaskHandle_t g_taskCanHandle     = nullptr;
TaskHandle_t g_taskUartHandle    = nullptr;
TaskHandle_t g_taskUiHandle      = nullptr;
TaskHandle_t g_taskLogicHandle   = nullptr;
TaskHandle_t g_taskAlarmHandle   = nullptr;

// ======================== UI/입력 상태 ===========================
volatile ScreenId g_currentScreen = SCREEN_DASHBOARD;

// 로터리 인코더
int16_t g_encoderPos = 0;
int16_t g_lastEncoderPos = 0;
int16_t g_lastScreenEncPos = 0;
int8_t  g_lastEncA = 0;
int8_t  g_lastEncB = 0;
bool    g_lastButton = false;          // "이전 프레임에 눌려 있었는지" (true면 눌림 상태)
volatile bool g_buttonClicked = false;      // 짧은 클릭(Short click)
volatile bool g_buttonLongClicked = false;  // 긴 클릭(Long click)
uint32_t g_buttonPressStartMs = 0;          // 버튼 눌린 시각 기록용

// ======================== 부저/LED 패턴 ==========================
volatile AlarmLevel g_alarmLevel = ALARM_NONE;



// ======================== setup / loop ===========================
void setup() {
  // 디버그 시리얼
  Serial.begin(115200);
  delay(1000);

  initPins();
  initTft();

  // NVS
  prefs.begin("aq_main", false);
  loadSettings();

  resetSystemState();

  // CAN / UART 초기화
  initCan();
  initUart();

  // 상태 보호용 mutex
  g_stateMutex = xSemaphoreCreateMutex();

  // ✅ 부팅 직후 대시보드 한 번 그리기
  drawCurrentScreen();

  // 큐
  g_canTxQueue      = xQueueCreate(16, sizeof(CanTxItem));
  g_serverCmdQueue  = xQueueCreate(16, sizeof(ServerCommand));

  // 부팅 부저 패턴
  playBootBuzzer();

  // Task 생성
  //xTaskCreatePinnedToCore(taskCan,   "CAN_Task",   4096, nullptr, 3, &g_taskCanHandle,   0);
  xTaskCreatePinnedToCore(taskUart,  "UART_Task",  4096, nullptr, 2, &g_taskUartHandle,  1);
  xTaskCreatePinnedToCore(taskUi,    "UI_Task",    8192, nullptr, 1, &g_taskUiHandle,    1);
  xTaskCreatePinnedToCore(taskLogic, "Logic_Task", 4096, nullptr, 2, &g_taskLogicHandle, 0);
  xTaskCreatePinnedToCore(taskAlarm, "Alarm_Task", 2048, nullptr, 1, &g_taskAlarmHandle, 0);

  // 부팅 후 3초 이내 Ready: 여기서는 이미 FreeRTOS가 돌고 있으므로 별도 처리 없이 넘어감
}

void loop() {
  // FreeRTOS 사용 시 loop는 비워두거나 슬립만
  vTaskDelay(pdMS_TO_TICKS(1000));
}

// ======================== 초기화 함수들 ==========================
void initPins() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_BLUE, LOW);

  pinMode(PIN_ROTARY_A, INPUT_PULLUP);
  pinMode(PIN_ROTARY_B, INPUT_PULLUP);
  pinMode(PIN_ROTARY_SW, INPUT_PULLUP);

  g_lastEncA = digitalRead(PIN_ROTARY_A);
  g_lastEncB = digitalRead(PIN_ROTARY_B);
  g_lastButton = (digitalRead(PIN_ROTARY_SW) == LOW);  // LOW면 눌림
}

void initTft() {
  tft.init();
  tft.setRotation(1);  // TODO: LCD 방향에 맞게 조정
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(0, 0);
  tft.println("Aquaponics Main Controller");
}


void initCan() {
  // 일단 디버그를 위해 CAN 초기화는 잠시 비활성화
  Serial.println("[CAN] init skipped (debug mode)");
}

/*
void initCan() {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)CAN_TX_PIN,
      (gpio_num_t)CAN_RX_PIN,
      TWAI_MODE_NORMAL
  );
  twai_timing_config_t  t_config = TWAI_TIMING_CONFIG_500KBITS();  // 이 부분은 그대로 두면 됨
  twai_filter_config_t  f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    if (twai_start() == ESP_OK) {
      Serial.println("[CAN] Started");
    } else {
      Serial.println("[CAN] Start failed");
    }
  } else {
    Serial.println("[CAN] Install failed");
  }
}
*/


void initUart() {
  // UART2: Raspberry Pi와 연결
  Serial2.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.println("[UART] Serial2 started (Raspberry Pi link)");
}

// ======================== 상태/설정 관리 =========================
void resetSystemState() {
  memset(&g_state, 0, sizeof(g_state));

  g_state.tank.status     = MODULE_OFFLINE;
  g_state.grow.status     = MODULE_OFFLINE;
  g_state.nutrient.status = MODULE_OFFLINE;
  g_state.feeder.status   = MODULE_OFFLINE;

  g_state.serverConnected = false;
  g_state.lastServerRxMs  = 0;
  g_state.hasWarning      = false;
  g_state.hasError        = false;
}

void loadSettings() {
  g_settings.displayOffMinutes = prefs.getUChar("dispOffMin", 10);
  g_settings.moduleEnabledTank = prefs.getBool("enTank", true);
  g_settings.moduleEnabledGrow = prefs.getBool("enGrow", true);
  g_settings.moduleEnabledNutrient = prefs.getBool("enNutr", true);
  g_settings.moduleEnabledFeeder   = prefs.getBool("enFeed", true);

  g_settings.feederHour   = prefs.getUChar("fdHour", 8);
  g_settings.feederMinute = prefs.getUChar("fdMin",  0);
  g_settings.feederAmountPercent = prefs.getUChar("fdAmt", 50);

  g_settings.growLedBrightness = prefs.getUChar("growBright", 70);

  g_settings.fwVersion = prefs.getULong("fwVer", 0x00010000); // v1.0.0
  g_settings.factoryInitialized = prefs.getBool("factoryInit", false);
}

void saveSettings() {
  prefs.putUChar("dispOffMin", g_settings.displayOffMinutes);
  prefs.putBool("enTank",      g_settings.moduleEnabledTank);
  prefs.putBool("enGrow",      g_settings.moduleEnabledGrow);
  prefs.putBool("enNutr",      g_settings.moduleEnabledNutrient);
  prefs.putBool("enFeed",      g_settings.moduleEnabledFeeder);

  prefs.putUChar("fdHour", g_settings.feederHour);
  prefs.putUChar("fdMin",  g_settings.feederMinute);
  prefs.putUChar("fdAmt",  g_settings.feederAmountPercent);

  prefs.putUChar("growBright", g_settings.growLedBrightness);

  prefs.putULong("fwVer", g_settings.fwVersion);
  prefs.putBool("factoryInit", g_settings.factoryInitialized);
}

// ======================== 부저/LED 유틸 ==========================
void playBootBuzzer() {
  // 부팅: 200ms ON, 200ms OFF, 2회
  for (int i = 0; i < 2; ++i) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(200);
    digitalWrite(PIN_BUZZER, LOW);
    delay(200);
  }
}

void playClickBuzzer() {
  // UI 클릭: 50ms ON, 1회
  digitalWrite(PIN_BUZZER, HIGH);
  delay(50);
  digitalWrite(PIN_BUZZER, LOW);
}

// ======================== 로깅(간단 버퍼) ========================
String g_logBuffer[LOG_BUFFER_SIZE];
int g_logHead = 0;
int g_logCount = 0;

void logEvent(const char *msg) {
  String s = String(millis()) + ": " + msg;
  g_logBuffer[g_logHead] = s;
  g_logHead = (g_logHead + 1) % LOG_BUFFER_SIZE;
  if (g_logCount < LOG_BUFFER_SIZE) g_logCount++;
  Serial.println(s);
}

// 로그 삭제 기능
void clearLogs() {
  g_logHead = 0;
  g_logCount = 0;
  // clear 후 한 줄 남겨두기
  logEvent("Log buffer cleared");
}





