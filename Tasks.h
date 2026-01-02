#ifndef TASKS_H
#define TASKS_H

/**
 * @file Tasks.h
 * @brief FreeRTOS 태스크 함수들의 선언을 포함합니다.
 * 
 * 이 함수들의 실제 구현은 Tasks.cpp 파일에 있습니다.
 */

/**
 * @brief CAN 통신(수신, 송신)을 처리하는 태스크
 */
void taskCan(void *pvParameters);

/**
 * @brief UART 통신(서버와 데이터 교환)을 처리하는 태스크
 */
void taskUart(void *pvParameters);

/**
 * @brief 사용자 입력(로터리 엔코더)을 처리하고 UI를 갱신하는 태스크
 */
void taskUi(void *pvParameters);

/**
 * @brief 시스템의 주요 로직(상태 점검, Fail-safe 등)을 처리하는 태스크
 */
void taskLogic(void *pvParameters);

/**
 * @brief 경고/오류 상태에 따라 부저를 울리는 태스크
 */
void taskAlarm(void *pvParameters);


#endif // TASKS_H
