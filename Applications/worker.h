#ifndef WORKER_H_
#define WORKER_H_

#include <stdint.h>

/* REQ = P1.4, GNT = P1.3 */
#define NODE_REQ_PIN    BIT4
#define NODE_GNT_PIN    BIT3
#define NODE_GNT_IV     0x08    /* P1.3 in P1IV */

typedef enum {
    LOCK_IDLE = 0,
    LOCK_WAIT_GRANT,
    LOCK_HELD
} lock_state_t;




void clock_init_8mhz(void);
void node_gpio_init(void);
void node_pulse_req_line(void);
void node_pulse_reset_on_gnt(void);

/* ---- Lock API ---- */

void lock_acquire(void);
void lock_release(void);
    
#endif /* WORKER_H_ */
