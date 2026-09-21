#ifndef TMC2209_DRIVER_H
#define TMC2209_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Fixed and previously verified wiring:
 *   PB24 -> STEP, IOMUX_PINCM52
 *   PB25 -> DIR,  IOMUX_PINCM56
 *   PA16 -> ENN,  IOMUX_PINCM38 (active-low)
 *
 * This is a deliberately low-speed 1 ms scheduler version for Supercar.
 * It must not replace the system SysTick configuration.
 */
/* The 1 ms pulse scheduler cannot issue more than one rising edge per tick,
 * so command rate is finite. This higher bench limit prioritizes fast slope
 * withdrawal during visual balancing. */
#define TMC2209_MAX_SAFE_STEP_HZ 250

void tmc2209_init(void);
void tmc2209_tick_1ms(void);

void tmc2209_enable_hold(void);          /* ENN=0: hold position, no STEP. */
void tmc2209_release_and_invalidate(void); /* ENN=1: release; position becomes invalid. */
void tmc2209_stop_hold(void);            /* Stop STEP and retain holding torque. */

void tmc2209_set_direction_invert(bool invert);
void tmc2209_set_position_zero(void);
bool tmc2209_position_is_valid(void);
int32_t tmc2209_get_position_steps(void);
int32_t tmc2209_get_target_step_hz(void);

/* A command is accepted only while ENN is enabled and a confirmed zero exists. */
bool tmc2209_set_target_step_hz(int32_t signed_step_hz);

#endif /* TMC2209_DRIVER_H */
