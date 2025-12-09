// ========================= Worker Node =========================
//                   MSP430FR5969 (Worker Node k)
//                 ---------------------------
//            /|\ |                       XIN|-
//             |  |                           | 32 kHz Crystal (optional)
//             ---|RST                    XOUT|-
//                |                           |
//                |                       P1.6|-> FRAM SI   (UCB0SIMO, MOSI, shared bus)
//                |                       P1.7|<- FRAM SO   (UCB0SOMI, MISO, shared bus)
//                |                       P2.2|-> FRAM SCK  (UCB0CLK, shared bus)
//                |                       P1.5|-> FRAM CS#  (chip select, active low, shared)
//                |                           |
//                |                       P1.4|-> REQk      (Node k -> Arbiter, request / release)
//                |                       P1.3|<-> GNTk     (Arbiter <-> Node k, grant / mail / reset)
//                |                           |
//                |                       P1.0|-> LED (lock held indication, green)
//                |                       P4.6|-> LED (no lock / idle indication, red)
//                |                           |
//               GND|---------------------------



#include <msp430.h>
#include <stdint.h>
#include "fram.h"
#include "mailbox.h"

/* Adjust per node build: IDs 1,2,3,... (index = ID-1) */
#define NODE_ID         1u
#define NODE_INDEX      (NODE_ID - 1u)

/* REQ = P1.4, GNT = P1.3 */
#define NODE_REQ_PIN    BIT4
#define NODE_GNT_PIN    BIT3
#define NODE_GNT_IV     0x08    /* P1.3 in P1IV */

/* ---- Lock state ---- */

typedef enum {
    LOCK_IDLE = 0,
    LOCK_WAIT_GRANT,
    LOCK_HELD
} lock_state_t;

static volatile lock_state_t g_lock_state = LOCK_IDLE;
static volatile uint8_t      g_mail_flag  = 0u;

/* ---- Clock ---- */

static void clock_init_8mhz(void)
{
    CSCTL0_H = CSKEY >> 8;
    CSCTL1   = DCOFSEL_6;
    CSCTL2   = SELS__DCOCLK | SELM__DCOCLK;
    CSCTL3   = DIVS__1 | DIVM__1;
    CSCTL0_H = 0;
}

/* ---- GPIO ---- */

static void node_gpio_init(void)
{
    /* REQ: P1.4 in, pulldown */
    P1SEL0 &= (uint8_t)~NODE_REQ_PIN;
    P1SEL1 &= (uint8_t)~NODE_REQ_PIN;

    P1DIR  &= (uint8_t)~NODE_REQ_PIN;
    P1REN  |= NODE_REQ_PIN;
    P1OUT  &= (uint8_t)~NODE_REQ_PIN;

    /* GNT: P1.3 in, pulldown, rising-edge interrupt */
    P1SEL0 &= (uint8_t)~NODE_GNT_PIN;
    P1SEL1 &= (uint8_t)~NODE_GNT_PIN;

    P1DIR  &= (uint8_t)~NODE_GNT_PIN;
    P1REN  |= NODE_GNT_PIN;
    P1OUT  &= (uint8_t)~NODE_GNT_PIN;

    P1IES  &= (uint8_t)~NODE_GNT_PIN;  /* low->high */
    P1IFG  &= (uint8_t)~NODE_GNT_PIN;
    P1IE   |= NODE_GNT_PIN;

    /* LEDs */
    P1DIR |= BIT0;
    P4DIR |= BIT6;
}

/* High pulse on REQ */
static void node_pulse_req_line(void)
{
    P1DIR |= NODE_REQ_PIN;
    P1OUT |= NODE_REQ_PIN;
    __delay_cycles(50u);
    P1OUT &= (uint8_t)~NODE_REQ_PIN;
    P1DIR &= (uint8_t)~NODE_REQ_PIN;
}

/* High pulse on GNT for reset notification at startup */
static void node_pulse_reset_on_gnt(void)
{
    P1IE  &= (uint8_t)~NODE_GNT_PIN;
    P1IFG &= (uint8_t)~NODE_GNT_PIN;

    P1DIR |= NODE_GNT_PIN;
    P1OUT |= NODE_GNT_PIN;
    __delay_cycles(50u);
    P1OUT &= (uint8_t)~NODE_GNT_PIN;
    P1DIR &= (uint8_t)~NODE_GNT_PIN;

    P1IFG &= (uint8_t)~NODE_GNT_PIN;
    P1IE  |= NODE_GNT_PIN;
}

/* ---- Lock API ---- */

static void lock_acquire(void)
{
    if (g_lock_state == LOCK_HELD) {
        return;
    }

    __disable_interrupt();
    g_lock_state = LOCK_WAIT_GRANT;
    __enable_interrupt();

    node_pulse_req_line();    /* request FRAM bus */

    while (g_lock_state != LOCK_HELD) {
        __bis_SR_register(LPM0_bits | GIE);
        __no_operation();
    }
}

