/*
 * THE FOLLOWING FIRMWARE IS PROVIDED: (1) "AS IS" WITH NO WARRANTY; AND
 * (2)TO ENABLE ACCESS TO CODING INFORMATION TO GUIDE AND FACILITATE CUSTOMER.
 * CONSEQUENTLY, CMOSTEK SHALL NOT BE HELD LIABLE FOR ANY DIRECT, INDIRECT OR
 * CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE CONTENT
 * OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING INFORMATION
 * CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
 *
 * Copyright (C) CMOSTEK SZ.
 */

/*!
 * @file    radio.h
 * @brief   Generic radio handlers
 *
 * @version 1.2
 * @date    Jul 17 2017
 * @author  CMOSTEK R@D
 */
 
#ifndef __RADIO_H
#define __RADIO_H

#include "cmt2300a.h"
#include <stdbool.h>
#include <stdint.h>

#define TUYA_RF_TX_PROFILE_868_RFPDK 0
#define TUYA_RF_TX_PROFILE_868_AHOY_OPENDTU 1
#define TUYA_RF_AGC_OOK_REGISTER_COUNT 9
#define TUYA_RF_MODULATION_OOK 0
#define TUYA_RF_MODULATION_2FSK 1
#define TUYA_RF_MODULATION_GFSK 2
#define TUYA_RF_FSK_PROFILE_RFPDK_868_2K4_DEV5 0
#define TUYA_RF_FSK_PROFILE_VELUX_86895_2K4_DEV5 1
#define TUYA_RF_FSK_DATA_RATE_REGISTER_COUNT 24
#define TUYA_RF_FSK_BASEBAND_REGISTER_COUNT 29

#ifdef __cplusplus 
extern "C" { 
#endif

int RF_Init(void);
int RF_SetFrequency(uint16_t frequency_mhz);
int StartTx(uint16_t frequency_mhz, uint8_t tx_profile_868, int8_t tx_power_868_dbm);
int StartRx(uint16_t frequency_mhz, bool dout_mute, int8_t rssi_avg_mode,
            uint16_t agc_ook_register_mask,
            const uint8_t agc_ook_registers[TUYA_RF_AGC_OOK_REGISTER_COUNT],
            uint8_t modulation, uint8_t fsk_profile, bool fsk_direct_mode,
            uint32_t fsk_data_rate_register_mask,
            const uint8_t fsk_data_rate_registers[TUYA_RF_FSK_DATA_RATE_REGISTER_COUNT],
            uint32_t fsk_baseband_register_mask,
            const uint8_t fsk_baseband_registers[TUYA_RF_FSK_BASEBAND_REGISTER_COUNT]);

#ifdef __cplusplus 
} 
#endif

#endif
