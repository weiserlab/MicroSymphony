#include <msp430.h>
#include <stdint.h>
#include <string.h>

#include "uart.h"
#include "fram.h"
#include "mailbox.h"
#include "worker.h"
#include "hypercam_full_256.h"     // <--- 256/512/768 HV table

/* ================================================================
 * CONFIGURATION
 * ================================================================ */

/* Total cooperating MCUs */
#define N_NODES       3u      /* change to 3 or 4 later */

/* Node ID: 0 .. N_NODES-1 (set individually per build) */
#define NODE_ID       0u

/* MNIST image size */
#define IMG_W         28u
#define IMG_H         28u
#define NUM_PIXELS    (IMG_W * IMG_H)

/* Bits per node slice */
#define BITS_PER_NODE   (HV_DIM_BITS / N_NODES)
#define WORDS_PER_NODE  (BITS_PER_NODE / 32u)
#define BYTES_PER_NODE  (BITS_PER_NODE / 8u)

/* Accumulator type */
typedef int16_t acc_t;

// declare external HV tables
extern const uint32_t X0_words[WORDS_PER_HV];
extern const uint32_t value_hv[256][WORDS_PER_HV];
extern const uint32_t class_hv[NUM_CLASSES][WORDS_PER_HV];
extern const uint8_t sample_image[NUM_PIXELS];
// extern

uint32_t  e1, e2, e3;


void timer_start(void){
    TA0CTL = TASSEL__SMCLK | MC__CONTINUOUS | TACLR | ID__8;  // SMCLK/8
    TA0EX0 = TAIDEX_7;   
}


/* ================================================================
 * COMMUNICATION STUBS (YOU FILL THESE)
 * ================================================================ */

/* Now takes uint8_t* slice (BYTES_PER_NODE bytes) */
static void send_hv_slice_to_node0(const uint8_t *slice)
{
    /* TODO: send BYTES_PER_NODE bytes to node 0 (SPI/UART/mailbox) */
    lock_acquire();
    uint8_t resp=0;
    resp = mailbox_send_bulk(0u, NODE_ID, slice, BYTES_PER_NODE);
    // __delay_cycles(40000); // small delay (switch to level based)
    // uart0_println("Send failed");
    lock_release();
    if (!resp) {P4OUT |= BIT6; P1OUT |= BIT0;}

}

#if NODE_ID == 0
/* Now receives uint8_t* slice (BYTES_PER_NODE bytes) */
static uint8_t recv_hv_slice_from_node(uint8_t *src, uint8_t *dst)
{
    /* TODO: receive BYTES_PER_NODE bytes from node src */
    lock_acquire();
    uint16_t len = 0u;
    uint8_t resp = 1;
    resp = mailbox_recv_msg(0u, src, &len, dst);

    lock_release();
    return resp;
}
#endif


/* ================================================================
 * BIT HELPERS (LOCAL SLICE ONLY)
 * ================================================================ */

static uint8_t get_bit(const uint32_t *words, uint16_t bit)
{
    uint16_t w = bit / 32u;
    uint16_t b = bit % 32u;
    return (words[w] >> b) & 1u;
}

/* Rotate the FULL HV (all WORDS_PER_HV words) left by 1 bit.
 * This matches the Python / training rotation, independent of node split.
 */
static void rotate_left1_full(uint32_t *words)
{
    uint32_t carry = 0, next = 0;
    uint16_t i;
    for (i = 0; i < WORDS_PER_HV; i++)
    {
        next = (words[i] >> 31) & 1u;
        words[i] = (words[i] << 1) | carry;
        carry = next;
    }
    /* wrap-around bit */
    if (carry)
        words[0] |= 1u;
}


/* ================================================================
 * GET GLOBAL SLICE OFFSET FOR THIS NODE
 * ================================================================ */

static inline uint16_t global_bit_offset(void)
{
    return NODE_ID * BITS_PER_NODE;
}


