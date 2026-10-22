/*
 * gpio_driver.h — собственный драйвер GPIO для STM32F4 (без HAL и LL).
 *
 * Из CMSIS используются только структура регистров GPIO_TypeDef, адреса
 * портов (GPIOA..GPIOI), RCC и битовые define-определения регистров.
 *
 * Префикс PIN_ у констант выбран, чтобы не пересекаться с макросами HAL
 * (GPIO_MODE_INPUT, GPIO_PULLUP, GPIO_SPEED_FREQ_LOW и т.п.), которые
 * остаются видимыми через main.h -> stm32f4xx_hal.h.
 */
#ifndef GPIO_DRIVER_H
#define GPIO_DRIVER_H

#include <stdint.h>
#include "stm32f4xx.h"

/* Маски выводов: GPIO_PIN(5), GPIO_PIN(12) | GPIO_PIN(13), GPIO_PIN_MASK_ALL */
#define GPIO_PIN(n)         ((uint16_t)(1u << (n)))
#define GPIO_PIN_MASK_ALL   ((uint16_t)0xFFFFu)

typedef enum {
  PIN_MODE_INPUT  = 0,   /* значения совпадают с кодами поля MODER */
  PIN_MODE_OUTPUT = 1,
  PIN_MODE_AF     = 2,
  PIN_MODE_ANALOG = 3
} gpio_mode_t;

typedef enum {
  PIN_OTYPE_PUSHPULL  = 0,
  PIN_OTYPE_OPENDRAIN = 1
} gpio_otype_t;

typedef enum {
  PIN_SPEED_LOW       = 0,
  PIN_SPEED_MEDIUM    = 1,
  PIN_SPEED_HIGH      = 2,
  PIN_SPEED_VERY_HIGH = 3
} gpio_speed_t;

typedef enum {
  PIN_PULL_NONE = 0,
  PIN_PULL_UP   = 1,
  PIN_PULL_DOWN = 2
} gpio_pull_t;

/* Начальный уровень выхода при инициализации в режиме OUTPUT */
typedef enum {
  PIN_LEVEL_LOW  = 0,
  PIN_LEVEL_HIGH = 1,
  PIN_LEVEL_KEEP = 2    /* не трогать ODR (оставить текущее значение) */
} gpio_level_t;

typedef struct {
  gpio_mode_t  mode;
  gpio_otype_t otype;   /* для OUTPUT и AF */
  gpio_speed_t speed;   /* для OUTPUT и AF */
  gpio_pull_t  pull;    /* для ANALOG принудительно NONE */
  uint8_t      af;      /* 0..15, используется только в режиме AF */
  gpio_level_t level;   /* при init: начальный уровень (только OUTPUT);
                           при get_config: текущее значение бита ODR */
} gpio_config_t;

typedef enum {
  GPIO_OK = 0,
  GPIO_ERR_PARAM,     /* неверный порт, пустая маска или параметр вне диапазона */
  GPIO_ERR_LOCKED,    /* конфигурация вывода заблокирована через LCKR */
  GPIO_ERR_NO_CLOCK   /* тактирование порта выключено (порт не инициализирован) */
} gpio_status_t;

/* ---------- Инициализация и настройки ---------- */

/*
 * Настраивает все выводы из маски pins одинаково и включает тактирование
 * порта. Можно вызывать повторно для смены режима: регистры записываются
 * в таком порядке, что выход ни на мгновение не принимает уровень, которого
 * нет ни в старой, ни в новой конфигурации (см. gpio_driver.c).
 */
gpio_status_t gpio_init(GPIO_TypeDef *port, uint16_t pins, const gpio_config_t *cfg);

/* Возвращает выводы к состоянию после сброса: вход без подтяжки, PP, LOW, AF0. */
gpio_status_t gpio_deinit(GPIO_TypeDef *port, uint16_t pins);

/* Читает текущие настройки одного вывода (pin = 0..15). */
gpio_status_t gpio_get_config(GPIO_TypeDef *port, uint8_t pin, gpio_config_t *cfg);

/* ---------- Чтение ---------- */

/* Уровень на входе одного вывода: 0 или 1 */
static inline uint8_t gpio_read_pin(GPIO_TypeDef *port, uint8_t pin) {
  return (uint8_t)((port->IDR >> (pin & 15u)) & 1u);
}

/* Входные уровни нескольких выводов одновременно (одно чтение IDR) */
static inline uint16_t gpio_read_pins(GPIO_TypeDef *port, uint16_t mask) {
  return (uint16_t)(port->IDR & mask);
}

/* Значение, записанное в выходной регистр (ODR), для выводов из маски */
static inline uint16_t gpio_read_output(GPIO_TypeDef *port, uint16_t mask) {
  return (uint16_t)(port->ODR & mask);
}

/* ---------- Запись (атомарно через BSRR) ---------- */

static inline void gpio_write_pin(GPIO_TypeDef *port, uint8_t pin, uint8_t value) {
  pin &= 15u;
  port->BSRR = value ? (1u << pin) : (1u << (pin + 16u));
}

/*
 * Одновременная запись нескольких выводов одного порта:
 * выводы из mask получают соответствующие биты value, остальные не меняются.
 * Одна запись в BSRR => все выводы переключаются в одном такте.
 */
static inline void gpio_write_pins(GPIO_TypeDef *port, uint16_t mask, uint16_t value) {
  port->BSRR = ((uint32_t)(uint16_t)(~value & mask) << 16) | (uint32_t)(value & mask);
}

static inline void gpio_set_pins(GPIO_TypeDef *port, uint16_t mask) {
  port->BSRR = mask;
}

static inline void gpio_reset_pins(GPIO_TypeDef *port, uint16_t mask) {
  port->BSRR = (uint32_t)mask << 16;
}

/* Инверсия выводов из mask; остальные выводы порта не затрагиваются. */
static inline void gpio_toggle_pins(GPIO_TypeDef *port, uint16_t mask) {
  uint32_t odr = port->ODR;
  port->BSRR = ((odr & mask) << 16) | (~odr & mask);
}

#endif /* GPIO_DRIVER_H */
