#include <msp430.h>
#include <stdint.h>

/* Arbiter node: FRAM lock owner
 *
 * Wiring:
 *  Node1: REQ -> P1.4,  GNT <-> P1.3
 *  Node2: REQ -> P1.6,  GNT <-> P1.7
 *  Node3: REQ -> P1.2,  GNT <-> P1.5
 */

#define NUM_NODES    3

#define NODE1_ID     1
#define NODE2_ID     2
#define NODE3_ID     3

/* Request pins (node -> arbiter, all on P1) */
#define N1_REQ_PIN   BIT4   // P1.4
#define N2_REQ_PIN   BIT6   // P1.6
#define N3_REQ_PIN   BIT2   // P1.2
#define REQ_PINS     (N1_REQ_PIN | N2_REQ_PIN | N3_REQ_PIN)

/* Grant pins (arbiter <-> node, all on P1) */
#define N1_GNT_PIN   BIT3   // P1.3
#define N2_GNT_PIN   BIT7   // P1.7
#define N3_GNT_PIN   BIT5   // P1.5
#define GNT_PINS    (N1_GNT_PIN | N2_GNT_PIN | N3_GNT_PIN)

/* Lock state:
 *  0   = no one holds lock
 *  1..3 = node ID currently holding lock
 */
static volatile uint8_t g_lock_holder   = 0;

/* Edge events from nodes (set in ISR, consumed in main).
 * bit0 -> node 1 REQ
 * bit1 -> node 2 REQ
 * bit2 -> node 3 REQ
 */
static volatile uint8_t g_req_event_mask = 0;

/* FCFS queue of pending nodes */
static volatile uint8_t g_queue[NUM_NODES];
static volatile uint8_t g_q_len  = 0;

/* Flag: main should run scheduler */
static volatile uint8_t g_need_schedule = 0;

/* ===== Helpers: map bit -> node ID ===== */

static uint8_t bit_to_node(uint8_t bit_index)
{
    switch (bit_index) {
    case 0: return NODE1_ID;
    case 1: return NODE2_ID;
    case 2: return NODE3_ID;
    default: return 0;
    }
}

/* ===== Clock / GPIO init ===== */

static void clock_init_simple(void)
{
    CSCTL0_H = CSKEY >> 8;
    CSCTL1   = DCOFSEL_0;            // ~1 MHz
    CSCTL1  &= ~DCORSEL;
    CSCTL2   = SELS__DCOCLK | SELM__DCOCLK;
    CSCTL3   = DIVS__1 | DIVM__1;
    CSCTL0_H = 0;
}

static void arbiter_gpio_init(void)
{
    /* REQ pins: P1.2, P1.4, P1.6 (node -> arbiter) */
    P1SEL0 &= ~REQ_PINS;
    P1SEL1 &= ~REQ_PINS;

    P1DIR  &= ~REQ_PINS;       // inputs
    P1REN  |=  REQ_PINS;       // enable resistor
    P1OUT  |=  REQ_PINS;       // pull-up

    P1IES  |=  REQ_PINS;       // interrupt on high->low
    P1IFG  &= ~REQ_PINS;
    P1IE   |=  REQ_PINS;

    /* GNT pins: normally INPUT with pull-up (for node reset pulses).
     * Arbiter temporarily drives them low/high to grant lock.
     */

    // Node1 GNT: P1.3
    P1SEL0 &= ~GNT_PINS;
    P1SEL1 &= ~GNT_PINS;

    P1DIR  &= ~GNT_PINS;     // input
    P1REN  |=  GNT_PINS;
    P1OUT  |=  GNT_PINS;     // pull-up

    P1IES  |=  GNT_PINS;     // interrupt on high->low (reset)
    P1IFG  &= ~GNT_PINS;
    P1IE   |=  GNT_PINS;
}

/* ===== Queue helpers (FCFS) ===== */

static uint8_t queue_contains(uint8_t node_id)
{
    uint8_t i;
    for (i = 0; i < g_q_len; i++) {
        if (g_queue[i] == node_id) {
            return 1;
        }
    }
    return 0;
}

static void queue_push(uint8_t node_id)
{
    if (g_q_len >= NUM_NODES) {
        return;
    }
    if (queue_contains(node_id)) {
        return;
    }
    g_queue[g_q_len++] = node_id;
}

static uint8_t queue_pop(void)
{
    uint8_t i;
    uint8_t first;

    if (g_q_len == 0) {
        return 0;
    }

    first = g_queue[0];
    for (i = 0; i < g_q_len - 1; i++) {
        g_queue[i] = g_queue[i + 1];
    }
    g_q_len--;

    return first;
}

static void queue_remove(uint8_t node_id)
{
    uint8_t i, j = 0;

    for (i = 0; i < g_q_len; i++) {
        if (g_queue[i] != node_id) {
            g_queue[j++] = g_queue[i];
        }
    }
    g_q_len = j;
}

/* ===== Grant pulse (main, not ISR) ===== */

