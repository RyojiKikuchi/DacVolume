/*
 * MAIN Generated Driver File
 * 
 * @file main.c
 * 
 * @defgroup main MAIN
 * 
 * @brief DacVolume - PIC16F13113 Firmware
 *
 * @version MAIN Driver Version 1.0.2
 *
 * @version Package Version: 3.1.2
 */

/*
? [2026] Microchip Technology Inc. and its subsidiaries.

    Subject to your compliance with these terms, you may use Microchip 
    software and any derivatives exclusively with Microchip products. 
    You are responsible for complying with 3rd party license terms  
    applicable to your use of 3rd party software (including open source  
    software) that may accompany Microchip software. SOFTWARE IS ?AS IS.? 
    NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS 
    SOFTWARE, INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT,  
    MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT 
    WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, 
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY 
    KIND WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF 
    MICROCHIP HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE 
    FORESEEABLE. TO THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP?S 
    TOTAL LIABILITY ON ALL CLAIMS RELATED TO THE SOFTWARE WILL NOT 
    EXCEED AMOUNT OF FEES, IF ANY, YOU PAID DIRECTLY TO MICROCHIP FOR 
    THIS SOFTWARE.
 */
#include "mcc_generated_files/system/system.h"

/*
 * ADCの入力を変換してDACに出力する
 * 
 * Pin assignments:
 *   RA0 : Vout   (DAC1OUT1)
 *   RA1 : Vrf+   (VREF+/DAC1REF0+)
 *   RA2 : LED    (RA2)
 *   RA3 : MCLR
 *   RA4 : ADJUST (RA4) internal pullup
 *   RA5 : ADC In (ANA5)
 */

/* SAF addresses
 * PIC16F13113: 2048 program words total, SAF = last 128 words.
 * SAF start = 2048 - 128 = 1920 = 0x0780.
 * Row size = 32 words; 0x0780 is row-aligned (0x0780 / 32 = 0x3C).
 * Row Addresses 0x780, 0x7A0, 0x7C0, 0x7E0
 */
#define SAF_ROW_ADDR            0x07E0U         /* Row-aligned start of SAF         */
#define SAF_ADDR_ADC_MIN        (SAF_ROW_ADDR + 0U)
#define SAF_ADDR_ADC_CENTER     (SAF_ROW_ADDR + 1U)
#define SAF_ADDR_ADC_MAX        (SAF_ROW_ADDR + 2U)
#define SAF_ADDR_ADC_REVERSE    (SAF_ROW_ADDR + 3U)
#define SAF_ADDR_DAC_MIN        (SAF_ROW_ADDR + 4U)
#define SAF_ADDR_DAC_MAX        (SAF_ROW_ADDR + 5U)

/* ADC校正値がSAFに記録されていない場合のデフォルト値(10bit) */
#define DEFAULT_ADC_MIN_VOL     0U
#define DEFAULT_ADC_CENTER_VOL  511U
#define DEFAULT_ADC_MAX_VOL     1023U

/* DAC校正値がSAFに記録されていない場合のデフォルト値(8bit) 
 * DAC出力範囲の上限・下限及び調整範囲 */
#define DEFAULT_DAC_MIN     47U    // 18.43%
#define DEFAULT_DAC_MAX    172U    // 67.45%
#define DAC_ADJUSTMENT_RANGE    5U 
#define DAC_ADJUSTMENT_MIN_MIN  (DEFAULT_DAC_MIN - DAC_ADJUSTMENT_RANGE)
#define DAC_ADJUSTMENT_MIN_MAX  (DEFAULT_DAC_MIN + DAC_ADJUSTMENT_RANGE)
#define DAC_ADJUSTMENT_MAX_MIN  (DEFAULT_DAC_MAX - DAC_ADJUSTMENT_RANGE)
#define DAC_ADJUSTMENT_MAX_MAX  (DEFAULT_DAC_MAX + DAC_ADJUSTMENT_RANGE)


/*
 * ADC取得値ノイズ判定範囲
 *   ADCの前回取得値と比較して変動が上下にこの範囲内ならLEDを点灯しない
 *   LEDの判定にのみ使用して、DAC出力には反映しない
 */
