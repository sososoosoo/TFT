#ifndef UI_H
#define UI_H

/**
 * @file UI.h
 * @brief UI 관련 함수(화면 그리기, 클릭 핸들러)들의 선언을 포함합니다.
 * 
 * 이 함수들의 실제 구현은 UI.cpp 파일에 있습니다.
 */

//==============================================================================
// UI 화면 그리기 함수
//==============================================================================

/**
 * @brief g_currentScreen 값에 따라 현재 화면을 다시 그립니다.
 */
void drawCurrentScreen();

/**
 * @brief 대시보드 화면을 그립니다.
 */
void drawDashboard();

/**
 * @brief 수조(Tank) 상세 화면을 그립니다.
 */
void drawTankScreen();

/**
 * @brief 재배기(Grow) 상세 화면을 그립니다.
 */
void drawGrowScreen();

/**
 * @brief 양액기(Nutrient) 상세 화면을 그립니다.
 */
void drawNutrientScreen();

/**
 * @brief 급여기(Feeder) 상세 화면을 그립니다.
 */
void drawFeederScreen();

/**
 * @brief 로그(Log) 화면을 그립니다.
 */
void drawLogScreen();

/**
 * @brief 설정(Settings) 화면을 그립니다.
 */
void drawSettingsScreen();


//==============================================================================
// UI 화면별 클릭 이벤트 처리 함수
//==============================================================================

/**
 * @brief 수조 화면에서 버튼 클릭 이벤트를 처리합니다.
 * @param shortClick 짧은 클릭이면 true
 * @param longClick 긴 클릭이면 true
 */
void handleTankClick(bool shortClick, bool longClick);

/**
 * @brief 재배기 화면에서 버튼 클릭 이벤트를 처리합니다.
 * @param shortClick 짧은 클릭이면 true
 * @param longClick 긴 클릭이면 true
 */
void handleGrowClick(bool shortClick, bool longClick);

/**
 * @brief 설정 화면에서 버튼 클릭 이벤트를 처리합니다.
 * @param shortClick 짧은 클릭이면 true
 * @param longClick 긴 클릭이면 true
 */
void handleSettingsClick(bool shortClick, bool longClick);

/**
 * @brief 로그 화면에서 버튼 클릭 이벤트를 처리합니다.
 * @param shortClick 짧은 클릭이면 true
 * @param longClick 긴 클릭이면 true
 */
void handleLogClick(bool shortClick, bool longClick);


#endif // UI_H
