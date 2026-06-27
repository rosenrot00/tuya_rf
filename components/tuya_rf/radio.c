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
 * @file    radio.c
 * @brief   Generic radio handlers
 *
 * @version 1.2
 * @date    Jul 17 2017
 * @author  CMOSTEK R@D
 */

#include "radio.h"
#include "cmt2300a_hal.h"
#include "cmt2300a_params_captured.h"
#include <stddef.h>

static uint16_t g_current_frequency_mhz = 433;
static uint16_t g_current_tx_bank_frequency_mhz = 433;
static uint8_t g_current_tx_bank_868_profile = TUYA_RF_TX_PROFILE_868_AHOY_OPENDTU;
static int8_t g_current_tx_power_868_dbm = 13;
static uint8_t g_current_modulation = TUYA_RF_MODULATION_OOK;
static uint8_t g_current_fsk_profile = 0xFF;
static bool g_current_fsk_direct_mode = true;
static const uint8_t g_agc_ook_register_addrs[TUYA_RF_AGC_OOK_REGISTER_COUNT] = {
    CMT2300A_CUS_AGC1,
    CMT2300A_CUS_AGC2,
    CMT2300A_CUS_AGC3,
    CMT2300A_CUS_AGC4,
    CMT2300A_CUS_OOK1,
    CMT2300A_CUS_OOK2,
    CMT2300A_CUS_OOK3,
    CMT2300A_CUS_OOK4,
    CMT2300A_CUS_OOK5,
};

static const uint8_t *RF_GetFrequencyBank(uint16_t frequency_mhz, uint8_t modulation, uint8_t fsk_profile)
{
    if (modulation != TUYA_RF_MODULATION_OOK &&
        fsk_profile == TUYA_RF_FSK_PROFILE_VELUX_86895_2K4_DEV5) {
        return g_cmt2300aFrequencyBank868950;
    }
    if (frequency_mhz == 315) {
        return g_cmt2300aFrequencyBank315;
    }
    if (frequency_mhz == 433) {
        return g_cmt2300aFrequencyBank;
    }
    if (frequency_mhz == 868) {
        return g_cmt2300aFrequencyBank868;
    }
    return NULL;
}

static int RF_ConfigFrequencyBank(uint16_t frequency_mhz, uint8_t modulation, uint8_t fsk_profile)
{
    const uint8_t *frequency_bank = RF_GetFrequencyBank(frequency_mhz, modulation, fsk_profile);
    if (frequency_bank == NULL) {
        return 3;
    }
    if (g_current_frequency_mhz == frequency_mhz &&
        g_current_modulation == modulation &&
        (modulation == TUYA_RF_MODULATION_OOK || g_current_fsk_profile == fsk_profile)) {
        return 0;
    }
    if (!CMT2300A_ConfigRegBank(CMT2300A_FREQUENCY_BANK_ADDR, frequency_bank, CMT2300A_FREQUENCY_BANK_SIZE)) {
        return 1;
    }
    g_current_frequency_mhz = frequency_mhz;
    return 0;
}

static void RF_ApplyRegisterOverrides(uint8_t start_addr, uint8_t count, uint32_t mask, const uint8_t *values)
{
    if (values == NULL || mask == 0) {
        return;
    }
    for (uint8_t i = 0; i < count; i++) {
        if ((mask & (1UL << i)) != 0) {
            CMT2300A_WriteReg(start_addr + i, values[i]);
        }
    }
}

