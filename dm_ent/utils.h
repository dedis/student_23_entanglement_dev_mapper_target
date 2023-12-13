#ifndef _ENT_UTILS_H_
#define _ENT_UTILS_H_

#include <linux/bio.h>
#include <linux/errno.h>

#include <linux/printk.h>

#include "device.h"

#define ENT_BLOCK_SIZE 4096
#define ENT_DEV_SECTOR_SCALE 8 // (4096 / kernel_sector_size (which is 512 bytes))

struct bio_set bioset;

/* Synchronously reads/writes one 4096-byte sector from/to the underlying device 
   to/from the provided page */
int ent_dev_rwSector(struct entanglement_device * ent_dev, struct page * page, sector_t sector, int rw)
{
        struct bio *bio;
        blk_opf_t opf;
        int err;

        /* Synchronous READ/WRITE */
        opf = ((rw == READ) ? REQ_OP_READ : REQ_OP_WRITE);
        opf |= REQ_SYNC;

        /* Allocate bio */
        bio = bio_alloc_bioset(ent_dev->dev->bdev, 1, opf,  GFP_NOIO, &bioset);
        if (!bio) {
            pr_err("Could not allocate bio\n");
            return -ENOMEM;
        }

        /* Set sector */
        bio->bi_iter.bi_sector = sector * ENT_DEV_SECTOR_SCALE;
        /* Add page */
        if (!bio_add_page(bio, page, ENT_BLOCK_SIZE, 0)) {
            pr_err("Catastrophe: could not add page to bio! WTF?\n");
            err = EINVAL;
            goto out;
        }

        /* Submit */
        err = submit_bio_wait(bio);

out:
        /* Free and return; */
        bio_put(bio);
        return err;
}


/*
    Found the crc32b function on Stack Overflow. 
*/

// ----------------------------- crc32b --------------------------------

/* This is the basic CRC-32 calculation with some optimization but no
table lookup. The the byte reversal is avoided by shifting the crc reg
right instead of left and by using a reversed 32-bit word to represent
the polynomial.
   When compiled to Cyclops with GCC, this function executes in 8 + 72n
instructions, where n is the number of bytes in the input message. It
should be doable in 4 + 61n instructions.
   If the inner loop is strung out (approx. 5*8 = 40 instructions),
it would take about 6 + 46n instructions. */

unsigned int crc32b(unsigned char *message) {

    printk(KERN_INFO "Entered the checksum function.\n");
    int i, j;
    unsigned int byte, crc, mask;

    i = 0;
    crc = 0xFFFFFFFF;
    while (message[i] != 0) {
        printk(KERN_INFO "Entered the while in the checksum function, with index i=%d.\n", i);
        byte = message[i];            // Get next byte.
        crc = crc ^ byte;
        for (j = 7; j >= 0; j--) {    // Do eight times.
            mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
        i = i + 1;
    }

    printk(KERN_INFO "Leaving the checksum function.\n");
    return ~crc;
}


int is_buffer_empty(char *arr, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (arr[i] != '\0') {
            return 0;  // Not empty
        }
    }
    return 1;  // Empty
}

#endif