#define ADC_NOISE_THRESHOLD  3U

/* ADC上限・下限から不感とする範囲
 * この範囲外の場合はDACの出力を0若しくは最大値とする
 *  */
#define NON_PERCEPTUAL_ON_THRESHOLD    6U
#define NON_PERCEPTUAL_OFF_THRESHOLD    4U

/* ADJUST校正の有効範囲: 最大-最小がこの値未満の場合はエラー */
#define ADJUST_MIN_RANGE            512U

/* Timing: TMR0 fires every 2 ms */
#define TICKS_500MS     250U            /* 500 ms / 2 ms per tick           */
#define TICKS_1S        500U            /* 1 s   / 2 ms per tick           */

/*
 * Global
 */
/* ADC校正値 */
volatile uint16_t g_adcMinVoltage; /* Min ADC value (persisted in SAF) */
volatile uint16_t g_adcCenterVoltage; /* Center ADC value (SAF)           */
volatile uint16_t g_adcMaxVoltage; /* Max ADC value (SAF)              */
volatile uint8_t g_adcReverse;      
/* DAC校正値 */
volatile uint8_t g_dacMinValue;
volatile uint8_t g_dacMaxValue;
/* ON/OFF */
volatile uint8_t g_status = 0;
/* ADJUST押下判定用 */
volatile uint16_t g_adjustPressCount; 
/* ADC変動時のLED点灯制御用 */
volatile uint16_t g_ledTimer; /* LED on-time countdown (ticks)    */
/* ADCの前回値 */
volatile uint16_t g_adcPrevValue; /* Previous ADC value for LED logic */
/* ADC実行中 */
volatile bool g_adc_exec = false;
/* 校正モード要求 */
volatile bool g_adjustFlag = false; /* Set when ADJUST held >= 1 s      */
/* ADC実行要求 */
volatile bool g_adcStartFlag = false;
/* ADC実行完了 */
volatile bool g_adcDoneFlag = false;

/*
    Main application
 */

/*
 * 指定時間LEDを点灯する
 *   100ms/loop, 10で1秒
 */
static bool solid_led(uint8_t loop) {
    bool adj_hold = true;
    IO_RA2_SetHigh();
    while (loop--) {
        if(IO_RA5_GetValue()){
            adj_hold = false;
        }
        CLRWDT();
        __delay_ms(100);
    }
    IO_RA2_SetLow();
    return adj_hold;
}

/*
 * 指定時間LEDを点滅させる
 *  interval_loop 点滅間隔
 *  loop ループ回数
 *  interval_loop * loop * 100 が時間
 */
static bool blink_led(uint8_t interval_loop, uint8_t loop) {
    bool adj_hold = true;
    for (uint8_t i = 0U; i < loop; i++) {
        IO_RA2_Toggle();
        if(IO_RA5_GetValue()){
            adj_hold = false;
        }
        for (uint8_t j = 0U; j < interval_loop; j++) {
            CLRWDT();
            __delay_ms(100);
        }
    }
    IO_RA2_SetLow();
    return adj_hold;
}

/*
 * 数字の桁毎にLEDを点滅させる
 */
static bool blink_number_led(uint8_t number) {
    uint8_t adj_count = 0;

    uint8_t loop;
    // 100の位
    loop = number / 100;
    for (uint8_t i = 0; i < loop; i++) {
        if (!IO_RA5_GetValue() && adj_count <= 10) {
            adj_count++;
            if (adj_count >= 2) {
                IO_RA2_SetLow();
                return true;
            }
        }
        blink_led(2U, 2U);
        number -= 100;
    }
    __delay_ms(500);
    // 10の位
    loop = number / 10;
    for (uint8_t i = 0; i < loop; i++) {
        if (!IO_RA5_GetValue() && adj_count <= 10) {
            adj_count++;
            if (adj_count >= 2) {
                IO_RA2_SetLow();
                return true;
            }
        }
        blink_led(2U, 2U);
        number -= 10;
    }
    __delay_ms(500);
    loop = number;
    for (uint8_t i = 0; i < loop; i++) {
        if (!IO_RA5_GetValue() && adj_count <= 10) {
            adj_count++;
            if (adj_count >= 2) {
                IO_RA2_SetLow();
                return true;
            }
        }
        blink_led(2U, 2U);
        number -= 10;
    }

    return false;
}

