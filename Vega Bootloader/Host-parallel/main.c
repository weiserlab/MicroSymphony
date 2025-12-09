
/* ---------- main.c (host MSP430FR5969) ------------------------------ */
/* =========================================================
 *  Host MSP430FR5969 – receive TI-TXT over UART0, store in
 *  FRAM, forward to slave MCU on P4.5 button press.
 *  UART style kept identical to user’s original code.
 USER SET: THIS CODE HAS DIFFERENT PIN MAPPINGS - CHECK
 * ========================================================= */
#include <msp430.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "bsl.h"

/* ---------------- Existing UART helpers ----------------- */
extern void uart0_init(void);          /* already in your code   */
extern void comm_send(uint8_t, bool);  /* sends one byte         */
extern void comm_print_str(const char*);

void uart0_init(void) {  // Debug UART0 (USB)
    UCA0CTLW0 = UCSWRST;                     // Put eUSCI in reset
    UCA0CTLW0 |= UCSSEL__SMCLK;              // Use SMCLK = 8 MHz

    UCA0BR0 = 160;                           // 416 = 0x01A0
    UCA0BR1 = 1;
    UCA0MCTLW = (uint16_t)(0xAA << 8);  // UCBRS = 0xAA, UCBRF = 0
    P2SEL1 |= BIT0 | BIT1;                   // Configure UART pins
    P2SEL0 &= ~(BIT0 | BIT1);

    UCA0CTLW0 &= ~UCSWRST;                   // Initialize eUSCI
}

void comm_send(uint8_t data, bool debug) {
    while (!(UCA0IFG & UCTXIFG));
    UCA0TXBUF = data;
}


void comm_print_str(const char *str) {
    while (*str) {
        comm_send((uint8_t)*str, true);
        str++;
    }
}

/* ---------------- Protocol constants -------------------- */
#define START   0x55
#define END     0x5A
#define ACK     0xAA
#define NACK    0xEE
#define FIN     0xAC
#define RST     0x5B

#define FSL     0xAE

/* --- detailed error codes (0xE1-0xE8) --------------- */
#define ERR_SIZE0   0xE1        /* size==0                        */
#define ERR_BIG     0xE2        /* size>256                       */
#define ERR_SEGMAX  0xE3        /* exceeded MAX_SEGS              */
#define ERR_OVF     0xE4        /* image_store overflow           */
#define ERR_CRC     0xE5        /* CRC mismatch                   */
#define ERR_PROTO   0xE6        /* bad framing/unknown byte etc   */
#define ERR_FLASH   0xE7        /* Error flashing to the slave   */
#define ERR_VER     0xE8        /* BSL version mismatch   */
#define ERR_NULL    0xE9        /* Slave didnt get the correct flash */
#define ERR_OFF     0xEA        /* Slave is not available*/

/*Slave select codes*/
#define SLAVE_1 BIT0
#define SLAVE_2 BIT1
#define SLAVE_3 BIT2
#define SLAVE_4 BIT3

// #define ALL_TARGETS BIT0 | BIT1 | BIT2 | BIT3 | BIT4 | BIT5 | BIT6
#define REPLY BIT7

/*Host command codes*/
#define SLAVE_SELECT    0x11
#define SLAVE_CHECK     0x12
#define CHECKSUM_CHECK  0x13
#define RECEIVE_IMG     0x14
#define FLASH_IMG       0x15
#define BAUD_CHANGE     0x16
#define HOST_BAUD_CHANGE 0x17
#define RESET_TARGET    0x19

/* Baud rate selection codes */
#define BAUD_9600     0x60
#define BAUD_19200    0x61
#define BAUD_38400    0x62
#define BAUD_57600    0x63
#define BAUD_115200   0x64
#define BAUD_230400   0x65
#define BAUD_460800   0x66
#define BAUD_921600   0x67



static uint32_t Calc_App_CRC(uint32_t * Addr_array, uint32_t *Size_array, uint8_t ** DataPtr_Array, uint8_t num_arrays);

// fetch values from lnk_fr5959_single_2k_Boot.cmd
static uint16_t CRC_Addr = 0x4400; // _Appl_Start
static uint16_t App_StartAddress = 0x4403; //
static uint16_t App_EndAddress = 0xF3FF; // _Appl_End
static uint32_t App_StartAddress_Upper = 0x10000; // _Flex_Start
static uint32_t App_EndAddress_Upper = 0x13ff7; // _Flex_End
// -8 for MSP5959 slaves

static uint8_t slave_select;
static uint8_t baud_code;
static uint8_t debug;



