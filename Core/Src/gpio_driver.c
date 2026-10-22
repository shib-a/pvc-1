/*
 * gpio_driver.c — собственный драйвер GPIO для STM32F4 (без HAL и LL).
 *
 * Работа ведётся напрямую с регистрами порта:
 *   MODER   — режим (2 бита на вывод)
 *   OTYPER  — тип выхода (1 бит)
 *   OSPEEDR — скорость (2 бита)
 *   PUPDR   — подтяжка (2 бита)
 *   AFR[2]  — номер альтернативной функции (4 бита)
 *   IDR/ODR/BSRR/LCKR
 *
 * Все выводы маски настраиваются одной записью в каждый регистр, поэтому
 * они переключаются одновременно, а не по одному, как в HAL_GPIO_Init.
 */
#include "gpio_driver.h"

#if defined(GPIOI)
#define GPIO_PORT_COUNT  9u      /* GPIOA..GPIOI */
#else
#define GPIO_PORT_COUNT  8u
#endif

/* Индекс порта (A=0, B=1, ...) или -1, если адрес не является портом GPIO. */
static int port_index(const GPIO_TypeDef *port)
{
  uint32_t addr = (uint32_t)port;
  if (addr < GPIOA_BASE) return -1;
  uint32_t offs = addr - GPIOA_BASE;
  if ((offs % 0x400u) != 0u || (offs / 0x400u) >= GPIO_PORT_COUNT) return -1;
  return (int)(offs / 0x400u);
}

/* Включает тактирование порта на шине AHB1. */
static void port_clock_enable(int idx)
{
  uint32_t bit = RCC_AHB1ENR_GPIOAEN << idx;
  if ((RCC->AHB1ENR & bit) == 0u) {
    RCC->AHB1ENR |= bit;
    /* Обратное чтение — задержка после включения тактирования (errata ES0182 2.1.13) */
    (void)RCC->AHB1ENR;
  }
}

static uint8_t port_clock_enabled(int idx)
{
  return (RCC->AHB1ENR & (RCC_AHB1ENR_GPIOAEN << idx)) ? 1u : 0u;
}

/* Растягивает 16-битную маску в 32-битную: бит n -> биты 2n и 2n+1 (поля MODER и т.п.). */
static uint32_t spread2(uint16_t pins)
{
  uint32_t r = 0u;
  for (uint32_t n = 0u; n < 16u; n++) {
    if (pins & (1u << n)) r |= 3u << (2u * n);
  }
  return r;
}

/* Заполняет 2-битные поля выводов из маски значением v (v = 0..3). */
static uint32_t field2(uint16_t pins, uint32_t v)
{
  return spread2(pins) & (v * 0x55555555u);
}

/* Выводы из mask, находящиеся в режиме AF с номером функции, отличным от af. */
static uint16_t pins_with_other_af(const GPIO_TypeDef *port, uint16_t mask, uint32_t af)
{
  uint16_t r = 0u;
  for (uint32_t n = 0u; n < 16u; n++) {
    if (!(mask & (1u << n))) continue;
    if (((port->MODER >> (2u * n)) & 3u) != PIN_MODE_AF) continue;
    if (((port->AFR[n >> 3] >> (4u * (n & 7u))) & 0xFu) != af) r |= (uint16_t)(1u << n);
  }
  return r;
}

/* Записывает номер AF для выводов маски (без промежуточных значений в регистре). */
static void write_afr(GPIO_TypeDef *port, uint16_t pins, uint32_t af)
{
  for (uint32_t half = 0u; half < 2u; half++) {
    uint32_t clr = 0u, set = 0u;
    for (uint32_t n = 0u; n < 8u; n++) {
      if (pins & (1u << (n + 8u * half))) {
        clr |= 0xFu << (4u * n);
        set |= af   << (4u * n);
      }
    }
    if (clr) port->AFR[half] = (port->AFR[half] & ~clr) | set;
  }
}

static uint8_t pins_locked(const GPIO_TypeDef *port, uint16_t pins)
{
  uint32_t lckr = port->LCKR;
  return ((lckr & GPIO_LCKR_LCKK) && (lckr & pins)) ? 1u : 0u;
}

/* Чтение-модификация-запись регистров должна быть неделимой: обработчик
   прерывания, меняющий тот же порт, не должен вклиниться между чтением и записью. */
