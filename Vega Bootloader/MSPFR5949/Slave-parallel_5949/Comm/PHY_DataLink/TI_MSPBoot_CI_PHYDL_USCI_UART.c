// Include files
//
#include "msp430.h"
#include "TI_MSPBoot_Common.h"
#include "TI_MSPBoot_CI.h"
#include "TI_MSPBoot_AppMgr.h"

//Need to change
//Simple change: Realize functions same with the example code. You can search "Simple change" to find where need to be changed.
//You can make more changes to add more functions based on your applicaiton 
//
//  Configuration checks
//
#ifndef MSPBoot_CI_UART
#   error "This file uses the UART interface"
#endif
// Baudrate configuration, check User's guide for table with values of 
// UCBRx, UCBRSx, UCBRFx and UCOS16


static void set_uart_baudrate(uint8_t baudcode) {

	uint16_t UCOSx = UCOS16;
	uint16_t UCBRx = 8;
	uint16_t UCBRFx = UCBRF_10;
	uint8_t UCBRSx = 0xF7;
    
    switch (baudcode) {
        case 0x60: // 9600
            UCBRx = 52; UCBRFx = UCBRF_1; UCBRSx = 0x49; UCOSx = UCOS16;
            break;
        case 0x61: // 19200
            UCBRx = 26; UCBRFx = UCBRF_0; UCBRSx = 0xD6; UCOSx = UCOS16;
            break;
		case 0x62: // 38400
			UCBRx = 13; UCBRFx = UCBRF_0; UCBRSx = 0x45; UCOSx = UCOS16;
			break;
        case 0x63: // 57600
            UCBRx = 8; UCBRFx = UCBRF_10; UCBRSx = 0xF7; UCOSx = UCOS16;
            break;
        case 0x64: // 115200
            UCBRx = 4; UCBRFx = UCBRF_5; UCBRSx = 0x55; UCOSx = UCOS16;
            break;
        case 0x65: // 230400
            UCBRx = 2; UCBRFx = UCBRF_2; UCBRSx = 0xBB; UCOSx = UCOS16;
            break;
        case 0x66: // 460800
            UCBRx = 17; UCBRFx = UCBRF_0; UCBRSx = 0x4A; UCOSx = 0;
            break;
        case 0x67: // 921600
            UCBRx = 8; UCBRFx = UCBRF_0; UCBRSx = 0xD6; UCOSx = 0;
			// No oversampling
            break;
        default: // Default to 57600
            break;
    }

    UCA1BRW = UCBRx;
	UCA1MCTLW = UCOSx | (UCBRSx << 1) | UCBRFx;
}

//
//  Type Definitions
//
/*! State machine used by this communication interface 
 *   USCI doesn't require too much control, so we only check for an idle state
 *   and when receiving a packet 
 */
typedef enum {
    USCI_STATE_IDLE=0,          /*! Initialized state waiting for start */
    USCI_STATE_RECEIVING,       /*! Receiving packet */
}USCI_State_Machine_e;


//
//  Global variables
//
//    Note that these variables are assigned to a specific location in RAM
//    since they can also be used during application.
//    If not used by application, the location doesn't matter.
//
/*! Pointer to the Communication Interface callback structure 
 *   The NWK layer will define its callback structure and pass the pointer to
 *   this layer. An application can also declare its own structure and pass it
 *   to this layer in order to use the same interface.
 *   Note that the declaration for IAR and CCS is different.
 */
#ifdef __IAR_SYSTEMS_ICC__
#pragma location="RAM_CICALLBACK"
__no_init t_CI_Callback  * CI_Callback_ptr;
#elif defined (__TI_COMPILER_VERSION__)
extern t_CI_Callback  * CI_Callback_ptr;
#endif

/*! State machine used by this interface. 
 *   Note that the declaration for IAR and CCS is different.
 */
#ifdef __IAR_SYSTEMS_ICC__
#pragma location="RAM_CISM"
__no_init USCI_State_Machine_e CI_State_Machine;
#elif defined (__TI_COMPILER_VERSION__)
extern USCI_State_Machine_e CI_State_Machine;
#endif


//
//  Local function prototypes
//

//
//  Function declarations
//
/******************************************************************************
*
 * @brief   Initializes the USCI UART interface
 *  - Sets corresponding GPIOs for UART functionality
 *  - Resets and then configures the UART module
 *  - Initializes the UART state machine
 *  - Initializes the UART callbacks
 *  The NWK layer will define its callback structure and pass the pointer to
 *   this function. An application can also declare its own structure and call
 *   this function in order to use the same interface.
 *
 *  @param  CI_Callback     Pointer to Communication interface callbacks
 *
 * @return  none
 *****************************************************************************/
// Assumptions: MSP430FR5969, UCA1 on P2.5 (TX) and P2.6 (RX).
#define TX_PIN    BIT5
#define RX_PIN    BIT6

static inline void bus_tx_enable(void) {
    // Map P2.5 to UCA1TXD just before sending
    P2SEL1 |=  TX_PIN;   // UCA1 function = SEL1:1, SEL0:0
    P2SEL0 &= ~TX_PIN;
}

