#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
extern "C" {
  #include "driver/twai.h"   // ESP32 CAN(TWAI) 드라이버
}

// ======================== 하드웨어 핀 설정 ========================
// TODO: 실제 보드 설계에 맞게 수정할 것
static const int PIN_BUZZER     = 25;
static const int PIN_LED_RED    = 26;
static const int PIN_LED_GREEN  = 27;
static const int PIN_LED_BLUE   = 14;

static const int PIN_ROTARY_A   = 32;
static const int PIN_ROTARY_B   = 33;
static const int PIN_ROTARY_SW  = 13;

static const int UART_TX_PIN    = 17;  // ESP32 -> Raspberry Pi
static const int UART_RX_PIN    = 16;  // ESP32 <- Raspberry Pi

static const int CAN_TX_PIN     = 5;
static const int CAN_RX_PIN     = 4;

// ======================== 주기/타이밍 설정 ========================
static const uint32_t PERIOD_CAN_COLLECT_MS  = 100;  // FR-001
static const uint32_t PERIOD_UART_TX_MS      = 200;  // FR-002
static const uint32_t PERIOD_UI_UPDATE_MS    = 5000;  // FR-003
static const uint32_t BOOT_READY_MS          = 3000; // 부팅 후 3초 이내 기본 루틴

static const uint32_t SERVER_TIMEOUT_MS      = 5000; // 5초 동안 명령 없으면 Fail-safe 모드

// ======================== 모듈/ID 정의(예시) ======================
enum ModuleId : uint8_t {
  MODULE_TANK      = 1,
  MODULE_GROW      = 2,
  MODULE_NUTRIENT  = 3,
  MODULE_FEEDER    = 4,
};

enum ModuleStatus : uint8_t {
  MODULE_OFFLINE = 0,
  MODULE_OK      = 1,
  MODULE_WARN    = 2,
  MODULE_ERROR   = 3,
};

// ======================== 모듈별 명령 코드 정의 =======================
// Tank 모듈 제어용 명령
enum TankCommand : uint8_t {
  TANK_CMD_SET_PUMP  = 1,   // param: 0=OFF, 1=ON
  TANK_CMD_SET_LIGHT = 2    // param: 0=OFF, 1=ON
};

// Grow 모듈 제어용 명령
enum GrowCommand : uint8_t {
  GROW_CMD_SET_LED_BRIGHTNESS = 1  // param: 0~100 (%)
};

// Feeder 모듈 제어용 명령
enum FeederCommand : uint8_t {
  FEEDER_CMD_FEED_ONCE = 1        // param: 급여량(%)
};

// ======================== 상태/설정 구조체 =======================
struct TankModuleState {
  ModuleStatus status;
  float tempC;
  float levelPercent;
  float pH;
  float tds;
  float turbidity;
  float do_mgL;
  bool  pumpOn;
  bool  lightOn;
  uint32_t lastUpdateMs;
};

struct GrowModuleState {
  ModuleStatus status;
  float tempC;
  float humidity;
  bool leak[4];
  uint8_t ledBrightness; // 0~100%
  uint32_t lastUpdateMs;
};

struct NutrientModuleState {
  ModuleStatus status;
  float channelRatio[4]; // 0~100%
  bool channelMotorOn[4];
  float levelPercent;
  uint32_t lastUpdateMs;
};

struct FeederModuleState {
  ModuleStatus status;
  float feedLevelPercent;
  uint32_t lastFeedTime; // epoch or millis 기준
  bool feedingNow;
  uint32_t lastUpdateMs;
};

struct SystemState {
  TankModuleState      tank;
  GrowModuleState      grow;
  NutrientModuleState  nutrient;
  FeederModuleState    feeder;

  bool serverConnected;
  uint32_t lastServerRxMs;

  bool hasWarning;
  bool hasError;
};

// 설정/영속화 (예시 필드 - 실제 프로젝트에 맞게 확장)
struct SystemSettings {
  uint8_t  displayOffMinutes;    // 화면 끄기 시간
  bool     moduleEnabledTank;
  bool     moduleEnabledGrow;
  bool     moduleEnabledNutrient;
  bool     moduleEnabledFeeder;

  // 간단한 스케줄 예시 (실 프로젝트에서는 배열/구조를 더 정교하게)
  uint8_t  feederHour;
  uint8_t  feederMinute;
  uint8_t  feederAmountPercent;

  uint8_t  growLedBrightness;    // 기본 밝기 0~100

  // 버전, 초기화 플래그 등
  uint32_t fwVersion;
  bool     factoryInitialized;
};

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
struct CanTxItem {
  uint32_t canId;
  uint8_t  dlc;
  uint8_t  data[8];
};
QueueHandle_t g_canTxQueue = nullptr;

