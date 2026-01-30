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
#include "worker.h"
#include "fram.h"



static volatile lock_state_t g_lock_state = LOCK_IDLE;
static volatile uint8_t      g_mail_flag  = 0u;

uint16_t spi_clk_div = 2u;


void clock_init_8mhz(void)
{
    CSCTL0_H = CSKEY >> 8;
    CSCTL1   = DCOFSEL_6;
    CSCTL2   = SELS__DCOCLK | SELM__DCOCLK;
    CSCTL3   = DIVS__1 | DIVM__1;
    CSCTL0_H = 0;
}
/* ---- GPIO ---- */

void node_gpio_init(void)
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
    // P1DIR |= BIT0;
    // P4DIR |= BIT6;

}

/* High pulse on REQ */
void node_pulse_req_line(void)
{
    P1DIR |= NODE_REQ_PIN;
    P1OUT |= NODE_REQ_PIN;
    __delay_cycles(50u);
    P1OUT &= (uint8_t)~NODE_REQ_PIN;
    P1DIR &= (uint8_t)~NODE_REQ_PIN;
}

/* High pulse on GNT for reset notification at startup */
void node_pulse_reset_on_gnt(void)
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

void lock_acquire(void)
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

void lock_release(void)
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
