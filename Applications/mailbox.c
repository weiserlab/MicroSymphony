#include <msp430.h>
#include <stdint.h>
#include "fram.h"
#include "mailbox.h"
#include "uart.h"

/* Global notification byte in FRAM (bit i => node i has new messages) */
#define FRAM_NOTIF_BYTE_ADDR   FRAM_NOTIF_BOX_ADDR

/* ------------ Layout helpers ------------ */

uint32_t mailbox_node_box_base(uint8_t node_index)
{
    switch (node_index) {
    case 0u: return NODE1_BASE;
    case 1u: return NODE2_BASE;
    case 2u: return NODE3_BASE;
    case 3u: return NODE4_BASE;
    default: return NODE1_BASE; /* default */
    }
}

uint32_t mailbox_node_desc_addr(uint8_t node_index)
{
    return mailbox_node_box_base(node_index) + NODE_DESC_OFFSET;
}

uint32_t mailbox_node_data_base(uint8_t node_index)
{
    return mailbox_node_box_base(node_index) + NODE_DATA_OFFSET;
}

/* ------------ Descriptor init ------------ */

static void mailbox_init_one_node(uint8_t node_index)
{
    node_box_desc_t d;

    d.base      = mailbox_node_data_base(node_index);
    d.size      = (uint16_t)NODE_BOX_DATA_SIZE;
    d.head      = 0u;
    d.tail      = 0u;
    d.used      = 0u;
    d.msg_size  = MSG_SLOT_SIZE;
    d.reserved0 = 0u;
    d.reserved1 = 0u;

    fram_write_bytes(mailbox_node_desc_addr(node_index),
                     (const uint8_t *)&d,
                     (uint32_t)sizeof(d));
}

void mailbox_init_layout(void)
{
    uint8_t i;
    uint8_t zero = 0u;

    /* Init all 4 boxes */
    for (i = 0u; i < MAILBOX_NUM_NODES; i++) {
        mailbox_init_one_node(i);
    }

    /* Clear notification byte */
    fram_write_bytes(FRAM_NOTIF_BYTE_ADDR, &zero, 1u);
}

/* ------------ Internal helpers ------------ */

static uint16_t mailbox_slot_count(const node_box_desc_t *d)
{
    uint16_t msize;
    uint16_t count;

    msize = d->msg_size;
    if (msize == 0u) {
        return 0u;
    }

    /* Assumes size is a multiple of msg_size */
    count = (uint16_t)(d->size / msize);
    return count;
}

/* Write a single slot (header + payload) to FRAM.
 * - Uses two FRAM writes: one for header (4B), one for payload (len B).
 * - Does NOT update descriptor or notification.
 */
static void mailbox_write_one_slot(uint32_t slot_addr,
                                   uint8_t  src_id,
                                   uint8_t  len,
                                   const uint8_t *payload)
{
    uint8_t header[4];

    header[0] = src_id;
    header[1] = 0u;       /* flags */
    header[2] = len;
    header[3] = 0u;       /* reserved */

    /* Header */
    fram_write_bytes(slot_addr, header, (uint32_t)sizeof(header));

    /* Payload: only len bytes; remainder of the slot is don't-care */
    if (len != 0u) {
        fram_write_bytes(slot_addr + 4u, payload, (uint32_t)len);
    }
}

/* ------------ Public send/recv ------------ */

