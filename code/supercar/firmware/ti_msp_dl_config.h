/*
 * Copyright (c) 2023, Texas Instruments Incorporated - http://www.ti.com
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ============ ti_msp_dl_config.h =============
 *  Configured MSPM0 DriverLib module declarations
 *
 *  DO NOT EDIT - This file is generated for the MSPM0G350X
 *  by the SysConfig tool.
 */
#ifndef ti_msp_dl_config_h
#define ti_msp_dl_config_h

#define CONFIG_MSPM0G350X
#define CONFIG_MSPM0G3507

#if defined(__ti_version__) || defined(__TI_COMPILER_VERSION__)
#define SYSCONFIG_WEAK __attribute__((weak))
#elif defined(__IAR_SYSTEMS_ICC__)
#define SYSCONFIG_WEAK __weak
#elif defined(__GNUC__)
#define SYSCONFIG_WEAK __attribute__((weak))
#endif

#include <ti/devices/msp/msp.h>
#include <ti/driverlib/driverlib.h>
#include <ti/driverlib/m0p/dl_core.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  ======== SYSCFG_DL_init ========
 *  Perform all required MSP DL initialization
 *
 *  This function should be called once at a point before any use of
 *  MSP DL.
 */


/* clang-format off */

#define POWER_STARTUP_DELAY                                                (16)


#define GPIO_HFXT_PORT                                                     GPIOA
#define GPIO_HFXIN_PIN                                             DL_GPIO_PIN_5
#define GPIO_HFXIN_IOMUX                                         (IOMUX_PINCM10)
#define GPIO_HFXOUT_PIN                                            DL_GPIO_PIN_6
#define GPIO_HFXOUT_IOMUX                                        (IOMUX_PINCM11)
#define CPUCLK_FREQ                                                     80000000
/* Defines for SYSPLL_ERR_01 Workaround */
/* Represent 1.000 as 1000 */
#define FLOAT_TO_INT_SCALE                                               (1000U)
#define FCC_EXPECTED_RATIO                                                  2000
#define FCC_UPPER_BOUND                       (FCC_EXPECTED_RATIO * (1 + 0.003))
#define FCC_LOWER_BOUND                       (FCC_EXPECTED_RATIO * (1 - 0.003))

bool SYSCFG_DL_SYSCTL_SYSPLL_init(void);


/* Defines for PWM_0 */
#define PWM_0_INST                                                         TIMG7
#define PWM_0_INST_IRQHandler                                   TIMG7_IRQHandler
#define PWM_0_INST_INT_IRQN                                     (TIMG7_INT_IRQn)
#define PWM_0_INST_CLK_FREQ                                             10000000
/* GPIO defines for channel 0 */
#define GPIO_PWM_0_C0_PORT                                                 GPIOA
#define GPIO_PWM_0_C0_PIN                                         DL_GPIO_PIN_26
#define GPIO_PWM_0_C0_IOMUX                                      (IOMUX_PINCM59)
#define GPIO_PWM_0_C0_IOMUX_FUNC                     IOMUX_PINCM59_PF_TIMG7_CCP0
#define GPIO_PWM_0_C0_IDX                                    DL_TIMER_CC_0_INDEX
/* GPIO defines for channel 1 */
#define GPIO_PWM_0_C1_PORT                                                 GPIOA
#define GPIO_PWM_0_C1_PIN                                         DL_GPIO_PIN_27
#define GPIO_PWM_0_C1_IOMUX                                      (IOMUX_PINCM60)
#define GPIO_PWM_0_C1_IOMUX_FUNC                     IOMUX_PINCM60_PF_TIMG7_CCP1
#define GPIO_PWM_0_C1_IDX                                    DL_TIMER_CC_1_INDEX