/*
 * SAFからボリューム補正値を取得する
 *   SAFから校正値を取得した場合はLED点灯(1800msec)
 *   SAFから校正値が取得できない場合はLED点滅(1800msec)
 */
static void load_correction_values() {

    // SAFから校正値の取得
    g_adcMinVoltage = (uint16_t) FLASH_Read(SAF_ADDR_ADC_MIN);
    g_adcCenterVoltage = (uint16_t) FLASH_Read(SAF_ADDR_ADC_CENTER);
    g_adcMaxVoltage = (uint16_t) FLASH_Read(SAF_ADDR_ADC_MAX);
    g_adcReverse = (uint8_t) FLASH_Read(SAF_ADDR_ADC_REVERSE);
    g_dacMinValue = (uint8_t) FLASH_Read(SAF_ADDR_DAC_MIN);
    g_dacMaxValue = (uint8_t) FLASH_Read(SAF_ADDR_DAC_MAX);

    /* ボリューム校正値が範囲内かチェックする */
    if (g_adcMinVoltage < g_adcCenterVoltage &&
            g_adcCenterVoltage < g_adcMaxVoltage &&
            g_adcMaxVoltage <= DEFAULT_ADC_MAX_VOL &&
            (g_adcMaxVoltage - g_adcMinVoltage) >= ADJUST_MIN_RANGE &&
            g_adcReverse <= 1) {
        solid_led(6U);
    } else {
        blink_led(1U, 6U);
        // 取得値が範囲外の場合はデフォルト値を採用する
        g_adcMinVoltage = DEFAULT_ADC_MIN_VOL;
        g_adcCenterVoltage = DEFAULT_ADC_CENTER_VOL;
        g_adcMaxVoltage = DEFAULT_ADC_MAX_VOL;
        g_adcReverse = 0;
    }

    // DAC最小値チェック
    if (g_dacMinValue >= DAC_ADJUSTMENT_MIN_MIN &&
            g_dacMinValue <= DAC_ADJUSTMENT_MIN_MAX) {
        solid_led(6U);
    } else {
        blink_led(1U, 6U);
        g_dacMinValue = DEFAULT_DAC_MIN;
    }

    // DAC最大値チェック
    if (g_dacMaxValue >= DAC_ADJUSTMENT_MAX_MIN &&
            g_dacMaxValue <= DAC_ADJUSTMENT_MAX_MAX) {
        solid_led(6U);
    } else {
        blink_led(1U, 6U);
        g_dacMaxValue = DEFAULT_DAC_MAX;
    }

}

/*
 * SAFにボリューム校正値を保存する
 */
static bool save_correction_values() {
    flash_data_t buf[6];
    buf[0] = g_adcMinVoltage;
    buf[1] = g_adcCenterVoltage;
    buf[2] = g_adcMaxVoltage;
    buf[3] = g_adcReverse;
    buf[4] = g_dacMinValue;
    buf[5] = g_dacMaxValue;
    uint8_t buf_length = 6U;

    // 消去・書き込みを行う前にアンロックキーをセットする
    NVM_UnlockKeySet(UNLOCK_KEY);

    if (FLASH_PageErase(SAF_ROW_ADDR) != NVM_OK) {
        // 削除失敗 100ms/1000ms
        blink_led(1U, 10U);
        return false;
    }
    if (FLASH_RowWrite(SAF_ROW_ADDR, buf, buf_length) != NVM_OK) {
        // 書込失敗 100ms/2500ms
        blink_led(1U, 25U);
        return false;
    }
    flash_address_t adr = SAF_ROW_ADDR;
    for (uint8_t i = 0; i < buf_length; i++, adr++) {
        if (FLASH_Read(adr) != buf[i]) {
            // 検証失敗 100ms/5000ms
            blink_led(1U, 50U);
            return false;
        }
    }

    return true;
}

/*
 * ADC結果の取得
 * ADC補正値反映結果を返却
 */
