#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <Arduino.h>
#include "Config.h" // ModuleStatus 같은 enum을 사용하기 위해 포함

/**
 * @file DataTypes.h
 * @brief 시스템 전역에서 사용되는 데이터 구조체(struct)와 열거형(enum)을 정의합니다.
 */


//==============================================================================
// 모듈별 상태 정보 구조체
//==============================================================================

/**
 * @brief 수조(Tank) 모듈의 상태를 저장하는 구조체
 */
struct TankModuleState {
  ModuleStatus status;       // 모듈 현재 상태 (온라인, 오프라인, 경고, 에러)
  float tempC;               // 수온 (섭씨)
  float levelPercent;        // 수위 (%)
  float pH;                  // pH 값
  float tds;                 // 총 용존 고형물 (ppm)
  float turbidity;           // 탁도
  float do_mgL;              // 용존 산소량 (mg/L)
  bool  pumpOn;              // 펌프 작동 여부
  bool  lightOn;             // 조명 켜짐 여부
  uint32_t lastUpdateMs;     // 마지막으로 데이터가 업데이트된 시각 (millis())
};

/**
 * @brief 재배기(Grow) 모듈의 상태를 저장하는 구조체
 */
struct GrowModuleState {
  ModuleStatus status;
  float tempC;               // 내부 온도 (섭씨)
  float humidity;            // 내부 습도 (%)
  bool leak[4];              // 누수 센서 상태 (채널 4개)
  uint8_t ledBrightness;     // LED 조명 밝기 (0-100%)
  uint32_t lastUpdateMs;
};

/**
 * @brief 양액기(Nutrient) 모듈의 상태를 저장하는 구조체
 */
struct NutrientModuleState {
  ModuleStatus status;
  float channelRatio[4];     // 각 채널별 비율 (0-100%)
  bool channelMotorOn[4];    // 각 채널별 모터 작동 여부
  float levelPercent;        // 양액 잔량 (%)
  uint32_t lastUpdateMs;
};

/**
 * @brief 급여기(Feeder) 모듈의 상태를 저장하는 구조체
 */
struct FeederModuleState {
  ModuleStatus status;
  float feedLevelPercent;    // 사료 잔량 (%)
  uint32_t lastFeedTime;     // 마지막 급여 시각
  bool feedingNow;           // 현재 급여가 진행중인지 여부
  uint32_t lastUpdateMs;
};


//==============================================================================
// 시스템 전체 상태 및 설정 구조체
//==============================================================================

/**
 * @brief 전체 시스템의 현재 상태를 종합하는 구조체
 */
struct SystemState {
  // 각 모듈의 상태
  TankModuleState      tank;
  GrowModuleState      grow;
  NutrientModuleState  nutrient;
  FeederModuleState    feeder;

  // 시스템 전역 상태
  bool serverConnected;      // 원격 서버(Raspberry Pi 등)와의 연결 여부
  uint32_t lastServerRxMs;   // 서버로부터 마지막으로 메시지를 받은 시각
  bool hasWarning;           // 시스템에 '경고' 상태가 있는지 여부
  bool hasError;             // 시스템에 '오류' 상태가 있는지 여부
};

/**
 * @brief 비휘발성 저장소(NVS)에 저장되어 유지되는 설정값 구조체
 */
struct SystemSettings {
  uint8_t  displayOffMinutes;     // 설정된 시간 동안 입력 없으면 화면 끄기 (0: 항상 켜기)
  bool     moduleEnabledTank;     // 수조 모듈 활성화 여부
  bool     moduleEnabledGrow;     // 재배기 모듈 활성화 여부
  bool     moduleEnabledNutrient; // 양액기 모듈 활성화 여부
  bool     moduleEnabledFeeder;   // 급여기 모듈 활성화 여부

  // 급여 스케줄
  uint8_t  feederHour;            // 급여 시(0-23)
  uint8_t  feederMinute;          // 급여 분(0-59)
  uint8_t  feederAmountPercent;   // 1회 급여량 (%)

  uint8_t  growLedBrightness;     // 재배기 LED 기본 밝기 (0-100%)

  // 시스템 정보
  uint32_t fwVersion;             // 펌웨어 버전
  bool     factoryInitialized;    // 공장 초기화 진행 여부 플래그
};


//==============================================================================
// 통신 및 작업 큐 항목 구조체
//==============================================================================

/**
 * @brief CAN 버스로 전송할 명령을 큐에 담기 위한 구조체
 */
struct CanTxItem {
  uint32_t canId; // CAN ID
  uint8_t  dlc;   // Data Length Code (0-8)
  uint8_t  data[8]; // 데이터 페이로드
};

/**
 * @brief UART로 수신한 서버 명령을 큐에 담기 위한 구조체
 */
struct ServerCommand {
  uint8_t targetModule; // 명령 대상 모듈 ID
  uint8_t command;      // 명령 코드
  int32_t param;        // 파라미터
};


//==============================================================================
// UI 및 알람 상태 열거형
//==============================================================================

/**
 * @brief TFT LCD에 표시될 화면의 종류를 정의하는 열거형
 */
enum ScreenId : uint8_t {
  SCREEN_DASHBOARD = 0, // 대시보드
  SCREEN_TANK,          // 수조 상세
  SCREEN_GROW,          // 재배기 상세
  SCREEN_NUTRIENT,      // 양액기 상세
  SCREEN_FEEDER,        // 급여기 상세
  SCREEN_LOG,           // 로그
  SCREEN_SETTINGS,      // 설정
  SCREEN_COUNT          // 전체 화면 개수 (UI 로직에 사용)
};

/**
 * @brief 시스템의 알람 수준을 정의하는 열거형 (부저, LED 제어에 사용)
 */
enum AlarmLevel : uint8_t {
  ALARM_NONE = 0,    // 정상
  ALARM_WARNING,     // 경고
  ALARM_ERROR        // 오류
};


#endif // DATA_TYPES_H