/* Defines for PWM_1 */
#define PWM_1_INST                                                         TIMA1
#define PWM_1_INST_IRQHandler                                   TIMA1_IRQHandler
#define PWM_1_INST_INT_IRQN                                     (TIMA1_INT_IRQn)
#define PWM_1_INST_CLK_FREQ                                             10000000
/* GPIO defines for channel 0 */
#define GPIO_PWM_1_C0_PORT                                                 GPIOB
#define GPIO_PWM_1_C0_PIN                                         DL_GPIO_PIN_17
#define GPIO_PWM_1_C0_IOMUX                                      (IOMUX_PINCM43)
#define GPIO_PWM_1_C0_IOMUX_FUNC                     IOMUX_PINCM43_PF_TIMA1_CCP0
#define GPIO_PWM_1_C0_IDX                                    DL_TIMER_CC_0_INDEX
/* GPIO defines for channel 1 */
#define GPIO_PWM_1_C1_PORT                                                 GPIOB
#define GPIO_PWM_1_C1_PIN                                         DL_GPIO_PIN_18
#define GPIO_PWM_1_C1_IOMUX                                      (IOMUX_PINCM44)
#define GPIO_PWM_1_C1_IOMUX_FUNC                     IOMUX_PINCM44_PF_TIMA1_CCP1
#define GPIO_PWM_1_C1_IDX                                    DL_TIMER_CC_1_INDEX



/* Defines for TIMER_0 */
#define TIMER_0_INST                                                     (TIMA0)
#define TIMER_0_INST_IRQHandler                                 TIMA0_IRQHandler
#define TIMER_0_INST_INT_IRQN                                   (TIMA0_INT_IRQn)
#define TIMER_0_INST_LOAD_VALUE                                           (999U)




/* Defines for I2C_0 */
#define I2C_0_INST                                                          I2C0
#define I2C_0_INST_IRQHandler                                    I2C0_IRQHandler
#define I2C_0_INST_INT_IRQN                                        I2C0_INT_IRQn
#define I2C_0_BUS_SPEED_HZ                                                100000
#define GPIO_I2C_0_SDA_PORT                                                GPIOA
#define GPIO_I2C_0_SDA_PIN                                         DL_GPIO_PIN_0
#define GPIO_I2C_0_IOMUX_SDA                                      (IOMUX_PINCM1)
#define GPIO_I2C_0_IOMUX_SDA_FUNC                       IOMUX_PINCM1_PF_I2C0_SDA
#define GPIO_I2C_0_SCL_PORT                                                GPIOA
#define GPIO_I2C_0_SCL_PIN                                         DL_GPIO_PIN_1
#define GPIO_I2C_0_IOMUX_SCL                                      (IOMUX_PINCM2)
#define GPIO_I2C_0_IOMUX_SCL_FUNC                       IOMUX_PINCM2_PF_I2C0_SCL