/* --------- Tiny blocking UART helpers (no printf) ------- */
static inline uint8_t uart_get_u8(void){
    while (!(UCA0IFG & UCRXIFG));
    return UCA0RXBUF;
}
static inline void uart_put_u8(uint8_t b)          /* raw byte */{
    comm_send(b, true);        /* true → same path as debug prints */
}
/* wipe ‘len’ incoming bytes to re-align after a header error */
static void uart_discard(uint16_t len){
    while (len--) (void)uart_get_u8();
}

/* --------------- FRAM image storage area ---------------- */
#define IMG_BASE      0xD000              /* adjust in linker   */
#define IMG_SIZE      0x2000              /* 8 KB total         */
#define MAX_SEGS      32

typedef struct {
    uint32_t addr;                        /* target addr on slave  */
    uint16_t size;                        /* bytes in segment      */
    uint16_t offset;                      /* offset in store[]     */
} seg_t;

#pragma PERSISTENT(image_store)
static uint8_t image_store[IMG_SIZE] = {0};   /* 8 KiB buffer               */

#pragma PERSISTENT(seg_table)
static seg_t seg_table[MAX_SEGS]     = {0};   /* ~256 B segment table       */

#pragma PERSISTENT(seg_cnt)
static uint8_t seg_cnt               = 0;     /* counter survives reset     */

static void reset(){
    PMMCTL0 = PMMPW | PMMSWBOR;
}

/* -------------- CRC16-CCITT (HW peripheral) ------------- */
static uint16_t crc16(const uint8_t *p, uint16_t len, uint16_t seed){
    CRCINIRES = seed;
    while (len--) CRCDIRB_L = *p++;
    return CRCINIRES;
}

/*----------------- Receive command from PC ----------------*/

static uint8_t receive_send_command(void){
    // commands are slave-select, reset, slave-check, checksum-check
    // always echo back the command received for confirmation   
    uint8_t cmd = uart_get_u8(); 
    uart_put_u8(cmd);
    return cmd;
}


/* ---------------- Receive TI-TXT from PC ---------------- */
static bool receive_image(void){
    uint16_t img_ofs = 0;
    uint8_t  cnt     = 0;

    while (1)
    {
        // if (slave_select == RST)
        //     reset(); // SOFTWARE RESET 

        if (uart_get_u8() != START)                 /* wait 0x55 */
            continue;

        uint32_t addr = (uint32_t)uart_get_u8()        |
                        ((uint32_t)uart_get_u8() << 8) |
                        ((uint32_t)uart_get_u8() << 16)|
                        ((uint32_t)uart_get_u8() << 24);

        uint16_t size = uart_get_u8() | (uart_get_u8() << 8);

        if (size == 0)                 { uart_put_u8(ERR_SIZE0);  continue; }
        if (size > 256)                { uart_put_u8(ERR_BIG);
                                        uart_discard(size+2);  continue; }
        if (cnt >= MAX_SEGS)           { uart_put_u8(ERR_SEGMAX);
                                        uart_discard(size+2);  continue; }
        if (img_ofs + size > IMG_SIZE) { uart_put_u8(ERR_OVF);
                                  uart_discard(size+2);  continue; }

        uint8_t *dst = &image_store[img_ofs];
        uint16_t i;
        for (i = 0; i < size; i++)
            dst[i] = uart_get_u8();

        uint16_t rx_crc = uart_get_u8() | (uart_get_u8() << 8);
        uint16_t cal_crc = crc16((uint8_t *)&addr, 4, 0xFFFF);
        cal_crc = crc16((uint8_t *)&size, 2, cal_crc);
        cal_crc = crc16(dst, size, cal_crc);

        if (cal_crc != rx_crc)         { uart_put_u8(ERR_CRC);   continue; }

        /* record segment entry */
        seg_table[cnt].addr   = addr;
        seg_table[cnt].size   = size;
        seg_table[cnt].offset = img_ofs;
        cnt++;  img_ofs += size;

        uart_put_u8(ACK);

        /* finished? */
        uint8_t nxt = uart_get_u8();
        if (nxt == END)
        {
            seg_cnt = cnt;
            uart_put_u8(FIN);
            return true;
        }
        else if (nxt != START)      /* bad sequence – restart */
        {
            continue;
        }
        else                        /* got new START byte immediately */
        {
            UCA0RXBUF = nxt;        /* push back 1-byte (cheap) */
        }
    }
}

