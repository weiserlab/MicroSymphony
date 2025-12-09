// ======================= Arbiter (Master) =======================
//                   MSP430FR5969 (Arbiter)
//                 ---------------------------
//            /|\ |                       XIN|-
//             |  |                           | 32 kHz Crystal (optional)
//             ---|RST                    XOUT|-
//                |                           |
//                |                       P2.0|-> UART TX  (UCA0TXD, to USB-UART / PC)
//                |                       P2.1|<- UART RX  (UCA0RXD, from USB-UART / PC)
//                |                           |
//                |                       P1.6|-> FRAM SI   (UCB0SIMO, MOSI)
//                |                       P1.7|<- FRAM SO   (UCB0SOMI, MISO)
//                |                       P2.2|-> FRAM SCK  (UCB0CLK)
//                |                       P1.5|-> FRAM CS#  (chip select, active low)
//                |                           |
//                |                       P1.4|<- REQ1      (Node1 -> Arbiter, request)
//                |                       P1.3|<-> GNT1     (Arbiter <-> Node1, grant / reset)
//                |                       P1.2|<- REQ2      (Node2 -> Arbiter, request)
//                |                       P3.0|<-> GNT2     (Arbiter <-> Node2, grant / reset)
//                |                       P3.5|<- REQ3      (Node3 -> Arbiter, request)
//                |                       P3.6|<-> GNT3     (Arbiter <-> Node3, grant / reset)
//                |                           |
//                |                       P1.0|-> LED (activity / REQ seen)
//                |                       P4.6|-> LED (FRAM bus owned by some node)
//                |                           |
//               GND|---------------------------

#include <msp430.h>
#include <stdint.h>
#include "fram.h"
#include "mailbox.h"
#include "uart.h"

/* Arbiter supports 3 physical nodes for now */
#define NUM_NODES    3u

#define NODE1_ID     1u
#define NODE2_ID     2u
#define NODE3_ID     3u

/* REQ pins on arbiter */
#define N1_REQ_P1_PIN   BIT4      /* P1.4 */
#define N2_REQ_P1_PIN   BIT2      /* P1.2 */
#define N3_REQ_P3_PIN   BIT5      /* P3.5 */

/* GNT pins on arbiter */
#define N1_GNT_P1_PIN   BIT3      /* P1.3 */
#define N2_GNT_P3_PIN   BIT0      /* P3.0 */
#define N3_GNT_P3_PIN   BIT6      /* P3.6 */


/* ---- Clock ---- */

static void clock_init_8mhz(void)
{
    CSCTL0_H = CSKEY >> 8;
    CSCTL1   = DCOFSEL_6;
    CSCTL2   = SELS__DCOCLK | SELM__DCOCLK;
    CSCTL3   = DIVS__1 | DIVM__1;
    CSCTL0_H = 0;
}

/* ---- Globals ---- */

/* 0   = no one holds bus
 * 1..3 = node ID currently owning SPI/FRAM bus
 */
static volatile uint8_t g_lock_holder     = 0u;

/* Pending REQ events encoded as bits 0..(NUM_NODES-1) */
static volatile uint8_t g_req_event_mask  = 0u;

/* FCFS queue */
static volatile uint8_t g_queue[NUM_NODES];
static volatile uint8_t g_q_len           = 0u;

/* Need to schedule */
static volatile uint8_t g_need_schedule   = 0u;

/* ---- Queue helpers ---- */

static uint8_t index_to_node_id(uint8_t idx)
{
    if (idx == 0u) return NODE1_ID;
    if (idx == 1u) return NODE2_ID;
    if (idx == 2u) return NODE3_ID;
    return 0u;
}

static uint8_t queue_contains(uint8_t node_id)
{
    volatile uint8_t i;

    for (i = 0u; i < g_q_len; i++) {
        if (g_queue[i] == node_id) {
            return 1u;
        }
    }
    return 0u;
}

