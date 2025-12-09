#include <msp430.h>
#include <stdint.h>
#include "fram.h"
#include "mailbox.h"

/* Global notification byte in FRAM (bit i => node i has new messages) */
#define FRAM_NOTIF_BYTE_ADDR   FRAM_NOTIF_BOX_ADDR

/* ---- Layout helpers ---- */

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

/* ---- Descriptor init ---- */

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

/* ---- Internal helpers ---- */

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

/* ---- Public send/recv ---- */

uint8_t mailbox_send_msg(uint8_t dest_index,
                         uint8_t src_id,
                         const uint8_t *data,
                         uint8_t len)
{
    node_box_desc_t d;
    msg_slot_t slot;
    uint16_t slot_count;
    uint16_t slot_index;
    uint32_t desc_addr;
    uint32_t slot_addr;
    uint8_t  notif;

    if (dest_index >= MAILBOX_NUM_NODES) {
        // uart0_println("dest_index invalid");
        return 0u;
    }
    if (len > MSG_SLOT_PAYLOAD_MAX) {
        // uart0_println("len too large");
        return 0u;
    }
    if(len == 0u) {
        // uart0_println("len is zero");
        return 0u;
    }

    /* Read descriptor */
    desc_addr = mailbox_node_desc_addr(dest_index);
    fram_read_bytes(desc_addr, (uint8_t *)&d, (uint32_t)sizeof(d));

    slot_count = mailbox_slot_count(&d);
    if (slot_count == 0u) {
        // uart0_println("slot_count is zero");
        return 0u;
    }

    /* Queue full? */
    if (d.used >= slot_count) {
        // uart0_println("queue full");
        return 0u;
    }

    /* Prepare slot in RAM */
    slot.src_id   = src_id;
    slot.flags    = 0u;
    slot.len      = len;
    slot.reserved = 0u;

    /* Copy payload (up to len) */
    {
        uint8_t i;
        for (i = 0u; i < len; i++) {
            slot.payload[i] = data[i];
        }
        // /* Optional: clear rest (not strictly needed) */
        // for (; i < MSG_SLOT_PAYLOAD_MAX; i++) {
        //     slot.payload[i] = 0u;
        // }
    }

    /* Compute slot address */
    slot_index = d.tail;
    slot_addr  = d.base + ((uint32_t)slot_index * (uint32_t)d.msg_size);



    /* Write slot to FRAM */
    fram_write_bytes(slot_addr, (const uint8_t *)&slot, (uint32_t)MSG_SLOT_SIZE);

    /* Advance tail and used */
    slot_index++;
    if (slot_index >= slot_count) {
        slot_index = 0u;
    }
    d.tail = slot_index;
    d.used++;

    /* Write back descriptor */
    fram_write_bytes(desc_addr, (const uint8_t *)&d, (uint32_t)sizeof(d));

    /* Set notification bit for this dest node */
    fram_read_bytes(FRAM_NOTIF_BYTE_ADDR, &notif, 1u);
    notif |= (uint8_t)(1u << dest_index);
    fram_write_bytes(FRAM_NOTIF_BYTE_ADDR, &notif, 1u);



    return 1u;
}

uint8_t mailbox_recv_msg(uint8_t node_index,
                         uint8_t *src_id_out,
                         uint8_t *len_out,
                         uint8_t *data_out)
{
    node_box_desc_t d;
    msg_slot_t slot;
    uint16_t slot_count;
    uint16_t slot_index;
    uint32_t desc_addr;
    uint32_t slot_addr;
    uint8_t  i;
    uint8_t  len;

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

    /* Compute slot address */
    slot_index = d.head;
    slot_addr  = d.base + ((uint32_t)slot_index * (uint32_t)d.msg_size);

    /* Read slot from FRAM */
    fram_read_bytes(slot_addr, (uint8_t *)&slot, (uint32_t)MSG_SLOT_SIZE);

    /* Extract header */
    *src_id_out = slot.src_id;
    len         = slot.len;

    if (len > MSG_SLOT_PAYLOAD_MAX) {
        /* Corrupt entry; drop it */
        len = 0u;
    }
    *len_out = len;

    /* Copy payload */
    for (i = 0u; i < len; i++) {
        data_out[i] = slot.payload[i];
    }

    /* Advance head and decrease used */
    slot_index++;
    if (slot_index >= slot_count) {
        slot_index = 0u;
    }
    d.head = slot_index;
    if (d.used > 0u) {
        d.used--;
    }

    /* Write back descriptor */
    fram_write_bytes(desc_addr, (const uint8_t *)&d, (uint32_t)sizeof(d));

    // print the start and end of data address
    return 1u;
}