uint8_t mailbox_send_msg(uint8_t dest_index,
                         uint8_t src_id,
                         const uint8_t *data,
                         uint8_t len)
{
    node_box_desc_t d;
    uint16_t slot_count;
    uint16_t slot_index;
    uint32_t desc_addr;
    uint32_t slot_addr;
    uint8_t  notif;

    if (dest_index >= MAILBOX_NUM_NODES) {
        uart0_println("Bad dest index");
        return 0u;
    }
    if (len == 0u) {
        uart0_println("Zero length");
        return 0u;
    }
    if (len > MSG_SLOT_PAYLOAD_MAX) {
        uart0_println("Length too big");
        return 0u;
    }

    // uart0_println("Will read descriptor");
    /* Read descriptor for this box */
    desc_addr = mailbox_node_desc_addr(dest_index);
    fram_read_bytes(desc_addr, (uint8_t *)&d, (uint32_t)sizeof(d));

    // print the descriptor values for debugging
    // uart0_println("Descriptor early:");
    // uart0_print(" base=0x");
    // uart0_print_hex(d.base);
    // uart0_print(" size=");
    // uart0_print_uint(d.size);
    // uart0_print(" head=");
    // uart0_print_uint(d.head);
    // uart0_print(" tail=");
    // uart0_print_uint(d.tail);
    // uart0_print(" used=");
    // uart0_print_uint(d.used);
    // uart0_print(" msg_size=");
    // uart0_print_uint(d.msg_size);
    // uart0_println("");

    // uart0_println("Descriptor read");

    slot_count = mailbox_slot_count(&d);
    if (slot_count == 0u) {
        uart0_println("Zero slot count");
        return 0u;
    }

    /* Queue full? */
    if (d.used >= slot_count) {
        uart0_println("Queue full");
        return 0u;
    }

    /* Compute slot address */
    slot_index = d.tail;
    slot_addr  = d.base + ((uint32_t)slot_index * (uint32_t)d.msg_size);

    // uart0_println("Will write one slot");
    /* Write single slot */
    mailbox_write_one_slot(slot_addr, src_id, len, data);
    // uart0_println("Slot written");

    /* Advance tail and used (ring buffer) */
    slot_index++;
    if (slot_index >= slot_count) {
        slot_index = 0u;
    }
    d.tail = slot_index;
    d.used++;

    // uart0_println("Will update descriptor");

    //     // print the descriptor values for debugging
    // uart0_println("Descriptor updated:");
    // uart0_print(" base=0x");
    // uart0_print_hex(d.base);
    // uart0_print(" size=");
    // uart0_print_uint(d.size);
    // uart0_print(" head=");
    // uart0_print_uint(d.head);
    // uart0_print(" tail=");
    // uart0_print_uint(d.tail);
    // uart0_print(" used=");
    // uart0_print_uint(d.used);
    // uart0_print(" msg_size=");
    // uart0_print_uint(d.msg_size);
    // uart0_println(" ");

    /* Write back descriptor */
    fram_write_bytes(desc_addr, (const uint8_t *)&d, (uint32_t)sizeof(d));
    // uart0_println("Descriptor updated");
    
    /* Set notification bit for this dest node */
    // fram_read_bytes(FRAM_NOTIF_BYTE_ADDR, &notif, 1u);
    // // uart0_println("Will update notification byte");
    // notif |= (uint8_t)(1u << dest_index);
    // fram_write_bytes(FRAM_NOTIF_BYTE_ADDR, &notif, 1u);
    // uart0_println("Notification byte updated");

    return 1u;
}

/* Max number of slots batched into one FRAM write */

/* Batch buffer: each entry is a full msg_slot_t */


/* Write 'len' bytes from src into the node's FRAM ring at
 * offset 'pos' (0..ring_size-1), wrapping as needed.
 * Returns new offset in [0, ring_size).
 */
static uint32_t mailbox_write_bytes_ring(uint32_t base,
                                         uint32_t ring_size,
                                         uint32_t pos,
                                         const uint8_t *src,
                                         uint32_t len)
{
    uint32_t remaining = len;
    uint32_t offset    = pos;
    const uint8_t *p   = src;

    while (remaining > 0u) {
        uint32_t chunk = ring_size - offset;
        if (chunk > remaining) {
            chunk = remaining;
        }

        fram_write_bytes(base + offset, p, chunk);

        p        += chunk;
        remaining -= chunk;
        offset   += chunk;
        if (offset >= ring_size) {
            offset = 0u;
        }
    }

    return offset;
}



