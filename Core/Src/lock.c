

#include "main.h"
#include "lock.h"
#include "gpio_driver.h"

#define LED_YELLOW_PORT  GPIOD
#define LED_YELLOW_PIN   14u

#define LED_RED_PORT     GPIOD
#define LED_RED_PIN      15u

#define LED_GREEN_PORT   GPIOD
#define LED_GREEN_PIN    13u

#define SHORT_PRESS_THRESHOLD_MS    500U
#define ENTRY_TIMEOUT_MS           5000U
#define GREEN_ON_MS                3000U
#define LOCKOUT_BLINK_MS           5000U
#define BLINK_HALF_MS               100U
#define YELLOW_LONG_PULSE_MS       1000U
#define DEBOUNCE_MS                  20U

#define PRESS_SHORT  0u
#define PRESS_LONG   1u
#define CODE_LENGTH  8u
#define MAX_WRONG    3u

static const uint8_t SECRET_CODE[CODE_LENGTH] = {
    PRESS_SHORT, PRESS_LONG,  PRESS_SHORT, PRESS_SHORT,
    PRESS_LONG,  PRESS_LONG,  PRESS_SHORT, PRESS_LONG
};

static void led_on(GPIO_TypeDef *port, uint8_t pin)
{
    gpio_write_pin(port, pin, 1u);
}

static void led_off(GPIO_TypeDef *port, uint8_t pin)
{
    gpio_write_pin(port, pin, 0u);
}

static void led_blink_once(GPIO_TypeDef *port, uint8_t pin)
{
    led_on(port, pin);
    HAL_Delay(BLINK_HALF_MS);
    led_off(port, pin);
    HAL_Delay(BLINK_HALF_MS);
}

static void led_blink_timed(GPIO_TypeDef *port, uint8_t pin, uint32_t duration_ms)
{
    uint32_t end = HAL_GetTick() + duration_ms;
    while (HAL_GetTick() < end) {
        gpio_toggle_pins(port, GPIO_PIN(pin));
        HAL_Delay(BLINK_HALF_MS);
    }
    led_off(port, pin);
}

static void all_leds_off(void)
{
    /* все три светодиода на порту D — гасим одной записью */
    gpio_reset_pins(GPIOD, GPIO_PIN(LED_GREEN_PIN) | GPIO_PIN(LED_YELLOW_PIN) | GPIO_PIN(LED_RED_PIN));
}

#define BTN_PORT  GPIOC
#define BTN_PIN   15u

static uint8_t btn_is_pressed(void)
{
    return (gpio_read_pin(BTN_PORT, BTN_PIN) == 0u) ? 1u : 0u;
}

static uint32_t measure_hold(void)
{
    uint32_t start = HAL_GetTick();
    while (btn_is_pressed()) {}
    return HAL_GetTick() - start;
}

static void reset_entry(uint8_t *pos, uint8_t *started)
{
    *pos     = 0u;
    *started = 0u;
}

void CodeLock_Run(void)
{
    uint8_t  pos         = 0u;
    uint8_t  wrong_count = 0u;
    uint8_t  started     = 0u;
    uint32_t last_tick   = 0u;

    all_leds_off();

    while (1)
    {
        if (started && (HAL_GetTick() - last_tick > ENTRY_TIMEOUT_MS))
        {
            led_on(LED_YELLOW_PORT, LED_YELLOW_PIN);
            HAL_Delay(YELLOW_LONG_PULSE_MS);
            led_off(LED_YELLOW_PORT, LED_YELLOW_PIN);
            reset_entry(&pos, &started);
            continue;
        }

        if (!btn_is_pressed())
        {
            HAL_Delay(5u);
            continue;
        }

        HAL_Delay(DEBOUNCE_MS);
        if (!btn_is_pressed()) continue;

        uint32_t held = measure_hold();
        HAL_Delay(DEBOUNCE_MS);

        uint8_t input = (held >= SHORT_PRESS_THRESHOLD_MS)
                        ? PRESS_LONG : PRESS_SHORT;

        started   = 1u;
        last_tick = HAL_GetTick();
        if (input == SECRET_CODE[pos])
        {
            pos++;
            led_blink_once(LED_YELLOW_PORT, LED_YELLOW_PIN);

            if (pos == CODE_LENGTH)
            {
                led_on(LED_GREEN_PORT, LED_GREEN_PIN);
                HAL_Delay(GREEN_ON_MS);
                led_off(LED_GREEN_PORT, LED_GREEN_PIN);
                wrong_count = 0u;
                reset_entry(&pos, &started);
            }
        }
        else
        {
            led_blink_once(LED_RED_PORT, LED_RED_PIN);
            reset_entry(&pos, &started);
            wrong_count++;

            if (wrong_count >= MAX_WRONG)
            {
                led_blink_timed(LED_RED_PORT, LED_RED_PIN, LOCKOUT_BLINK_MS);
                wrong_count = 0u;
            }
        }
    }
}