static inline void bus_tx_disable(void) {
    while (UCA1STATW & UCBUSY) { } // wait for stop bit
    // Add delay to ensure last bits are fully transmitted at high baud rates
    __delay_cycles(20);  // ~2 bit times at 921600 baud
    // Return P2.5 to Hi-Z GPIO input
    P2SEL1 &= ~TX_PIN;
    P2SEL0 &= ~TX_PIN;
    P2DIR  &= ~TX_PIN;
    P2REN  &= ~TX_PIN;
}


// void TI_MSPBoot_CI_PHYDL_Init(t_CI_Callback * CI_Callback, uint8_t br_zero, uint8_t br_one, uint8_t oversample, uint8_t brf, uint8_t brs)
void TI_MSPBoot_CI_PHYDL_Init(t_CI_Callback * CI_Callback, uint8_t baudcode)
{
    UCA1CTLW0 = UCSWRST | UCSSEL_2;            // hold eUSCI_A1, SMCLK

    // original pin select
    P2SEL0 &= ~(BIT5 | BIT6);                   // P2.5/2.6 → eUSCI_A1
    P2SEL1 |=  (BIT5 | BIT6);

    // NEW: keep RX mapped, but tri-state TX by default
    P2SEL1 &= ~TX_PIN;                   // deselect UCA1TXD on P2.5
    P2SEL0 &= ~TX_PIN;
    P2DIR  &= ~TX_PIN;                   // input = Hi-Z
    P2REN  &= ~TX_PIN;                   // no internal pull
    // RX stays selected by the lines above (P2.6 SEL1=1, SEL0=0)

    // Baudrate configuration
    set_uart_baudrate(baudcode);

    UCA1CTLW0 &= ~UCSWRST;                      // enable eUSCI

    CI_Callback_ptr  = CI_Callback;
    CI_State_Machine = USCI_STATE_IDLE;
}

/******************************************************************************
 *
 * @brief   Disables the USCI module
 *
 * @return  none
 *****************************************************************************/
void TI_MSPBoot_CI_PHYDL_disable(void)
{
    UCA1CTLW0 |= UCSWRST; //Simple change
}

/******************************************************************************
 *
 * @brief   Enables the USCI module
 *
 * @return  none
 *****************************************************************************/
void TI_MSPBoot_CI_PHYDL_reenable(void)
{
    UCA1CTLW0 &= ~UCSWRST; //Simple change
}

/******************************************************************************
*
 * @brief   Polls for USCI flags
 *  - Checks the RX, TX flags
 *  - Calls the corresponding Callback functions and informs 
 *    higher level layers, so they can handle data properly
 *
 * @return  none
 *****************************************************************************/
void TI_MSPBoot_CI_PHYDL_Poll(void)
{
    uint8_t temp;
    uint8_t flag_ifg2;
   
    // Read flags at the beginning of function 
    // New detected flags will be handled in the next poll
    flag_ifg2 = UCA1IFG;  //Simple change

    if (flag_ifg2 & UCRXIFG)  // Check the RX flag 
    {
        temp = UCA1RXBUF;     // Get received byte //Simple change
        if (CI_Callback_ptr->RxCallback != NULL)
        {
            // Call RX Callback (if valid) and send byte to upper layer
            CI_Callback_ptr->RxCallback(temp);
        }
    }
    else if (flag_ifg2 & UCTXIFG)     // Check for TX flag
    {
        // Send ACK after byte reception
        if (CI_Callback_ptr->TxCallback != NULL)
        {
            // Call TXCallback (if valid) and get byte to send from upper layer
            CI_Callback_ptr->TxCallback(&temp);     
            UCA1TXBUF = temp;     // Send byte //Simple change
        }
    }

    // If necessary and if enough space is available, UCA1STAT can be checked
    // for errors and ErrorCallback can be called

}   // TI_MSPBoot_CI_PHYDL_Poll


/******************************************************************************
*
 * @brief   Sends a byte via UART. Function added for UART CI. 
 *  
 *  @param  byte     byte to send
 *
 * @return  none
 *****************************************************************************/
void TI_MSPBoot_CI_PHYDL_TXByte(uint8_t byte)
{
    bus_tx_enable();                 // NEW: claim the bus
    while (!(UCA1IFG & UCTXIFG)) { } // TX ready
    UCA1TXBUF = byte;                // send
    bus_tx_disable();                // NEW: release bus to Hi-Z
}

//  Constant table
//
/*! Peripheral Interface vectors for Application:
 *  These vectors are shared with application and can be used by the App to
 *   initialize and use the Peripheral interface
 *   Note that the declaration for IAR and CCS is different.
 */

#ifdef __IAR_SYSTEMS_ICC__
 #pragma location="BOOT_APP_VECTORS"
 __root const uint32_t Boot2App_Vector_Table[] =
#elif defined (__TI_COMPILER_VERSION__)
 #pragma DATA_SECTION(Boot2App_Vector_Table, ".BOOT_APP_VECTORS")
 #pragma RETAIN(Boot2App_Vector_Table)
 const uint32_t Boot2App_Vector_Table[] =
#endif
{
    (uint32_t) &TI_MSPBoot_CI_PHYDL_Init,       /*! Initialization routine */
    (uint32_t) &TI_MSPBoot_CI_PHYDL_Poll,       /*! Poll routine */
    (uint32_t) &TI_MSPBoot_CI_PHYDL_TXByte      /*! Transmit byte */
};
