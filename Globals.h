#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "DataTypes.h"

/**
 * @file Globals.h
 * @brief 다른 소스 파일에서 공통으로 참조하는 전역 변수들을 'extern'으로 선언합니다.
 * @note 변수의 실제 정의(메모리 할당)는 MainController.ino에 있습니다.
 */

//==============================================================================
// 전역 객체 인스턴스
//==============================================================================
extern TFT_eSPI tft;      // TFT LCD 라이브러리 객체
extern Preferences prefs; // 비휘발성 저장소(NVS) 라이브러리 객체


//==============================================================================
// 시스템 상태 및 설정
//==============================================================================
extern SystemState g_state;       // 시스템의 현재 상태를 담는 전역 변수
extern SystemSettings g_settings; // 시스템의 설정값을 담는 전역 변수


//==============================================================================
// FreeRTOS 관련 핸들 및 동기화 객체
//==============================================================================
extern SemaphoreHandle_t g_stateMutex;     // g_state 보호를 위한 뮤텍스
extern QueueHandle_t g_canTxQueue;         // CAN 전송 명령 큐
extern QueueHandle_t g_serverCmdQueue;     // 서버 수신 명령 큐
extern TaskHandle_t g_taskCanHandle;       // CAN 통신 태스크 핸들
extern TaskHandle_t g_taskUartHandle;      // UART 통신 태스크 핸들
extern TaskHandle_t g_taskUiHandle;        // UI 처리 태스크 핸들
extern TaskHandle_t g_taskLogicHandle;     // 로직 처리 태스크 핸들
extern TaskHandle_t g_taskAlarmHandle;     // 알람 처리 태스크 핸들


//==============================================================================
// UI 및 사용자 입력 상태
//==============================================================================
extern volatile ScreenId g_currentScreen;  // 현재 표시 중인 화면 ID

// 로터리 엔코더 관련 변수
extern int16_t g_encoderPos;
extern int16_t g_lastEncoderPos;
extern int16_t g_lastScreenEncPos;
extern int8_t  g_lastEncA;
extern int8_t  g_lastEncB;

// 로터리 엔코더 버튼 관련 변수
extern bool    g_lastButton;
extern volatile bool g_buttonClicked;
extern volatile bool g_buttonLongClicked;
extern uint32_t g_buttonPressStartMs;


//==============================================================================
// 소프트웨어 시계 및 스케줄러
//==============================================================================
extern uint32_t g_uptimeSeconds;
extern uint32_t g_lastClockUpdateMs;
extern uint8_t  g_timeHour;
extern uint8_t  g_timeMinute;
extern int16_t  g_lastFeederScheduleMinute;


//==============================================================================
// 알람 및 로깅
//==============================================================================
extern volatile AlarmLevel g_alarmLevel; // 현재 알람 레벨

// 간단 로그 버퍼
extern String g_logBuffer[];
extern int g_logHead;
extern int g_logCount;


//==============================================================================
// 전역 함수 프로토타입
//==============================================================================
// 초기화
void initPins();
void initTft();
void initCan();
void initUart();
void loadSettings();
void saveSettings();
void resetSystemState();

// 유틸리티
void playBootBuzzer();
void playClickBuzzer();
void logEvent(const char *msg);
void clearLogs();

// 입력 처리
void updateRotary();
bool fetchButtonClicked();
bool fetchShortClick();
bool fetchLongClick();

// 통신
String buildStatusJson();
void parseServerLine(const String &line);
void handleServerCommand(const ServerCommand &cmd);
void handleCanFrame(const twai_message_t &msg);
void enqueueCanCommand(uint8_t moduleId, uint8_t cmd, int32_t param);
void requestTankPump(bool on);
void requestTankLight(bool on);
void requestGrowLedBrightness(uint8_t brightness);
void requestFeederOnce(uint8_t amountPercent);

// UI
void drawCurrentScreen();
void drawDashboard();
void drawTankScreen();
void drawGrowScreen();
void drawNutrientScreen();
void drawFeederScreen();
void drawLogScreen();
void drawSettingsScreen();
void handleTankClick(bool shortClick, bool longClick);
void handleGrowClick(bool shortClick, bool longClick);
void handleSettingsClick(bool shortClick, bool longClick);
void handleLogClick(bool shortClick, bool longClick);

// FreeRTOS 태스크
void taskCan(void *pvParameters);
void taskUart(void *pvParameters);
void taskUi(void *pvParameters);
void taskLogic(void *pvParameters);
void taskAlarm(void *pvParameters);


#endif // GLOBALS_H