/* --------------- Flash stored image to slave ------------ */
static uint8_t flash_slave(void){
    uint8_t  res = 0;
    uint32_t addr_arr[MAX_SEGS];
    uint32_t size_arr[MAX_SEGS];
    uint8_t *ptr_arr [MAX_SEGS];

    uint8_t target_select = slave_select;
    // erase all targets, do not give respond flag

    // default set to no replies
    target_select &= ~REPLY;

    /* ---------- 1. wake the slave BSL  ------------------ */

    BSL_sendSingleByte(VBOOT_ENTRY_CMD);
    __delay_cycles(80000);
    BSL_flush();

    // check with each slave individually (already performed earlier)

    debug = 0x00;
    uint8_t i, j;

    /* ---------- 2. erase application area --------------- */

    res = BSL_sendCommand(BSL_ERASE_APP_CMD, target_select);
    BSL_flush();

    /* ---------- 3. program each segment ----------------- */
    // uint8_t i;
    for (i = 0; i < seg_cnt; i++)
    {
        uint8_t *p = &image_store[seg_table[i].offset];
        res = BSL_programMemorySegment(seg_table[i].addr,
                                       p,
                                       seg_table[i].size,
                                       target_select);

        /* fill the arrays for CRC calculation */
        addr_arr[i] = seg_table[i].addr;
        size_arr[i] = seg_table[i].size;
        ptr_arr [i] = p;
    }

    /* ---------- 4. calculate & store CRC ---------------- */
    uint32_t crc = Calc_App_CRC(addr_arr,
                                size_arr,
                                ptr_arr,
                                seg_cnt);

    // check CRC for each slave separately!!!
    // target_select = BIT0 | REPLY; // check for first slave
    debug = 0x00;
    for(j = 0; j < 3; j++){
        uint8_t board_mask = (1 << j+4);
        if (!(target_select & board_mask)) continue;
        for (i = 0; i < 4; i++) {
            uint8_t slave_mask = (1 << i); // check CRC and receive ACK for ith slave
            if (target_select & slave_mask) {
                slave_mask |= REPLY;
                res = BSL_programMemorySegment(CRC_Addr,
                                    (uint8_t *)&crc,
                                    2,
                                    board_mask | slave_mask);            /* 16-bit CRC word */
                // BSL_flush();
                if (res != 0) // fix the notation for debug**
                    debug |= slave_mask; // note all the nodes whose CRC checks failed
            }
        }
    }

    debug |= REPLY;
    if (debug != REPLY)
        return 2;

    /* ---------- 5. jump to the freshly-flashed app ------ */

    
    (void)BSL_sendCommand(BSL_JMP_APP_CMD, target_select);       /* no response expected */
    return 0;                                     /* success */
}

static void reset_slave(void){
    // reset the slave and enter bootloader - by driving the select pin high
    /* Reset all the slaves, and let them decide if the message is for them or not*/
    
    P1OUT |= BIT3;   // All slaves: P1.3 to high

    __delay_cycles(80000); // not required
    
    // pass and release the universal reset
    P3DIR |= BIT0;     // Make P3.0 output (from high z)
    P3OUT &= ~BIT0;   // Drive P3.0 low
    __delay_cycles(80000); // 0.1x the delay for reset - 80000
    P3OUT |= BIT0;
    __delay_cycles(1200000); // 1M works

    // revert the pins back to low
    P1OUT &= ~BIT3;    // All slaves: P1.3 to low

    // revert to input with pull-up (high-Z default HIGH)
    P3DIR &= ~BIT0;    // Make P3.0 input
    P3REN |= BIT0;     // Enable resistor
    P3OUT |= BIT0;     // Resistor = pull-up
}

static void ping_slave(uint8_t target_select){
    
    uint8_t res, i, j;
    for(i=4; i<7; i++){ // # of boards
        uint8_t board_mask = (1 << i);
        if (target_select & board_mask){
            for(j=0; j<4; j++){ // # of slaves per board
                uint8_t slave_mask = (1 << j);
                if (target_select & slave_mask){
                    BSL_sendSingleByte(VBOOT_ENTRY_CMD);
                    __delay_cycles(80000);
                    BSL_flush();

                    res = BSL_sendCommand(BSL_VERSION_CMD, board_mask | slave_mask | REPLY);
                    BSL_flush();
                    
                    uart_put_u8(board_mask | slave_mask);
                    
                    if ((res & 0xF0) != (VBOOT_VERSION & 0xF0)) {
                        uart_put_u8(res & 0xF0); // slave version mismatch
                    } 
                    else if (res == 0xFF){
                        uart_put_u8(NACK); // slave not responding
                    }
                    else {
                        uart_put_u8(ACK);
                    }
                }
            }
        }
    }
    uart_put_u8(FIN); // end of slave check
}
/* ---Change baud rate of the target(s)--- */
static void change_baud(uint8_t baud_code, uint8_t target_select){
    uint8_t res;
    // change the baud rate of the slaves
    target_select &= ~REPLY; // remove the reply flag if any
    
    /* 1. Send updated Baud to Targets*/
    res = BSL_changeBaudRate(target_select, baud_code);

    /* 2. Change host's baud rate for UART interface connected to target(s)*/
    BSL_Comm_Init(baud_code);
    __delay_cycles(80000); // let the line stabilize

}