static uint16_t adc_get_collection_value(uint16_t rawValue) {
    if (g_adcReverse) {
        return DEFAULT_ADC_MAX_VOL - rawValue;
    }
    return rawValue;
}

/* 
 * ADC即時実行
 * ADCの生データを返却
 * 割り込み禁止状態でcallすること
 */
static uint16_t adc_exec() {
    ADC_ConversionDoneInterruptDisable();
    ADC_ConversionDoneInterruptFlagClear();

    ADC_ConversionStart();
    while (!ADC_IsConversionDone()) {
        CLRWDT();
    };
    uint16_t result = (uint16_t)ADC_ConversionResultGet();

    ADC_ConversionDoneInterruptEnable();

    return result;
}

/**
 * ADCの結果をDACの出力値に変換する
 *
 * @param adcVal  Raw ADC value (0-1023).
 * @return        DAC value as a 8-bit value (0-255).
 */
static uint8_t convert_adc_to_dac(uint16_t adcVal) {

    // 不感範囲判定
    
    if (g_status) {
        g_status = 0;
        // ON => OFF
        if (adcVal < (g_adcMinVoltage + NON_PERCEPTUAL_OFF_THRESHOLD)) 
            return 0;
        if (adcVal > (g_adcMaxVoltage - NON_PERCEPTUAL_OFF_THRESHOLD)) 
            return g_dacMaxValue;
    } else {
        // OFF => ON
        if (adcVal < (g_adcMinVoltage + NON_PERCEPTUAL_ON_THRESHOLD)) 
            return 0;
        if (adcVal > (g_adcMaxVoltage - NON_PERCEPTUAL_ON_THRESHOLD)) 
            return g_dacMaxValue;
    }
    g_status = 1;
    
    // 上限/下限の校正値に不感範囲を反映して最大値と最小値を決定する
    uint16_t minVolt = g_adcMinVoltage + NON_PERCEPTUAL_ON_THRESHOLD;
    uint16_t maxVolt = g_adcMaxVoltage - NON_PERCEPTUAL_ON_THRESHOLD;
    uint8_t dac_range = g_dacMaxValue - g_dacMinValue;
    
    // 最小値未満補正
    if (adcVal < minVolt)
        adcVal = minVolt;
    // 最大値超過補正
    if (adcVal > maxVolt)
        adcVal = maxVolt;
    
    // 電圧範囲をPWMデューティー範囲に変換
    uint16_t voltDiff = adcVal - minVolt;
    uint16_t voltRange = maxVolt - minVolt;

    // 比例計算 + 四捨五入のための補正 (+ voltRange / 2)
    // 最大値: (voltDiff * DAC_RANGE) + (voltRange / 2)
    //         <= (1023 * 255) + (1023 / 2) = 261376 (32bit範囲内)
    uint32_t numerator = (uint32_t) voltDiff * dac_range + (voltRange >> 1);
    uint16_t duty = g_dacMinValue + (uint16_t) (numerator / voltRange);

    if (duty > g_dacMaxValue) {
        duty = g_dacMaxValue;
    }

    return (uint8_t) duty;

}

/*
 * 電源ON時にDACの出力値を徐々に大きくする
 */
static void set_dac_value_fade(uint8_t value) {
    for (uint8_t i = 0; i <= value; i++) {
        DAC1_SetOutput(i);
        __delay_ms(4);
    }
}

/*
 *  ADC変換完了割り込み
 */
static void adc_isc() {

    g_adcDoneFlag = true;

}

/*
 * Timer0割り込み
 * 
 * 2ms(500KHz)毎に呼び出されてADCの起動とADJUSTピンの判定を行う
 * 
 */
static void timer0_isc() {

    // ADJUST_PINが一秒間押された場合、更正モードを有効にする
    if (!IO_RA5_GetValue()) {
        if (!g_adjustFlag) {
            if (g_adjustPressCount < TICKS_1S) {
                g_adjustPressCount++;
            } else {
                // ADC起動中ならADJUSTフラグを立てない
                if (!g_adc_exec) {
                    g_adjustFlag = true;
                    return;
                }
            }
        } else {
            g_adjustPressCount = 0;
        }
    }

    // ADC起動する
    if (!g_adjustFlag && !g_adc_exec) {
        g_adc_exec = true;
        g_adcStartFlag = true;
    }

    // LED点灯後0.5秒でLEDを消灯する
    if (g_ledTimer > 0U) {
        g_ledTimer--;
        if (!g_ledTimer) {
            IO_RA2_SetLow();
        }
    }

}

