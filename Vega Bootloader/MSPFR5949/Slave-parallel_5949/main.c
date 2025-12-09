/*
 * \file   main.c
 *
 * \brief  Main routine for the bootloader for FR5969
 *
 */

//                                  
//               MSP430FR5969                 MSP430F5969
//                   Host                        Target
//             -----------------          -----------------
//            |        P2.6/RXD |<-----+->|P2.5/TXD         |
//            |                 |         |                 | 
//            |        P2.5/TXD |<-+----->|P2.6/RXD         |
//     LED <--|P1.0             |         |             P1.0|--> LED 
//     LED <--|P4.6          GND|<-+----->|GND          P4.6|--> LED
//             -----------------          -----------------
//

//Need to change
//Simple change: Realize functions same with the example code. You can search "Simple change" to find where need to be changed.
//You can make more changes to add more functions based on your applicaiton

// change baud at CONFIG_CI_PHYDL_UART_BAUDRATE
// Path: Slave-parallel/TI_MSPBoot_Config.h

// assign the slave address to SLAVE_ADDRESS_MASK variable at Slave-parallel_5959/TI_MSPBoot_Config.h

//  Include files
//
#include "msp430.h"
#include "TI_MSPBoot_Common.h"
#include "TI_MSPBoot_CI.h"
#include "TI_MSPBoot_MI.h"
#include "TI_MSPBoot_AppMgr.h"
// #include "stdbool.h"


//
//  Local function prototypes
//
static void clock_init(void);
static void HW_init(void);
static void MPU_init(void);


/******************************************************************************
 *
 * @brief   Main function
 *  - Initializes the MCU
 *  - Selects whether to run application or bootloader
 *  - If bootloader:
 *      - Initializes the peripheral interface
 *      - Waits for a command
 *      - Sends the corresponding response
 *  - If application:
 *      - Jump to application
 *
 * @return  none
 *****************************************************************************/
int main_boot( void )
{
    // Stop watchdog timer to prevent time out reset
    WDTCTL = WDTPW + WDTHOLD;

    // Initialize MPU
    MPU_init();
    
    // Initialize MCU
    HW_init();
    clock_init();

    P1DIR |= BIT0;     // Used for debugging purposes to show entry to MSPBoot
    P1OUT &= ~BIT0;     // clear the pin

    // P4DIR |= BIT6;     // Used for debugging purposes to show entry to MSPBoot
    // P4OUT |= BIT6;     // set the pin

    __delay_cycles(800000);



    // Validate the application and jump if needed given boot not forced
    if (TI_MSPBoot_AppMgr_ValidateApp() == TRUE_t){
        // P4OUT &= ~BIT6;
        P1OUT |= BIT0;
        TI_MSPBoot_APPMGR_JUMPTOAPP();
    }
    else {
        P1OUT |= BIT0;      // set the bit
        __delay_cycles(4000000);
        P1OUT ^= BIT0;
    }


    TI_MSPBoot_CI_Init(0x63);      // Initialize the UART Communication Interface with 57600 baudrate

    uint8_t ret;

    while(1)
    {
        // Poll PHY and Data Link interface for new packets
        TI_MSPBoot_CI_PHYDL_Poll();

        ret = TI_MSPBoot_CI_Process(); 
        // If a new packet is detected, process it
        if (ret == RET_JUMP_TO_APP)
        {
            // Wait for any pending UART transmission to complete
            __delay_cycles(1000);  // ~11 byte times at 921600 baud
            TI_MSPBoot_AppMgr_JumpToApp();
        }

#ifdef NDEBUG
        // Feed the dog every ~1000ms
        WATCHDOG_FEED();
#endif
    }


}

/******************************************************************************
 *
 * @brief   Initializes the MSP430 Clock
 *
 * @return  none
 *****************************************************************************/
//inline static void clock_init(void)
static void clock_init(void)
{
    CSCTL0_H = CSKEY_H;
    CSCTL1 = DCOFSEL_6;                         // Set DCO = 8Mhz
    CSCTL2 = SELA__VLOCLK + SELM__DCOCLK + SELS__DCOCLK;  // set ACLK = VLO
                                                          // MCLK=SMCLK=DCO
    
#if (MCLK==1000000)
    CSCTL3 = DIVA__1 + DIVS__8 + DIVM__8;                 // Divide DCO/8
#elif (MCLK==4000000)
    CSCTL3 = DIVA__1 + DIVS__2 + DIVM__2;                 // Divide DCO/2
    #elif (MCLK==8000000)
    CSCTL3 = DIVA__1 + DIVS__1 + DIVM__1;                 // Divide DCO/1
#else
#error "Please define a valid MCLK or add configuration"
#endif

}


/******************************************************************************
 *
 * @brief   Initializes the basic MCU HW
 *
 * @return  none
 *****************************************************************************/
static void HW_init(void)
{
    // USER: Initialize boot enable pin
    // check TI_MSPBoot_Config.h and AppMgr/TI_MSPBoot_AppMgr.c for modifications
    // slave bit address set at variable SLAVE_ADDRESS_MASK
    P1DIR &= ~BIT3;  // Set P1.3 as input
    P1REN |= BIT3;   // Enable pull-up resistor
    P1OUT &= ~BIT3;      // Pull-down: makes default logic LOW

    PM5CTL0 &= ~LOCKLPM5;

}

/******************************************************************************
 *
 * @brief   Initializes the Memory Protection Unit of FR5969
 *          This allows for HW protection of Bootloader area
 *
 * @return  none
 *****************************************************************************/
static void MPU_init(void)
{
    // These calculations work for FR5969 (check user guide for MPUSEG values)
    //  Border 1 = Start of bootloader
    //  Border 2 = 0x10000
    //  Segment 1 = 0x4400 - Bootloader
    //  Segment 2 = Bootloader - 0xFFFF
    //  Segment 3 = 0x10000 - 0x23FFF
    //  Segment 2 is write protected and generates a PUC

    MPUCTL0 = MPUPW;                        // Write PWD to access MPU registers
    MPUSEGB1 = (BOOT_START_ADDR) >> 4;      // B1 = Start of Boot; B2 = 0x10000
    MPUSEGB2 = (0x10000) >> 4;
    MPUSAM &= ~MPUSEG2WE;                   // Segment 2 is protected from write
    MPUSAM |= MPUSEG2VS;                     // Violation select on write access
    MPUCTL0 = MPUPW | MPUENA;                 // Enable MPU protection
    MPUCTL0_H = 0x00;                         // Disable access to MPU registers

}
