/******************************************************************************/
/* LINKER COMMAND FILE FOR MSPBoot BOOTLOADER USING msp430fr5949  */
/* File generated with MSPBootLinkerGen.py on 2025-09-08 */
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
__Flash_Start = 0x4400;             /* Start of Application area */
   /* Reserved Flash locations for Bootloader Area */
    __Boot_Start = 0xf800;         /* Boot flash */
    __Boot_Reset = 0xFFFE;                          /* Boot reset vector */
    __Boot_VectorTable = 0xFF90;      /* Boot vector table */
    __Boot_SharedCallbacks_Len = 0x10; /* Length of shared callbacks (2 calls =4B(msp430) or 8B(msp430x) */
    __Boot_SharedCallbacks = 0xff70; /* Start of Shared callbacks */
     _BOOT_APPVECTOR = __Boot_SharedCallbacks;       /* Definition for application table             */
    _Appl_Vector_Start = 0xf790; /* Interrupt table */
    /* Reserved Flash locations for Application Area */
 
/* MEMORY definition, adjust based on definitions above */
MEMORY
{
    SFR                     : origin = 0x0000, length = 0x0010
    PERIPHERALS_8BIT        : origin = 0x0010, length = 0x00F0
    PERIPHERALS_16BIT       : origin = 0x0100, length = 0x0100
    // RAM from _NonReserved_RAM_Start - __RAM_End
    RAM                     : origin = 0x1c08, length = 0x7f8
    // Flash from _App_Start -> (APP_VECTORS-1)
    FLASH                   : origin = 0x4403, length = 0xb38d
    FLASH2                  : origin = 0x10000, length = 0x3ff8
    // Interrupt table from  _App_Vector_Start->(RESET-1)
    INT00            : origin = 0xf790, length = 0x0002
    INT01            : origin = 0xf792, length = 0x0002
    INT02            : origin = 0xf794, length = 0x0002
    INT03            : origin = 0xf796, length = 0x0002
    INT04            : origin = 0xf798, length = 0x0002
    INT05            : origin = 0xf79a, length = 0x0002
    INT06            : origin = 0xf79c, length = 0x0002
    INT07            : origin = 0xf79e, length = 0x0002
    INT08            : origin = 0xf7a0, length = 0x0002
    INT09            : origin = 0xf7a2, length = 0x0002
    INT10            : origin = 0xf7a4, length = 0x0002
    INT11            : origin = 0xf7a6, length = 0x0002
    INT12            : origin = 0xf7a8, length = 0x0002
    INT13            : origin = 0xf7aa, length = 0x0002
    INT14            : origin = 0xf7ac, length = 0x0002
    INT15            : origin = 0xf7ae, length = 0x0002
    INT16            : origin = 0xf7b0, length = 0x0002
    INT17            : origin = 0xf7b2, length = 0x0002
    INT18            : origin = 0xf7b4, length = 0x0002
    INT19            : origin = 0xf7b6, length = 0x0002
    INT20            : origin = 0xf7b8, length = 0x0002
    INT21            : origin = 0xf7ba, length = 0x0002
    INT22            : origin = 0xf7bc, length = 0x0002
    INT23            : origin = 0xf7be, length = 0x0002
    INT24            : origin = 0xf7c0, length = 0x0002
    INT25            : origin = 0xf7c2, length = 0x0002
    INT26            : origin = 0xf7c4, length = 0x0002
    INT27            : origin = 0xf7c6, length = 0x0002
    INT28            : origin = 0xf7c8, length = 0x0002
    INT29            : origin = 0xf7ca, length = 0x0002
    INT30            : origin = 0xf7cc, length = 0x0002
    INT31            : origin = 0xf7ce, length = 0x0002
    INT32            : origin = 0xf7d0, length = 0x0002
    INT33            : origin = 0xf7d2, length = 0x0002
    INT34            : origin = 0xf7d4, length = 0x0002
    INT35            : origin = 0xf7d6, length = 0x0002
    INT36            : origin = 0xf7d8, length = 0x0002
    INT37            : origin = 0xf7da, length = 0x0002
    INT38            : origin = 0xf7dc, length = 0x0002
    INT39            : origin = 0xf7de, length = 0x0002
    INT40            : origin = 0xf7e0, length = 0x0002
    INT41            : origin = 0xf7e2, length = 0x0002
    INT42            : origin = 0xf7e4, length = 0x0002
    INT43            : origin = 0xf7e6, length = 0x0002
    INT44            : origin = 0xf7e8, length = 0x0002
    INT45            : origin = 0xf7ea, length = 0x0002
    INT46            : origin = 0xf7ec, length = 0x0002
    INT47            : origin = 0xf7ee, length = 0x0002
    INT48            : origin = 0xf7f0, length = 0x0002
    INT49            : origin = 0xf7f2, length = 0x0002
    INT50            : origin = 0xf7f4, length = 0x0002
    INT51            : origin = 0xf7f6, length = 0x0002
    INT52            : origin = 0xf7f8, length = 0x0002
    INT53            : origin = 0xf7fa, length = 0x0002
    INT54            : origin = 0xf7fc, length = 0x0002
    
    // App reset from _App_Reset_Vector
    RESET                   : origin = 0xf7fe, length = 0x0002
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

    .text:_isr        : {}  > FLASH            /* Code ISRs                         */
    #ifndef __LARGE_DATA_MODEL__
	.text       : {} >> FLASH            /* CODE                 */
    #else 
	.text       : {} >> FLASH | FLASH2      /* CODE                 */
    #endif 
       .cinit      : {} > FLASH        /* INITIALIZATION TABLES*/ 
    #ifndef __LARGE_DATA_MODEL__ 
      .const      : {} >> FLASH       /* CONSTANT DATA        */ 
    #else 
      .const      : {} >> FLASH2 | FLASH    /* CONSTANT DATA        */ 
    #endif 

    .cio        : {} > RAM                /* C I/O BUFFER                      */

    /* MSP430 INTERRUPT VECTORS          */
    .int00       : {}               > INT00
    .int01       : {}               > INT01
    .int02       : {}               > INT02
    .int03       : {}               > INT03
    .int04       : {}               > INT04
    .int05       : {}               > INT05
    .int06       : {}               > INT06
    .int07       : {}               > INT07
    .int08       : {}               > INT08
    .int09       : {}               > INT09
    .int10       : {}               > INT10
    .int11       : {}               > INT11
    .int12       : {}               > INT12
    .int13       : {}               > INT13
    .int14       : {}               > INT14
    .int15       : {}               > INT15
    .int16       : {}               > INT16
    .int17       : {}               > INT17
    .int18       : {}               > INT18
    .int19       : {}               > INT19
    .int20       : {}               > INT20
    .int21       : {}               > INT21
    .int22       : {}               > INT22
    .int23       : {}               > INT23
    .int24       : {}               > INT24
    .int25       : {}               > INT25
    .int26       : {}               > INT26
    .int27       : {}               > INT27
    .int28       : {}               > INT28
    .int29       : {}               > INT29
    AES256       : { * ( .int30 ) } > INT30 type = VECT_INIT
    RTC          : { * ( .int31 ) } > INT31 type = VECT_INIT
    PORT4        : { * ( .int32 ) } > INT32 type = VECT_INIT
    PORT3        : { * ( .int33 ) } > INT33 type = VECT_INIT
    TIMER3_A1    : { * ( .int34 ) } > INT34 type = VECT_INIT
    TIMER3_A0    : { * ( .int35 ) } > INT35 type = VECT_INIT
    PORT2        : { * ( .int36 ) } > INT36 type = VECT_INIT
    TIMER2_A1    : { * ( .int37 ) } > INT37 type = VECT_INIT
    TIMER2_A0    : { * ( .int38 ) } > INT38 type = VECT_INIT
    PORT1        : { * ( .int39 ) } > INT39 type = VECT_INIT
    TIMER1_A1    : { * ( .int40 ) } > INT40 type = VECT_INIT
    TIMER1_A0    : { * ( .int41 ) } > INT41 type = VECT_INIT
    DMA          : { * ( .int42 ) } > INT42 type = VECT_INIT
    USCI_A1      : { * ( .int43 ) } > INT43 type = VECT_INIT
    TIMER0_A1    : { * ( .int44 ) } > INT44 type = VECT_INIT
    TIMER0_A0    : { * ( .int45 ) } > INT45 type = VECT_INIT
    ADC12        : { * ( .int46 ) } > INT46 type = VECT_INIT
    USCI_B0      : { * ( .int47 ) } > INT47 type = VECT_INIT
    USCI_A0      : { * ( .int48 ) } > INT48 type = VECT_INIT
    WDT          : { * ( .int49 ) } > INT49 type = VECT_INIT
    TIMER0_B1    : { * ( .int50 ) } > INT50 type = VECT_INIT
    TIMER0_B0    : { * ( .int51 ) } > INT51 type = VECT_INIT
    COMP_E       : { * ( .int52 ) } > INT52 type = VECT_INIT
    UNMI         : { * ( .int53 ) } > INT53 type = VECT_INIT
    SYSNMI       : { * ( .int54 ) } > INT54 type = VECT_INIT

    .reset       : {}               > RESET  /* MSP430 RESET VECTOR                 */
}

/****************************************************************************/
/* INCLUDE PERIPHERALS MEMORY MAP                                           */
/****************************************************************************/

-l msp430fr5949.cmd

