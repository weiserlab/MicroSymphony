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
    // if(!i) {P4OUT |= BIT6; P1OUT |= BIT0;} // indicate failure
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



static inline void wait_for_mail(void)
{
    while(!g_mail_flag);
    // __disable_interrupt();
    // while (!g_mail_flag) {
    //     __bis_SR_register(LPM0_bits | GIE);  // sleep, atomically set GIE+LPM

    //     // Woke up from ISR: interrupts are enabled here.
    //     __disable_interrupt();               // close race before rechecking
    // }
    // __enable_interrupt();
}

void test(void){
    uint32_t e1, e2, e3;
    build_msg_payload(payload, 1024u);

    timer_start();
    e3 = TA0R;
    lock_acquire();
    // uart0_println("Lock acquired");
    e1 = TA0R;
    // mailbox_send_bulk(NODE_INDEX, NODE_INDEX, payload, 1024u);
    send_dummy_message(NODE_INDEX, NODE_INDEX, 1024u, payload);
    e1 = TA0R - e1;
    // uart0_println("Bulk message sent");
    uint8_t src_id = 0u;
    uint16_t len = 0u;
    e2 = TA0R;
    mailbox_recv_msg(NODE_INDEX, &src_id, &len, payload);
    e2 = TA0R - e2;
    if(len != 1024u){
        // uart0_println("Bulk message received correctly");
    // }
    // else {
        uart0_println("Bulk message received incorrectly");
    }
    lock_release();
    e3 = TA0R - e3;
    uart0_println("Lock released");

    uart0_print("Bulk send time (uS): ");
    uart0_print_uint(((uint32_t)e1*8));
    uart0_println("");

    uart0_print("Bulk recv time (uS): ");
    uart0_print_uint(e2*8);
    uart0_println("");

    uart0_print("Lock total time (uS): ");
    uart0_print_uint(e3*8);
}

void ping_pong_test_sender(void)
{
    uint16_t size[] = {16u, 32u, 64u, 128u, 256u, 512u, 1024u};
    uint8_t i;
    uint32_t  e1, e2, e3, e4, e5, e0;

    uint8_t src_id = 0u;
    uint16_t len = 0u;
    uint8_t got;

    for (i=0; i<7; i++){

        build_msg_payload(payload, size[i]);

        timer_start();

        e0 = TA0R;
        e1 = TA0R;
        lock_acquire();
        // e4 = TA0R;
        send_dummy_message(1u, NODE_INDEX, size[i], payload);
        // e4 = TA0R - e4;
        lock_release();
        e1 = TA0R - e1;


        e2 = TA0R;
        wait_for_mail();
        g_mail_flag = 0u; // clear flag**
        e2 = TA0R - e2;


        e3 = TA0R;
        lock_acquire();
        // e5 = TA0R;
        (void)mailbox_recv_msg(NODE_INDEX, &src_id, &len, payload);
        // e5 = TA0R - e5;
        lock_release();
        e3 = TA0R - e3;
        e0 = TA0R - e0;

        if(len == size[i]){
            uart0_println("Experiment passed");
            uart0_print("Message size: ");
            uart0_print_uint(size[i]);
            uart0_println("");
            uart0_print("Write time (uS): ");
            uart0_print_uint(e1*8u);
            // uart0_print_uint(((uint32_t)e1*5000000u)/32768);
            uart0_println("");
            uart0_print("Notification time (uS): ");
            // uart0_print_uint(((uint32_t)e2*5000000u)/32768);
            uart0_print_uint(e2*8u);
            uart0_println("");
            uart0_print("Read time (uS): ");
            // uart0_print_uint(((uint32_t)e3*5000000u)/32768);
            uart0_print_uint(e3*8u);
            uart0_println("");

            // uart0_print("write time without lock (uS): ");
            // uart0_print_uint(e4*8u);
            // uart0_println("");

            // uart0_print("read time without lock (uS): ");
            // uart0_print_uint(e5*8u);
            // uart0_println("");

            uart0_print("Total round-trip time (uS): ");
            // uart0_print_uint(((uint32_t)(e0)*5000000u)/32768);
            uart0_print_uint(e0*8u);
            uart0_println(" ");
        }
        else {
            uart0_println("Experiment failed");
            uart0_print("Received src_id=");
            uart0_print_uint(src_id);
            uart0_print(", len=");
            uart0_print_uint(len);
            uart0_println("");
        }

        __delay_cycles(2000000); // sleep for 0.25 second

    }

}