static void queue_push(uint8_t node_id)
{
    if (g_q_len >= NUM_NODES) {
        return;
    }
    if (queue_contains(node_id) != 0u) {
        return;
    }
    g_queue[g_q_len] = node_id;
    g_q_len++;
}

static uint8_t queue_pop(void)
{
    uint8_t i;
    uint8_t first;

    if (g_q_len == 0u) {
        return 0u;
    }

    first = g_queue[0];
    for (i = 1u; i < g_q_len; i++) {
        g_queue[i - 1u] = g_queue[i];
    }
    g_q_len--;

    return first;
}

static void queue_remove(uint8_t node_id)
{
    uint8_t i;
    uint8_t j = 0u;

    for (i = 0u; i < g_q_len; i++) {
        if (g_queue[i] != node_id) {
            g_queue[j] = g_queue[i];
            j++;
        }
    }
    g_q_len = j;
}

/* ---- GPIO init ---- */

static void arbiter_gpio_init(void)
{
    /* Port 1: N1_REQ=P1.4, N2_REQ=P1.2, N1_GNT=P1.3 */
    P1SEL0 &= ~(N1_REQ_P1_PIN | N2_REQ_P1_PIN | N1_GNT_P1_PIN);
    P1SEL1 &= ~(N1_REQ_P1_PIN | N2_REQ_P1_PIN | N1_GNT_P1_PIN);

    /* REQ inputs, pulldown, rising edge interrupt */
    P1DIR &= ~(N1_REQ_P1_PIN | N2_REQ_P1_PIN);
    P1REN |= (N1_REQ_P1_PIN | N2_REQ_P1_PIN);
    P1OUT &= ~(N1_REQ_P1_PIN | N2_REQ_P1_PIN);

    P1IES &= ~(N1_REQ_P1_PIN | N2_REQ_P1_PIN);
    P1IFG &= ~(N1_REQ_P1_PIN | N2_REQ_P1_PIN);
    P1IE  |= (N1_REQ_P1_PIN | N2_REQ_P1_PIN);

    /* GNT1 input, pulldown, rising edge for reset */
    P1DIR &= ~N1_GNT_P1_PIN;
    P1REN |= N1_GNT_P1_PIN;
    P1OUT &= ~N1_GNT_P1_PIN;
    
    P1IES &= ~N1_GNT_P1_PIN;
    P1IFG &= ~N1_GNT_P1_PIN;
    P1IE  |= N1_GNT_P1_PIN;

    /* Port 3: N3_REQ=P3.5, N2_GNT=P3.0, N3_GNT=P3.6 */
    P3SEL0 &= ~(N3_REQ_P3_PIN | N2_GNT_P3_PIN | N3_GNT_P3_PIN);
    P3SEL1 &= ~(N3_REQ_P3_PIN | N2_GNT_P3_PIN | N3_GNT_P3_PIN);

    /* REQ3 */
    P3DIR &= ~N3_REQ_P3_PIN;
    P3REN |= N3_REQ_P3_PIN;
    P3OUT &= ~N3_REQ_P3_PIN;
    
    P3IES &= ~N3_REQ_P3_PIN;
    P3IFG &= ~N3_REQ_P3_PIN;
    P3IE  |= N3_REQ_P3_PIN;

    /* GNT2, GNT3 inputs with pulldown, rising edge for reset */
    P3DIR &= ~(N2_GNT_P3_PIN | N3_GNT_P3_PIN);
    P3REN |= (N2_GNT_P3_PIN | N3_GNT_P3_PIN);
    P3OUT &= ~(N2_GNT_P3_PIN | N3_GNT_P3_PIN);

    P3IES &= ~(N2_GNT_P3_PIN | N3_GNT_P3_PIN);
    P3IFG &= ~(N2_GNT_P3_PIN | N3_GNT_P3_PIN);
    P3IE  |= (N2_GNT_P3_PIN | N3_GNT_P3_PIN);

    /* LEDs */
    P1DIR |= BIT0;
    P4DIR |= BIT6;
}

