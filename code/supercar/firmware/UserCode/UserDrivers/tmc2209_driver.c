#include "tmc2209_driver.h"
#include "ti_msp_dl_config.h"

#define TMC_STEP_PORT  GPIOB
#define TMC_STEP_PIN   DL_GPIO_PIN_24
#define TMC_STEP_IOMUX IOMUX_PINCM52
#define TMC_DIR_PORT   GPIOB
#define TMC_DIR_PIN    DL_GPIO_PIN_25
#define TMC_DIR_IOMUX  IOMUX_PINCM56
#define TMC_ENN_PORT   GPIOA
#define TMC_ENN_PIN    DL_GPIO_PIN_16
#define TMC_ENN_IOMUX  IOMUX_PINCM38

static volatile int32_t s_target_step_hz;
static volatile int32_t s_position_steps;
static volatile uint16_t s_period_ms;
static volatile uint16_t s_elapsed_ms;
static volatile uint8_t s_step_high;
static volatile uint8_t s_position_valid;
static volatile uint8_t s_hold_enabled;
static bool s_direction_invert;

static int32_t clamp_step_hz(int32_t step_hz)
{
    if (step_hz > TMC2209_MAX_SAFE_STEP_HZ) return TMC2209_MAX_SAFE_STEP_HZ;
    if (step_hz < -TMC2209_MAX_SAFE_STEP_HZ) return -TMC2209_MAX_SAFE_STEP_HZ;
    return step_hz;
}

static void output_init_low(GPIO_Regs *port, uint32_t pin, uint32_t iomux)
{
    DL_GPIO_initDigitalOutput(iomux);
    DL_GPIO_clearPins(port, pin);
    DL_GPIO_enableOutput(port, pin);
}

static void enn_output_init_high(void)
{
    /* ENN is active-low: latch high before the output is enabled. */
    DL_GPIO_initDigitalOutput(TMC_ENN_IOMUX);
    DL_GPIO_setPins(TMC_ENN_PORT, TMC_ENN_PIN);
    DL_GPIO_enableOutput(TMC_ENN_PORT, TMC_ENN_PIN);
}

static void stop_step_output(void)
{
    s_target_step_hz = 0;
    s_period_ms = 0U;
    s_elapsed_ms = 0U;
    s_step_high = 0U;
    DL_GPIO_clearPins(TMC_STEP_PORT, TMC_STEP_PIN);
}

void tmc2209_init(void)
{
    /* Safe order inherited from the validated test project. */
    enn_output_init_high();
    output_init_low(TMC_STEP_PORT, TMC_STEP_PIN, TMC_STEP_IOMUX);
    output_init_low(TMC_DIR_PORT, TMC_DIR_PIN, TMC_DIR_IOMUX);

    s_target_step_hz = 0;
    s_position_steps = 0;
    s_period_ms = 0U;
    s_elapsed_ms = 0U;
    s_step_high = 0U;
    s_position_valid = 0U;
    s_hold_enabled = 0U;
    s_direction_invert = false;
    DL_GPIO_setPins(TMC_ENN_PORT, TMC_ENN_PIN);
}

void tmc2209_enable_hold(void)
{
    stop_step_output();
    DL_GPIO_clearPins(TMC_ENN_PORT, TMC_ENN_PIN);
    s_hold_enabled = 1U;
}

void tmc2209_release_and_invalidate(void)
{
    stop_step_output();
    DL_GPIO_setPins(TMC_ENN_PORT, TMC_ENN_PIN);
    s_hold_enabled = 0U;
    s_position_valid = 0U;
}

void tmc2209_stop_hold(void)
{
    stop_step_output();
    if (s_hold_enabled != 0U) {
        DL_GPIO_clearPins(TMC_ENN_PORT, TMC_ENN_PIN);
    }
}

void tmc2209_set_direction_invert(bool invert)
{
    s_direction_invert = invert;
}

void tmc2209_set_position_zero(void)
{
    stop_step_output();
    s_position_steps = 0;
    s_position_valid = 1U;
}

bool tmc2209_position_is_valid(void)
{
    return (s_position_valid != 0U);
}

int32_t tmc2209_get_position_steps(void)
{
    return s_position_steps;
}

int32_t tmc2209_get_target_step_hz(void)
{
    return s_target_step_hz;
}

bool tmc2209_set_target_step_hz(int32_t signed_step_hz)
{
    int32_t safe_step_hz;

    if ((s_hold_enabled == 0U) || (s_position_valid == 0U)) {
        stop_step_output();
        return false;
    }

    safe_step_hz = clamp_step_hz(signed_step_hz);
    if (safe_step_hz == 0) {
        stop_step_output();
        return true;
    }

    /* The visual position follower refreshes this request every 10 ms. Do not
     * restart the timing phase for an unchanged command, otherwise the low-rate
     * scheduler could never accumulate enough time to emit a STEP pulse. */
    if (safe_step_hz == s_target_step_hz) {
        return true;
    }

    /* A reversal must never alter DIR while an existing STEP high pulse is
     * active. Keep the old command for this one 1 ms pulse; the next 10 ms
     * visual update applies the requested direction after STEP is low. */
    if (s_step_high != 0U) {
        return true;
    }

    if (safe_step_hz > 0) {
        if (s_direction_invert) DL_GPIO_clearPins(TMC_DIR_PORT, TMC_DIR_PIN);
        else DL_GPIO_setPins(TMC_DIR_PORT, TMC_DIR_PIN);
    } else {
        if (s_direction_invert) DL_GPIO_setPins(TMC_DIR_PORT, TMC_DIR_PIN);
        else DL_GPIO_clearPins(TMC_DIR_PORT, TMC_DIR_PIN);
    }

    s_target_step_hz = safe_step_hz;
    s_period_ms = (uint16_t)(1000U / (uint32_t)((safe_step_hz < 0) ? -safe_step_hz : safe_step_hz));
    if (s_period_ms == 0U) s_period_ms = 1U;
    s_elapsed_ms = 0U;
    return true;
}

void tmc2209_tick_1ms(void)
{
    int32_t step_hz = s_target_step_hz;

    /* Keep every high phase one full millisecond; falling edge is never counted. */
    if (s_step_high != 0U) {
        DL_GPIO_clearPins(TMC_STEP_PORT, TMC_STEP_PIN);
        s_step_high = 0U;
        return;
    }

    if (step_hz == 0) return;
    if ((s_hold_enabled == 0U) || (s_position_valid == 0U)) {
        stop_step_output();
        return;
    }

    s_elapsed_ms++;
    if (s_elapsed_ms < s_period_ms) return;
    s_elapsed_ms = 0U;

    /* Position changes only on this actual STEP rising edge. */
    DL_GPIO_setPins(TMC_STEP_PORT, TMC_STEP_PIN);
    s_step_high = 1U;
    if (step_hz > 0) s_position_steps++;
    else s_position_steps--;
}