static bool adjust_mode_adc(void) {

    uint16_t newMin, newCenter, newMax;

    /* ----- Phase 1: capture center voltage -------------------------- */

    /* Blink LED at 0.5 s intervals for 5 s (10 half-periods) */
    if(blink_led(5U, 6U)){
        // 補正値をリセットして再起動する
        g_adcMinVoltage = 0x3FFU;
        g_adcCenterVoltage = 0x3FFU;
        g_adcMaxVoltage = 0x3FFU;
        g_adcReverse = 0;
        g_dacMinValue = 0xFFU;
        g_dacMaxValue = 0xFFU;
        save_correction_values();
        __delay_ms(100);
        RESET();
        return false;
    }
    blink_led(5U, 4U);
    newCenter = adc_exec();

    /* LED solid for 1 s                      */
    solid_led(10U);

    /* ----- Phase 2: capture maximum voltage ----------------------------- */

    /* Blink LED at 0.5 s intervals for 5 s (10 half-periods) */
    blink_led(5U, 10U);
    newMax = adc_exec();

    /* LED solid for 1 s                      */
    solid_led(10U);

    /* ----- Phase 3: capture minimum voltage -------------------------- */

    /* Blink LED at 0.5 s intervals for 5 s */
    blink_led(5U, 10U);
    newMin = adc_exec();

    /* ----- Validate calibration range --------------------------------- */
    if (newMin < newCenter && newCenter < newMax && (newMax - newMin) >= ADJUST_MIN_RANGE) {
        /* Valid: commit captured values to globals and persist to SAF    */
        g_adcMinVoltage = newMin;
        g_adcCenterVoltage = newCenter;
        g_adcMaxVoltage = newMax;
        g_adcReverse = 0;
        return true;

    } else if (newMax < newCenter && newCenter < newMin && (newMin - newMax) >= ADJUST_MIN_RANGE) {
        /* Valid: commit captured values to globals and persist to SAF    */
        g_adcMinVoltage = DEFAULT_ADC_MAX_VOL - newMin;
        g_adcCenterVoltage = DEFAULT_ADC_MAX_VOL - newCenter;
        g_adcMaxVoltage = DEFAULT_ADC_MAX_VOL - newMax;
        g_adcReverse = 1;
        return true;
        
    } else {
        // 最小＜中間＜最大になっていない
        // 2秒間点滅
        blink_led(1U, 20U);
    }
    return false;
}

static uint8_t adjust_mode_dac(uint8_t current_value, uint8_t range_min, uint8_t range_max) {
    // 現在地を2回表示
    blink_number_led(current_value);
    __delay_ms(1000);
    blink_number_led(current_value);
    __delay_ms(1000);

    uint8_t adc_value = (uint8_t) (adc_get_collection_value(adc_exec()) >> 2);
    if (adc_value < 0x55U || adc_value > 0xAAU) {
        // adcの結果が中央値以外ならここで終了

        // 2秒間点滅
        blink_led(1U, 20U);
        return 0;
    }

    uint8_t new_value = current_value;
    uint8_t noupdate_count = 0;

    // 1sec wait
    __delay_ms(1000);

    while (true) {

        // 現在値を出力
        if (blink_number_led(new_value)) {
            return new_value;
        }

        // adc結果取得
        adc_value = (uint8_t) (adc_get_collection_value(adc_exec()) >> 2);
        if (adc_value < 0x55U) {
            // デクリメント
            if (new_value > range_min) {
                new_value--;
            }
            noupdate_count = 0;
        } else if (adc_value > 0xAAU) {
            // インクリメント
            if (new_value < range_max) {
                new_value++;
            }
            noupdate_count = 0;
        } else {
            noupdate_count++;
        }

        __delay_ms(1000);

        // 10回連続で値が変更されなかったら変更せずに終了
        if (noupdate_count > 10) {
            // 2秒間点滅
            blink_led(1U, 20U);
            return 0;
        }

    }

}