struct ServerCommand {
  uint8_t targetModule;
  uint8_t command;
  int32_t param;
};
QueueHandle_t g_serverCmdQueue = nullptr;

TaskHandle_t g_taskCanHandle     = nullptr;
TaskHandle_t g_taskUartHandle    = nullptr;
TaskHandle_t g_taskUiHandle      = nullptr;
TaskHandle_t g_taskLogicHandle   = nullptr;
TaskHandle_t g_taskAlarmHandle   = nullptr;

// ======================== UI/입력 상태 ===========================
enum ScreenId : uint8_t {
  SCREEN_DASHBOARD = 0,
  SCREEN_TANK,
  SCREEN_GROW,
  SCREEN_NUTRIENT,
  SCREEN_FEEDER,
  SCREEN_LOG,
  SCREEN_SETTINGS,
  SCREEN_COUNT
};

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
enum AlarmLevel : uint8_t {
  ALARM_NONE = 0,
  ALARM_WARNING,
  ALARM_ERROR
};

volatile AlarmLevel g_alarmLevel = ALARM_NONE;

// ======================== 전방 선언 ==============================
void initPins();
void initTft();
void initCan();
void initUart();
void loadSettings();
void saveSettings();      // 필요 시 UI에서 호출
void resetSystemState();

void playBootBuzzer();
void playClickBuzzer();

void logEvent(const char *msg);

void updateRotary();
bool fetchButtonClicked();

String buildStatusJson();
void parseServerLine(const String &line);
void handleServerCommand(const ServerCommand &cmd);

void handleCanFrame(const twai_message_t &msg);
void enqueueCanCommand(uint8_t moduleId, uint8_t cmd, int32_t param);

// UI 관련
void drawCurrentScreen();
void drawDashboard();
void drawTankScreen();
void drawGrowScreen();
void drawNutrientScreen();
void drawFeederScreen();
void drawLogScreen();
void drawSettingsScreen();

void taskCan(void *pvParameters);
void taskUart(void *pvParameters);
void taskUi(void *pvParameters);
void taskLogic(void *pvParameters);
void taskAlarm(void *pvParameters);

// RTOS Tasks
void taskCan(void *pvParameters);
void taskUart(void *pvParameters);
void taskUi(void *pvParameters);
void taskLogic(void *pvParameters);
void taskAlarm(void *pvParameters);

// UI 클릭 핸들러
void handleTankClick(bool shortClick, bool longClick);
void handleGrowClick(bool shortClick, bool longClick);
void handleSettingsClick(bool shortClick, bool longClick);
void handleLogClick(bool shortClick, bool longClick);

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
static const int LOG_BUFFER_SIZE = 64;
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

// ======================== 로터리 인코더 ==========================
void updateRotary() {
  // ===== 로터리 인코더 (A falling edge 기준으로 1 step) =====
  static uint32_t lastStepTime = 0;

  int a = digitalRead(PIN_ROTARY_A);
  int b = digitalRead(PIN_ROTARY_B);

  // 이전에 HIGH였다가 지금 LOW가 되면(A falling edge) 한 스텝으로 간주
  if (g_lastEncA == HIGH && a == LOW) {
    uint32_t now = millis();
    // 간단한 디바운스 (2ms 이상 간격)
    if (now - lastStepTime > 2) {
      if (b == HIGH) {
        g_encoderPos++;   // 한 방향
      } else {
        g_encoderPos--;   // 반대 방향
      }
      lastStepTime = now;
    }
  }

  g_lastEncA = a;
  g_lastEncB = b;

  // ===== 로터리 버튼 (짧은 클릭 / 긴 클릭) =====
  bool btnPressed = (digitalRead(PIN_ROTARY_SW) == LOW); // LOW면 눌림
  uint32_t nowMs = millis();

  if (btnPressed && !g_lastButton) {
    // 버튼이 막 눌리기 시작한 시점
    g_buttonPressStartMs = nowMs;
  } else if (!btnPressed && g_lastButton) {
    // 버튼에서 손을 뗀 시점 → 눌려 있던 시간 계산
    uint32_t pressDuration = nowMs - g_buttonPressStartMs;
    if (pressDuration >= 700) {
      // 700ms 이상: 긴 클릭으로 간주
      g_buttonLongClicked = true;
    } else {
      // 700ms 미만: 짧은 클릭으로 간주
      g_buttonClicked = true;
    }
  }

  g_lastButton = btnPressed;
}

bool fetchShortClick() {
  bool clicked = g_buttonClicked;
  g_buttonClicked = false;
  return clicked;
}

