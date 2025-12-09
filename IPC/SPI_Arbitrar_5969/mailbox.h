#ifndef MAILBOX_H_
#define MAILBOX_H_

#include <stdint.h>

/* Logical number of boxes defined in FRAM layout (4 total).
 * Your current hardware uses 3 nodes; the 4th is reserved for future.
 */
#define MAILBOX_NUM_NODES      4u

/* Per-node region in FRAM: 20 kB = 0x5000 bytes */
#define NODE_BOX_SIZE          0x5000UL

/* Node base addresses (4 boxes, 20 kB each) */
#define NODE1_BASE             0x01000UL
#define NODE2_BASE             0x06000UL
#define NODE3_BASE             0x0B000UL
#define NODE4_BASE             0x10000UL

/* Descriptor + data offsets within each box */
#define NODE_DESC_OFFSET       0x0000UL
#define NODE_DATA_OFFSET       0x0100UL
#define NODE_BOX_DATA_SIZE     0x4F00UL   /* 0x5000 - 0x0100 = 0x4F00 (~20kB-256B) */

/* Fixed-size message slots */
#define MSG_SLOT_SIZE          64u
#define MSG_SLOT_PAYLOAD_MAX   (MSG_SLOT_SIZE - 4u)

/* Descriptor stored at NODEk_BASE + NODE_DESC_OFFSET */
typedef struct {
    uint32_t base;      /* absolute FRAM address of data region */
    uint16_t size;      /* total size in bytes (NODE_BOX_DATA_SIZE) */
    uint16_t head;      /* slot index of next message to read */
    uint16_t tail;      /* slot index of next free slot to write */
    uint16_t used;      /* number of slots currently used */
    uint16_t msg_size;  /* slot size in bytes (MSG_SLOT_SIZE) */
    uint16_t reserved0;
    uint16_t reserved1;
} node_box_desc_t;

/* One slot (fixed size) */
typedef struct {
    uint8_t  src_id;
    uint8_t  flags;
    uint8_t  len;       /* payload length <= MSG_SLOT_PAYLOAD_MAX */
    uint8_t  reserved;
    uint8_t  payload[MSG_SLOT_PAYLOAD_MAX];
} msg_slot_t;

/* Layout helpers */
uint32_t mailbox_node_box_base(uint8_t node_index);
uint32_t mailbox_node_desc_addr(uint8_t node_index);
uint32_t mailbox_node_data_base(uint8_t node_index);

/* One-time init: sets up descriptors for all 4 boxes, clears notification byte. */
void mailbox_init_layout(void);

/* Send fixed-size message into dest node's box.
 * dest_index: 0..MAILBOX_NUM_NODES-1
 * src_id    : logical sender ID (1..N)
 * data      : payload (len bytes)
 * len       : payload length (<= MSG_SLOT_PAYLOAD_MAX)
 * Returns 1 on success, 0 on failure (queue full or bad args).
 * Must be called only while holding global FRAM lock.
 */
uint8_t mailbox_send_msg(uint8_t dest_index,
                         uint8_t src_id,
                         const uint8_t *data,
                         uint8_t len);

/* Receive one message from this node's own box.
 * node_index: this node's index (0..)
 * src_id_out: set to source ID
 * len_out   : set to payload length
 * data_out  : buffer of at least MSG_SLOT_PAYLOAD_MAX bytes
 * Returns 1 on success (message filled), 0 if box empty.
 * Must be called only while holding global FRAM lock.
 */
uint8_t mailbox_recv_msg(uint8_t node_index,
                         uint8_t *src_id_out,
                         uint8_t *len_out,
                         uint8_t *data_out);

#endif /* MAILBOX_H_ */