static inline uint32_t irq_save(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static inline void irq_restore(uint32_t primask)
{
  __set_PRIMASK(primask);
}

gpio_status_t gpio_init(GPIO_TypeDef *port, uint16_t pins, const gpio_config_t *cfg)
{
  int idx = port_index(port);
  if (idx < 0 || pins == 0u || cfg == 0) return GPIO_ERR_PARAM;
  if ((uint32_t)cfg->mode > PIN_MODE_ANALOG || (uint32_t)cfg->otype > PIN_OTYPE_OPENDRAIN ||
      (uint32_t)cfg->speed > PIN_SPEED_VERY_HIGH || (uint32_t)cfg->pull > PIN_PULL_DOWN ||
      (uint32_t)cfg->level > PIN_LEVEL_KEEP || cfg->af > 15u)
    return GPIO_ERR_PARAM;

  port_clock_enable(idx);
  if (pins_locked(port, pins)) return GPIO_ERR_LOCKED;

  const uint32_t mode    = (uint32_t)cfg->mode;
  const uint32_t pull    = (mode == PIN_MODE_ANALOG) ? PIN_PULL_NONE : (uint32_t)cfg->pull;
  const uint8_t  driving = (mode == PIN_MODE_OUTPUT || mode == PIN_MODE_AF);
  const uint32_t mask2   = spread2(pins);

  /* Значение для BSRR: задаёт уровень ODR, только для режима OUTPUT */
  uint32_t bsrr = 0u;
  if (mode == PIN_MODE_OUTPUT) {
    if (cfg->level == PIN_LEVEL_HIGH)     bsrr = pins;
    else if (cfg->level == PIN_LEVEL_LOW) bsrr = (uint32_t)pins << 16;
  }

  uint32_t primask = irq_save();

  /* 1. Смена номера AF у вывода, который уже работает в AF. Переключение AFR
        «на ходу» мгновенно подменило бы источник сигнала, поэтому вывод сначала
        «паркуется» как обычный выход, удерживающий текущий уровень на ножке
        (тип выхода не меняется), и лишь затем меняется AFR. */
  if (mode == PIN_MODE_AF) {
    uint16_t park = pins_with_other_af(port, pins, cfg->af);
    if (park) {
      uint16_t lvl = (uint16_t)(port->IDR & park);
      port->BSRR = ((uint32_t)(uint16_t)(park & ~lvl) << 16) | lvl;
      port->MODER = (port->MODER & ~spread2(park)) | field2(park, PIN_MODE_OUTPUT);
    }
  }

  /* 2. Параметры, не влияющие на уровень, который выдаёт выход. PUPDR пишется
        до MODER, чтобы вход сразу получил нужную подтяжку и не «висел в воздухе». */
  if (driving) {
    port->OSPEEDR = (port->OSPEEDR & ~mask2) | field2(pins, (uint32_t)cfg->speed);
  }
  port->PUPDR = (port->PUPDR & ~mask2) | field2(pins, pull);
  if (mode == PIN_MODE_AF) {
    write_afr(port, pins, cfg->af);
  }

  /* 3. Тип выхода, уровень и режим. Порядок выбран так, чтобы в промежуточном
        состоянии вывод никогда не выдавал активно уровень, которого нет ни в
        старой, ни в новой конфигурации. Допустимое промежуточное состояние —
        только «более слабое» (открытый сток с 1 = высокий импеданс).

        Push-pull:  ODR -> MODER -> OTYPER
          Пока OTYPER ещё старый, вывод выдаёт новый уровень (или Z, если был OD).
          Обратный порядок (OTYPER раньше ODR) при переходе OD/1 (Z) -> PP/0
          дал бы короткий импульс активной 1.
        Open-drain, уровень LOW:  ODR -> OTYPER -> MODER
          «0» одинаково выдаётся и в PP, и в OD, поэтому сначала пишем уровень.
        Open-drain, уровень HIGH/KEEP (и AF):  OTYPER -> ODR -> MODER
          Сначала отключаем верхний ключ, иначе переход PP/0 -> OD/1 дал бы
          импульс активной 1 вместо высокого импеданса. */
  uint32_t moder_new = (port->MODER & ~mask2) | field2(pins, mode);

  if (!driving) {
    port->MODER = moder_new;                       /* вход / аналог */
  } else if (cfg->otype == PIN_OTYPE_PUSHPULL) {
    if (bsrr) port->BSRR = bsrr;
    port->MODER = moder_new;
    port->OTYPER &= ~(uint32_t)pins;
  } else if (mode == PIN_MODE_OUTPUT && cfg->level == PIN_LEVEL_LOW) {
    port->BSRR = bsrr;
    port->OTYPER |= pins;
    port->MODER = moder_new;
  } else {
    port->OTYPER |= pins;
    if (bsrr) port->BSRR = bsrr;
    port->MODER = moder_new;
  }

  irq_restore(primask);
  return GPIO_OK;
}

gpio_status_t gpio_deinit(GPIO_TypeDef *port, uint16_t pins)
{
  int idx = port_index(port);
  if (idx < 0 || pins == 0u) return GPIO_ERR_PARAM;
  if (!port_clock_enabled(idx)) return GPIO_ERR_NO_CLOCK;
  if (pins_locked(port, pins)) return GPIO_ERR_LOCKED;

  const uint32_t mask2 = spread2(pins);
  uint32_t primask = irq_save();

  /* Сначала перестаём управлять ножкой (вход), затем сбрасываем остальное */
  port->MODER   &= ~mask2;
  port->PUPDR   &= ~mask2;
  port->OTYPER  &= ~(uint32_t)pins;
  port->OSPEEDR &= ~mask2;
  write_afr(port, pins, 0u);

  irq_restore(primask);
  return GPIO_OK;
}

gpio_status_t gpio_get_config(GPIO_TypeDef *port, uint8_t pin, gpio_config_t *cfg)
{
  int idx = port_index(port);
  if (idx < 0 || pin > 15u || cfg == 0) return GPIO_ERR_PARAM;
  if (!port_clock_enabled(idx)) return GPIO_ERR_NO_CLOCK;

  uint32_t s2 = 2u * pin;
  cfg->mode  = (gpio_mode_t)((port->MODER >> s2) & 3u);
  cfg->otype = (gpio_otype_t)((port->OTYPER >> pin) & 1u);
  cfg->speed = (gpio_speed_t)((port->OSPEEDR >> s2) & 3u);
  cfg->pull  = (gpio_pull_t)((port->PUPDR >> s2) & 3u);
  cfg->af    = (uint8_t)((port->AFR[pin >> 3] >> (4u * (pin & 7u))) & 0xFu);
  cfg->level = (gpio_level_t)((port->ODR >> pin) & 1u);
  return GPIO_OK;
}
