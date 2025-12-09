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
#include "uart.h"

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


static const uint16_t spi_clk_div = 2u;

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

    g_lock_state = LOCK_IDLE;

}

/* ---- Lock API ---- */

static void lock_acquire(void)
{
    if (g_lock_state == LOCK_HELD) {
        return;
    }

    __disable_interrupt();
    g_lock_state = LOCK_WAIT_GRANT;
    // uart0_println("Acquiring lock");
    node_pulse_req_line();    /* request FRAM bus */

    while (g_lock_state != LOCK_HELD) {
        __bis_SR_register(LPM0_bits | GIE);
        __disable_interrupt();
    }
    __enable_interrupt();

    spi_enable(spi_clk_div);
    // uart0_println("Lock acquired");
}

static void lock_release(void)
{
    if (g_lock_state != LOCK_HELD) {
        return;
    }

    spi_disable();
    // uart0_println("Releasing lock");
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


    uint8_t src_id = 0u;
    uint16_t len = 0u;
    uint8_t buf[MSG_SLOT_PAYLOAD_MAX];
    uint8_t got;

    for (;;) {
        got = mailbox_recv_msg(0u, &src_id, &len, buf);

        if (got == 0u) {
            break;
        }
        uart0_print("Received msg from ");
        uart0_print_uint(src_id); 
        uart0_print(", len=");
        uart0_print_uint(len);
        uart0_println("");
        /* TODO: handle message (src_id, buf[0..len-1]) */
        /* For now, just toggle green LED once per message */
        P1OUT ^= BIT0;
    }

    // notification bytes cleared by arbiter

    lock_release();
    P1OUT &= ~BIT0;
    P4OUT |= BIT6;
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
            // uart0_println("Mail");
            g_mail_flag = 1u;
        }
        __bic_SR_register_on_exit(LPM0_bits);
    }
}

/* Evaluations and Experiments*/

static uint8_t payload[1024u];

static void send_dummy_message(uint8_t dst_idx, uint8_t src_idx, uint16_t len, const uint8_t *payload)
{
    uint8_t i;
    // i = mailbox_send_msg(dst_idx, src_idx, payload, len);
    i = mailbox_send_bulk(dst_idx, src_idx, payload, len);
    if(!i) {
        P4OUT |= BIT6; P1OUT |= BIT0; 
        // uart0_println("Message send failed");
        __delay_cycles(8000000);
        P4OUT &= ~BIT6; P1OUT &= ~BIT0;
    } // indicate failure
    // else {uart0_println("Message sent");uart0_print_uint(i);uart0_println("");}
}


static void build_msg_payload(uint8_t *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0u; i < len; i++) {
        buf[i] = 0xEE;
    }
}


/* ---- main ---- */

void timer_start(void){
    TA0CTL = TASSEL__SMCLK | MC__CONTINUOUS | TACLR | ID__8;  // SMCLK/8
    TA0EX0 = TAIDEX_7;   
    // TA0CTL = TASSEL__ACLK | MC__CONTINUOUS | TACLR | ID__1; // ACLK/1
    // TA0EX0 = TAIDEX_4;// Extended divider /5  (TAIDEX_4 = divide by 5)
}




void test(){

    uint16_t counter = 15; // total 20k bytes
    uint8_t i;
    uint16_t len[] = {16,32,64,128,256,512,1024};
    build_msg_payload(payload, 1024u);

    for(i=0;i<7;i++){
        
        counter = 15;
        // wait for high on pin P4.2
        while(!(P4IN & BIT2));
        
        while(counter--){
            lock_acquire();

            send_dummy_message(NODE_INDEX, NODE_INDEX, len[i], payload);
            
            lock_release();

        }
        __delay_cycles(24000000); // 3 sec delay for logging
    }

}




int main(void){

    uint32_t delay_cnt;
    // spi_clk_div = 16u; // SMCLK / 2 = 4MHz
    // change payload size in mailbox.h 

    WDTCTL = WDTPW | WDTHOLD;

    clock_init_8mhz();
    uart0_init(); //
    node_gpio_init();

    // clear any startup glitches
    // P1IFG = 0u;


    PM5CTL0 &= ~LOCKLPM5;

    spi_init();

    /* Reset pulse to arbiter (inform it we may have reset) */
    node_pulse_reset_on_gnt();

    /* LED: red ON (no lock), green OFF */
    P1OUT &= ~BIT0;
    P4OUT |= BIT6;

    __bis_SR_register(GIE);


    uart0_println("SPI Worker Node Started");

    test();

}



