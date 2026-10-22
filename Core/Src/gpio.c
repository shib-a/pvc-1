/**
  ******************************************************************************
  * File Name          : gpio.c
  * Description        : This file provides code for the configuration
  *                      of all used GPIO pins.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"
/* USER CODE BEGIN 0 */
#include "gpio_driver.h"
/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

}

/* USER CODE BEGIN 2 */
/*
 * Выводы стенда, настраиваемые собственным драйвером (не HAL/LL):
 *   PD13, PD14, PD15 — светодиоды, выход push-pull, исходно погашены;
 *   PC15             — боковая кнопка, вход с подтяжкой вверх (нажата = 0);
 *   PA8              — MCO1 (выход тактовой частоты HSI), AF0.
 * В графическом конфигураторе (SDK_LED.ioc) эти выводы не назначены.
 */
void Board_GPIO_Init(void)
{
  const gpio_config_t led = {
    .mode  = PIN_MODE_OUTPUT,
    .otype = PIN_OTYPE_PUSHPULL,
    .speed = PIN_SPEED_HIGH,
    .pull  = PIN_PULL_NONE,
    .af    = 0,
    .level = PIN_LEVEL_LOW,      /* уровень задаётся до включения выхода */
  };
  const gpio_config_t button = {
    .mode  = PIN_MODE_INPUT,
    .pull  = PIN_PULL_UP,
  };
  const gpio_config_t mco = {
    .mode  = PIN_MODE_AF,
    .otype = PIN_OTYPE_PUSHPULL,
    .speed = PIN_SPEED_VERY_HIGH,
    .pull  = PIN_PULL_NONE,
    .af    = 0,                  /* AF0 = MCO1 */
  };

  if (gpio_init(GPIOD, GPIO_PIN(13) | GPIO_PIN(14) | GPIO_PIN(15), &led) != GPIO_OK ||
      gpio_init(GPIOC, GPIO_PIN(15), &button) != GPIO_OK)
  {
    Error_Handler();
  }

  /* MCO1: источник HSI, без деления (замена HAL_RCC_MCOConfig, который
     настраивал PA8 через HAL_GPIO_Init) */
  RCC->CFGR &= ~(RCC_CFGR_MCO1 | RCC_CFGR_MCO1PRE);
  if (gpio_init(GPIOA, GPIO_PIN(8), &mco) != GPIO_OK)
  {
    Error_Handler();
  }
}
/* USER CODE END 2 */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