uint8_t mailbox_send_bulk(uint8_t dest_index,
                          uint8_t src_id,
                          const uint8_t *data,
                          uint16_t total_len)
{
    node_box_desc_t d;
    uint16_t slot_count;
    uint32_t desc_addr;
    uint16_t free_slots;
    uint16_t used_slots;
    uint32_t ring_size;
    uint32_t write_pos;
    uint32_t bytes_to_write;
    uint8_t  notif;

    bulk_header_t hdr;


    /* ---------- Basic argument checks ---------- */

    if (dest_index >= MAILBOX_NUM_NODES) {
        uart0_println("Bad dest index");
        return 0u;
    }
    if (total_len == 0u) {
        uart0_println("Zero length");
        return 0u;
    }

    /* total_len will be stored in 16 bits */
    if (total_len > 0xFFFFu) {
        uart0_println("Total length too big");
        return 0u;
    }

    /* ---------- Read descriptor once ---------- */

    desc_addr = mailbox_node_desc_addr(dest_index);
    // uart0_print_hex(desc_addr);
    // uart0_println("");
    fram_read_bytes(desc_addr, (uint8_t *)&d, (uint32_t)sizeof(d));

    // print all the descriptor values for debugging
    // uart0_println("Descriptor early:");
    // uart0_print(" base=0x");
    // uart0_print_hex(d.base);
    // uart0_print(" size=");
    // uart0_print_uint(d.size);
    // uart0_print(" head=");
    // uart0_print_uint(d.head);
    // uart0_print(" tail=");
    // uart0_print_uint(d.tail);
    // uart0_print(" used=");
    // uart0_print_uint(d.used);
    // uart0_print(" msg_size=");
    // uart0_print_uint(d.msg_size);
    // uart0_println("");

    slot_count = mailbox_slot_count(&d);
    if (slot_count == 0u) {
        uart0_println("Zero slot count");
        return 0u;
    }
    if (d.msg_size == 0u) {
        uart0_println("Zero message size");
        return 0u;
    }

    ring_size = (uint32_t)d.size;  /* should be slot_count * d.msg_size */

    if (d.used >= slot_count) {
        uart0_println("Queue full");
        /* Queue full */
        return 0u;
    }

    free_slots = (uint16_t)(slot_count - d.used);

    /* ---------- Compute bytes, slots required ---------- */

    {
        const uint16_t header_bytes = (uint16_t)sizeof(bulk_header_t);
        bytes_to_write = (uint32_t)header_bytes + (uint32_t)total_len;

        /* Compute slots required: ceil(bytes_to_write / d.msg_size) */
        used_slots = (uint16_t)((bytes_to_write +
                                (uint32_t)d.msg_size - 1u) /
                                (uint32_t)d.msg_size);

        if (used_slots == 0u) {
            return 0u;
        }
        if (used_slots > free_slots) {
            /* Not enough free slots */
            return 0u;
        }
    }

    /* Guard (should always hold): we cannot write more than ring_size bytes */
    if (bytes_to_write > ring_size) {
        return 0u;
    }

    /* ---------- Prepare header in RAM ---------- */

    hdr.src_id    = src_id;
    hdr.flags     = MSG_FLAG_BULK;
    hdr.total_len = total_len;

    /* ---------- Compute starting write offset in ring (in bytes) ---------- */

    write_pos = (uint32_t)d.tail * (uint32_t)d.msg_size;  /* 0..ring_size-1 */

    /* ---------- Write header then payload into FRAM ring ---------- */

    /* 1) Header */
    write_pos = mailbox_write_bytes_ring(d.base,
                                         ring_size,
                                         write_pos,
                                         (const uint8_t *)&hdr,
                                         (uint32_t)sizeof(hdr));

    /* 2) Payload (total_len bytes) */
    write_pos = mailbox_write_bytes_ring(d.base,
                                         ring_size,
                                         write_pos,
                                         data,
                                         (uint32_t)total_len);

    /* No padding is explicitly written; the last partially used slot
     * contains some “unused” tail bytes. The receiver will rely on
     * hdr.total_len, not on slot fullness.
     */

    /* ---------- Update descriptor (slot space) ---------- */


    d.used = (uint16_t)(d.used + used_slots);
    if (d.used > slot_count) {
        d.used = slot_count;    /* safety clamp */
    }

    d.tail = (uint16_t)(d.tail + used_slots);
    if (d.tail >= slot_count) {
        d.tail = (uint16_t)(d.tail - slot_count);  /* wrap in slot-space */
    }

    /* Write back descriptor once after the bulk */
    fram_write_bytes(desc_addr, (const uint8_t *)&d, (uint32_t)sizeof(d));

    /* ---------- Notification byte ---------- */

    // fram_read_bytes(FRAM_NOTIF_BOX_ADDR, &notif, 1u);
    // notif |= (uint8_t)(1u << dest_index);
    // fram_write_bytes(FRAM_NOTIF_BOX_ADDR, &notif, 1u);

    return 1u;
}

/* Read 'len' bytes from the node's FRAM ring at
 * offset 'pos' (0..ring_size-1), wrapping as needed.
 * Returns new offset in [0, ring_size).
 */
static uint32_t mailbox_read_bytes_ring(uint32_t base,
                                        uint32_t ring_size,
                                        uint32_t pos,
                                        uint8_t *dst,
                                        uint32_t len)
{
    uint32_t remaining = len;
    uint32_t offset    = pos;
    uint8_t *p         = dst;

    while (remaining > 0u) {
        uint32_t chunk = ring_size - offset;
        if (chunk > remaining) {
            chunk = remaining;
        }

        fram_read_bytes(base + offset, p, chunk);

        p        += chunk;
        remaining -= chunk;
        offset   += chunk;
        if (offset >= ring_size) {
            offset = 0u;
        }
    }

    return offset;
}

