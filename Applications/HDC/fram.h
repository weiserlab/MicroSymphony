#ifndef FRAM_H_
#define FRAM_H_

#include <stdint.h>

/* FRAM notification register addresses (global) */
#define FRAM_NOTIF_BOX_ADDR   0x00010UL   /* bit i: node i's box has new data */

/* SPI / FRAM API
 *
 * fram_spi_init  : configure UCB0 for SPI (pins + clock divider)
 * fram_read_bytes: READ command + streaming read
 * fram_write_bytes:
 *    - WREN + WRITE command + streaming write
 *    - Uses DMA for larger transfers, polled for very small ones
 * fram_read_status: RDSR
 * fram_read_id    : RDID
 * spi_deinit      : put SPI pins back to GPIO (to stop leakage)
 */
void fram_read_bytes(uint32_t addr, uint8_t *dst, uint32_t len);
void fram_write_bytes(uint32_t addr, const uint8_t *src, uint32_t len);
uint8_t fram_read_status(void);
void fram_read_id(uint8_t *id, uint8_t len);

void spi_init(void);
void spi_enable(uint8_t clk_div);
void spi_disable(void);


#endif /* FRAM_H_ */