static bool adjust_mode_dac_min(void) {
    uint8_t new_value = adjust_mode_dac(g_dacMinValue, DAC_ADJUSTMENT_MIN_MIN, DAC_ADJUSTMENT_MIN_MAX);
    if (new_value) {
        g_dacMinValue = new_value;
        return true;
    }
    return false;
}

static bool adjust_mode_dac_max(void) {
    uint8_t new_value = adjust_mode_dac(g_dacMaxValue, DAC_ADJUSTMENT_MAX_MIN, DAC_ADJUSTMENT_MAX_MAX);
    if (new_value) {
        g_dacMaxValue = new_value;
        return true;
    }
    return false;
}

/*
 * ADJUSTモード
 */
static void adjust_mode_run(void) {

    // DAC出力OFF
    DAC1_SetOutput(0U);

    // LED消灯
    IO_RA2_SetLow();

    // 現在のボリューム位置を取得してどのモードに入るか決定する
    uint8_t adc_value = (uint8_t) (adc_get_collection_value(adc_exec()) >> 2);
    bool is_save = false;
    if (adc_value <= 0x55U) {

        // DAC最小値修正
        is_save = adjust_mode_dac_min();

    } else if (adc_value <= 0xAAU) {

        // ADC校正
        is_save = adjust_mode_adc();

    } else {

        // DAC最大値修正
        is_save = adjust_mode_dac_max();

    }

    if (is_save) {
        if (save_correction_values()) {
            /* Success indicator: LED solid for 2 s                          */
            solid_led(20U);
        }
    }

    //    ADC_ConversionDoneInterruptEnable();

    g_adjustPressCount = 0U;
    g_adjustFlag = false;

}

int main(void) {

    // 初期化
    SYSTEM_Initialize();

    // 割り込みコールバック登録
    ADC_ConversionDoneCallbackRegister(adc_isc);
    TMR0_PeriodMatchCallbackRegister(timer0_isc);

    // SAFから校正値を取得する。
    load_correction_values();

    // 起動時のボリューム位置取得を取得してDAC出力
    g_adcPrevValue = adc_get_collection_value(adc_exec());
    set_dac_value_fade(convert_adc_to_dac(g_adcPrevValue));

    // Enable the Global Interrupts 
    INTERRUPT_GlobalInterruptEnable();

    // Disable the Global Interrupts 
    //INTERRUPT_GlobalInterruptDisable(); 

    // Enable the Peripheral Interrupts 
    INTERRUPT_PeripheralInterruptEnable();

    // Disable the Peripheral Interrupts 
    //INTERRUPT_PeripheralInterruptDisable(); 

    while (1) {

        CLRWDT();

        // ADC開始要求
        if (g_adcStartFlag) {
            INTERRUPT_GlobalInterruptDisable();
            g_adcStartFlag = false;
            ADC_ConversionStart();
            INTERRUPT_GlobalInterruptEnable();
        }

        // ADC終了
        if (g_adcDoneFlag) {
            INTERRUPT_GlobalInterruptDisable();
            g_adc_exec = false;
            g_adcDoneFlag = false;

            // ADC結果を取得してDACの出力値に変換
            uint16_t adc_value = adc_get_collection_value((uint16_t)ADC_ConversionResultGet());
            uint8_t dac_value = convert_adc_to_dac(adc_value);

            DAC1_SetOutput(dac_value);

            uint16_t adc_diff = (adc_value > g_adcPrevValue)
                    ? (adc_value - g_adcPrevValue)
                    : (g_adcPrevValue - adc_value);

            if (adc_diff > ADC_NOISE_THRESHOLD) {
                IO_RA2_SetHigh();
                g_ledTimer = TICKS_500MS;
                g_adcPrevValue = adc_value;
            }

            INTERRUPT_GlobalInterruptEnable();
        }

        // 校正モード
        if (g_adjustFlag) {
            INTERRUPT_GlobalInterruptDisable();
            adjust_mode_run();
            INTERRUPT_GlobalInterruptEnable();
        }

    }
}