static void lock_release(void)
{
    if (g_lock_state != LOCK_HELD) {
        return;
    }

    node_pulse_req_line();    /* release FRAM bus */

    __disable_interrupt();
    g_lock_state = LOCK_IDLE;
    __enable_interrupt();
}

/* ---- Mailbox helpers ---- */

static void process_incoming_messages(void)
{
    lock_acquire();
    g_mail_flag = 0u; // clear flag**

    P1OUT |= BIT0;
    P4OUT &= ~BIT6;


    uint8_t src_id;
    uint8_t len;
    uint8_t buf[MSG_SLOT_PAYLOAD_MAX];
    uint8_t got;

    for (;;) {
        got = mailbox_recv_msg(NODE_INDEX, &src_id, &len, buf);
        if (got == 0u) {
            break;
        }

        /* TODO: handle message (src_id, buf[0..len-1]) */
        /* For now, just toggle green LED once per message */
        P1OUT ^= BIT0;
    }

    lock_release();
    P1OUT &= ~BIT0;
    P4OUT |= BIT6;
}

static void send_dummy_message(void)
{
    uint8_t payload[MSG_SLOT_PAYLOAD_MAX];
    uint8_t len;
    uint8_t i;
    uint8_t dest_index;

    /* Example: send to node index 1 (node2) if exists and not self */
    dest_index = 1u;
    // if (dest_index == NODE_INDEX) {
    //     dest_index = 0u;
    // }

    /* Build simple payload (e.g., NODE_ID repeated) */
    len = 20u;
    for (i = 0u; i < len; i++) {
        payload[i] = 0xA0;
    }

    i = mailbox_send_msg(dest_index, NODE_ID, payload, len);
    if(!i) {P4OUT |= BIT6; P1OUT |= BIT0;} // indicate failure
    // else {uart0_println("Message sent");uart0_print_uint(i);uart0_println("");}
}

static void build_msg_payload(uint8_t *buf, uint8_t len)
{
    uint8_t i;
    for (i = 0u; i < len; i++) {
        buf[i] = 0xEE;
    }
}

static void uart0_init(void) // baud rate = 19200 with 8MHz SMCLK
{
    UCA0CTLW0 = UCSWRST;
    UCA0CTLW0 |= UCSSEL__SMCLK;

    UCA0BR0 = 160u;
    UCA0BR1 = 1u;
    UCA0MCTLW = (uint16_t)(0xAAu << 8);

    P2SEL1 |= (BIT0 | BIT1);
    P2SEL0 &= (uint8_t)~(BIT0 | BIT1);

    UCA0CTLW0 &= ~UCSWRST;
}

void uart0_send(char c) {
    while (!(UCA0IFG & UCTXIFG));
    UCA0TXBUF = c;
}

void uart0_print(const char *s) {
    while (*s) uart0_send(*s++);
}

void uart0_println(const char *s) {
    uart0_print(s);
    uart0_send('\r');
    uart0_send('\n');
}

void uart0_print_uint(uint32_t num) {
    char buf[16];
    int i = 0;
    if (num == 0) {
        uart0_send('0');
        return;
    }
    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    while (i--) uart0_send(buf[i]);
}

void uart0_print_hex(uint32_t num) {
    char buf[9];
    int i;
    buf[8] = '\0';
    for (i = 7; i >= 0; i--) {
        uint8_t nibble = num & 0x0F;
        if (nibble < 10) buf[i] = '0' + nibble;
        else buf[i] = 'A' + (nibble - 10);
        num >>= 4;
    }
    uart0_print(buf);
}

/* ---- ISR ---- */

#pragma vector = PORT1_VECTOR
__interrupt void PORT1_ISR(void)
{
    uint16_t iv = P1IV;

    if (iv == NODE_GNT_IV) {
        if (g_lock_state == LOCK_WAIT_GRANT) {
            g_lock_state = LOCK_HELD;
        } else {
            /* GNT pulse with no pending lock => mail notification */
            g_mail_flag = 1u;
        }
        __bic_SR_register_on_exit(LPM0_bits);
    }
}

/* ---- main ---- */

