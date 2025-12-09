#include <msp430.h>
#include <stdint.h>
#include "fram.h"

/* FRAM opcodes */
#define FRAM_CMD_WREN   0x06
#define FRAM_CMD_WRDI   0x04
#define FRAM_CMD_RDSR   0x05
#define FRAM_CMD_WRSR   0x01
#define FRAM_CMD_WRITE  0x02
#define FRAM_CMD_READ   0x03
#define FRAM_CMD_RDID   0x9F

/* Chip Select: P1.5 (active low) */
#define FRAM_CS_DIR     P1DIR
#define FRAM_CS_OUT     P1OUT
#define FRAM_CS_REN     P1REN
#define FRAM_CS_PIN     BIT5

#define FRAM_CS_LOW()   do {                     \
    FRAM_CS_DIR |= FRAM_CS_PIN;                 \
    FRAM_CS_OUT &= (uint8_t)~FRAM_CS_PIN;       \
} while (0)

#define FRAM_CS_HIGH()  do {                     \
    FRAM_CS_DIR &= (uint8_t)~FRAM_CS_PIN;       /* input, high-Z */ \
} while (0)

/* -------------- Internal SPI helpers (UCB0) -------------- */

static uint8_t spi_transfer(uint8_t data)
{
    while ((UCB0IFG & UCTXIFG) == 0u) { }
    UCB0TXBUF = data;
    // uart0_print_hex(data); uart0_print(" ");
    while ((UCB0IFG & UCRXIFG) == 0u) { }
    // uart0_println("");
    return UCB0RXBUF;
}

void spi_init(void)
{
    // Make sure I/Os are unlocked if you use LPMx.5 at any point
    PM5CTL0 &= (uint16_t)~LOCKLPM5;

    // Put eUSCI_B0 in reset
    UCB0CTLW0 = UCSWRST;

    /* Map pins permanently to SPI function */
    // MOSI/MISO
    P1SEL1 |= (BIT6 | BIT7);
    P1SEL0 &= (uint8_t)~(BIT6 | BIT7);

    // SCK
    P2SEL1 |= BIT2;
    P2SEL0 &= (uint8_t)~BIT2;

    // CS: always use external pull-up, all MCUs share this net
    FRAM_CS_DIR &= (uint8_t)~FRAM_CS_PIN;
    FRAM_CS_REN &= (uint8_t)~FRAM_CS_PIN;
    FRAM_CS_HIGH();  // input, high-Z, external pull keeps it high
}

void spi_enable(uint8_t clk_div)
{
    // Still keep in reset while configuring
    UCB0CTLW0 = UCSWRST |
                UCMST    |
                UCSYNC   |
                UCMSB    |
                UCMODE_0 |
                UCCKPH;

    UCB0CTLW0 |= UCSSEL__SMCLK;
    UCB0BRW    = clk_div;   // 8 MHz / clk_div

    // Ensure lines are stable before enabling module, as per TI note
    __delay_cycles(8);      // small guard, 1 µs at 8 MHz is plenty

    UCB0CTLW0 &= (uint16_t)~UCSWRST;
}

void spi_disable(void)
{
    // Make sure last byte fully clocked
    while ((UCB0IFG & UCTXIFG) == 0u) {
        /* wait */
    }
    while (UCB0STATW & UCBUSY) {
        /* errata USCI41 is for eUSCI_A; UCB0BUSY is reliable here,
           but you can still be conservative and check RXIFG if worried */
    }

    FRAM_CS_HIGH();              // release FRAM first

    UCB0CTLW0 |= UCSWRST;        // disable SPI outputs on this MCU
}


/* -------------- FRAM + DMA (TX only) -------------- */

/* Threshold above which we use DMA; below this use simple polled SPI */
static volatile uint8_t fram_dma_dummy = 0u;
#define FRAM_DMA_THRESHOLD   16u

/* Route DMA0 -> UCB0RXIFG0, DMA1 -> UCB0TXIFG0 */
static void fram_config_dma_spi_txrx(void)
{
    /* Configure DMA0 trigger (low 5 bits of DMACTL0) */
    DMACTL0 &= (uint16_t)~(DMA0TSEL0 | DMA0TSEL1 | DMA0TSEL2 | DMA0TSEL3 | DMA0TSEL4);
    DMACTL0 |= DMA0TSEL__UCB0RXIFG0;

    /* Configure DMA1 trigger (high 5 bits of DMACTL0) */
    DMACTL0 &= (uint16_t)~(DMA1TSEL0 | DMA1TSEL1 | DMA1TSEL2 | DMA1TSEL3 | DMA1TSEL4);
    DMACTL0 |= DMA1TSEL__UCB0TXIFG0;

    /* Avoid DMA during CPU read-modify-write sequences */
    DMACTL4 |= DMARMWDIS;
}

/* Stream-write bytes over SPI using DMA0 for TX.
 * Assumes:
 *  - FRAM_CS_LOW(), WRITE opcode and address have already been sent.
 *  - len >= FRAM_DMA_THRESHOLD.
 */
