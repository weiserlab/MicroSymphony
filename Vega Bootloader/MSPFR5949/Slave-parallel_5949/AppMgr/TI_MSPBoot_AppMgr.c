
//
// Include files
//
#include "msp430.h"
#include "TI_MSPBoot_Common.h"
#include "TI_MSPBoot_AppMgr.h"
#include "crc.h"


//
//  Global variables
//
/*! Password sent by Application to force boot mode. This variable is in a fixed
    location and should keep same functionality and location in Boot and App */
#ifdef __IAR_SYSTEMS_ICC__
#           pragma location="RAM_PASSWORD"
    __no_init uint16_t  PassWd;
#       elif defined (__TI_COMPILER_VERSION__)
extern uint16_t  PassWd;
#endif

/*! Status and Control byte. This variable is in a fixed
 location and should keep same functionality and location in Boot and App */
#ifdef __IAR_SYSTEMS_ICC__
#           pragma location="RAM_STATCTRL"
    __no_init uint8_t  StatCtrl;
#       elif defined (__TI_COMPILER_VERSION__)
extern uint8_t  StatCtrl;
#endif

//
//  Local function prototypes
//
static tBOOL TI_MSPBoot_AppMgr_BootisForced(void);

/******************************************************************************
 *
 * @brief   Checks if an Application is valid
 *  Depending on CONFIG_APPMGR_APP_VALIDATE, this function can validate app by:
 *  CONFIG_APPMGR_APP_VALIDATE  |    Function
 *          2                   | Check if reset vector is different from 0xFFFF
 *          other               | Application is expected to be valid
 *
 *
 * @return  TRUE_t if application is valid,
 *          FALSE_t if applicaiton is invalid
 *****************************************************************************/
#if (CONFIG_APPMGR_APP_VALIDATE == Level_1)
static tBOOL TI_MSPBoot_AppMgr_AppisValid(void)
{
    // Check if Application Reset vector exists
    if (*(volatile uint16_t *)(&_Appl_Reset_Vector) != 0xFFFF)
    {
        return TRUE_t;
    }
    else
    {
        return FALSE_t;
    }
}
#elif (CONFIG_APPMGR_APP_VALIDATE == Level_2)
static tBOOL TI_MSPBoot_AppMgr_AppisValid(void)
{
    extern uint32_t _Appl_Start_Memory;     // Address of Download area start
    extern uint32_t _Flex_Start;
    extern uint32_t _Appl_CRC_Size1;
    extern uint32_t _Appl_CRC_Size2;
    extern uint32_t _Appl_Checksum;   // Address of A area checksum
    
    uint32_t i;
    uint8_t* data_ptr;
    static uint16_t result = 0;
    
    CRCINIRES = 0xFFFF;
    data_ptr = (uint8_t*) &_Appl_Start_Memory;
    for(i = 0 ; i < (uint32_t) &_Appl_CRC_Size1 ; i++) CRCDIRB_L = *data_ptr++;
    if ((uint32_t) &_Flex_Start == 0x10000)
    	{
	data_ptr = (uint8_t*) 0x10000;
	for(i = 0 ; i < (uint32_t) &_Appl_CRC_Size2 ; i++) CRCDIRB_L = *data_ptr++;
    	}
    result = CRCINIRES;
    if (result != __data20_read_short((unsigned long)&_Appl_Checksum)) return FALSE_t;
    else return TRUE_t;
}
#else
// Always assume that Application is valid
#warning "Application is not validated"
#define TI_MSPBoot_AppMgr_AppisValid()   TRUE_t
#endif


/******************************************************************************
 *
 * @brief   Decides whether to stay in MSPBoot or if device should jump to App
 *  MSPBoot:  Boot mode is forced by a call from App, OR
 *          Boot mode is forced by an external event (button pressed), OR
 *          Application is invalid
 *  App:    Boot mode is not forced, AND
 *          Application is valid
 *
 * @return  TRUE_t if application is valid and should be executed
 *          FALSE_t if we must stay in Boot mode
 *****************************************************************************/
tBOOL TI_MSPBoot_AppMgr_ValidateApp(void)
{
    // if(TI_MSPBoot_AppMgr_AppisValid() == TRUE_t){
    //     // P4OUT &= ~BIT6;     // reset the pin
    //     P1OUT |= BIT0;
    // }
    // else{
    //     // P4OUT |= BIT6;     // set the pin
    //     P1OUT &= ~BIT0;     // reset the pin
    // }
    if ((TI_MSPBoot_AppMgr_BootisForced() == FALSE_t) && 
        (TI_MSPBoot_AppMgr_AppisValid() == TRUE_t))
    {
        return TRUE_t;  // Boot is not forced and App is valid
    }
    else
    {
        return FALSE_t; // Boot is forced or App is invalid
    }
}


/******************************************************************************
 *
 * @brief   Jump to Appplication 
 *          A Reset is forced in order to validate Application after Reset
 *          Software BOR is used on devices supporting this feature,
 *          other devices use a PUC by an invalid watchdog write
 *          Check HW_RESET_BOR 
 *
 * @return  None
 *****************************************************************************/
void TI_MSPBoot_AppMgr_JumpToApp(void)
{
#if defined (HW_RESET_BOR)
    // Force a Software BOR
    PMMCTL0 = PMMPW | PMMSWBOR;
#else
    // Force a PUC reset by writing incorrect Watchdog password
    WDTCTL = 0xDEAD;
#endif
}


/******************************************************************************
 *
 * @brief   Checks if Boot mode is forced
 *          Boot mode is forced by an application call sending a request and 
 *          password, or by an external event such as a button press
 *
 * @return  TRUE_t Boot mode is forced
 *          FALSE_t Boot mode is not forced
 *****************************************************************************/
static tBOOL TI_MSPBoot_AppMgr_BootisForced(void)
{
    tBOOL ret = FALSE_t;

    // Check if application is requesting Boot mode and password is correct
    if (((StatCtrl & BOOT_APP_REQ) != 0) && (PassWd == BSL_PASSWORD))
    {
        ret = TRUE_t;
    }

    // Check for an external event such as S2 (P1.2) button in Launchpad
    __delay_cycles(10000);   // Wait for pull-up to go high
    //If S2 button (P1.1) is pressed, force BSL
    if (HW_ENTRY_CONDITION)
    // if ((HW_ENTRY_CONDITION) || (P1IN & BIT3) )
    {
        ret = TRUE_t;
    }


    // Clear Status and Control byte and password
    PassWd = 0;
    StatCtrl = 0;
    return ret;
}