/* ---- GNT pulses ---- */

static void gnt_pulse_node(uint8_t node_id)
{
    volatile uint8_t *pdir;
    volatile uint8_t *pout;
    uint8_t port_bit;

    if (node_id == NODE1_ID) {
        pdir     = &P1DIR;
        pout     = &P1OUT;
        port_bit = N1_GNT_P1_PIN;
    } else if (node_id == NODE2_ID) {
        pdir     = &P3DIR;
        pout     = &P3OUT;
        port_bit = N2_GNT_P3_PIN;
    } else if (node_id == NODE3_ID) {
        pdir     = &P3DIR;
        pout     = &P3OUT;
        port_bit = N3_GNT_P3_PIN;
    } else {
        return;
    }

    /* Disable reset interrupt on this pin during pulse */
    if (node_id == NODE1_ID) {
        P1IE  &= (uint8_t)~port_bit;
        P1IFG &= (uint8_t)~port_bit;
    } else {
        P3IE  &= (uint8_t)~port_bit;
        P3IFG &= (uint8_t)~port_bit;
    }

    *pdir |=  port_bit;
    *pout |=  port_bit;
    __delay_cycles(50u);
    *pout &= (uint8_t)~port_bit;
    *pdir &= (uint8_t)~port_bit;

    if (node_id == NODE1_ID) {
        P1IFG &= (uint8_t)~port_bit;
        P1IE  |= port_bit;
    } else {
        P3IFG &= (uint8_t)~port_bit;
        P3IE  |= port_bit;
    }
}

/* ---- Scheduler ---- */

static void arbiter_schedule(void)
{
    uint8_t next;

    if (g_lock_holder != 0u) {
        return;
    }

    next = queue_pop();
    if (next == 0u) {
        P4OUT &= ~BIT6;
        return;
    }

    g_lock_holder = next;
    gnt_pulse_node(next);   /* grant bus */

    P4OUT |= BIT6;

}

/* ---- Process REQ events ---- */

static void arbiter_process_req_events(void)
{
    uint8_t events;
    uint8_t bit;
    uint8_t node_id;

    if (g_req_event_mask == 0u) {
        return;
    }

    __disable_interrupt();
    events           = g_req_event_mask;
    g_req_event_mask = 0u;
    __enable_interrupt();

    P1OUT ^= BIT0; /* activity indicator */

    for (bit = 0u; bit < NUM_NODES; bit++) {
        if ((events & (1u << bit)) == 0u) {
            continue;
        }

        node_id = index_to_node_id(bit);
        if (node_id == 0u) {
            continue;
        }

        if (g_lock_holder == node_id) {
            /* Current holder pulsed REQ => release. */
            g_lock_holder = 0u;
            P4OUT &= ~BIT6;
            g_need_schedule = 1u;
        } else {
            /* New request */
            queue_push(node_id);
            g_need_schedule = 1u;
        }
    }
}

/* ---- Notification from FRAM ---- */

static void arbiter_check_notifications(void)
{
    uint8_t notif;
    uint8_t mask;
    uint8_t idx;
    uint8_t node_id;

    if (g_lock_holder != 0u) {
        return;
    }

    fram_read_bytes(FRAM_NOTIF_BOX_ADDR, &notif, 1u);
    if (notif == 0u) {
        // uart0_println("No notifications");
        return;
    }
    // uart0_println("Notifications received");
    // uart0_print_hex(notif);
    // uart0_println("");

    mask = 1u;
    for (idx = 0u; idx < NUM_NODES; idx++) {
        if ((notif & mask) != 0u) {
            node_id = index_to_node_id(idx);
            if (node_id != 0u && !queue_contains(node_id)) { // check this condition just in case
                gnt_pulse_node(node_id); /* "you have mail" */
                // uart0_print("Notified node ");
                // uart0_print_uint(node_id);
                // uart0_println("");
            }
        }
        mask <<= 1;
    }

    
    // notif &= (uint8_t)~((uint8_t)((1u << NUM_NODES) - 1u)); /* Clear only serviced bits */
    notif = 0u; /* simplest: clear entire notif byte */
    fram_write_bytes(FRAM_NOTIF_BOX_ADDR, &notif, 1u);
}