/* Defines for UART_BLE */
#define UART_BLE_INST                                                      UART3
#define UART_BLE_INST_FREQUENCY                                         80000000
#define UART_BLE_INST_IRQHandler                                UART3_IRQHandler
#define UART_BLE_INST_INT_IRQN                                    UART3_INT_IRQn
#define GPIO_UART_BLE_RX_PORT                                              GPIOB
#define GPIO_UART_BLE_TX_PORT                                              GPIOB
#define GPIO_UART_BLE_RX_PIN                                      DL_GPIO_PIN_13
#define GPIO_UART_BLE_TX_PIN                                      DL_GPIO_PIN_12
#define GPIO_UART_BLE_IOMUX_RX                                   (IOMUX_PINCM30)
#define GPIO_UART_BLE_IOMUX_TX                                   (IOMUX_PINCM29)
#define GPIO_UART_BLE_IOMUX_RX_FUNC                    IOMUX_PINCM30_PF_UART3_RX
#define GPIO_UART_BLE_IOMUX_TX_FUNC                    IOMUX_PINCM29_PF_UART3_TX
#define UART_BLE_BAUD_RATE                                              (115200)
#define UART_BLE_IBRD_80_MHZ_115200_BAUD                                    (43)
#define UART_BLE_FBRD_80_MHZ_115200_BAUD                                    (26)
/* Defines for UART_1 */
#define UART_1_INST                                                        UART1
#define UART_1_INST_FREQUENCY                                           40000000
#define UART_1_INST_IRQHandler                                  UART1_IRQHandler
#define UART_1_INST_INT_IRQN                                      UART1_INT_IRQn
#define GPIO_UART_1_RX_PORT                                                GPIOA
#define GPIO_UART_1_TX_PORT                                                GPIOA
#define GPIO_UART_1_RX_PIN                                         DL_GPIO_PIN_9
#define GPIO_UART_1_TX_PIN                                         DL_GPIO_PIN_8
#define GPIO_UART_1_IOMUX_RX                                     (IOMUX_PINCM20)
#define GPIO_UART_1_IOMUX_TX                                     (IOMUX_PINCM19)
#define GPIO_UART_1_IOMUX_RX_FUNC                      IOMUX_PINCM20_PF_UART1_RX
#define GPIO_UART_1_IOMUX_TX_FUNC                      IOMUX_PINCM19_PF_UART1_TX
#define UART_1_BAUD_RATE                                                (115200)
#define UART_1_IBRD_40_MHZ_115200_BAUD                                      (21)
#define UART_1_FBRD_40_MHZ_115200_BAUD                                      (45)
/* Defines for UART_K230 */
#define UART_K230_INST                                                     UART2
#define UART_K230_INST_FREQUENCY                                        40000000
#define UART_K230_INST_IRQHandler                               UART2_IRQHandler
#define UART_K230_INST_INT_IRQN                                   UART2_INT_IRQn
#define GPIO_UART_K230_RX_PORT                                             GPIOB
#define GPIO_UART_K230_TX_PORT                                             GPIOB
#define GPIO_UART_K230_RX_PIN                                     DL_GPIO_PIN_16
#define GPIO_UART_K230_TX_PIN                                     DL_GPIO_PIN_15
#define GPIO_UART_K230_IOMUX_RX                                  (IOMUX_PINCM33)
#define GPIO_UART_K230_IOMUX_TX                                  (IOMUX_PINCM32)
#define GPIO_UART_K230_IOMUX_RX_FUNC                   IOMUX_PINCM33_PF_UART2_RX
#define GPIO_UART_K230_IOMUX_TX_FUNC                   IOMUX_PINCM32_PF_UART2_TX
#define UART_K230_BAUD_RATE                                             (115200)
#define UART_K230_IBRD_40_MHZ_115200_BAUD                                   (21)
#define UART_K230_FBRD_40_MHZ_115200_BAUD                                   (45)




/* Defines for SPI_0 */
#define SPI_0_INST                                                         SPI1
#define SPI_0_INST_IRQHandler                                   SPI1_IRQHandler
#define SPI_0_INST_INT_IRQN                                       SPI1_INT_IRQn
#define GPIO_SPI_0_PICO_PORT                                              GPIOB
#define GPIO_SPI_0_PICO_PIN                                       DL_GPIO_PIN_8
#define GPIO_SPI_0_IOMUX_PICO                                   (IOMUX_PINCM25)
#define GPIO_SPI_0_IOMUX_PICO_FUNC                   IOMUX_PINCM25_PF_SPI1_PICO
#define GPIO_SPI_0_POCI_PORT                                              GPIOB
#define GPIO_SPI_0_POCI_PIN                                       DL_GPIO_PIN_7
#define GPIO_SPI_0_IOMUX_POCI                                   (IOMUX_PINCM24)
#define GPIO_SPI_0_IOMUX_POCI_FUNC                   IOMUX_PINCM24_PF_SPI1_POCI
/* GPIO configuration for SPI_0 */
#define GPIO_SPI_0_SCLK_PORT                                              GPIOB
#define GPIO_SPI_0_SCLK_PIN                                       DL_GPIO_PIN_9
#define GPIO_SPI_0_IOMUX_SCLK                                   (IOMUX_PINCM26)
#define GPIO_SPI_0_IOMUX_SCLK_FUNC                   IOMUX_PINCM26_PF_SPI1_SCLK



