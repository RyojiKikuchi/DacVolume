 /**
 * Generated Driver File
 * 
 * @file pins.c
 * 
 * @ingroup  pinsdriver
 * 
 * @brief This is generated driver implementation for pins. 
 *        This file provides implementations for pin APIs for all pins selected in the GUI.
 *
 * @version Driver Version 3.0.0
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

#include "../pins.h"


void PIN_MANAGER_Initialize(void)
{
   /**
    LATx registers
    */
    LATA = 0b00000000;

    /**
    TRISx registers
     * RA5 1 input  ADJ(pull-up)
     * RA4 1 input  VOL(ADC)
     * RA3 0        MCLR
     * RA2 0 output LED
     * RA1 1 input  Vref
     * RA0 0 output Vout(DAC)
    */
    TRISA = 0b00110010;

    /**
    ANSELx registers
     * RA5 0 digital ADJ
     * RA4 1 analog  VOL(ADC)
     * RA3 0 digital MCLR
     * RA2 0 digital LED
     * RA1 1 analog  Vref
     * RA0 1 analog  Vout(DAC)
    */
    ANSELA = 0b00010011;  

    /**
    WPUx registers
     * RA5 1 pullup
     * RA4 0
     * RA3 0
     * RA2 0
     * RA1 0
     * RA0 0
    */
    WPUA = 0b00100000;
  
    /**
    ODx registers
     * RA0
     * RA1
     * RA2
     * RA3
     * RA4
     * RA5
    */
    ODCONA = 0b00000000;
    /**
    SLRCONx registers
    */
    SLRCONA = 0b00110111;
    
    /**
    INLVLx registers
    */
    INLVLA = 0b00001000;

    /**
    PPS registers
    */

    /**
    APFCON registers
    */

   /**
    IOCx registers 
    */
    IOCAP = 0x0;
    IOCAN = 0x0;
    IOCAF = 0x0;


}
  
void PIN_MANAGER_IOC(void)
{
}
/**
 End of File
*/