/* Stream-write bytes over SPI using DMA (TX + RX drain).
 * Assumes:
 *  - FRAM_CS_LOW(), WRITE opcode and address have already been sent.
 *  - len >= FRAM_DMA_THRESHOLD.
 */
static void fram_stream_write_dma(const uint8_t *src, uint32_t len)
{
    uint16_t n = (uint16_t)len;

    if (n == 0u) {
        return;
    }

    // /* Small transfers: just use blocking spi_transfer */
    // if (n < FRAM_DMA_THRESHOLD) {
    //     uint16_t i;
    //     for (i = 0u; i < n; i++) {
    //         (void)spi_transfer(*src++);
    //     }
    //     return;
    // }

    /* Configure DMA triggers for UCB0 RX/TX */
    fram_config_dma_spi_txrx();

    /* Clear any stale errors / flags on eUSCI_B0 */
    (void)UCB0RXBUF;                           /* flush RXBUF */
    UCB0STATW &= (uint16_t)~(UCOE | UCFE);     /* clear overrun/frame error */

    /* ---------- Prepare DMA0: RXBUF -> dummy ---------- */

    DMA0CTL = 0;
    DMA0SZ  = 0;
    DMA0CTL &= (uint16_t)~DMAIFG;

    DMA0SA = (uint32_t)(uintptr_t)&UCB0RXBUF;      /* source: SPI RXBUF */
    DMA0DA = (uint32_t)(uintptr_t)&fram_dma_dummy; /* dest: dummy byte  */
    DMA0SZ = n;                                    /* same count as TX   */

    DMA0CTL =
        DMADT_0        |    /* Single block transfer (SZ down to 0)       */
        DMASRCINCR_0   |    /* source fixed (RXBUF)                       */
        DMADSTINCR_0   |    /* dest fixed (dummy)                         */
        DMASBDB;             /* byte -> byte                              */

    /* ---------- Prepare DMA1: src[] -> TXBUF ---------- */

    DMA1CTL = 0;
    DMA1SZ  = 0;
    DMA1CTL &= (uint16_t)~DMAIFG;

    /* We will send src[0] with CPU and src[1..n-1] via DMA1 */
    if (n <= 1u) {
        /* Degenerate: only one byte, just send by CPU and return */
        while ((UCB0IFG & UCTXIFG) == 0u) { }
        UCB0TXBUF = src[0];
        while (UCB0STATW & UCBUSY) { }
        return;
    }

    DMA1SA = (uint32_t)(uintptr_t)&src[1];        /* start from src[1]  */
    DMA1DA = (uint32_t)(uintptr_t)&UCB0TXBUF;     /* dest: TXBUF        */
    DMA1SZ = (uint16_t)(n - 1u);                  /* remaining bytes    */

    DMA1CTL =
        DMADT_0        |    /* Single block transfer                      */
        DMASRCINCR_3   |    /* increment source address                   */
        DMADSTINCR_0   |    /* dest fixed (TXBUF)                         */
        DMASBDB;             /* byte -> byte                              */

    /* ---------- Enable DMA and kick off transfer ---------- */

    /* Make sure SPI is idle before starting a new burst */
    while (UCB0STATW & UCBUSY) {
        /* wait */
    }

    /* Enable both channels before first TXIFG edge */
    DMA0CTL |= DMAEN;
    DMA1CTL |= DMAEN;

    /* Send first byte by CPU: this starts the SPI stream.
     * When this byte finishes, UCB0TXIFG0 asserts, which will trigger
     * DMA1 to start feeding src[1..].
     */
    while ((UCB0IFG & UCTXIFG) == 0u) {
        /* TXBUF empty */
    }
    UCB0TXBUF = src[0];

    /* Wait until DMA1 has pushed all remaining bytes */
    while ((DMA1CTL & DMAIFG) == 0u) {
        /* spin until SZ->0 and DMAIFG set */
    }

    /* Optional: wait until last bit actually leaves the wire */
    while (UCB0STATW & UCBUSY) {
        /* last byte still shifting out */
    }

    /* Disable DMA channels until next use */
    DMA0CTL &= (uint16_t)~DMAEN;
    DMA1CTL &= (uint16_t)~DMAEN;
}

/* Stream-read bytes over SPI using DMA (RX -> dst, TX <- dummy).
 * Assumes:
 *  - FRAM_CS_LOW(), READ opcode and address have already been sent.
 *  - len >= FRAM_DMA_THRESHOLD.
 */