static void arbiter_pulse_grant_line(uint8_t node_id)
{
    volatile uint8_t *pdir = &P1DIR;
    volatile uint8_t *pout = &P1OUT;
    uint8_t           pin  = 0;

    switch (node_id) {
    case NODE1_ID:
        pin  = N1_GNT_PIN;
        break;
    case NODE2_ID:
        pin  = N2_GNT_PIN;
        break;
    case NODE3_ID:
        pin  = N3_GNT_PIN;
        break;
    default:
        return;
    }

    // disable reset detection
    P1IE  &= ~pin;
    P1IFG &= ~pin;

    // Drive low briefly
    *pdir |=  pin;        // output
    *pout &= ~pin;        // low
    __delay_cycles(250);  // ~250 us

    // Release back to input with pull-up
    *pout |=  pin;        // OUT=1 for pull-up
    *pdir &= ~pin;        // input again

    // Re-enable reset detection
    P1IFG &= ~pin;
    P1IE  |=  pin;

}

/* ===== Scheduler: FCFS ===== */

static void arbiter_schedule(void)
{
    uint8_t next;

    if (g_lock_holder != 0) {
        return; // lock busy
    }

    next = queue_pop();
    if (next == 0) {
        // no pending; ensure "lock held" LED is OFF
        P4OUT &= ~BIT6;
        return;
    }

    g_lock_holder = next;
    arbiter_pulse_grant_line(next);

    // some node holds lock -> red LED ON
    P4OUT |= BIT6;
}

/* ===== Process REQ events (main) ===== */

static void arbiter_process_req_events(void)
{
    uint8_t events;
    uint8_t bit;
    uint8_t node_id;

    if (!g_req_event_mask) {
        return;
    }

    __disable_interrupt();
    events = g_req_event_mask;
    g_req_event_mask = 0;
    __enable_interrupt();

    if (!events) {
        return;
    }

    // Toggle request-indicator LED (P1.0) whenever we see any REQ edge
    P1OUT ^= BIT0;

    // For each node with an event bit set:
    for (bit = 0; bit < NUM_NODES; bit++) {
        if (!(events & (1u << bit))) {
            continue;
        }

        node_id = bit_to_node(bit);
        if (node_id == 0) {
            continue;
        }

        if (g_lock_holder == node_id) {
            // REQ pulse from current holder -> treat as release
            g_lock_holder = 0;
            P4OUT &= ~BIT6;    // no lock held
            g_need_schedule = 1;
        } else {
            // REQ from some other node -> treat as request
            queue_push(node_id);
            g_need_schedule = 1;
        }
    }
}

/* ===== Port 1 ISR: REQ + RESET events ===== */

#pragma vector = PORT1_VECTOR
__interrupt void PORT1_ISR(void)
{
    uint16_t iv = P1IV; // reading clears highest-priority IFG

    switch (iv) {




    case 0x0A: // P1.4 (Node 1 REQ)
        g_req_event_mask |= (1u << 0);
        break;
    case 0x0E: // P1.6 (Node 2 REQ)
        g_req_event_mask |= (1u << 1);
        break;
    case 0x06: // P1.2 (Node 3 REQ)
        g_req_event_mask |= (1u << 2);
        break;

    case 0x08: // P1.3 (Node 1 GNT -> RESET pulse)
        queue_remove(NODE1_ID);
        if (g_lock_holder == NODE1_ID) {
            g_lock_holder = 0;
            P4OUT &= ~BIT6;
            g_need_schedule = 1;
        }
        break;
    case 0x10: // P1.7 (Node 2 GNT -> RESET pulse)
        queue_remove(NODE2_ID);
        if (g_lock_holder == NODE2_ID) {
            g_lock_holder = 0;
            P4OUT &= ~BIT6;
            g_need_schedule = 1;
        }
        break;
    case 0x0C: // P1.5 (Node 3 GNT -> RESET pulse)
        queue_remove(NODE3_ID);
        if (g_lock_holder == NODE3_ID) {
            g_lock_holder = 0;
            P4OUT &= ~BIT6;
            g_need_schedule = 1;
        }
        break;

    default:
        break;
    }

    __bic_SR_register_on_exit(LPM0_bits);
}

/* ===== main ===== */

int main(void)
{
    WDTCTL = WDTPW | WDTHOLD;

    clock_init_simple();
    arbiter_gpio_init();

    // LEDs
    P1DIR |= BIT0;   // P1.0: request indication (toggles on REQ)
    P4DIR |= BIT6;   // P4.6: lock held indication (red)

    PM5CTL0 &= ~LOCKLPM5;    // release GPIO from high-Z

    // Clear any startup glitches
    P1IFG &= ~(REQ_PINS | GNT_PINS);

    // Initialise LEDs: no lock held
    P1OUT &= ~BIT0;  // green off
    P4OUT &= ~BIT6;  // red off

    g_lock_holder   = 0;
    g_need_schedule = 0;
    g_q_len         = 0;

    __bis_SR_register(GIE);  // enable global interrupts

    while (1) {
        arbiter_process_req_events();

        if (g_need_schedule) {
            g_need_schedule = 0;
            arbiter_schedule();
        }

        __bis_SR_register(LPM0_bits | GIE);
        __no_operation();
    }
}
