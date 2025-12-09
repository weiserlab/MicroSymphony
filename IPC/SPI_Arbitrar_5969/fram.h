#ifndef FRAM_H_
#define FRAM_H_

#include <stdint.h>

/* FRAM notification register addresses (global) */
#define FRAM_NOTIF_BOX_ADDR   0x00010UL   /* bit i: node i's box has new data */
#define SPI_CLK_DIV           16u

/* API */
uint8_t fram_init(void);
void fram_read_bytes(uint32_t addr, uint8_t *dst, uint32_t len);
void fram_write_bytes(uint32_t addr, const uint8_t *src, uint32_t len);
uint8_t fram_read_status(void);
void fram_read_id(uint8_t *id, uint8_t len);
void spi_deinit(void);

void spi_pins_init_once(void);
void spi_enable_owner(uint8_t clk_div);
void spi_disable_owner(void);

#endif /* FRAM_H_ */