/* Defines for ADC12_0 */
#define ADC12_0_INST                                                        ADC0
#define ADC12_0_INST_IRQHandler                                  ADC0_IRQHandler
#define ADC12_0_INST_INT_IRQN                                    (ADC0_INT_IRQn)
#define ADC12_0_ADCMEM_3                                      DL_ADC12_MEM_IDX_3
#define ADC12_0_ADCMEM_3_REF                     DL_ADC12_REFERENCE_VOLTAGE_VDDA
#define ADC12_0_ADCMEM_3_REF_VOLTAGE_V                                       3.3
#define GPIO_ADC12_0_C3_PORT                                               GPIOA
#define GPIO_ADC12_0_C3_PIN                                       DL_GPIO_PIN_24
#define GPIO_ADC12_0_IOMUX_C3                                    (IOMUX_PINCM54)
#define GPIO_ADC12_0_IOMUX_C3_FUNC                (IOMUX_PINCM54_PF_UNCONNECTED)



/* Port definition for Pin Group GPIO */
#define GPIO_PORT                                                        (GPIOA)

/* Defines for BUZZER: GPIOA.7 with pinCMx 14 on package pin 49 */
#define GPIO_BUZZER_PIN                                          (DL_GPIO_PIN_7)
#define GPIO_BUZZER_IOMUX                                        (IOMUX_PINCM14)
/* Port definition for Pin Group GPIO_LED */
#define GPIO_LED_PORT                                                    (GPIOB)

/* Defines for R: GPIOB.26 with pinCMx 57 on package pin 28 */
#define GPIO_LED_R_PIN                                          (DL_GPIO_PIN_26)
#define GPIO_LED_R_IOMUX                                         (IOMUX_PINCM57)
/* Defines for G: GPIOB.22 with pinCMx 50 on package pin 21 */
#define GPIO_LED_G_PIN                                          (DL_GPIO_PIN_22)
#define GPIO_LED_G_IOMUX                                         (IOMUX_PINCM50)
/* Defines for B: GPIOB.27 with pinCMx 58 on package pin 29 */
#define GPIO_LED_B_PIN                                          (DL_GPIO_PIN_27)
#define GPIO_LED_B_IOMUX                                         (IOMUX_PINCM58)
/* Port definition for Pin Group GPIO_KEY */
#define GPIO_KEY_PORT                                                    (GPIOB)

/* Defines for PIN_1: GPIOB.0 with pinCMx 12 on package pin 47 */
#define GPIO_KEY_PIN_1_PIN                                       (DL_GPIO_PIN_0)
#define GPIO_KEY_PIN_1_IOMUX                                     (IOMUX_PINCM12)
/* Defines for PIN_2: GPIOB.21 with pinCMx 49 on package pin 20 */
#define GPIO_KEY_PIN_2_PIN                                      (DL_GPIO_PIN_21)
#define GPIO_KEY_PIN_2_IOMUX                                     (IOMUX_PINCM49)
/* Defines for PIN_3: GPIOB.23 with pinCMx 51 on package pin 22 */
#define GPIO_KEY_PIN_3_PIN                                      (DL_GPIO_PIN_23)
#define GPIO_KEY_PIN_3_IOMUX                                     (IOMUX_PINCM51)
/* Port definition for Pin Group GPIO_ENCODER */
#define GPIO_ENCODER_PORT                                                (GPIOB)