static int RF_ConfigModemBanks(uint8_t modulation, uint8_t fsk_profile, bool fsk_direct_mode,
                               uint32_t fsk_data_rate_register_mask, const uint8_t *fsk_data_rate_registers,
                               uint32_t fsk_baseband_register_mask, const uint8_t *fsk_baseband_registers)
{
    const bool is_fsk = modulation == TUYA_RF_MODULATION_2FSK || modulation == TUYA_RF_MODULATION_GFSK;
    if (!is_fsk) {
        if (g_current_modulation != TUYA_RF_MODULATION_OOK) {
            if (!CMT2300A_ConfigRegBank(CMT2300A_DATA_RATE_BANK_ADDR, g_cmt2300aDataRateBank,
                                        CMT2300A_DATA_RATE_BANK_SIZE)) {
                return 1;
            }
            if (!CMT2300A_ConfigRegBank(CMT2300A_BASEBAND_BANK_ADDR, g_cmt2300aBasebandBank,
                                        CMT2300A_BASEBAND_BANK_SIZE)) {
                return 1;
            }
        }
        g_current_modulation = TUYA_RF_MODULATION_OOK;
        g_current_fsk_profile = 0xFF;
        g_current_fsk_direct_mode = true;
        return 0;
    }

    if (g_current_modulation == modulation &&
        g_current_fsk_profile == fsk_profile &&
        g_current_fsk_direct_mode == fsk_direct_mode &&
        fsk_data_rate_register_mask == 0 &&
        fsk_baseband_register_mask == 0) {
        return 0;
    }

    const uint8_t *data_rate_bank = g_cmt2300aDataRateBankFskRfpdk2k4Dev5;
    const uint8_t *baseband_bank = fsk_direct_mode ? g_cmt2300aBasebandBankFskDirect : g_cmt2300aBasebandBankFskPacket;

    if (!CMT2300A_ConfigRegBank(CMT2300A_DATA_RATE_BANK_ADDR, data_rate_bank, CMT2300A_DATA_RATE_BANK_SIZE)) {
        return 1;
    }
    if (!CMT2300A_ConfigRegBank(CMT2300A_BASEBAND_BANK_ADDR, baseband_bank, CMT2300A_BASEBAND_BANK_SIZE)) {
        return 1;
    }
    RF_ApplyRegisterOverrides(CMT2300A_DATA_RATE_BANK_ADDR, CMT2300A_DATA_RATE_BANK_SIZE,
                              fsk_data_rate_register_mask, fsk_data_rate_registers);
    RF_ApplyRegisterOverrides(CMT2300A_BASEBAND_BANK_ADDR, CMT2300A_BASEBAND_BANK_SIZE,
                              fsk_baseband_register_mask, fsk_baseband_registers);

    g_current_modulation = modulation;
    g_current_fsk_profile = fsk_profile;
    g_current_fsk_direct_mode = fsk_direct_mode;
    return 0;
}

static void RF_SetTxPowerDouble(bool enable)
{
    uint8_t tmp = CMT2300A_ReadReg(CMT2300A_CUS_CMT4);
    if (enable) {
        CMT2300A_WriteReg(CMT2300A_CUS_CMT4, tmp | 0x01);
    } else {
        CMT2300A_WriteReg(CMT2300A_CUS_CMT4, tmp & 0xFE);
    }
}

static void RF_SetDoutMute(bool enable)
{
    uint8_t tmp = CMT2300A_ReadReg(CMT2300A_CUS_SYS10);
    if (enable) {
        CMT2300A_WriteReg(CMT2300A_CUS_SYS10, tmp | CMT2300A_MASK_DOUT_MUTE);
    } else {
        CMT2300A_WriteReg(CMT2300A_CUS_SYS10, tmp & ~CMT2300A_MASK_DOUT_MUTE);
    }
}

static void RF_ConfigRxDebugTuning(int8_t rssi_avg_mode, uint16_t agc_ook_register_mask,
                                   const uint8_t agc_ook_registers[TUYA_RF_AGC_OOK_REGISTER_COUNT])
{
    if (rssi_avg_mode >= 0 && rssi_avg_mode <= 7) {
        uint8_t tmp = CMT2300A_ReadReg(CMT2300A_CUS_SYS11);
        tmp = (tmp & ~CMT2300A_MASK_RSSI_AVG_MODE) | ((uint8_t) rssi_avg_mode & CMT2300A_MASK_RSSI_AVG_MODE);
        CMT2300A_WriteReg(CMT2300A_CUS_SYS11, tmp);
    }

    if (agc_ook_registers == NULL) {
        return;
    }
    for (uint8_t i = 0; i < TUYA_RF_AGC_OOK_REGISTER_COUNT; i++) {
        if ((agc_ook_register_mask & (1U << i)) != 0) {
            CMT2300A_WriteReg(g_agc_ook_register_addrs[i], agc_ook_registers[i]);
        }
    }
}

static void RF_ConfigTxPower868(int8_t tx_power_868_dbm)
{
    if (tx_power_868_dbm == 20) {
        RF_SetTxPowerDouble(true);
        CMT2300A_WriteReg(CMT2300A_CUS_TX5, 0x07);
        CMT2300A_WriteReg(CMT2300A_CUS_TX8, 0x8A);
        CMT2300A_WriteReg(CMT2300A_CUS_TX9, 0x18);
        CMT2300A_WriteReg(CMT2300A_CUS_TX10, 0x3F);
    } else {
        RF_SetTxPowerDouble(false);
    }
}