/* ================================================================
 * ENCODING (HV-SPLIT)
 * ================================================================ */

/* slice_out is now uint8_t*, length BYTES_PER_NODE. */
static void encode_image_slice(const uint8_t *img,
                               uint8_t *slice_out)
{
    acc_t  acc[BITS_PER_NODE];
    /* full position HV for global rotation */
    uint32_t pos_full[WORDS_PER_HV];
    /* this node's bound slice */
    uint32_t bound[WORDS_PER_NODE];

    uint16_t i, b, k;

    // uart0_println("Encoding image slice...");

    // uart0_println("1...");
    // uart0_println("Clearing accumulator...");
    /* 1. Clear accumulator */
    for (i = 0; i < BITS_PER_NODE; i++)
        acc[i] = 0;


    /* 2. Initialize FULL position HV:
     *    Copy entire X0_words (global HV), not just this node's slice.
     */
    for (i = 0; i < WORDS_PER_HV; i++)
        pos_full[i] = X0_words[i];

    // uart0_println("Position HV full initialized.");
    /* Node's slice word offset in the full HV */
    uint16_t off = global_bit_offset() / 32u;   // word offset

    /* 3. Process each pixel */
    for (k = 0; k < NUM_PIXELS; k++)
    {
        // uart0_println("Processing pixel:");
        // uart0_print_uint(k);
        // uart0_println("");
        uint8_t v = img[k];  /* pixel 0..255 */

        /* Slice of value HV for this pixel:
         * value_hv[v] is full HV; we take this node's slice.
         * Slice of position HV for this pixel:
         * pos_full is full HV; we take this node's slice.
         */
        for (i = 0; i < WORDS_PER_NODE; i++)
            bound[i] = pos_full[off + i] ^ value_hv[v][off + i];

        /* Update acc */
        for (b = 0; b < BITS_PER_NODE; b++)
        {
            uint8_t bit = get_bit(bound, b);
            acc[b] += (bit ? 1 : -1);
        }

        /* Permute FULL position HV */
        rotate_left1_full(pos_full);
    }

    // uart0_println("Finalizing slice...");
    /* 4. Majority vote → slice_out (byte-packed) */
    for (i = 0; i < BYTES_PER_NODE; i++)
        slice_out[i] = 0;

    // uart0_println("Performing majority vote...");
    for (b = 0; b < BITS_PER_NODE; b++)
    {
        uint8_t bit = (acc[b] > 0);
        if (bit) {
            /* bit b → byte index = b/8, bit position = b%8 (LSB-first) */
            uint16_t byte_idx = b >> 3;
            uint8_t bit_pos = b & 7u;
            slice_out[byte_idx] |= (uint8_t)(1u << bit_pos);
        }
    }
}


/* ================================================================
 * NODE 0: COLLECT SLICES, FORM FINAL HV, CLASSIFY
 * ================================================================ */

#if NODE_ID == 0
static uint32_t img_hv_full[WORDS_PER_HV];

/* Now takes uint8_t* slice (BYTES_PER_NODE bytes) */
static void combine_slice(uint8_t src, const uint8_t *slice)
{
    /* dst byte offset in full HV */
    uint16_t byte_off = (uint16_t)(BYTES_PER_NODE * src);

    /* Copy raw bytes into img_hv_full (reinterpret as uint8_t*) */
    uint8_t *dst_bytes = (uint8_t *)img_hv_full;

    uint16_t i;
    for (i = 0; i < BYTES_PER_NODE; i++) {
        dst_bytes[byte_off + i] = slice[i];
    }
}