uint8_t mailbox_recv_msg(uint8_t node_index,
                         uint8_t *src_id_out,
                         uint16_t *len_out,
                         uint8_t *data_out)
{
    node_box_desc_t d;
    msg_slot_t slot;
    uint16_t slot_count;
    uint16_t slot_index;
    uint32_t desc_addr;
    uint32_t slot_addr;
    uint32_t ring_size;
    uint8_t  i;

    if (node_index >= MAILBOX_NUM_NODES) {
        return 0u;
    }

    /* Read descriptor */
    desc_addr = mailbox_node_desc_addr(node_index);
    fram_read_bytes(desc_addr, (uint8_t *)&d, (uint32_t)sizeof(d));

    slot_count = mailbox_slot_count(&d);
    if (slot_count == 0u) {
        return 0u;
    }

    /* Empty? */
    if (d.used == 0u) {
        return 0u;
    }

    ring_size = (uint32_t)d.size;

    /* Compute slot address (head) */
    slot_index = d.head;
    slot_addr  = d.base + ((uint32_t)slot_index * (uint32_t)d.msg_size);

    /* Read full slot from FRAM (backwards compatible) */
    fram_read_bytes(slot_addr, (uint8_t *)&slot, (uint32_t)MSG_SLOT_SIZE);

    *src_id_out = slot.src_id;

    /* Check flags to distinguish normal vs bulk message */
    if ((slot.flags & MSG_FLAG_BULK) == 0u) {
        /* ---------- Normal single-slot message ---------- */

        uint8_t len = slot.len;

        if (len > MSG_SLOT_PAYLOAD_MAX) {
            /* Corrupt entry; drop this slot */
            len = 0u;
        }
        *len_out = len;

        /* Copy payload */
        for (i = 0u; i < len; i++) {
            data_out[i] = slot.payload[i];
        }

        /* Advance head by 1 slot and decrease used by 1 */
        slot_index++;
        if (slot_index >= slot_count) {
            slot_index = 0u;
        }
        d.head = slot_index;
        if (d.used > 0u) {
            d.used--;
        }
    } else {
        /* ---------- Bulk message (spans multiple slots) ---------- */

        uint16_t total_len;
        uint32_t bytes_total;
        uint16_t used_slots;
        uint32_t start_offset;
        uint32_t payload_pos;

        /* In bulk_header_t, total_len is uint16_t and is laid out as:
         *   src_id (byte 0)
         *   flags  (byte 1)
         *   total_len LSB (byte 2)
         *   total_len MSB (byte 3)
         * msg_slot_t has: src_id, flags, len, reserved,
         * so len and reserved hold the 16-bit total_len in bulk case.
         */
        total_len = (uint16_t)slot.len |
                    ((uint16_t)slot.reserved << 8);

        if (total_len == 0u) {
            /* Corrupt bulk header; drop this slot */
            *len_out = 0u;

            slot_index++;
            if (slot_index >= slot_count) {
                slot_index = 0u;
            }
            d.head = slot_index;
            if (d.used > 0u) {
                d.used--;
            }

            fram_write_bytes(desc_addr, (const uint8_t *)&d, (uint32_t)sizeof(d));
            return 0u;
        }

        /* Total bytes occupied in ring = header + payload */
        bytes_total = (uint32_t)sizeof(bulk_header_t) +
                      (uint32_t)total_len;

        /* Guard: corruption check vs ring size */
        if (bytes_total > ring_size) {
            /* Header claims more bytes than ring; drop everything */
            *len_out = 0u;
            d.used = 0u;
            d.head = 0u;
            fram_write_bytes(desc_addr, (const uint8_t *)&d, (uint32_t)sizeof(d));
            return 0u;
        }

        /* Read bulk payload from ring, starting just after header.
         * Header starts at offset: slot_index * msg_size.
         */
        start_offset = (uint32_t)slot_index * (uint32_t)d.msg_size;
        payload_pos  = start_offset + (uint32_t)sizeof(bulk_header_t);
        if (payload_pos >= ring_size) {
            payload_pos -= ring_size;
        }

        /* Read total_len bytes into data_out (caller must ensure buffer large enough). */
        (void)mailbox_read_bytes_ring(d.base,
                                      ring_size,
                                      payload_pos,
                                      data_out,
                                      (uint32_t)total_len);

        /* Note: len_out is uint8_t, bulk length is uint16_t.
         * We store the lower 8 bits; if you need full length,
         * change the API to use uint16_t *len_out.
         // currently changed to uint16_t
         */
        *len_out = total_len;
        // *len_out = (uint8_t)total_len;

        /* Compute how many slots this bulk message consumed:
         * used_slots = ceil(bytes_total / msg_size)
         */
        used_slots = (uint16_t)((bytes_total +
                                (uint32_t)d.msg_size - 1u) /
                                (uint32_t)d.msg_size);

        /* Advance head and decrease used by used_slots */
        slot_index = (uint16_t)(slot_index + used_slots);
        if (slot_index >= slot_count) {
            slot_index = (uint16_t)(slot_index - slot_count);
        }
        d.head = slot_index;

        if (d.used >= used_slots) {
            d.used = (uint16_t)(d.used - used_slots);
        } else {
            d.used = 0u;
        }
    }

    /* Write back descriptor */
    fram_write_bytes(desc_addr, (const uint8_t *)&d, (uint32_t)sizeof(d));

    return 1u;
}