void experiment_setup(uint16_t spi_clk_div, uint32_t sends){

    uint32_t  e1, e2, e3;
    uint32_t calc;
    uint32_t delay_cnt;

    /* Reset pulse to arbiter (inform it we may have reset) */
    node_pulse_reset_on_gnt();
    g_lock_state = LOCK_IDLE;

    /* LED: red ON (no lock), green OFF */
    P1OUT &= ~BIT0;
    P4OUT |= BIT6;

    __bis_SR_register(GIE);
    uart0_println("SPI Worker Node Started");

    while ((P4IN & BIT5));
    __delay_cycles(8000000); // sleep for 1 second

    fram_spi_init(spi_clk_div);      /* init SPI + FRAM interface */

    // TA0CTL = TASSEL__SMCLK | MC__CONTINUOUS | TACLR;   // SMCLK, continuous, clear TAR
    TA0CTL = TASSEL__SMCLK | MC__CONTINUOUS | TACLR | ID__8;  // SMCLK/8
    TA0EX0 = TAIDEX_7;                                        // extra /8  (total /64)

    uart0_println("Experiment started...");
    P1OUT |= BIT0; // RED ON GREEN ON

    e1 = TA0R;
    lock_acquire();
    e1 = TA0R - e1;
    P4OUT &= ~BIT6; // RED OFF GREEN ON
    uart0_println("Lock acquired");

    uint8_t payload[MSG_SLOT_PAYLOAD_MAX];
    build_msg_payload(payload, MSG_SLOT_PAYLOAD_MAX);

    TA0CTL = TASSEL__SMCLK | MC__CONTINUOUS | TACLR | ID__8;  // SMCLK/8
    TA0EX0 = TAIDEX_7;  

    e2 = TA0R;
    for(delay_cnt = sends; delay_cnt > 0u; delay_cnt--) {
        (void)mailbox_send_msg(1u, NODE_ID, payload, MSG_SLOT_PAYLOAD_MAX);
    }
    e2 = TA0R - e2;
    uart0_println("Messages sent");

    
    // e3 = TA0R;
    lock_release();
    // e3 = TA0R - e3;

    // report timings over UART (yet to be implemented) 

    uart0_println("SPI Worker Node Report:");
    uart0_print("Messages sent: ");
    uart0_print_uint(sends);    uart0_println("");

    uart0_print("Lock acquire time (cycles): ");
    uart0_print_uint(e1);    uart0_println("");
    
    uart0_print("Message send time (cycles): ");
    uart0_print_uint(e2);    uart0_println("");

    uart0_print("Message size (bits): ");
    calc = sends * MSG_SLOT_SIZE * 8u;
    uart0_print_uint(calc);    uart0_println("");

    uart0_print("Message send time (microseconds): ");
    uart0_print_uint(e2 * 8u);   uart0_println("");
    spi_deinit();
    
//     uart0_print("Lock release time (cycles): ");
//     uart0_print_uint(e3);    uart0_println("");
}

int main(void)
{
    uint32_t sends = 100u;
    // uint16_t spi_clk_div = 2u; // SMCLK / 2 = 4MHz
    uint16_t spi_clk_div = 8u; // SMCLK / 8 = 1MHz
    // change payload size in mailbox.h 

    WDTCTL = WDTPW | WDTHOLD;

    clock_init_8mhz();
    node_gpio_init();
    uart0_init(); //
    // fram_spi_init(spi_clk_div);      /* init SPI + FRAM interface */
    __delay_cycles(2000u);


    
    // for tactile switch on P4.5 
    P4DIR &= ~BIT5;
    P4REN |= BIT5;
    P4OUT |= BIT5;
    
    PM5CTL0 &= ~LOCKLPM5;

    experiment_setup(spi_clk_div,10u);
    experiment_setup(spi_clk_div,75u);
    // experiment_setup(spi_clk_div,100u);
    experiment_setup(spi_clk_div,150u);
    // experiment_setup(spi_clk_div,300u);
    

    PMMCTL0 = PMMPW | PMMSWBOR;  // Trigger software Brown-Out Reset
}

// int main(void){

//     uint32_t delay_cnt;
//     uint16_t spi_clk_div = 8u; // SMCLK / 8 = 1MHz
//     // change payload size in mailbox.h 

//     WDTCTL = WDTPW | WDTHOLD;

//     clock_init_8mhz();
//     uart0_init(); //
//     node_gpio_init();
//     fram_spi_init(spi_clk_div);      /* init SPI + FRAM interface */
//     __delay_cycles(2000u);

    
//     PM5CTL0 &= ~LOCKLPM5;

//     /* Reset pulse to arbiter (inform it we may have reset) */
//     node_pulse_reset_on_gnt();
//     g_lock_state = LOCK_IDLE;

//     /* LED: red ON (no lock), green OFF */
//     P1OUT &= ~BIT0;
//     P4OUT |= BIT6;

//     __bis_SR_register(GIE);

//     uart0_println("SPI Worker Node Started");
//     for (;;) {
//         /* If notified of new messages, read them - disabled for now** */
//         // if (g_mail_flag != 0u) {
//         //     process_incoming_messages();
//         // }

//         /* Periodically send a dummy message */
//         lock_acquire();
//         P1OUT |= BIT0;
//         P4OUT &= ~BIT6;

//         send_dummy_message(); // separate data preparation and sending
        
//         for (delay_cnt = 8000000u; delay_cnt > 0u; delay_cnt--) {
//             __no_operation();
//         }

//         lock_release();
//         P1OUT &= ~BIT0;
//         P4OUT |= BIT6;

//         /* 1sec Delay between sends */
//         for (delay_cnt = 8000000u; delay_cnt > 0u; delay_cnt--) {
//             __no_operation();
//         }
//     }
// }

