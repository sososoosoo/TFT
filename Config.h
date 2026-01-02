#ifndef CONFIG_H
#define CONFIG_H

/**
 * @file Config.h
 * @brief 하드웨어 핀, 통신 주기, 모듈 ID 등 프로젝트 전반의 상수와 설정을 정의합니다.
 */

//==============================================================================
// 하드웨어 핀 설정
//==============================================================================
// TODO: 실제 보드 설계에 맞게 핀 번호를 최종 수정해야 합니다.

const int PIN_BUZZER     = 25;  // 부저
const int PIN_LED_RED    = 26;  // 상태 표시 LED (적색)
const int PIN_LED_GREEN  = 27;  // 상태 표시 LED (녹색)
const int PIN_LED_BLUE   = 14;  // 상태 표시 LED (청색)

const int PIN_ROTARY_A   = 32;  // 로터리 엔코더 A상
const int PIN_ROTARY_B   = 33;  // 로터리 엔코더 B상
const int PIN_ROTARY_SW  = 13;  // 로터리 엔코더 스위치

const int UART_TX_PIN    = 17;  // UART 통신 TX (ESP32 -> 외부 장치)
const int UART_RX_PIN    = 16;  // UART 통신 RX (ESP32 <- 외부 장치)

const int CAN_TX_PIN     = 5;   // CAN 통신 TX
const int CAN_RX_PIN     = 4;   // CAN 통신 RX


//==============================================================================
// 주기 및 타이밍 설정 (밀리초 단위)
//==============================================================================
const uint32_t PERIOD_CAN_COLLECT_MS  = 100;  // CAN 데이터 수집 주기
const uint32_t PERIOD_UART_TX_MS      = 200;  // UART 데이터 전송 주기
const uint32_t PERIOD_UI_UPDATE_MS    = 5000; // UI 화면 자동 갱신 주기
const uint32_t BOOT_READY_MS          = 3000; // 부팅 후 초기 동작 허용 시간
const uint32_t SERVER_TIMEOUT_MS      = 5000; // 서버로부터 응답이 없을 때 타임아웃으로 간주하는 시간


//==============================================================================
// 기타 설정
//==============================================================================
const int LOG_BUFFER_SIZE = 64; // 로그 메시지를 저장할 버퍼의 크기


//==============================================================================
// 모듈 식별자 (ID) 및 상태 정의
//==============================================================================
/**
 * @brief CAN 통신에 사용될 각 기능 모듈의 고유 ID
 */
enum ModuleId : uint8_t {
  MODULE_TANK      = 1, // 수조
  MODULE_GROW      = 2, // 재배기
  MODULE_NUTRIENT  = 3, // 양액기
  MODULE_FEEDER    = 4, // 급여기
};

/**
 * @brief 모듈의 현재 상태를 나타내는 열거형
 */
enum ModuleStatus : uint8_t {
  MODULE_OFFLINE = 0, // 연결되지 않음
  MODULE_OK      = 1, // 정상
  MODULE_WARN    = 2, // 경고
  MODULE_ERROR   = 3, // 오류
};


//==============================================================================
// 모듈별 제어 명령 코드 정의
//==============================================================================
/**
 * @brief 수조(Tank) 모듈 제어용 명령
 */
enum TankCommand : uint8_t {
  TANK_CMD_SET_PUMP  = 1,   // 펌프 제어 (파라미터: 0=OFF, 1=ON)
  TANK_CMD_SET_LIGHT = 2    // 조명 제어 (파라미터: 0=OFF, 1=ON)
};

/**
 * @brief 재배기(Grow) 모듈 제어용 명령
 */
enum GrowCommand : uint8_t {
  GROW_CMD_SET_LED_BRIGHTNESS = 1  // LED 밝기 조절 (파라미터: 0~100%)
};

/**
 * @brief 급여기(Feeder) 모듈 제어용 명령
 */
enum FeederCommand : uint8_t {
  FEEDER_CMD_FEED_ONCE = 1        // 1회 급여 (파라미터: 급여량 %)
};


#endif // CONFIG_H