static int RF_ConfigTxBank(uint16_t frequency_mhz, uint8_t tx_profile_868, int8_t tx_power_868_dbm)
{
    const uint8_t *tx_bank = g_cmt2300aTxBank;
    uint16_t tx_bank_frequency_mhz = 433;
    uint8_t tx_bank_868_profile = TUYA_RF_TX_PROFILE_868_AHOY_OPENDTU;
    int8_t tx_power_dbm = 13;

    if (frequency_mhz == 868) {
        if (tx_profile_868 == TUYA_RF_TX_PROFILE_868_RFPDK) {
            tx_bank = g_cmt2300aTxBank868Rfpdk;
            tx_bank_868_profile = TUYA_RF_TX_PROFILE_868_RFPDK;
        } else {
            tx_bank = g_cmt2300aTxBank868AhoyOpenDtu;
            tx_bank_868_profile = TUYA_RF_TX_PROFILE_868_AHOY_OPENDTU;
        }
        tx_bank_frequency_mhz = 868;
        tx_power_dbm = tx_power_868_dbm == 20 ? 20 : 13;
    }

    if (g_current_tx_bank_frequency_mhz == tx_bank_frequency_mhz &&
        (tx_bank_frequency_mhz != 868 ||
         (g_current_tx_bank_868_profile == tx_bank_868_profile && g_current_tx_power_868_dbm == tx_power_dbm))) {
        return 0;
    }
    if (!CMT2300A_ConfigRegBank(CMT2300A_TX_BANK_ADDR, tx_bank, CMT2300A_TX_BANK_SIZE)) {
        return 1;
    }
    if (tx_bank_frequency_mhz == 868) {
        RF_ConfigTxPower868(tx_power_dbm);
    } else {
        RF_SetTxPowerDouble(false);
    }
    g_current_tx_bank_frequency_mhz = tx_bank_frequency_mhz;
    g_current_tx_bank_868_profile = tx_bank_868_profile;
    g_current_tx_power_868_dbm = tx_power_dbm;
    return 0;
}

int RF_SetFrequency(uint16_t frequency_mhz)
{
    if (!CMT2300A_GoStby()) {
        return 2;
    }
    return RF_ConfigFrequencyBank(frequency_mhz, TUYA_RF_MODULATION_OOK, 0);
}

int RF_Init(void)
{
    uint8_t tmp;
    
    CMT2300A_InitGpio();
	CMT2300A_Init();
    
    /* Config registers */
    CMT2300A_ConfigRegBank(CMT2300A_CMT_BANK_ADDR       , g_cmt2300aCmtBank       , CMT2300A_CMT_BANK_SIZE       );
    CMT2300A_ConfigRegBank(CMT2300A_SYSTEM_BANK_ADDR    , g_cmt2300aSystemBank    , CMT2300A_SYSTEM_BANK_SIZE    );
    CMT2300A_ConfigRegBank(CMT2300A_FREQUENCY_BANK_ADDR , g_cmt2300aFrequencyBank , CMT2300A_FREQUENCY_BANK_SIZE );
    CMT2300A_ConfigRegBank(CMT2300A_DATA_RATE_BANK_ADDR , g_cmt2300aDataRateBank  , CMT2300A_DATA_RATE_BANK_SIZE );
    CMT2300A_ConfigRegBank(CMT2300A_BASEBAND_BANK_ADDR  , g_cmt2300aBasebandBank  , CMT2300A_BASEBAND_BANK_SIZE  );
    CMT2300A_ConfigRegBank(CMT2300A_TX_BANK_ADDR        , g_cmt2300aTxBank        , CMT2300A_TX_BANK_SIZE        );
    g_current_frequency_mhz = 433;
    g_current_tx_bank_frequency_mhz = 433;
    g_current_tx_bank_868_profile = TUYA_RF_TX_PROFILE_868_AHOY_OPENDTU;
    g_current_tx_power_868_dbm = 13;
    g_current_modulation = TUYA_RF_MODULATION_OOK;
    g_current_fsk_profile = 0xFF;
    g_current_fsk_direct_mode = true;
    
    // xosc_aac_code[2:0] = 2
    tmp = (~0x07) & CMT2300A_ReadReg(CMT2300A_CUS_CMT10);
    CMT2300A_WriteReg(CMT2300A_CUS_CMT10, tmp|0x02);
    
	if(false==CMT2300A_IsExist()) 
	{
        //CMT2300A not found!
        return -1;
    }
    else
	{
        return 0;
    }
}