/* Defines for RA: GPIOB.4 with pinCMx 17 on package pin 52 */
// pins affected by this interrupt request:["RA","RB","LA","LB"]
#define GPIO_ENCODER_INT_IRQN                                   (GPIOB_INT_IRQn)
#define GPIO_ENCODER_INT_IIDX                   (DL_INTERRUPT_GROUP1_IIDX_GPIOB)
#define GPIO_ENCODER_RA_IIDX                                 (DL_GPIO_IIDX_DIO4)
#define GPIO_ENCODER_RA_PIN                                      (DL_GPIO_PIN_4)
#define GPIO_ENCODER_RA_IOMUX                                    (IOMUX_PINCM17)
/* Defines for RB: GPIOB.5 with pinCMx 18 on package pin 53 */
#define GPIO_ENCODER_RB_IIDX                                 (DL_GPIO_IIDX_DIO5)
#define GPIO_ENCODER_RB_PIN                                      (DL_GPIO_PIN_5)
#define GPIO_ENCODER_RB_IOMUX                                    (IOMUX_PINCM18)
/* Defines for LA: GPIOB.10 with pinCMx 27 on package pin 62 */
#define GPIO_ENCODER_LA_IIDX                                (DL_GPIO_IIDX_DIO10)
#define GPIO_ENCODER_LA_PIN                                     (DL_GPIO_PIN_10)
#define GPIO_ENCODER_LA_IOMUX                                    (IOMUX_PINCM27)
/* Defines for LB: GPIOB.11 with pinCMx 28 on package pin 63 */
#define GPIO_ENCODER_LB_IIDX                                (DL_GPIO_IIDX_DIO11)
#define GPIO_ENCODER_LB_PIN                                     (DL_GPIO_PIN_11)
#define GPIO_ENCODER_LB_IOMUX                                    (IOMUX_PINCM28)
/* Port definition for Pin Group GPIO_GRAY */
#define GPIO_GRAY_PORT                                                   (GPIOA)

/* Defines for AD0: GPIOA.28 with pinCMx 3 on package pin 35 */
#define GPIO_GRAY_AD0_PIN                                       (DL_GPIO_PIN_28)
#define GPIO_GRAY_AD0_IOMUX                                       (IOMUX_PINCM3)
/* Defines for AD1: GPIOA.29 with pinCMx 4 on package pin 36 */
#define GPIO_GRAY_AD1_PIN                                       (DL_GPIO_PIN_29)
#define GPIO_GRAY_AD1_IOMUX                                       (IOMUX_PINCM4)
/* Defines for AD2: GPIOA.30 with pinCMx 5 on package pin 37 */
#define GPIO_GRAY_AD2_PIN                                       (DL_GPIO_PIN_30)
#define GPIO_GRAY_AD2_IOMUX                                       (IOMUX_PINCM5)
/* Port definition for Pin Group GPIO_SPI */
#define GPIO_SPI_PORT                                                    (GPIOB)

/* Defines for GYRO_CS: GPIOB.6 with pinCMx 23 on package pin 58 */
#define GPIO_SPI_GYRO_CS_PIN                                     (DL_GPIO_PIN_6)
#define GPIO_SPI_GYRO_CS_IOMUX                                   (IOMUX_PINCM23)
/* Defines for ACC_CS: GPIOB.14 with pinCMx 31 on package pin 2 */
#define GPIO_SPI_ACC_CS_PIN                                     (DL_GPIO_PIN_14)
#define GPIO_SPI_ACC_CS_IOMUX                                    (IOMUX_PINCM31)




/* clang-format on */

void SYSCFG_DL_init(void);
void SYSCFG_DL_initPower(void);
void SYSCFG_DL_GPIO_init(void);
void SYSCFG_DL_SYSCTL_init(void);

bool SYSCFG_DL_SYSCTL_SYSPLL_init(void);
void SYSCFG_DL_PWM_0_init(void);
void SYSCFG_DL_PWM_1_init(void);
void SYSCFG_DL_TIMER_0_init(void);
void SYSCFG_DL_I2C_0_init(void);
void SYSCFG_DL_UART_BLE_init(void);
void SYSCFG_DL_UART_1_init(void);
void SYSCFG_DL_UART_K230_init(void);
void SYSCFG_DL_SPI_0_init(void);
void SYSCFG_DL_ADC12_0_init(void);

void SYSCFG_DL_SYSTICK_init(void);

bool SYSCFG_DL_saveConfiguration(void);
bool SYSCFG_DL_restoreConfiguration(void);

#ifdef __cplusplus
}
#endif

#endif /* ti_msp_dl_config_h */
