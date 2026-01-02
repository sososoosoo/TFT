#include "Globals.h"
#include "Input.h"

/**
 * @file Input.cpp
 * @brief 로터리 엔코더 입력 처리 함수의 실제 구현을 포함합니다.
 */

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