int StartTx(uint16_t frequency_mhz, uint8_t tx_profile_868, int8_t tx_power_868_dbm) {
    if (!CMT2300A_GoStby()) {
        return 2;
    }
    int freq_res = RF_ConfigFrequencyBank(frequency_mhz, TUYA_RF_MODULATION_OOK, 0);
    if (freq_res != 0) {
        return freq_res;
    }
    int modem_res = RF_ConfigModemBanks(TUYA_RF_MODULATION_OOK, 0, true, 0, NULL, 0, NULL);
    if (modem_res != 0) {
        return modem_res;
    }
    int tx_bank_res = RF_ConfigTxBank(frequency_mhz, tx_profile_868, tx_power_868_dbm);
    if (tx_bank_res != 0) {
        return tx_bank_res;
    }
    CMT2300A_WriteReg(CMT2300A_CUS_SYS2,0);
    CMT2300A_ConfigGpio(CMT2300A_GPIO1_SEL_DOUT | CMT2300A_GPIO3_SEL_DIN | CMT2300A_GPIO2_SEL_INT2);
	CMT2300A_EnableTxDin(true);
	CMT2300A_ConfigTxDin(CMT2300A_TX_DIN_SEL_GPIO1);
	CMT2300A_EnableTxDinInvert(false);
	CMT2300A_ClearInterruptFlags();  // Clear stale TX_DONE flags to prevent false early exit in AutoSwitchStatus
	delay(2);  // Stabilization delay: let CMT2300A settle in Standby before TX transition
	if (CMT2300A_GoTx()) {
	    delay(2);  // Stabilization delay: let CMT2300A fully enter TX mode
        return 0;
    } else {
        return 2;
    }
}
 

int StartRx(uint16_t frequency_mhz, bool dout_mute, int8_t rssi_avg_mode,
            uint16_t agc_ook_register_mask,
            const uint8_t agc_ook_registers[TUYA_RF_AGC_OOK_REGISTER_COUNT],
            uint8_t modulation, uint8_t fsk_profile, bool fsk_direct_mode,
            uint32_t fsk_data_rate_register_mask,
            const uint8_t fsk_data_rate_registers[TUYA_RF_FSK_DATA_RATE_REGISTER_COUNT],
            uint32_t fsk_baseband_register_mask,
            const uint8_t fsk_baseband_registers[TUYA_RF_FSK_BASEBAND_REGISTER_COUNT]) {
    if (!CMT2300A_GoStby()) {
        return 2;
    }
    int freq_res = RF_ConfigFrequencyBank(frequency_mhz, modulation, fsk_profile);
    if (freq_res != 0) {
        return freq_res;
    }
    int modem_res = RF_ConfigModemBanks(modulation, fsk_profile, fsk_direct_mode,
                                        fsk_data_rate_register_mask, fsk_data_rate_registers,
                                        fsk_baseband_register_mask, fsk_baseband_registers);
    if (modem_res != 0) {
        return modem_res;
    }

	CMT2300A_WriteReg(CMT2300A_CUS_SYS2 , 0);
    if (modulation == TUYA_RF_MODULATION_OOK) {
        RF_SetDoutMute(dout_mute);
        RF_ConfigRxDebugTuning(rssi_avg_mode, agc_ook_register_mask, agc_ook_registers);
    }
	CMT2300A_EnableTxDin(false);
	CMT2300A_EnableFifoMerge(true);
	CMT2300A_WriteReg(CMT2300A_CUS_PKT29, 0x20); 
	CMT2300A_ConfigGpio (CMT2300A_GPIO1_SEL_DCLK | CMT2300A_GPIO2_SEL_DOUT | CMT2300A_GPIO3_SEL_INT2);
	CMT2300A_ConfigInterrupt(CMT2300A_INT_SEL_SYNC_OK, CMT2300A_INT_SEL_SL_TMO);
	CMT2300A_EnableInterrupt(0);
	CMT2300A_ClearInterruptFlags();
	CMT2300A_ClearRxFifo();
	delay(2);  // Stabilization delay: let FIFO clear complete before RX transition

    if (!CMT2300A_GoRx()) {
        return 2;
    }

	CMT2300A_ClearInterruptFlags();
    return 0;
}
