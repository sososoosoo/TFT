#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <Arduino.h>

// twai.h는 C 라이브러리이므로 extern "C"로 감싸야 합니다.
extern "C" {
  #include "driver/twai.h"
}

/**
 * @file Communication.h
 * @brief CAN, UART 통신 관련 함수의 선언을 포함합니다.
 */


//==============================================================================
// CAN 버스 관련 함수
//==============================================================================

/**
 * @brief CAN 버스로 전송할 명령을 생성하여 큐에 추가합니다.
 * @param moduleId 대상 모듈 ID
 * @param cmd 명령 코드
 * @param param 파라미터
 */
void enqueueCanCommand(uint8_t moduleId, uint8_t cmd, int32_t param);

/**
 * @brief CAN 버스에서 수신된 메시지를 처리합니다.
 * @param msg 수신된 a twai_message_t 메시지
 */
void handleCanFrame(const twai_message_t &msg);


//==============================================================================
// 모듈 제어 요청 헬퍼 함수 (CAN 명령 전송)
//==============================================================================

void requestTankPump(bool on);
void requestTankLight(bool on);
void requestGrowLedBrightness(uint8_t brightness);
void requestFeederOnce(uint8_t amountPercent);


//==============================================================================
// UART (서버) 통신 관련 함수
//==============================================================================

/**
 * @brief 현재 시스템 상태를 JSON 형식의 문자열로 만들어 반환합니다.
 * @return String JSON 형식의 상태 정보
 */
String buildStatusJson();

/**
 * @brief 서버로부터 수신된 한 줄의 명령 문자열을 파싱합니다.
 * @param line 수신된 문자열
 */
void parseServerLine(const String &line);

/**
 * @brief 파싱된 서버 명령을 실제 CAN 명령으로 변환하여 전송 큐에 넣습니다.
 * @param cmd 파싱된 ServerCommand 구조체
 */
void handleServerCommand(const struct ServerCommand &cmd);


#endif // COMMUNICATION_H