void ping_pong_test_receiver(void)
{
    while(1){

        wait_for_mail();
        g_mail_flag = 0u; // clear flag**
        lock_acquire();
        // process_incoming_messages();
        uint8_t src_id = 0u;
        uint16_t len = 0u;
        uint8_t got;

        got = mailbox_recv_msg(NODE_INDEX, &src_id, &len, payload);
        send_dummy_message(src_id, NODE_INDEX, len, payload);
        lock_release();
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
    g_lock_state = LOCK_IDLE;

    /* LED: red ON (no lock), green OFF */
    P1OUT &= ~BIT0;
    P4OUT |= BIT6;

    __bis_SR_register(GIE);

    uart0_println("SPI Worker Node Started");

    // test();
    #if NODE_ID == 1u
        ping_pong_test_sender();
    #elif NODE_ID == 2u
        ping_pong_test_receiver();
    #endif

}


// No UART + LEDs

// #include <msp430.h>
// #include <stdint.h>
// #include "fram.h"
// #include "mailbox.h"
// #include "uart.h"

// /* Adjust per node build: IDs 1,2,3,... (index = ID-1) */
// #define NODE_ID         1u
// #define NODE_INDEX      (NODE_ID - 1u)

// /* REQ = P1.4, GNT = P1.3 */
// #define NODE_REQ_PIN    BIT4
// #define NODE_GNT_PIN    BIT3
// #define NODE_GNT_IV     0x08    /* P1.3 in P1IV */

// /* ---- Lock state ---- */

// typedef enum {
//     LOCK_IDLE = 0,
//     LOCK_WAIT_GRANT,
//     LOCK_HELD
// } lock_state_t;

// static volatile lock_state_t g_lock_state = LOCK_IDLE;
// static volatile uint8_t      g_mail_flag  = 0u;


// static const uint16_t spi_clk_div = 2u;

// /* ---- Clock ---- */

// static void clock_init_8mhz(void)
// {
//     CSCTL0_H = CSKEY >> 8;
//     CSCTL1   = DCOFSEL_6;
//     CSCTL2   = SELS__DCOCLK | SELM__DCOCLK;
//     CSCTL3   = DIVS__1 | DIVM__1;
//     CSCTL0_H = 0;
// }

// /* ---- GPIO ---- */

// static void node_gpio_init(void)
// {
//     /* REQ: P1.4 in, pulldown */
//     P1SEL0 &= (uint8_t)~NODE_REQ_PIN;
//     P1SEL1 &= (uint8_t)~NODE_REQ_PIN;

//     P1DIR  &= (uint8_t)~NODE_REQ_PIN;
//     P1REN  |= NODE_REQ_PIN;
//     P1OUT  &= (uint8_t)~NODE_REQ_PIN;

//     /* GNT: P1.3 in, pulldown, rising-edge interrupt */
//     P1SEL0 &= (uint8_t)~NODE_GNT_PIN;
//     P1SEL1 &= (uint8_t)~NODE_GNT_PIN;

//     P1DIR  &= (uint8_t)~NODE_GNT_PIN;
//     P1REN  |= NODE_GNT_PIN;
//     P1OUT  &= (uint8_t)~NODE_GNT_PIN;

//     P1IES  &= (uint8_t)~NODE_GNT_PIN;  /* low->high */
//     P1IFG  &= (uint8_t)~NODE_GNT_PIN;
//     P1IE   |= NODE_GNT_PIN;

//     /* LEDs */
//     // P1DIR |= BIT0;
//     // P4DIR |= BIT6;

// }

// /* High pulse on REQ */
// static void node_pulse_req_line(void)
// {
//     P1DIR |= NODE_REQ_PIN;
//     P1OUT |= NODE_REQ_PIN;
//     __delay_cycles(50u);
//     P1OUT &= (uint8_t)~NODE_REQ_PIN;
//     P1DIR &= (uint8_t)~NODE_REQ_PIN;
// }

// /* High pulse on GNT for reset notification at startup */
// static void node_pulse_reset_on_gnt(void)
// {
//     P1IE  &= (uint8_t)~NODE_GNT_PIN;
//     P1IFG &= (uint8_t)~NODE_GNT_PIN;

//     P1DIR |= NODE_GNT_PIN;
//     P1OUT |= NODE_GNT_PIN;
//     __delay_cycles(50u);
//     P1OUT &= (uint8_t)~NODE_GNT_PIN;
//     P1DIR &= (uint8_t)~NODE_GNT_PIN;

//     P1IFG &= (uint8_t)~NODE_GNT_PIN;
//     P1IE  |= NODE_GNT_PIN;
// }

// /* ---- Lock API ---- */

// static void lock_acquire(void)
// {
//     if (g_lock_state == LOCK_HELD) {
//         return;
//     }

//     __disable_interrupt();
//     g_lock_state = LOCK_WAIT_GRANT;
//     // uart0_println("Acquiring lock");
//     node_pulse_req_line();    /* request FRAM bus */

//     while (g_lock_state != LOCK_HELD) {
//         __bis_SR_register(LPM0_bits | GIE);
//         __disable_interrupt();
//     }
//     __enable_interrupt();

//     spi_enable(spi_clk_div);
//     // uart0_println("Lock acquired");
// }

// static void lock_release(void)
// {
//     if (g_lock_state != LOCK_HELD) {
//         return;
//     }

//     spi_disable();
//     // uart0_println("Releasing lock");
//     node_pulse_req_line();    /* release FRAM bus */

//     __disable_interrupt();
//     g_lock_state = LOCK_IDLE;
//     __enable_interrupt();

// }

// /* ---- Mailbox helpers ---- */

// static void process_incoming_messages(void)
// {
//     lock_acquire();
//     g_mail_flag = 0u; // clear flag**

//     P1OUT |= BIT0;
//     P4OUT &= ~BIT6;


//     uint8_t src_id = 0u;
//     uint16_t len = 0u;
//     uint8_t buf[MSG_SLOT_PAYLOAD_MAX];
//     uint8_t got;

//     for (;;) {
//         got = mailbox_recv_msg(0u, &src_id, &len, buf);

//         if (got == 0u) {
//             break;
//         }
//         uart0_print("Received msg from ");
//         uart0_print_uint(src_id); 
//         uart0_print(", len=");
//         uart0_print_uint(len);
//         uart0_println("");
//         /* TODO: handle message (src_id, buf[0..len-1]) */
//         /* For now, just toggle green LED once per message */
//         P1OUT ^= BIT0;
//     }

//     // notification bytes cleared by arbiter

//     lock_release();
//     P1OUT &= ~BIT0;
//     P4OUT |= BIT6;
// }

// /* ---- ISR ---- */

// #pragma vector = PORT1_VECTOR
// __interrupt void PORT1_ISR(void)
// {
//     uint16_t iv = P1IV;

//     if (iv == NODE_GNT_IV) {
//         if (g_lock_state == LOCK_WAIT_GRANT) {
//             g_lock_state = LOCK_HELD;
//         } else {
//             /* GNT pulse with no pending lock => mail notification */
//             // uart0_println("Mail");
//             g_mail_flag = 1u;
//         }
//         __bic_SR_register_on_exit(LPM0_bits);
//     }
// }

// /* Evaluations and Experiments*/

// static uint8_t payload[1024u];

// static void send_dummy_message(uint8_t dst_idx, uint8_t src_idx, uint16_t len, const uint8_t *payload)
// {
//     uint8_t i;
//     // i = mailbox_send_msg(dst_idx, src_idx, payload, len);
//     i = mailbox_send_bulk(dst_idx, src_idx, payload, len);
//     // if(!i) {P4OUT |= BIT6; P1OUT |= BIT0;} // indicate failure
//     // else {uart0_println("Message sent");uart0_print_uint(i);uart0_println("");}
// }


// static void build_msg_payload(uint8_t *buf, uint16_t len)
// {
//     uint16_t i;
//     for (i = 0u; i < len; i++) {
//         buf[i] = 0xEE;
//     }
// }


// /* ---- main ---- */

// void timer_start(void){
//     TA0CTL = TASSEL__SMCLK | MC__CONTINUOUS | TACLR | ID__8;  // SMCLK/8
//     TA0EX0 = TAIDEX_7;   
//     // TA0CTL = TASSEL__ACLK | MC__CONTINUOUS | TACLR | ID__1; // ACLK/1
//     // TA0EX0 = TAIDEX_4;// Extended divider /5  (TAIDEX_4 = divide by 5)
// }



// static inline void wait_for_mail(void)
// {
//     // while(!g_mail_flag);

//     __disable_interrupt();
//     while (!g_mail_flag) {
//         __bis_SR_register(LPM0_bits | GIE);  // sleep, atomically set GIE+LPM

//         // Woke up from ISR: interrupts are enabled here.
//         __disable_interrupt();               // close race before rechecking
//     }
//     __enable_interrupt();
// }

// void test(void){
//     uint32_t e1, e2, e3;
//     build_msg_payload(payload, 1024u);

//     timer_start();
//     e3 = TA0R;
//     lock_acquire();
//     // uart0_println("Lock acquired");
//     e1 = TA0R;
//     // mailbox_send_bulk(NODE_INDEX, NODE_INDEX, payload, 1024u);
//     send_dummy_message(NODE_INDEX, NODE_INDEX, 1024u, payload);
//     e1 = TA0R - e1;
//     // uart0_println("Bulk message sent");
//     uint8_t src_id = 0u;
//     uint16_t len = 0u;
//     e2 = TA0R;
//     mailbox_recv_msg(NODE_INDEX, &src_id, &len, payload);
//     e2 = TA0R - e2;
//     if(len != 1024u){
//         // uart0_println("Bulk message received correctly");
//     // }
//     // else {
//         uart0_println("Bulk message received incorrectly");
//     }
//     lock_release();
//     e3 = TA0R - e3;
//     uart0_println("Lock released");

//     uart0_print("Bulk send time (uS): ");
//     uart0_print_uint(((uint32_t)e1*8));
//     uart0_println("");

//     uart0_print("Bulk recv time (uS): ");
//     uart0_print_uint(e2*8);
//     uart0_println("");

//     uart0_print("Lock total time (uS): ");
//     uart0_print_uint(e3*8);
// }

// void ping_pong_test_sender(void)
// {
//     uint16_t size[] = {16u, 32u, 64u, 128u, 256u, 512u, 1024u};
//     uint8_t i;
//     uint32_t  e1, e2, e3, e4, e5, e0;

//     uint8_t src_id = 0u;
//     uint16_t len = 0u;
//     uint8_t got;

//     build_msg_payload(payload, 1024u);

//     for (i=0; i<7; i++){


//         // timer_start();

//         // e0 = TA0R;
//         // e1 = TA0R;
//         lock_acquire();
//         // e4 = TA0R;
//         send_dummy_message(1u, NODE_INDEX, size[i], payload);
//         // e4 = TA0R - e4;
//         lock_release();
//         // e1 = TA0R - e1;


//         // uart0_println("Waiting for mail...");
//         // e2 = TA0R;
//         wait_for_mail();
//         g_mail_flag = 0u; // clear flag**
//         // e2 = TA0R - e2;


//         // e3 = TA0R;
//         lock_acquire();
//         // e5 = TA0R;
//         (void)mailbox_recv_msg(NODE_INDEX, &src_id, &len, payload);
//         // e5 = TA0R - e5;
//         lock_release();
//         // e3 = TA0R - e3;
//         // e0 = TA0R - e0;

//         // if(len == size[i]){
//         //     uart0_println("Experiment passed");
//         //     uart0_print("Message size: ");
//         //     uart0_print_uint(size[i]);
//         //     uart0_println("");
//         //     uart0_print("Write time (uS): ");
//         //     uart0_print_uint(e1*8u);
//         //     // uart0_print_uint(((uint32_t)e1*5000000u)/32768);
//         //     uart0_println("");
//         //     uart0_print("Notification time (uS): ");
//         //     // uart0_print_uint(((uint32_t)e2*5000000u)/32768);
//         //     uart0_print_uint(e2*8u);
//         //     uart0_println("");
//         //     uart0_print("Read time (uS): ");
//         //     // uart0_print_uint(((uint32_t)e3*5000000u)/32768);
//         //     uart0_print_uint(e3*8u);
//         //     uart0_println("");

//         //     // uart0_print("write time without lock (uS): ");
//         //     // uart0_print_uint(e4*8u);
//         //     // uart0_println("");

//         //     // uart0_print("read time without lock (uS): ");
//         //     // uart0_print_uint(e5*8u);
//         //     // uart0_println("");

//         //     uart0_print("Total round-trip time (uS): ");
//         //     // uart0_print_uint(((uint32_t)(e0)*5000000u)/32768);
//         //     uart0_print_uint(e0*8u);
//         //     uart0_println(" ");
//         // }
//         // else {
//         //     uart0_println("Experiment failed");
//         //     uart0_print("Received src_id=");
//         //     uart0_print_uint(src_id);
//         //     uart0_print(", len=");
//         //     uart0_print_uint(len);
//         //     uart0_println("");
//         // }

//         __delay_cycles(2000000); // sleep for 0.25 second

//     }

// }

// void ping_pong_test_receiver(void)
// {
//     while(1){

//         wait_for_mail();
//         g_mail_flag = 0u; // clear flag**
//         lock_acquire();
//         // process_incoming_messages();
//         uint8_t src_id = 0u;
//         uint16_t len = 0u;
//         uint8_t got;

//         got = mailbox_recv_msg(NODE_INDEX, &src_id, &len, payload);
//         send_dummy_message(src_id, NODE_INDEX, len, payload);
//         lock_release();
//         }
// }



// int main(void){

//     uint32_t delay_cnt;
//     // spi_clk_div = 16u; // SMCLK / 2 = 4MHz
//     // change payload size in mailbox.h 

//     WDTCTL = WDTPW | WDTHOLD;

//     clock_init_8mhz();
//     // uart0_init(); //
//     node_gpio_init();

//     // clear any startup glitches
//     // P1IFG = 0u;

//     PM5CTL0 &= ~LOCKLPM5;

//     spi_init();

//     /* Reset pulse to arbiter (inform it we may have reset) */
//     node_pulse_reset_on_gnt();
//     g_lock_state = LOCK_IDLE;

//     /* LED: red ON (no lock), green OFF */
//     P1OUT &= ~BIT0;
//     P4OUT |= BIT6;

//     __bis_SR_register(GIE);

//     // uart0_println("SPI Worker Node Started");

//     // test();
//     #if NODE_ID == 1u
//         ping_pong_test_sender();
//     #elif NODE_ID == 2u
//         ping_pong_test_receiver();
//     #endif

//     __bis_SR_register(LPM0_bits | GIE);

// }
