#ifndef FRAM_H_
#define FRAM_H_

#include <stdint.h>

/* FRAM notification register addresses (global) */
#define FRAM_NOTIF_BOX_ADDR   0x00010UL   /* bit i: node i's box has new data */

/* API */
void fram_spi_init(uint8_t clk_div);
void fram_read_bytes(uint32_t addr, uint8_t *dst, uint32_t len);
void fram_write_bytes(uint32_t addr, const uint8_t *src, uint32_t len);
uint8_t fram_read_status(void);
void fram_read_id(uint8_t *id, uint8_t len);
void spi_deinit(void);

#endif /* FRAM_H_ */