/* ---- ISRs ---- */

#pragma vector = PORT1_VECTOR
__interrupt void PORT1_ISR(void)
{
    uint16_t iv = P1IV;

    switch (iv) {
    case 0x06:  /* P1.2 = N2_REQ */
        g_req_event_mask |= (1u << 1);
        break;
    case 0x08:  /* P1.3 = N1_GNT reset pulse */
        queue_remove(NODE1_ID);
        if (g_lock_holder == NODE1_ID) {
            g_lock_holder = 0u;
            P4OUT &= ~BIT6;
            g_need_schedule = 1u;
            // uart_print_str("Node 1 detected\r\n");
        }
        break;
    case 0x0A:  /* P1.4 = N1_REQ */
        g_req_event_mask |= (1u << 0);
        break;
    default:
        break;
    }

    __bic_SR_register_on_exit(LPM0_bits);
}

#pragma vector = PORT3_VECTOR
__interrupt void PORT3_ISR(void)
{
    uint16_t iv = P3IV;

    switch (iv) {
    case 0x02:  /* P3.0 = N2_GNT reset pulse */
        queue_remove(NODE2_ID);
        if (g_lock_holder == NODE2_ID) {
            g_lock_holder = 0u;
            P4OUT &= ~BIT6;
            g_need_schedule = 1u;
            // uart_print_str("Node 2 detected\r\n");
        }
        break;
    case 0x0C:  /* P3.5 = N3_REQ */
        g_req_event_mask |= (1u << 2);
        break;
    case 0x0E:  /* P3.6 = N3_GNT reset pulse */
        queue_remove(NODE3_ID);
        if (g_lock_holder == NODE3_ID) {
            g_lock_holder = 0u;
            P4OUT &= ~BIT6;
            g_need_schedule = 1u;
            // uart_print_str("Node 3 detected\r\n");
        }
        break;
    default:
        break;
    }

    __bic_SR_register_on_exit(LPM0_bits);
}

/* ---- main ---- */

int main(void)
{
    uint8_t zero = 0u;

    WDTCTL = WDTPW | WDTHOLD;

    clock_init_8mhz();
    uart0_init();
    arbiter_gpio_init();
    spi_pins_init_once();
    
    fram_init();
    // if (fram_init()) uart0_println("FRAM initialized");
    // else { uart0_println("FRAM init failed"); P4OUT |= BIT6; P1OUT |= BIT0; return 0; }
    mailbox_init_layout();   /* initialize all boxes + clear notif */

    // Clear any startup glitches
    P1IFG = 0u;
    P3IFG = 0u;

    P1OUT |= BIT0;
    P4OUT &= ~BIT6;

    PM5CTL0 &= ~LOCKLPM5;



    g_lock_holder     = 0u;
    g_q_len           = 0u;
    g_req_event_mask  = 0u;
    g_need_schedule   = 0u;

    /* Ensure notif byte is zero */
    fram_write_bytes(FRAM_NOTIF_BOX_ADDR, &zero, 1u);

    // uart0_println("Arbiter ready");

    __bis_SR_register(GIE);

    for (;;) {
        arbiter_process_req_events();

        if (g_need_schedule != 0u) {
            g_need_schedule = 0u;
            arbiter_schedule();
        }

        __disable_interrupt();
        arbiter_check_notifications(); 

        __bis_SR_register(LPM0_bits | GIE);
        __enable_interrupt();
    }
}