bool fetchLongClick() {
  bool clicked = g_buttonLongClicked;
  g_buttonLongClicked = false;
  return clicked;
}

// 기존 함수는 "짧은 클릭"을 반환하도록 유지
bool fetchButtonClicked() {
  return fetchShortClick();
}

// ======================== CAN 처리 ===============================
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

// ======================== 모듈 제어 helper ============================

// Tank: 펌프 ON/OFF
void requestTankPump(bool on) {
  enqueueCanCommand(MODULE_TANK, TANK_CMD_SET_PUMP, on ? 1 : 0);
  logEvent(on ? "Tank pump ON requested" : "Tank pump OFF requested");
}

// Tank: 조명 ON/OFF
void requestTankLight(bool on) {
  enqueueCanCommand(MODULE_TANK, TANK_CMD_SET_LIGHT, on ? 1 : 0);
  logEvent(on ? "Tank light ON requested" : "Tank light OFF requested");
}

// Grow: LED 밝기 설정 (0~100%)
void requestGrowLedBrightness(uint8_t brightness) {
  if (brightness > 100) brightness = 100;
  enqueueCanCommand(MODULE_GROW, GROW_CMD_SET_LED_BRIGHTNESS, (int32_t)brightness);
  logEvent(("Grow LED brightness set to " + String(brightness) + "%").c_str());
}

// Feeder: 한 번 급여
void requestFeederOnce(uint8_t amountPercent) {
  if (amountPercent > 100) amountPercent = 100;
  enqueueCanCommand(MODULE_FEEDER, FEEDER_CMD_FEED_ONCE, (int32_t)amountPercent);
  logEvent(("Feeder once, amount " + String(amountPercent) + "% requested").c_str());
}


// 각 모듈이 100ms 주기로 보내는 상태 프레임 처리 (예시)
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

// ======================== UART 프로토콜(예시) ====================
// 상태 전송: 간단한 JSON 유사 문자열
String buildStatusJson() {
  String s = "{";

  if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    s += "\"tank\":{\"st\":" + String(g_state.tank.status) +
         ",\"temp\":" + String(g_state.tank.tempC, 1) +
         ",\"lvl\":" + String(g_state.tank.levelPercent, 1) +
         ",\"pH\":" + String(g_state.tank.pH, 2) + "}";

    s += ",\"grow\":{\"st\":" + String(g_state.grow.status) +
         ",\"temp\":" + String(g_state.grow.tempC, 1) +
         ",\"hum\":" + String(g_state.grow.humidity, 1) + "}";

    s += ",\"nutr\":{\"st\":" + String(g_state.nutrient.status) + "}";
    s += ",\"feed\":{\"st\":" + String(g_state.feeder.status) + "}";

    s += ",\"srv\":" + String(g_state.serverConnected ? 1 : 0);

    xSemaphoreGive(g_stateMutex);
  }

  s += "}";
  return s;
}

// 서버 → 메인 컨트롤러 명령 예시 포맷 (아주 단순화)
// "CMD,<moduleId>,<command>,<param>\n"
void parseServerLine(const String &line) {
  if (!line.startsWith("CMD")) return;

  ServerCommand cmd{};
  int idx1 = line.indexOf(',');
  int idx2 = line.indexOf(',', idx1 + 1);
  int idx3 = line.indexOf(',', idx2 + 1);

  if (idx1 < 0 || idx2 < 0 || idx3 < 0) return;

  cmd.targetModule = (uint8_t) line.substring(idx1 + 1, idx2).toInt();
  cmd.command      = (uint8_t) line.substring(idx2 + 1, idx3).toInt();
  cmd.param        = (int32_t) line.substring(idx3 + 1).toInt();

  if (g_serverCmdQueue) {
    xQueueSend(g_serverCmdQueue, &cmd, 0);
  }

  if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    g_state.serverConnected = true;
    g_state.lastServerRxMs  = millis();
    xSemaphoreGive(g_stateMutex);
  }
}

// 서버 명령 처리 → CAN 라우팅
void handleServerCommand(const ServerCommand &cmd) {
  // FR-006 명령 라우팅
  enqueueCanCommand(cmd.targetModule, cmd.command, cmd.param);

  // 예시: UI 클릭 피드백
  playClickBuzzer();
}

// ======================== UI 그리기 ==============================
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
  tft.println("Rotary: LED/Leak select");
  tft.println("Btn:    toggle/adjust");
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
    tft.println(g_logBuffer[idx]);
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

// ======================== RTOS Tasks =============================
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

  tft.println();
  tft.println("Short Btn: LED 0/50/100%");
  tft.println("Long  Btn: Reset leaks");
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
