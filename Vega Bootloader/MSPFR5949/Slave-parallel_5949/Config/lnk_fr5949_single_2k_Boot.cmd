/******************************************************************************/
/* LINKER COMMAND FILE FOR MSPBoot BOOTLOADER USING msp430fr5949  */
/* File generated with MSPBootLinkerGen.py on 2025-10-03 */
/*----------------------------------------------------------------------------*/


/****************************************************************************/
/* SPECIFY THE SYSTEM MEMORY MAP                                            */
/****************************************************************************/
/* The following definitions can be changed to customize the memory map for a different device
 *   or other adjustments
 *  Note that the changes should match the definitions used in MEMORY and SECTIONS
 *
 */
/* RAM Memory Addresses */
__RAM_Start = 0x1c00;                 /* RAM Start */
__RAM_End = 0x23ff;                     /* RAM End */
    /* RAM shared between App and Bootloader, must be reserved */
    PassWd = 0x1c00;                 /* Password sent by App to force boot  mode */
    StatCtrl = 0x1c02;             /* Status and Control  byte used by Comm */
    CI_State_Machine = 0x1c03;         /*  State machine variable used by Comm */
    CI_Callback_ptr = 0x1c04;   /* Pointer to Comm callback structure */
    /* Unreserved RAM used for Bootloader or App purposes */
    _NonReserved_RAM_Start = 0x1c08; /* Non-reserved RAM */

/* Flash memory addresses */ 
_Appl_Start = 0x4400;             /* Start of Application area */
_Appl_End = 0xf3ff;                 /* End of Application area */
_Flex_Start = 0x10000;             /* Start of flex space (app or download, project-dependent) */
_Flex_End = 0x13ff7;                 /* End of flex space (app or download, project-dependent) */
 
    /* Reserved Flash locations for Bootloader Area */
    __Boot_Start = 0xf400;         /* Boot flash */
    __Boot_Reset = 0xFFFE;                          /* Boot reset vector */
    __Boot_VectorTable = 0xFF90;      /* Boot vector table */
    __Boot_SharedCallbacks_Len = 0x10; /* Length of shared callbacks (2 calls =4B(msp430) or 8B(msp43 ) */
    __Boot_SharedCallbacks = 0xff70; /* Start of Shared callbacks */
    _Appl_Vector_Start = 0xf390; /* Interrupt table */
    /* Reserved Flash locations for Application Area */
    _Appl_Checksum = (_Appl_Start);                 /* CRC16 of Application */
    _Appl_Checksum_8 = (_Appl_Start+2);             /* CRC8 of Application */
    _Appl_Start_Memory = (_Appl_Start+3);                 /* Application Area */
    _Appl_CRC_Size1 = (_Appl_End - _Appl_Start_Memory +1);         /* Number of bytes in lower memory calculated for CRC */
    _Appl_CRC_Size2 = (_Flex_End - _Flex_Start + 1); /* Number of bytes in upper memory calculated for CRC */

    _Appl_Reset_Vector = (__Boot_Start - 2);

    
    /* MEMORY definition, adjust based on definitions above */
MEMORY
{
    SFR                     : origin = 0x0000, length = 0x0010
    PERIPHERALS_8BIT        : origin = 0x0010, length = 0x00F0
    PERIPHERALS_16BIT       : origin = 0x0100, length = 0x0100
    // RAM from _NonReserved_RAM_Start - __RAM_End
    RAM                     : origin = 0x1c08, length = 0x7f8
    // Flash from __Boot_Start -( __Boot_SharedCallbacks or INT_VECTOR_TABLE)
    FLASH                   : origin = 0xf400, length = 0xb70
    // Shared callbacks from __Boot_SharedCallbacks + Len (when used)
    BOOT_SHARED_CALLBACKS   : origin = 0xff70, length = 0x10
    // Boot vector Table from __Boot_VectorTable- __Boot_Reset
    INT_VECTOR_TABLE        : origin = 0xFF90, length = 110
    // Boot reset from __Boot_Reset-_Flash_End
    RESET                   : origin = 0xFFFE, length = 0x0002
}

/****************************************************************************/
/* SPECIFY THE SECTIONS ALLOCATION INTO MEMORY                              */
/****************************************************************************/

SECTIONS
{
    .bss        : {} > RAM                /* GLOBAL & STATIC VARS              */
    .data       : {} > RAM                /* GLOBAL & STATIC VARS              */
    .sysmem     : {} > RAM                /* DYNAMIC MEMORY ALLOCATION AREA    */
    .stack      : {} > RAM (HIGH)         /* SOFTWARE SYSTEM STACK             */

    .text       : {} >> FLASH       /* CODE                 */
    .cinit      : {} >> FLASH       /* INITIALIZATION TABLES*/
    .const      : {} >> FLASH       /* CONSTANT DATA        */
    .cio        : {} > RAM          /* C I/O BUFFER                      */

    .BOOT_APP_VECTORS : {} > BOOT_SHARED_CALLBACKS
    /* MSP430 INTERRUPT VECTORS          */
    .BOOT_VECTOR_TABLE : {} > INT_VECTOR_TABLE
    .reset       : {}               > RESET  /* MSP430 RESET VECTOR         */ 
}

/****************************************************************************/
/* INCLUDE PERIPHERALS MEMORY MAP                                           */
/****************************************************************************/

-l msp430fr5949.cmd