static void hw_init(void) {  // 
    //set P1.0 green LED to show we are ready
    P1OUT |= BIT0; P1DIR |= BIT0;

    //USER SET: universal reset pin: P3.0
    P3DIR &= ~BIT0;    // Make P3.0 input (High Impedance)
    P3REN |= BIT0;     // Enable resistor
    P3OUT |= BIT0;     // Resistor = pull-up


    //USER SET: set select pins of the slave devices
    
    P1OUT &= ~BIT3; P1DIR |= BIT3; // All slaves: P1.3 to low

    PM5CTL0 &= ~LOCKLPM5;
}

/* --------------------------- main ----------------------- */
int main(void)
{
    WDTCTL = WDTPW | WDTHOLD;
    CSCTL0_H = CSKEY_H;
    CSCTL1   = DCOFSEL_6;           /* 8 MHz */
    CSCTL2   = SELA__VLOCLK | SELM__DCOCLK | SELS__DCOCLK;
    CSCTL3   = DIVA__1 | DIVS__1 | DIVM__1;

    uart0_init(); // PC connection
    BSL_Init(BAUD_57600); // slave connection
    hw_init();

    // initialize variables
    bool image_received = false;
    slave_select = 0x00;

    while(1){
        if (UCA0IFG & UCRXIFG){
            uint8_t cmd;
            cmd = receive_send_command();
            switch (cmd)
            {
                case SLAVE_SELECT:
                    slave_select = uart_get_u8();
                    uart_put_u8(ACK);
                    break;

                case RECEIVE_IMG:
                    image_received = receive_image();
                    break;

                case FLASH_IMG:
                    if (image_received)
                    {
                        // reset_slave();
                        uint8_t e = flash_slave();
                        if (e == 0) uart_put_u8(FSL);
                        else if (e == 3) uart_put_u8(debug);
                        else if (e == 2)  uart_put_u8(debug);
                        else if (e == 1) uart_put_u8(ERR_FLASH);
                        // else if (e == 3) uart_put_u8(0x03);
                        else uart_put_u8(ERR_NULL); // corresponds to uncertainity in flashing
                        image_received = false;
                        slave_select = 0x00;
                    }
                    else uart_put_u8(ERR_NULL);
                    break;

                case SLAVE_CHECK:
                    if (!slave_select){
                        uart_put_u8(NACK);
                        break;
                    } 
                    ping_slave(slave_select); // check this
                    break;

                case BAUD_CHANGE:
                    if (!slave_select){
                            uart_put_u8(NACK);
                            break;
                        }

                    // change baud rate of the slaves
                    baud_code = uart_get_u8();
                    change_baud(baud_code, slave_select);
                    ping_slave(slave_select);
                    break;
                
                case HOST_BAUD_CHANGE:
                    BSL_Comm_Init(BAUD_57600);
                    __delay_cycles(80000); // let the line stabilize
                    break;

                case CHECKSUM_CHECK:
                    // send a known value and the slave(s) will check and acknowledge
                    break;

                case RST: // software reset
                    reset();
                    break;

                case RESET_TARGET:
                    reset_slave();
                    break;

                default: // do nothing
                    break;
            }

        }

    }

}



/* Calculate the CRC of the application*/
static uint32_t Calc_App_CRC(uint32_t * Addr_array, uint32_t *Size_array, uint8_t ** DataPtr_Array, uint8_t num_arrays)
{
    uint16_t addr;
    uint8_t i;

    CRCINIRES = 0xFFFF;

    // Calculate CRC for the whole Application address range
    for (addr=App_StartAddress; addr <= App_EndAddress; addr++)
    {
        for (i = 0; i < num_arrays; i ++)
        {
            // Check if address is defined by application image
            if ( (addr >= Addr_array[i]) &&
                 (addr < (Addr_array[i] + Size_array[i])) )
            {
                // If address is defined, add it to the CRC
            	CRCDIRB_L = DataPtr_Array[i][addr-Addr_array[i]];
                break;
            }
        }
        if (i==num_arrays)
        {
            // If not, simply add 0xFF
        	CRCDIRB_L = 0xFF;
        }
    }
    // CRC includes the upper Application address range
    uint32_t addr20;
    for (addr20=App_StartAddress_Upper; addr20 <= App_EndAddress_Upper; addr20++)
    {
        for (i = 0; i < num_arrays; i ++)
        {
            // Check if address is defined by application image
            if ( (addr20 >= Addr_array[i]) &&
                 (addr20 < (Addr_array[i] + Size_array[i])) )
            {
                // If address is defined, add it to the CRC
            	CRCDIRB_L = DataPtr_Array[i][addr20-Addr_array[i]];
                break;
            }
        }
        if (i==num_arrays)
        {
            // If not, simply add 0xFF
        	CRCDIRB_L = 0xFF;
        }
    }
// #endif
    return CRCINIRES;
}