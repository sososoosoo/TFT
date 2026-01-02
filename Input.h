#ifndef INPUT_H
#define INPUT_H

/**
 * @file Input.h
 * @brief 로터리 엔코더 입력 처리에 관련된 함수 선언을 포함합니다.
 */

/**
 * @brief 로터리 엔코더의 회전과 버튼 상태를 읽고 내부 상태를 업데이트합니다.
 * 이 함수는 UI 태스크에서 주기적으로 호출되어야 합니다.
 */
void updateRotary();

/**
 * @brief 짧은 클릭 이벤트가 발생했는지 확인하고 상태를 초기화합니다.
 * @return true 짧은 클릭이 감지되었으면
 * @return false 감지되지 않았으면
 */
bool fetchShortClick();

/**
 * @brief 긴 클릭 이벤트가 발생했는지 확인하고 상태를 초기화합니다.
 * @return true 긴 클릭이 감지되었으면
 * @return false 감지되지 않았으면
 */
bool fetchLongClick();

/**
 * @brief (호환성을 위해) 짧은 클릭이 발생했는지 확인합니다. fetchShortClick()과 동일합니다.
 * @return true 짧은 클릭이 감지되었으면
 * @return false 감지되지 않았으면
 */
bool fetchButtonClicked();


#endif // INPUT_H