static void fram_stream_read_dma(uint8_t *dst, uint32_t len)
{
    uint16_t n = (uint16_t)len;

    if (n == 0u) {
        return;
    }

    /* We only call this when len >= FRAM_DMA_THRESHOLD, so n >= 2. */

    /* Configure DMA triggers for UCB0 RX/TX */
    fram_config_dma_spi_txrx();

    /* Clear any stale errors / flags on eUSCI_B0 */
    (void)UCB0RXBUF;                           /* flush RXBUF */
    UCB0STATW &= (uint16_t)~(UCOE | UCFE);     /* clear overrun/frame error */

    fram_dma_dummy = 0xFFu;

    /* ---------- DMA0: RXBUF -> dst[0..n-1] ---------- */

    DMA0CTL = 0;
    DMA0SZ  = 0;
    DMA0CTL &= (uint16_t)~DMAIFG;

    DMA0SA = (uint32_t)(uintptr_t)&UCB0RXBUF;  /* source: SPI RXBUF */
    DMA0DA = (uint32_t)(uintptr_t)dst;         /* dest: dst[]       */
    DMA0SZ = n;

    DMA0CTL =
        DMADT_0        |    /* Single block transfer                  */
        DMASRCINCR_0   |    /* source fixed (RXBUF)                   */
        DMADSTINCR_3   |    /* dest increment                         */
        DMASBDB;             /* byte -> byte                           */

    /* ---------- DMA1: dummy -> TXBUF for remaining n-1 bytes ---------- */

    DMA1CTL = 0;
    DMA1SZ  = 0;
    DMA1CTL &= (uint16_t)~DMAIFG;

    DMA1SA = (uint32_t)(uintptr_t)&fram_dma_dummy;  /* src: dummy       */
    DMA1DA = (uint32_t)(uintptr_t)&UCB0TXBUF;       /* dest: TXBUF      */
    DMA1SZ = (uint16_t)(n - 1u);                    /* remaining bytes  */

    DMA1CTL =
        DMADT_0        |    /* Single block transfer                  */
        DMASRCINCR_0   |    /* source fixed                           */
        DMADSTINCR_0   |    /* dest fixed (TXBUF)                     */
        DMASBDB;             /* byte -> byte                           */

    /* ---------- Enable DMA and run ---------- */

    /* Make sure SPI is idle before starting a new burst */
    while (UCB0STATW & UCBUSY) {
        /* wait */
    }

    /* Enable both channels before first TXIFG edge */
    DMA0CTL |= DMAEN;
    DMA1CTL |= DMAEN;

    /* Kick off stream: send first dummy byte by CPU.
     * This will produce the first RX byte, which DMA0 will store as dst[0].
     * DMA1 will then drive the remaining n-1 dummy bytes.
     */
    while ((UCB0IFG & UCTXIFG) == 0u) {
        /* wait for TXBUF empty */
    }
    UCB0TXBUF = fram_dma_dummy;

    /* Wait until RX DMA completes all n bytes */
    while ((DMA0CTL & DMAIFG) == 0u) {
        /* spin */
    }

    /* Ensure last bit has left the wire */
    while (UCB0STATW & UCBUSY) {
        /* wait */
    }

    /* Disable DMA channels until next use */
    DMA0CTL &= (uint16_t)~DMAEN;
    DMA1CTL &= (uint16_t)~DMAEN;
}


/* -------------- FRAM core helpers -------------- */

static void fram_write_enable(void)
{
    FRAM_CS_LOW();
    spi_transfer(FRAM_CMD_WREN);
    FRAM_CS_HIGH();
}

uint8_t fram_read_status(void)
{
    uint8_t sr;

    FRAM_CS_LOW();
    spi_transfer(FRAM_CMD_RDSR);
    sr = spi_transfer(0xFF);
    FRAM_CS_HIGH();

    return sr;
}

static void fram_send_addr(uint32_t addr)
{
    spi_transfer((uint8_t)((addr >> 16) & 0xFFu));
    spi_transfer((uint8_t)((addr >>  8) & 0xFFu));
    spi_transfer((uint8_t)( addr        & 0xFFu));
}

/* -------------- Public API -------------- */

void fram_read_bytes(uint32_t addr, uint8_t *dst, uint32_t len)
{
    uint32_t i;

    if (len == 0u) {
        return;
    }

    FRAM_CS_LOW();
    spi_transfer(FRAM_CMD_READ);
    fram_send_addr(addr);

    /* Symmetric to write: use DMA for larger transfers */
    if (len >= FRAM_DMA_THRESHOLD) {
        fram_stream_read_dma(dst, len);
    } else {
        /* Small reads: simple polled SPI */
        for (i = len; i > 0u; i--) {
            *dst = spi_transfer(0xFF);
            dst++;
        }
    }

    FRAM_CS_HIGH();
}


void fram_write_bytes(uint32_t addr, const uint8_t *src, uint32_t len)
{
    uint32_t i;

    if (len == 0u) {
        return;
    }

    fram_write_enable();

    FRAM_CS_LOW();
    spi_transfer(FRAM_CMD_WRITE);
    fram_send_addr(addr);

    /* Use DMA for larger payloads, polled for very small writes. */
    if (len >= FRAM_DMA_THRESHOLD) {
        fram_stream_write_dma(src, len);
    } else {
        for (i = len; i > 0u; i--) {
            (void)spi_transfer(*src);
            src++;
        }
    }

    FRAM_CS_HIGH();
}

void fram_read_id(uint8_t *id, uint8_t len)
{
    uint8_t i;

    if (len == 0u) {
        return;
    }

    FRAM_CS_LOW();
    spi_transfer(FRAM_CMD_RDID);
    for (i = len; i > 0u; i--) {
        *id = spi_transfer(0xFF);
        id++;
    }
    FRAM_CS_HIGH();
}