static uint8_t classify_image(void)
{
    /* Full-HV Hamming classification using class_hv[][] */

    uint32_t best = UINT32_MAX;
    uint8_t best_c = 0;

    uint8_t c;
    uint16_t i;

    for (c = 0; c < NUM_CLASSES; c++) {
        uint32_t dist = 0;
        for (i = 0; i < WORDS_PER_HV; i++) {
            uint32_t x = img_hv_full[i] ^ class_hv[c][i];

            /* 32-bit SWAR popcount (portable, efficient on MSP430 GCC) */
            x = x - ((x >> 1) & 0x55555555u);
            x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
            uint32_t pc = (((x + (x >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24;

            dist += pc;
        }
        if (dist < best) {
            best = dist;
            best_c = c;
        }
    }

    return best_c;
}
#endif


/* ================================================================
 * MAIN PROCESSING
 * ================================================================ */

// static uint8_t image_buf[NUM_PIXELS];   // Fill this with your MNIST image

static void node_run(void)
{
    uint8_t my_slice[BYTES_PER_NODE];

    /* image already loaded*/

    /* Compute this node's slice */
    // uart0_println("Computing node slice...");
    encode_image_slice(sample_image, my_slice);
    // uart0_println("Image slice encoded.");

#if NODE_ID == 0
    /* Root node keeps its own slice */
    combine_slice(0, my_slice);

    uint8_t src=0;
    uint8_t counter = 0;

    
    // uart0_println("Waiting for slice...");
    timer_start();
    e1 = TA0R;
    /* For N_NODES == 1 this loop is skipped (no remote nodes). */
    
    while (counter < (uint8_t)(N_NODES - 1u))
    {

        if (!recv_hv_slice_from_node(&src, my_slice)) {
            /* No message yet; you can spin, sleep, or just retry */
            __delay_cycles(500000u);
            continue;
        }

        /* Expect slices from nodes 1..N_NODES-1 */
        if (src != 0u && src < N_NODES) {
            // uart0_print("Received slice from node ");
            // uart0_print_uint(src);
            // // print the slice for debugging
            // uint8_t i;
            // for (i = 0; i < BYTES_PER_NODE; i++) {
            //     uart0_print_uint(my_slice[i]);
            //     uart0_print(" ");
            // }
            // uart0_println("");

            combine_slice(src, my_slice);
            counter++;
        } else {
            // uart0_print("Unexpected src=");
            // uart0_print_uint(src);
            // uart0_println("");
            ;
        }

        // __delay_cycles(16000000u);
    }

    e1 = TA0R - e1;
    /* Now img_hv_full[] contains the full HV_DIM_BITS final HV */

    timer_start();
    e2 = TA0R;
    uint8_t predicted = classify_image();
    e2 = TA0R - e2;

    // uart0_print("Predicted class: ");
    // uart0_print_uint(predicted);
    // uart0_println("");

    // uart0_print("Communication time (microseconds): ");
    // uart0_print_uint(e1*8u);
    // uart0_println("");

    // uart0_print("Classification time (microseconds): ");
    // uart0_print_uint(e2*8u);
    // uart0_println("");


#else
    /* Non-root nodes send their slice to node 0 */
    // uart0_println("Sending slice to node 0...");
    send_hv_slice_to_node0(my_slice);
    // uart0_print("Slice sent to node 0 from node ");
    // uart0_print_uint(NODE_ID);
    // uart0_println("");

#endif
    // spi_deinit();  // optional: free SPI if not needed anymore

}


/* ================================================================
 * MSP430 MAIN
 * ================================================================ */

int main(void)
{


    WDTCTL = WDTPW | WDTHOLD;

    /* TODO: clock / UART / SPI init */
    clock_init_8mhz();
    // uart0_init();
    node_gpio_init();


    PM5CTL0 &= ~LOCKLPM5;   // Disable the GPIO power-on default high-impedance mode

    spi_init();
    /* Reset pulse to arbiter (inform it we may have reset) */
    node_pulse_reset_on_gnt();


    P1OUT &= ~BIT0;
    P4OUT |= BIT6;


    __bis_SR_register(GIE);

    // uart0_println("HyperCam HDC Node Starting...");

    node_run();
    __bis_SR_register(LPM0_bits | GIE);
    
}



