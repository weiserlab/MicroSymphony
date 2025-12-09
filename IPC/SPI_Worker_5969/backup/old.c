#include <msp430.h>
#include <stdint.h>

/* Node: requests FRAM lock from arbiter
 *
 * Wiring:
 *  REQ P1.4 -> arbiter REQx
 *  GNT P1.3 <-> arbiter GNTx  (used for grant, and for reset notification)
 *
 * LEDs:
 *  P1.0 (green): ON when lock held
 *  P4.6 (red)  : ON when lock not held
 */


/*** SET THESE 3 MACROS DIFFERENTLY PER NODE BUILD ***/
#define NODE_REQ_PIN   BIT4      // example: Node1 -> BIT4, Node2 -> BIT6, Node3 -> BIT2
#define NODE_GNT_PIN   BIT3      // example: Node1 -> BIT3, Node2 -> BIT7, Node3 -> BIT5
#define NODE_GNT_IV    0x08      // Node1: 0x08, Node2: 0x10, Node3: 0x0C
/*** ---------------------------------------------- ***/

typedef enum {
    LOCK_IDLE = 0,
    LOCK_WAIT_GRANT,
    LOCK_HELD
} lock_state_t;

static volatile lock_state_t g_lock_state = LOCK_IDLE;

/* ============= Clock ============= */

static void clock_init_simple(void)
{
    CSCTL0_H = CSKEY >> 8;
    CSCTL1   = DCOFSEL_0;            // ~1MHz
    CSCTL1  &= ~DCORSEL;
    CSCTL2   = SELS__DCOCLK | SELM__DCOCLK;
    CSCTL3   = DIVS__1 | DIVM__1;
    CSCTL0_H = 0;
}

/* ============= GPIO for REQ / GNT ============= */

static void node_gpio_init(void)
{
    /* REQ pin: default input with pull-up, pulsed low when needed. */
    P1SEL0 &= ~NODE_REQ_PIN;
    P1SEL1 &= ~NODE_REQ_PIN;

    P1DIR  &= ~NODE_REQ_PIN;
    P1REN  |=  NODE_REQ_PIN;
    P1OUT  |=  NODE_REQ_PIN;   // pull-up

    /* GNT pin: input with pull-up, falling-edge interrupt for grant. */
    P1SEL0 &= ~NODE_GNT_PIN;
    P1SEL1 &= ~NODE_GNT_PIN;

    P1DIR  &= ~NODE_GNT_PIN;
    P1REN  |=  NODE_GNT_PIN;
    P1OUT  |=  NODE_GNT_PIN;   // pull-up

    P1IES  |=  NODE_GNT_PIN;   // high->low
    P1IFG  &= ~NODE_GNT_PIN;
    P1IE   |=  NODE_GNT_PIN;
}

/* Generate a LOW pulse on REQ pin (used for request and release). */
static void node_pulse_req_line(void)
{
    P1DIR |=  NODE_REQ_PIN;   // output
    P1OUT &= ~NODE_REQ_PIN;   // low
    __delay_cycles(250);      // ~250 us

    P1OUT |=  NODE_REQ_PIN;   // OUT=1 for pull-up
    P1DIR &= ~NODE_REQ_PIN;   // back to input
}

/* Generate a LOW pulse on GNT pin as reset-notification to arbiter. */
static void node_pulse_reset_on_gnt(void)
{
    P1IE  &= ~NODE_GNT_PIN;
    P1IFG &= ~NODE_GNT_PIN;

    P1DIR |=  NODE_GNT_PIN;   // output
    P1OUT &= ~NODE_GNT_PIN;   // low
    __delay_cycles(250);      // ~250 us

    P1OUT |=  NODE_GNT_PIN;   // OUT=1 for pull-up
    P1DIR &= ~NODE_GNT_PIN;   // input

    P1IFG &= ~NODE_GNT_PIN;
    P1IE  |=  NODE_GNT_PIN;
}

/* ============= Lock API ============= */

static void lock_acquire(void)
{
    if (g_lock_state == LOCK_HELD)
        return;

    __disable_interrupt();
    g_lock_state = LOCK_WAIT_GRANT;
    __enable_interrupt();

    node_pulse_req_line();    // request

    while (g_lock_state != LOCK_HELD) {
        __bis_SR_register(LPM0_bits | GIE);
        __no_operation();
    }
}

static void lock_release(void)
{
    if (g_lock_state != LOCK_HELD)
        return;

    node_pulse_req_line();    // release

    __disable_interrupt();
    g_lock_state = LOCK_IDLE;
    __enable_interrupt();
}

/* ============= Port 1 ISR (node side) ============= */

#pragma vector = PORT1_VECTOR
__interrupt void PORT1_ISR(void)
{
    uint16_t iv = P1IV;

    if (iv == NODE_GNT_IV) {   // GNT falling edge from arbiter
        if (g_lock_state == LOCK_WAIT_GRANT) {
            g_lock_state = LOCK_HELD;
        }
        __bic_SR_register_on_exit(LPM0_bits);
    }
}

/* ============= Application main ============= */

int main(void)
{
    WDTCTL = WDTPW | WDTHOLD;

    clock_init_simple();
    node_gpio_init();

    // LEDs
    P1DIR |= BIT0;   // green
    P4DIR |= BIT6;   // red

    PM5CTL0 &= ~LOCKLPM5;

    // On reset: notify arbiter we may have reset mid-lock
    node_pulse_reset_on_gnt();
    g_lock_state = LOCK_IDLE;

    // Initial LED: no lock
    P1OUT &= ~BIT0;
    P4OUT |=  BIT6;

    __bis_SR_register(GIE);

    while (1)
    {
        lock_acquire();

        // lock held
        P1OUT |=  BIT0;
        P4OUT &= ~BIT6;

        __delay_cycles(1000000UL);  // ~1 s critical section simulation

        lock_release();

        // no lock
        P1OUT &= ~BIT0;
        P4OUT |=  BIT6;

        __delay_cycles(1000000UL);  // ~1 s between attempts
    }
}
