#include "msp430.h"
#include <stdint.h>
#include "TI_MSPBoot_Mgr_Vectors.h"

static void clock_init(void)
{
    CSCTL0_H = CSKEY_H;
    CSCTL1 = DCOFSEL_6;                         // Set DCO = 8Mhz
    CSCTL2 = SELA__VLOCLK + SELM__DCOCLK + SELS__DCOCLK;  // set ACLK = VLO
                                                          // MCLK=SMCLK=DCO
    
    CSCTL3 = DIVA__1 + DIVS__1 + DIVM__1;                 // Divide DCO/1
}

int main( void )
{
  // Stop watchdog timer to prevent time out reset
  WDTCTL = WDTPW + WDTHOLD;

    // clock_init();


    P1DIR |= BIT0;     // Used for debugging purposes to show entry to MSPBoot
    P1OUT |= BIT0; // set the pin P1.0     

    P1DIR |= BIT5;
    P1OUT |= BIT5; // set the pin P1.0     

    PM5CTL0 &= ~LOCKLPM5;

    
    while(1){
        __delay_cycles(800000);
        P1OUT ^= BIT0;
        P1OUT ^= BIT5;
    }

  return 0;
}

// #include <msp430.h>
// #include "TI_MSPBoot_Mgr_Vectors.h"
// #include <stdint.h>


// int main(void)
// {
//   WDTCTL = WDTPW | WDTHOLD;       // Stop WDT

//   // Configure GPIO
//   // P1OUT &= ~BIT3;                 // Clear P1.0 output latch
//   P1DIR |= BIT3;                  // Set P1.0 to output
  

//   // P4OUT &= ~BIT6;                 // Clear P4.6 output latch
//   // P4DIR |= BIT6;                  // Set P4.6 to output

//   // S2 button for reset
//   // // Start P1.1 (S2 button) as interrupt with pull-up
//   //   P1OUT |= BIT1;
//   //   P1REN |= BIT1;
//   //   P1IES |= BIT1;
//   //   P1IE |= BIT1;
//   //   P1IFG &= ~BIT1;

//   PM5CTL0 &= ~LOCKLPM5;           // Disable GPIO high-impedance mode

//     P1OUT ^= BIT3;                // Toggle P1.0
//   while(1)
//   {
//     P1OUT ^= BIT3;                // Toggle P1.0
//     // P4OUT ^= BIT6;                // Toggle P4.6
//     __delay_cycles(400000);

//   }
// }


// /******************************************************************************
//  *
//  * @brief   Port 1 Interrupt service routine
//  *  Forces Boot mode when button S2 (P1.1) is pressed
//  *   Note that this function prototype should be accessible by TI_MSPBoot_Mgr_Vectors.c
//  *
//  * @return  none
//  *****************************************************************************/
// // this should be uncommented ********
// #pragma vector = PORT1_VECTOR
// __interrupt void Port_Isr(void)
// {
//     P1IFG = 0;
//     TI_MSPBoot_JumpToBoot();
// }


/* TI's app1 code*/

// /*
//  * \file   main.c
//  *
//  * \brief  Sample application for FR5969 using MSPBoot
//  *      This example places application in the appropiate area
//  *      of memory (check linker file) and shows how to use interrupts 
// */

// //Need to change
// //Simple change: Realize functions same with the example code. You can search "Simple change" to find where need to be changed.
// //You can make more changes to add more functions based on your applicaiton

// #include "msp430.h"
// #include <stdint.h>
// #include "TI_MSPBoot_Mgr_Vectors.h"

// //force something to be big enough to need upper memory, for testing
// // const unsigned char testArray[20000] = {0xAA};
// // volatile unsigned char test;

// /******************************************************************************
//  *
//  * @brief   Main function
//  *  This example application performs the following functions:
//  *  - Toggle LED1 (P4.6) at startup (to indicate App1 execution)
//  *  - Toggles LED1 using a timer periodic interrupt (demonstrates vector redirection)
//  *  - Forces Boot mode when button S2 (P1.1) is pressed (demonstrates vector
//  *      redirection and Boot Mode call
//  *
//  * @return  none
//  *****************************************************************************/
// int main( void )
// {
//   // Stop watchdog timer to prevent time out reset
//   WDTCTL = WDTPW + WDTHOLD;

//   //code so that tstArray doesn't get optimized out
// //  test = testArray[5];
// //  test++;
// //Simple change start
//     // Toggle LED1 in P4.6
//     P4DIR |= BIT6;
//     P4OUT |= BIT6;
//     PM5CTL0 &= ~LOCKLPM5; //unlock GPIOs so settings take effect
//     __delay_cycles(2000000);
//     P4OUT &= ~BIT6;
//     __delay_cycles(2000000);
//     P4OUT |= BIT6;
//     __delay_cycles(500000);
//     P4OUT &= ~BIT6;

//     // Start P1.1 (S2 button) as interrupt with pull-up
//     P1OUT |= BIT1;
//     P1REN |= BIT1;
//     P1IES |= BIT1;
//     P1IE |= BIT1;
//     P1IFG &= ~BIT1;

//     // Start Timer interrupt
//     TA0CCTL0 = CCIE;                             // CCR0 interrupt enabled
//     TA0CCR0 = 2000-1;
//     TA0CTL = TASSEL_1 + MC_1;                  // ACLK, upmode
// //Simple change end
//     __bis_SR_register(LPM3_bits + GIE);
//     __no_operation();


//   return 0;
// }

// /******************************************************************************
//  *
//  * @brief   Timer A Interrupt service routine
//  *  This routine simply toggles an LED but it shows how to declare interrupts
//  *   in Application space
//  *   Note that this function prototype should be accessible by 
//  *   TI_MSPBoot_Mgr_Vectors.c
//  *
//  * @return  none
//  *****************************************************************************/
// //Simple change start
// #pragma vector = TIMER0_A0_VECTOR
// __interrupt void Timer_A (void)
// {
//   P4OUT ^= BIT6;                            // Toggle P4.6
//   TA0CCTL0 &= ~CCIFG;
// }

// /******************************************************************************
//  *
//  * @brief   Port 1 Interrupt service routine
//  *  Forces Boot mode when button S2 (P4.1) is pressed
//  *   Note that this function prototype should be accessible by TI_MSPBoot_Mgr_Vectors.c
//  *
//  * @return  none
//  *****************************************************************************/
// #pragma vector = PORT1_VECTOR
// __interrupt void Port_Isr(void)
// {
//     P1IFG = 0;
//     TI_MSPBoot_JumpToBoot();
// }
// //Simple change end
