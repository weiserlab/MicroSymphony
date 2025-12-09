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
#define FRAM_CS_PIN     BIT5
#define FRAM_CS_REN     P1REN

#define FRAM_CS_LOW()   do { \
    FRAM_CS_DIR |= FRAM_CS_PIN; \
    FRAM_CS_OUT &= (uint8_t)~FRAM_CS_PIN; \
} while (0)

#define FRAM_CS_HIGH()  do { \
    FRAM_CS_DIR &= (uint8_t)~FRAM_CS_PIN; /* input, high-Z */ \
} while (0)
/* ---------- SPI low-level (UCB0) ---------- */

static uint8_t spi_transfer(uint8_t data)
{
    while ((UCB0IFG & UCTXIFG) == 0u) { }
    UCB0TXBUF = data;
    while ((UCB0IFG & UCRXIFG) == 0u) { }
    return UCB0RXBUF;
}

static void spi_init(void)
{
    UCB0CTLW0 = UCSWRST;

    /* MOSI = P1.6, MISO = P1.7 */
    P1SEL1 |= (BIT6 | BIT7);
    P1SEL0 &= ~(BIT6 | BIT7);

    /* SCK = P2.2 */
    P2SEL1 |= BIT2;
    P2SEL0 &= ~BIT2;

    /* CS pin = P1.5 as GPIO input (high-z) */
    FRAM_CS_DIR &= ~FRAM_CS_PIN;
    FRAM_CS_REN &= ~FRAM_CS_PIN;   // disable internal resistor
    FRAM_CS_HIGH();

    PM5CTL0 &= ~LOCKLPM5;

    UCB0CTLW0 = UCSWRST |
                UCMST   |
                UCSYNC  |
                UCMSB   |
                UCMODE_0 |
                UCCKPH;

    UCB0CTLW0 |= UCSSEL__SMCLK;
    UCB0BRW    = 16u;        /* SMCLK/16 */

    UCB0CTLW0 &= ~UCSWRST;
}

static void spi_deinit(void)
{
    UCB0CTLW0 |= UCSWRST;

    /* MOSI = P1.6, MISO = P1.7 */
    P1SEL1 &= ~(BIT6 | BIT7);
    P1SEL0 &= ~(BIT6 | BIT7);
    P1DIR &= ~(BIT6 | BIT7);
    P1OUT &= ~(BIT6 | BIT7);

    /* SCK = P2.2 */
    P2SEL1 &= ~BIT2;
    P2SEL0 &= ~BIT2;
    P2DIR &= ~BIT2;

    // /* CS pin = P1.5 as GPIO input (high-z) */
    // FRAM_CS_DIR &= ~FRAM_CS_PIN;
    // FRAM_CS_REN &= ~FRAM_CS_PIN;   // disable internal resistor
    // FRAM_CS_HIGH();
}

/* ---------- FRAM helpers ---------- */

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

void fram_read_bytes(uint32_t addr, uint8_t *dst, uint32_t len)
{
    uint32_t i;

    if (len == 0u) {
        return;
    }

    spi_init();

    FRAM_CS_LOW();
    spi_transfer(FRAM_CMD_READ);
    fram_send_addr(addr);

    for (i = len; i > 0u; i--) {
        *dst = spi_transfer(0xFF);
        dst++;
    }

    FRAM_CS_HIGH();
    spi_deinit();
}

void fram_write_bytes(uint32_t addr, const uint8_t *src, uint32_t len)
{
    uint32_t i;

    // if (len == 0u) {
    //     return;
    // }

    spi_init();

    fram_write_enable();

    FRAM_CS_LOW();
    spi_transfer(FRAM_CMD_WRITE);
    fram_send_addr(addr);

    for (i = len; i > 0u; i--) {
        spi_transfer(*src);
        src++;
    }

    FRAM_CS_HIGH();
    spi_deinit();
}

void fram_read_id(uint8_t *id, uint8_t len)
{
    uint8_t i;

    FRAM_CS_LOW();
    spi_transfer(FRAM_CMD_RDID);
    for (i = len; i > 0u; i--) {
        *id = spi_transfer(0xFF);
        id++;
    }
    FRAM_CS_HIGH();
}

uint8_t fram_init(void)
{
    spi_init();
    __delay_cycles(2000u);
    // write and read back for test
    uint8_t test_data = 0xDE;
    fram_write_bytes(0x000000, &test_data, 1u);
    uint8_t read_data = 0x00;
    fram_read_bytes(0x000000, &read_data, 1u);
    if (read_data != 0xDE) {
        return 0u;  // error
    }
    else {
        return 1u;  // success
    }
    // spi_deinit();
}
