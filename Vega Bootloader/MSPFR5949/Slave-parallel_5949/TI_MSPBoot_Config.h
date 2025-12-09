/*
 * \file   TI_MSPBoot_Config.h
 *
 * \brief  Contains definitions used to configure the bootloader for FR5969
 *         supporting the BSL-Based configuration using UART
 *
 */


#ifndef __TI_MSPBoot_CONFIG_H__
#define __TI_MSPBoot_CONFIG_H__

//Need to change
//Simple change: Realize functions same with the example code. You can search "Simple change" to find where need to be changed.
//You can make more changes to add more functions based on your applicaiton 
//
// Include files
//
#define MCLK                    (8000000L)  /*! MCLK Frequency in Hz */

/*! Watchdog feed sequence */
#define WATCHDOG_FEED()         {WDTCTL = (WDTPW+WDTCNTCL+WDTSSEL__VLO+WDTIS__8192);}
/*! Hardware entry condition in order to force bootloader mode */
#define HW_ENTRY_CONDITION      (P1IN & BIT3)   //Simple change
// Slaves: BIT0, BIT1, BIT2, BIT3
// Boards: BIT4, BIT5, BIT6
// Reply: BIT7
#define SLAVE_ADDRESS_MASK      (BIT0)  /*! Mask for slave address, used to check if packet is for this slave */
#define BOARD_ADDRESS_MASK      (BIT5)

/*! HW_RESET_BOR: Force a software BOR in order to reset MCU 
     Not all MCUs support this funcitonality. Check datasheet/UG for details.
    HW_RESET_PUC: Force a PUC in order to reset MCU 
     An invalid write to watchdog will force the PUC.
 */
#define HW_RESET_BOR


//
// Configuration MACROS
//
/* MSPBoot_SIMPLE :
 *  If MSPBoot_SIMPLE is defined for the project (in this file or in project 
 *    preprocessor options), the following settings will apply:
 *
 *  NDEBUG = defined: Debugging is disabled, ASSERT_H function is ignored,
 *      GPIOs are not used for debugging
 *
 *  CONFIG_APPMGR_APP_VALIDATE =1: Application is validated by checking its reset
 *      vector. If the vector is != 0xFFFF, the application is valid
 *
 *  CONFIG_MI_MEMORY_RANGE_CHECK = undefined: Address range of erase and Write
 *      operations are not validated. The algorithm used by SimpleProtocol
 *      doesn't write to invalid areas anyways
 *
 *  CONFIG_CI_PHYDL_COMM_SHARED = undefined: Communication interface is not used 
 *      by application. Application can still initialize and use same interface 
 *      on its own
 *
 *  CONFIG_CI_PHYDL_ERROR_CALLBACK = undefined: The communication interface  
 *      doesn't report errors with a callback
 *
 *  CONFIG_CI_PHYDL_I2C_TIMEOUT = undefined: The communication interface doesn't 
 *      detect timeouts. Only used with I2C
 *
 *  CONFIG_CI_PHYDL_START_CALLBACK = undefined: Start Callback is not needed
 *      and is not implemented to save flash space. Only used with I2C
 *
 *  CONFIG_CI_PHYDL_STOP_CALLBACK = undefined: Stop callback is not needed
 *      and is not implemented to save flash space. Only used with I2C
 *
 *  CONFIG_CI_PHYDL_I2C_SLAVE_ADDR = 0x40. I2C slave address of this device 
 *      is 0x40
 *
 *  CONFIG_CI_PHYDL_UART_BAUDRATE = 9600. UART baudrate is 9600. Only used with
 *      UART
 */
#if defined(MSPBoot_SIMPLE)
//#   define NDEBUG
#   define CONFIG_APPMGR_APP_VALIDATE    (1)
#   undef CONFIG_MI_MEMORY_RANGE_CHECK
#   undef CONFIG_CI_PHYDL_COMM_SHARED
#   undef CONFIG_CI_PHYDL_ERROR_CALLBACK
#   undef CONFIG_CI_PHYDL_I2C_TIMEOUT
#   ifdef MSPBoot_CI_UART
#       undef CONFIG_CI_PHYDL_START_CALLBACK
#       undef CONFIG_CI_PHYDL_STOP_CALLBACK
// #       define CONFIG_CI_PHYDL_UART_BAUDRATE        (9600)
#   endif
   

/* MSPBoot BSL-based:
 *  If MSPBoot_BSL is defined for the project (in this file or in project 
 *  preprocessor options), the following settings will apply:
 *
 *  NDEBUG = defined: Debugging is disabled, ASSERT_H function is ignored,
 *      GPIOs are not used for debugging
 *
 *  CONFIG_APPMGR_APP_VALIDATE =2: Application is validated by checking its CRC-16.
 *      An invalid CRC over the whole Application area will keep the mcu in MSPBoot
 *
 *  CONFIG_MI_MEMORY_RANGE_CHECK = defined: Address range of erase and Write
 *      operations are validated. BSL-based commands can write/erase any area
 *      including MSPBoot, but this defition prevents modifications to area 
 *      outside of Application
 *
 *  CONFIG_CI_PHYDL_COMM_SHARED = defined: Communication interface can be used 
 *      by application. Application can call MSPBoot initialization and poll 
 *      routines to use the same interface.
 *
 *  CONFIG_CI_PHYDL_ERROR_CALLBACK = undefined: The communication interface  
 *      doesn't report errors with a callback
 *
 *  CONFIG_CI_PHYDL_I2C_TIMEOUT = undefined: The communication interface doesn't 
 *      detect timeouts. Only used with I2C
 *
 *  CONFIG_CI_PHYDL_START_CALLBACK = defined: Start Callback is required
 *      by the BSL-based protocol and is implemented. Only used with I2C.
 *
 *  CONFIG_CI_PHYDL_STOP_CALLBACK = undefined: Stop callback is not needed
 *      and is not implemented to save flash space. Only used with I2C
 *
 *  CONFIG_CI_PHYDL_I2C_SLAVE_ADDR = 0x40. I2C slave address of this device 
 *      is 0x40. Only used with I2C
 *
 *  CONFIG_CI_PHYDL_UART_BAUDRATE = 9600. UART baudrate is 9600. Only used with
 *      UART
 */
#elif defined(MSPBoot_BSL)
//#   define NDEBUG
   #define Level_1 1
   #define Level_2 2
#   define CONFIG_APPMGR_APP_VALIDATE    (Level_2)
#   define CONFIG_MI_MEMORY_RANGE_CHECK
#   undef CONFIG_CI_PHYDL_ERROR_CALLBACK
#   ifdef MSPBoot_CI_UART
#       undef CONFIG_CI_PHYDL_START_CALLBACK
#       undef CONFIG_CI_PHYDL_STOP_CALLBACK
// #       define CONFIG_CI_PHYDL_UART_BAUDRATE        (57600)
#   endif
#else
#error "Define a proper configuration in TI_MSPBoot_Config.h or in project preprocessor options"
#endif

#endif            //__TI_MSPBoot_CONFIG_H__
