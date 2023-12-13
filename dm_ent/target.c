#include <linux/device-mapper.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/mempool.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>

#include "utils.h"
#include "device.h"

#define BIOSET_SIZE 1024
#define PAGE_POOL_SIZE 1024

#define NUMBER_OF_SECTORS_IN_BLOCK 4096 / sizeof(sector_t)

/*
    These constants are used to represent an unused block sector/checksum. 
    In more detail, these values are present on disk, and when for example you read blocks and their metadata, 
    if you come across one of these values, it means you got to the end and read all sectors/checksums.  
*/
#define DEFAULT_SECTOR_VALUE 0xFFFFFFFFFFFFFFFFULL
#define DEFAULT_CHECKSUM_VALUE 0xFFFFFFFF

// Enum used to describe a buffer which is being flushed in the writing process.
enum BufferType {
    SECTOR,
    CHECKSUM
};

/*
    The following two enums are used in the repair of corrupted blocks.
    They describe the direction to take in the entanglement list, and if a block in the process was repaired or cannot be repaired. 
*/ 
enum RepairDirection {
    LEFT, 
    RIGHT
};
enum RepairState {
    REPAIRED, 
    IRRECOVERABLE
};

mempool_t *page_pool;

struct entangled_block {

    sector_t block_sector;
    uint block_checksum;
    struct list_head list_node;
};

int load_entanglement_and_checksums(struct entanglement_device *ent_dev) {
    
    struct page *sector_page;
    u8 *sector_page_ptr;
    struct page *checksum_page;
    u8 *checksum_page_ptr;
    sector_t sector;
    sector_t checksum_sector;
    int err;

    int curr_sector_index;
    int curr_checksum_index;
    sector_t last_entangled_block_sector;

    sector_page = mempool_alloc(page_pool, GFP_NOIO);
    if (!sector_page) {
        pr_err("Could not allocate data page.\n");
        return -ENOMEM;
    }

    checksum_page = mempool_alloc(page_pool, GFP_NOIO);
    if (!checksum_page) {
        pr_err("Could not allocate checksum page.\n");
        err = -ENOMEM;
        goto err_page_allocation;
    }

    sector_page_ptr = kmap(sector_page);
    checksum_page_ptr = kmap(checksum_page);

    // Grab the lock for the entanglement list.  
    if (mutex_lock_interruptible(&ent_dev->entanglement_lock)) {
        pr_err("Interrupted while waiting for the lock to the entanglement.\n");
        return -EINTR;
    }

    // First we load the entanglement. 
    // sector = 0;
    sector = ent_dev->metadata_start_sector;
    checksum_sector = ent_dev->metadata_sector_size;
    int i;

    for (i = 0 ; i < ent_dev->metadata_sector_size ; i++) {
        err = ent_dev_rwSector(ent_dev, sector_page, sector, READ);
        if (err) {
            pr_err("Error while reading block %d at sector %llu which contains information about the entanglement: %d\n", i, sector, err);
            goto out;
        }

        // Only read a new checksum block for every two entanglement blocks, since sectors are twice as large as checksums. 
        if (i % 2 == 0) {
            err = ent_dev_rwSector(ent_dev, checksum_page, checksum_sector, READ);
            if (err) {
                pr_err("Error while reading block %d at sector %llu which contains information about a checksum: %d\n", i, sector+512, err);
                goto out;
            }
            checksum_sector += 1;
        }
        

        // Loop through 512 entries in this block. Each entry is a sector for an entangled block.
        // Here we also load form the checksum blocks, since we have a static correspondence between sectors and checksums. 
        for (int j = 0 ; j < NUMBER_OF_SECTORS_IN_BLOCK ; j++) {
            
            if (checksum_page_ptr[j * sizeof(uint)] == DEFAULT_SECTOR_VALUE) {
                // We got to the end, just break out of both loops. 
                curr_sector_index = j;
                curr_checksum_index = (i % 2 == 0) ? j : j + NUMBER_OF_SECTORS_IN_BLOCK;
                goto end_loop;
            }
            struct entangled_block *new_block = kmalloc(sizeof(struct entangled_block), GFP_KERNEL);
            if (!new_block) {
                pr_err("Error while allocating new block.\n");
                err = -ENOMEM;
                goto out;
            }

            // Ensure we are reading the correct checksum. 
            uint entangled_block_checksum;
            if (i % 2 == 0) {
                // entangled_block_checksum = (void *) (checksum_page_ptr + (j * sizeof(uint)));
                entangled_block_checksum = checksum_page_ptr[j * sizeof(uint)];
            }else {
                // entangled_block_checksum = (void *) (checksum_page_ptr + ((j + 512) * sizeof(uint)));
                entangled_block_checksum = checksum_page_ptr[(j + 512) * sizeof(uint)];
            }

            // sector_t entangled_block_sector = (void *) (sector_page_ptr + (j * sizeof(sector_t)));
            sector_t entangled_block_sector = sector_page_ptr[j * sizeof(sector_t)];
            // Constantly update the sector of last block, so we can read it afterwards. 
            last_entangled_block_sector = entangled_block_sector;
            new_block->block_sector = entangled_block_sector;
            new_block->block_checksum = entangled_block_checksum;
            INIT_LIST_HEAD(&new_block->list_node);
            list_add_tail(&new_block->list_node, &ent_dev->entanglement);
        }

        sector += 1;
    }

end_loop:

    memcpy(ent_dev->block_sector_buffer, sector_page_ptr, ENT_BLOCK_SIZE);
    memcpy(ent_dev->block_checksum_buffer, checksum_page_ptr, ENT_BLOCK_SIZE);
    
    ent_dev->next_sector = sector;
    ent_dev->next_checksum = checksum_sector;

    // I use the sector page here because it is unnecessary to allocate a new page for this. 
    err = ent_dev_rwSector(ent_dev, sector_page, last_entangled_block_sector, READ);
    if (err) {
        pr_err("Error while reading data from the last block in the entanglement, while loading the entanglement.\n");
        goto out;
    }

    // Put the data in the last entangled block buffer. 
    memcpy(ent_dev->last_entangled_block, sector_page_ptr, ENT_BLOCK_SIZE);

    err = 0;

out:
    kunmap(sector_page);
    kunmap(checksum_page);
    mempool_free(checksum_page, page_pool);
    mutex_unlock(&ent_dev->entanglement_lock);
err_page_allocation:
    mempool_free(sector_page, page_pool);
    return err;
}

int store_entanglement_and_checksums(struct entanglement_device *ent_dev) {

    struct page *sector_page;
    u8 *sector_page_ptr;
    struct page *checksum_page;
    u8 *checksum_page_ptr;
    sector_t sector;
    int err;

    sector_page = mempool_alloc(page_pool, GFP_NOIO);
    if (!sector_page) {
        pr_err("Could not allocate data page.\n");
        return -ENOMEM;
    }

    checksum_page = mempool_alloc(page_pool, GFP_NOIO);
    if (!checksum_page) {
        pr_err("Could not allocate checksum page.\n");
        err = -ENOMEM;
        goto err_page_allocation;
    }

    sector_page_ptr = kmap(sector_page);
    checksum_page_ptr = kmap(checksum_page);

    // Two last writes of the buffers, in case of any leftovers in the buffers. 
    memcpy(sector_page_ptr, ent_dev->block_sector_buffer, sizeof(ent_dev->block_sector_buffer));
    err = ent_dev_rwSector(ent_dev, sector_page, ent_dev->next_sector, WRITE);
    if (err) {
        pr_err("Error while writing block at sector %llu which contains information about the entanglement: %d\n", sector, err);
        goto out;
    }

    memcpy(checksum_page_ptr, ent_dev->block_checksum_buffer, sizeof(ent_dev->block_checksum_buffer));
    err = ent_dev_rwSector(ent_dev, checksum_page, ent_dev->next_checksum, WRITE);
    if (err) {
        pr_err("Error while writing block at sector %llu which contains information about the entanglement: %d\n", sector, err);
        goto out;
    }

    // Grab the lock for the entanglement list.  
    if (mutex_lock_interruptible(&ent_dev->entanglement_lock)) {
        pr_err("Interrupted while waiting for the lock to the entanglement.\n");
        return -EINTR;
    }

    struct entangled_block *block;
    list_for_each_entry(block, &ent_dev->entanglement, list_node) {
        // Free the memory that was allocated in load_entanglement_and_checksums() and while this device mapper was being used.
        kfree(block); 
    }

out:
    mutex_unlock(&ent_dev->entanglement_lock);
    kunmap(checksum_page);
    kunmap(sector_page);
    mempool_free(checksum_page, page_pool);
err_page_allocation:
    mempool_free(sector_page, page_pool);
    return err;
}

// This function is actually called for parity blocks. Data blocks call the repair_block function, which initiates the recursion if needed.
enum RepairState repair_block_rec(struct entanglement_device *ent_dev, struct entangled_block *block, 
                                    unsigned long *irrecoverable_blocks_bitmap, enum RepairDirection direction) {


    enum RepairState result_state = IRRECOVERABLE;
    struct entangled_block *next_data_block;
    struct entangled_block *next_parity_block;
    struct page *data_page;
    u8 *data_page_ptr;
    struct page *parity_page;
    u8 *parity_page_ptr;
    struct page *repaired_block_page;
    u8 *repaired_block_page_ptr;

    int err;

    if (direction == LEFT) {
        
        next_data_block = list_entry(block->list_node.prev, struct entangled_block, list_node);
        // If a data block to the left is corrupted, we ran into a type B or type C failure in our entanglement.
        // We mark the block as irrecoverable and return. 
        if (test_bit(next_data_block->block_sector, ent_dev->corrupted_blocks)) {
            bitmap_set(irrecoverable_blocks_bitmap, block->block_sector, 1);
            return IRRECOVERABLE;
        }

        // If the data block is fine, but it is the end of the entanglement (in this case the beginning, i.e. the head of the list),
        // we cannot recover any block in the recursion so far, so we mark the current as irrecoverable, and return. 
        if (next_data_block->list_node.prev == &ent_dev->entanglement) {
            bitmap_set(irrecoverable_blocks_bitmap, block->block_sector, 1);
            return IRRECOVERABLE;
        }

        next_parity_block = list_entry(next_data_block->list_node.prev, struct entangled_block, list_node);
        if (test_bit(next_parity_block->block_sector, ent_dev->corrupted_blocks)) {
            result_state = repair_block_rec(ent_dev, next_parity_block, irrecoverable_blocks_bitmap, direction);
        }

        // If we cannot repair left_parity, then we cannot repair this one as well, so mark it as irrecoverable, and return.
        if (result_state == IRRECOVERABLE) {
            bitmap_set(irrecoverable_blocks_bitmap, block->block_sector, 1);
            return IRRECOVERABLE;
        }

    }else { // Direction is RIGHT.
        // If this block is at the right end of the entanglement (i.e. end), then we mark it as irrecoverable and return. 
        // This is because the current block is corrupted, and needs others to be fixed, but there are none. 
        if (block->list_node.next == &ent_dev->entanglement) {
            bitmap_set(irrecoverable_blocks_bitmap, block->block_sector, 1);
            return IRRECOVERABLE;
        }

        next_data_block = list_entry(block->list_node.next, struct entangled_block, list_node);
        if (test_bit(next_data_block->block_sector, ent_dev->corrupted_blocks)) {
            bitmap_set(irrecoverable_blocks_bitmap, block->block_sector, 1);
            return IRRECOVERABLE;
        }

        next_parity_block = list_entry(next_data_block->list_node.next, struct entangled_block, list_node);
        if (test_bit(next_parity_block->block_sector, ent_dev->corrupted_blocks)) {
            result_state = repair_block_rec(ent_dev, next_parity_block, irrecoverable_blocks_bitmap, direction);
        }

        if (result_state == IRRECOVERABLE) {
            bitmap_set(irrecoverable_blocks_bitmap, block->block_sector, 1);
            return IRRECOVERABLE;
        }
    }

    // If we can repair it, then we repair it. Should be the same steps for both directions.
    // We allocate pages, read the blocks, do the XOR, and write the repaired block.
    data_page = mempool_alloc(page_pool, GFP_NOIO);
    if (!data_page) {
        pr_err("Error while allocating data page for parity repair.\n");
        goto out;
    }
    data_page_ptr = kmap(data_page);
    
    parity_page = mempool_alloc(page_pool, GFP_NOIO);
    if (!parity_page) {
        pr_err("Error while allocating parity page for parity repair.\n");
        goto err_parity_page_alloc;
    }
    parity_page_ptr = kmap(parity_page);

    repaired_block_page = mempool_alloc(page_pool, GFP_NOIO);
    if (!repaired_block_page) {
        pr_err("Error while allocating parity page for parity repair.\n");
        goto err_repaired_block_page_alloc;
    }
    repaired_block_page_ptr = kmap(repaired_block_page);

    err = ent_dev_rwSector(ent_dev, data_page, next_data_block->block_sector, READ);
    if (err) {
        pr_err("Error while reading right block in repair process.\n");
        goto out_2;
    }

    err = ent_dev_rwSector(ent_dev, parity_page, next_parity_block->block_sector, READ);
    if (err) {
        pr_err("Error while reading right block in repair process.\n");
        goto out_2;
    }

    for (int i = 0 ; i < ENT_BLOCK_SIZE ; i++) {
        repaired_block_page_ptr[i] = data_page_ptr[i] ^ parity_page_ptr[i];
    }

    err = ent_dev_rwSector(ent_dev, repaired_block_page, block->block_sector, WRITE);
    if (err) {
        pr_err("Error while writing the repaired block in repair process.\n");
        goto out_2;
    }

    // Current block is repaired, so clear the bit in the corrupted blocks bitmap.
    bitmap_clear(ent_dev->corrupted_blocks, block->block_sector, 1);

    result_state = REPAIRED;

out_2:
    kunmap(repaired_block_page);
    mempool_free(repaired_block_page, page_pool);
err_repaired_block_page_alloc:
    kunmap(parity_page);
    mempool_free(parity_page, page_pool);
err_parity_page_alloc:
    kunmap(data_page);
    mempool_free(data_page, page_pool);
out:

    return result_state;
}

/*
    Starts the block repair, calling the recursive repair of adjacent blocks if necessary. 
*/
void repair_block(struct entanglement_device *ent_dev, struct entangled_block *block, unsigned long *irrecoverable_blocks_bitmap) {
    
    // We use the REPAIRED state as both a signal that a block has been repaired, or that it was never even corrupted.
    // The end result is the same, we just want to know if we can repair the current block. 
    enum RepairState left_state = REPAIRED;
    enum RepairState right_state = REPAIRED;
    struct entangled_block *left_block;
    struct entangled_block *right_block;

    int err;
    struct page *left_page;
    u8 *left_page_ptr;
    struct page *right_page;
    u8 *right_page_ptr;
    struct page *repaired_block_page;
    u8 *repaired_block_page_ptr;

    if (block->list_node.prev != &ent_dev->entanglement) {
        left_block = list_entry(block->list_node.prev, struct entangled_block, list_node);
        if (test_bit(left_block->block_sector, ent_dev->corrupted_blocks)) {
            left_state = repair_block_rec(ent_dev, left_block, irrecoverable_blocks_bitmap, LEFT);
        }
    }

    // The check if we are at the end of the entanglement is not needed, since we only call this function for data blocks.
    right_block = list_entry(block->list_node.prev, struct entangled_block, list_node);
    if (test_bit(right_block->block_sector, ent_dev->corrupted_blocks)) {
        right_state = repair_block_rec(ent_dev, right_block, irrecoverable_blocks_bitmap, RIGHT);
    }
    
    
    // If even one of the adjacent blocks is irrecoverable, it means we ran into one of the irrecoverable types of failure.
    if (left_state == IRRECOVERABLE || right_state == IRRECOVERABLE) {
        bitmap_set(irrecoverable_blocks_bitmap, block->block_sector, 1);
        // TODO: Nothing else happens here. Maybe I should add some count how many are irrecoverable, although I have the bitmap.
        //       This is just for the statistic on how many block are lost. Probably nothing else needs to happen here. 
        return;
    }

    repaired_block_page = mempool_alloc(page_pool, GFP_NOIO);
    if (!repaired_block_page) {
        pr_err("Error while allocating new page for the block being repaired.\n");
        return;
    }
    repaired_block_page_ptr = kmap(repaired_block_page);

    right_page = mempool_alloc(page_pool, GFP_NOIO);
    if (!right_page) {
        pr_err("Error while allocating right page during block repair. \n");
        goto err_right_page_alloc;
    }
    right_page_ptr = kmap(right_page);

    // Repair the current block, in the case that both adjacent block have the REPAIRED state. 
    if (block->list_node.prev == &ent_dev->entanglement) {
        // Just copy the right adjacent block, this means we are at the first data block, so it is the same as the parity next to it.
        err = ent_dev_rwSector(ent_dev, right_page, right_block->block_sector, READ);
        if (err) {
            pr_err("Error while reading right block in repair process.\n");
            goto out_2;
        }

        memcpy(repaired_block_page_ptr, right_page_ptr, ENT_BLOCK_SIZE);

        err = ent_dev_rwSector(ent_dev, repaired_block_page, block->block_sector, WRITE);
        if (err) {
            pr_err("Error while writing the repaired block in repair process.\n");
            goto out_2;
        }

    }else {
        // Do the XOR operation between the adjacent blocks. 

        left_page = mempool_alloc(page_pool, GFP_NOIO);
        if (!left_page) {
            pr_err("Error while allocating left page during block repair. \n");
            goto out_2;
        }
        left_page_ptr = kmap(left_page);

        err = ent_dev_rwSector(ent_dev, left_page, left_block->block_sector, READ);
        if (err) {
            pr_err("Error while reading right block in repair process.\n");
            goto out;
        }

        err = ent_dev_rwSector(ent_dev, right_page, right_block->block_sector, READ);
        if (err) {
            pr_err("Error while reading right block in repair process.\n");
            goto out;
        }

        for (int i = 0 ; i < ENT_BLOCK_SIZE ; i++) {
            repaired_block_page_ptr[i] = left_page_ptr[i] ^ right_page_ptr[i];
        }

        err = ent_dev_rwSector(ent_dev, repaired_block_page, block->block_sector, WRITE);
        if (err) {
            pr_err("Error while writing the repaired block in repair process.\n");
            goto out;
        }
    }

    // Current block is repaired, so clear the bit in the corrupted blocks bitmap.
    bitmap_clear(ent_dev->corrupted_blocks, block->block_sector, 1);
    
out:
    kunmap(left_page);
    mempool_free(left_page, page_pool);
out_2:
    kunmap(right_page);
    mempool_free(right_page, page_pool);
err_right_page_alloc:
    kunmap(repaired_block_page);
    mempool_free(repaired_block_page, page_pool);
    return;

}

int repair_corrupted_blocks(struct entanglement_device *ent_dev) {

    int err;
    unsigned long *irrecoverable_blocks_bitmap;
    struct entangled_block *block;

    irrecoverable_blocks_bitmap = bitmap_alloc(ent_dev->dev_size, GFP_KERNEL);
    if (!irrecoverable_blocks_bitmap) {
        pr_err("Error while allocating bitmap for irrecoverable blocks.\n");
        return -ENOMEM;
    }

    // Iterate through list and repair blocks. We call the repair_block() recursion only on corrupted, recoverable data blocks. 
    list_for_each_entry(block, &ent_dev->entanglement, list_node) {
        if (test_bit(block->block_sector, ent_dev->corrupted_blocks) && 
            !test_bit(block->block_sector, irrecoverable_blocks_bitmap) && 
            block->block_sector % 2 == 0) {

            repair_block(ent_dev, block, irrecoverable_blocks_bitmap);
        }
    }

    // At this point I have repaired all blocks that can be repaired. 
    // TODO: I can maybe save the statistics of how many blocks were lost before freeing the bitmap. 
    bitmap_free(irrecoverable_blocks_bitmap);

    return err;
}

int check_corruption(struct entanglement_device *ent_dev) {

    int err;
    sector_t sector; 
    struct page *page;
    u8 *page_ptr;

    page = mempool_alloc(page_pool, GFP_NOIO);
    if (!page) {
        pr_err("Error while allocating page from pool.\n");
        return -ENOMEM;
    }

    page_ptr = kmap(page);

    // Grab the lock for the corrupted blocks bitmap.  
    if (mutex_lock_interruptible(&ent_dev->corrupted_blocks_lock)) {
        pr_err("Interrupted while waiting for the lock to the corrputed blocks bitmap.\n");
        return -EINTR;
    }
 
    for (sector = ent_dev->metadata_size ; sector < ent_dev->dev_size ; sector += 1) {
        if (ent_dev->sector_checksum_map[sector] != 0) {
            err = ent_dev_rwSector(ent_dev, page, sector, READ);
            if (err) {
                pr_err("Error while reading block at sector %llu which contains data: %d\n", sector, err);
                goto out;
            }

            uint checksum = crc32b((unsigned char *)page_ptr);
            if (checksum != ent_dev->sector_checksum_map[sector]) {
                // Set the bit corresponding to the sector of this block. 
                bitmap_set(ent_dev->corrupted_blocks, sector, 1);
            }
        }

        sector += 1;
    }

    err = repair_corrupted_blocks(ent_dev);

out:
    kunmap(page);
    mempool_free(page, page_pool);
    mutex_unlock(&ent_dev->corrupted_blocks_lock);

    return err;
}

static int entanglement_tgt_ctr(struct dm_target *ti, unsigned int argc, char **argv) {

    struct entanglement_device *ent_dev;
    int err;
    char *dev_path;
    uint dev_size;
    int redundancy_flag;

    // For now, the plan is to have three arguments, first the device path, second the device size as number of 4KB blocks, 
    // and third the redundancy flag, to know when to try and repair it.
    if (argc != 3) {
        ti->error = "Invaid argument count";
        return -EINVAL;
    }
    dev_path = argv[0];
    sscanf(argv[1], "%u", &dev_size);
    sscanf(argv[2], "%u", &redundancy_flag);

    ent_dev = kzalloc(sizeof(struct entanglement_device), GFP_KERNEL);
    if (!ent_dev) {
        pr_err("Could not allocate enough bytes for entanglement_device.\n");
        err = -ENOMEM;
        goto err_dev_allocation;
    }

    ent_dev->dev_size = dev_size;

    // Number of blocks for metadata. Calculated as number of 4KB blocks (dev_size) * 0.002929688.
    // This number (0.002929688) we get from the fact that for every 4096 bytes, we have a 12-byte overhead. (12/4096) 
    ent_dev->metadata_size = (dev_size * 3U) >> 10;
    ent_dev->metadata_sector_size = (ent_dev->metadata_size * 2U) / 3U;
    ent_dev->metadata_checksum_size = (ent_dev->metadata_size * 1U) / 3U;

    // Calculating the starting sector of the metadata, and the scale with which we redirect the writes of parity blocks.
    ent_dev->metadata_start_sector = ((dev_size - ent_dev->metadata_size) / 2) / 8 * 8;
    ent_dev->write_sector_scale = ((dev_size - ent_dev->metadata_size)/2 / 8 * 8) + ent_dev->metadata_size;

    mutex_init(&ent_dev->entanglement_lock);
    INIT_LIST_HEAD(&ent_dev->entanglement);

    err = dm_get_device(ti, dev_path, dm_table_get_mode(ti->table), &ent_dev->dev);
    if (err) {
        pr_err("Error when calling dm_get_device: %d\n", err);
        goto err_dm_get_dev;
    }

    mutex_init(&ent_dev->corrupted_blocks_lock);


    ent_dev->corrupted_blocks = bitmap_alloc(dev_size, GFP_KERNEL);
    if (!ent_dev->corrupted_blocks) {
        pr_err("Error while allocating bitmap for corrupted blocks.\n");
        err = -ENOMEM;
        goto err_bitmap_alloc;
    }

    ent_dev->sector_checksum_map = kzalloc(dev_size * sizeof(uint), GFP_KERNEL);
    if (!ent_dev->sector_checksum_map) {
        pr_err("Error while allocating sector->checksum map.\n");
        err = -ENOMEM;
        goto err_sector_checksum_map_alloc;
    }

    ent_dev->last_entangled_block = kzalloc(ENT_BLOCK_SIZE, GFP_KERNEL);
    if (!ent_dev->last_entangled_block) {
        pr_err("Error while allocating the last_entangled_block buffer.\n");
        err = -ENOMEM;
        goto err_last_buffer_alloc;
    }

    mutex_init(&ent_dev->metadata_buffers_lock);

    ent_dev->block_sector_buffer = kmalloc(ENT_BLOCK_SIZE, GFP_KERNEL);
    if (!ent_dev->block_sector_buffer) {
        pr_err("Error while allocating the buffer for periodically writing block sectors to disk.\n");
        err = -ENOMEM;
        goto err_sector_buffer_alloc;
    }
    ent_dev->sector_buffer_size = 0;

    ent_dev->block_checksum_buffer = kmalloc(ENT_BLOCK_SIZE, GFP_KERNEL);
    if (!ent_dev->block_checksum_buffer) {
        pr_err("Error while allocating the buffer for periodically writing block checksums to disk.\n");
        err = -ENOMEM;
        goto err_checksum_buffer_alloc;
    }
    ent_dev->checksum_buffer_size = 0;

    // TODO: Change this, for now I use the redundancy flag.
    if (redundancy_flag) {
        err = load_entanglement_and_checksums(ent_dev);
        if (err) {
            pr_err("Error while loading entanglement and checksums: %d\n", err);
            goto err_loading;
        }
    }
    

    if (redundancy_flag) {
        err = check_corruption(ent_dev);
        if (err) {
            pr_err("Error while checking for corruption: %d\n", err);
            goto err_check_corruption;
        }
    }

    ti->max_io_len = ENT_BLOCK_SIZE;
    ti->num_flush_bios = 1;
    ti->num_secure_erase_bios = 1;
    ti->num_write_zeroes_bios = 1;
    ti->num_discard_bios = 1;
    ti->private = ent_dev;

    return 0;


err_check_corruption:

err_loading:
    kfree(ent_dev->block_checksum_buffer);
err_checksum_buffer_alloc:
    kfree(ent_dev->block_sector_buffer);
err_sector_buffer_alloc:
    kfree(ent_dev->last_entangled_block);
err_buffer_alloc:
err_dm_get_dev:
err_last_buffer_alloc:
    kfree(ent_dev->sector_checksum_map);
err_sector_checksum_map_alloc:
    kfree(ent_dev->corrupted_blocks);
err_bitmap_alloc:
    dm_put_device(ti, ent_dev->dev);
    kfree(ent_dev);
err_dev_allocation:
    printk(KERN_INFO "Leaving the entanglement_tgt_ctr function with error.\n");
    return err;
}

static void entanglement_tgt_dtr(struct dm_target *ti) {

    printk(KERN_INFO "Entered entanglement_tgt_dtr function\n");

    struct entanglement_device *ent_dev = (struct entanglement_device *) ti->private;

    // Store the entanglement list and checksums. Actually just flushes the buffers in case of leftover metadata. 
    store_entanglement_and_checksums(ent_dev);
    printk(KERN_INFO "Successfully stored metadata\n");

    dm_put_device(ti, ent_dev->dev);
    kfree(ent_dev->block_checksum_buffer);
    kfree(ent_dev->block_sector_buffer);
    kfree(ent_dev->last_entangled_block);
    kfree(ent_dev->sector_checksum_map);
    bitmap_free(ent_dev->corrupted_blocks);
    kfree(ent_dev);
}
/*
    Functions that process read/write requests. 
*/

static void ent_dev_read_end_io(struct bio *bio) {

    struct bio *orig_bio = bio->bi_private;

    bio_put(orig_bio);
    bio_endio(orig_bio);

    bio_put(bio);
}

int process_read_bio(struct entanglement_device *ent_dev, struct bio *bio) {
 
    printk(KERN_INFO "[READ PROCESS] Entered process_read_bio.\n");

    int err;
    struct bio *cloned_bio;

    bio_get(bio);

    cloned_bio = bio_alloc_clone(ent_dev->dev->bdev, bio, GFP_NOIO, &bioset);
    if (!cloned_bio) {
        pr_err("Error while cloning bio for read.\n");
        err = -ENOMEM;
        goto err_bio_cloning;
    }
    printk(KERN_INFO "[READ PROCESS] Successfully cloned READ bio.\n");


    if (bio->bi_iter.bi_sector % 2 != 0) {
        cloned_bio->bi_iter.bi_sector -= 1;
    }

    cloned_bio->bi_end_io = ent_dev_read_end_io;
    cloned_bio->bi_private = bio;

    printk(KERN_INFO "[READ PROCESS] Submitting READ bio.\n");
    submit_bio(cloned_bio);
    printk(KERN_INFO "[READ PROCESS] READ submitted, returning from process_read_bio.\n");
    
    return 0;

err_bio_cloning:
    bio_put(bio);

    return err;
}

// TODO: Check if this is correct and done. 
static void ent_dev_write_end_io(struct bio *bio) {

    struct bio *orig_bio = bio->bi_private;

    bio_put(orig_bio);
    bio_endio(orig_bio);

    bio_put(bio);
    mempool_free(bio->bi_io_vec->bv_page, page_pool);
}

// TODO: Check if this is correct and done. 
static void ent_dev_write_end_io_clone(struct bio *bio) {

    struct bio *orig_bio = bio->bi_private;

    bio_put(orig_bio);
    bio_endio(orig_bio);

    bio_put(bio);
}

int flush_metadata(struct entanglement_device *ent_dev, enum BufferType type) {

    printk(KERN_INFO "[WRITE PROCESS] Entered the flush_metadata function.\n");

    char *buffer = (type == SECTOR) ? ent_dev->block_sector_buffer : ent_dev->block_checksum_buffer;
    sector_t sector = (type == SECTOR) ? ent_dev->next_sector : ent_dev->next_checksum;
    struct page *page;
    u8 *page_ptr;
    int err;

    page = mempool_alloc(page_pool, GFP_NOIO);
    if (!page) {
        pr_err("Error while allocating new page for parity.\n");
        return -ENOMEM;
    }

    page_ptr = kmap(page);
    memcpy(page_ptr, buffer, ENT_BLOCK_SIZE);

    err = ent_dev_rwSector(ent_dev, page, sector, WRITE);
    if (err) {
        pr_err("Error while flushing buffer of type %d.\n", type);
        goto out;
    }

    // Reset buffer. 
    if (type == SECTOR) {
        for (int i = 0; i < ENT_BLOCK_SIZE / sizeof(DEFAULT_SECTOR_VALUE); i++) {
            memcpy(buffer + i*sizeof(DEFAULT_SECTOR_VALUE), DEFAULT_SECTOR_VALUE, sizeof(DEFAULT_SECTOR_VALUE));   
        }
        // ent_dev->sector_buffer_size = buffer;
        ent_dev->sector_buffer_size = 0;

        // Update sector. 
        ent_dev->next_sector += 1;
    }else {
        for (int i = 0; i < ENT_BLOCK_SIZE / sizeof(DEFAULT_CHECKSUM_VALUE); i++) {
            memcpy(buffer + i*sizeof(DEFAULT_CHECKSUM_VALUE), DEFAULT_CHECKSUM_VALUE, sizeof(DEFAULT_CHECKSUM_VALUE));   
        }
        // ent_dev->checksum_buffer_size = buffer;
        ent_dev->checksum_buffer_size = 0;

        // Update sector. 
        ent_dev->next_checksum += 1;
    }

out:   
    kunmap(page);
    // Free the page. 
    mempool_free(page, page_pool);

    return err;
}

int process_write_bio(struct entanglement_device *ent_dev, struct bio *bio) {

    printk(KERN_INFO "[WRITE PROCESS] Entered process_write_bio.\n");

    struct bio *data_bio;
    struct bio *parity_bio;
    sector_t data_sector;
    sector_t parity_sector;
    int err;
    
    char *data_buffer;
    struct page *parity_page;
    u8 *parity_page_ptr;
    char *parity_buffer;
    struct bio_vec bvec;
    struct bvec_iter iter;

    struct page *sector_page;
    struct page *checksum_page;
    u8 *sector_page_ptr;
    u8 *checksum_page_ptr;

    // Allocation of the new page needed for the parity block. 
    parity_page = mempool_alloc(page_pool, GFP_NOIO);
    if (!parity_page) {
        pr_err("Error while allocating new page for parity.\n");
        return -ENOMEM;
    }
    printk(KERN_INFO "[WRITE PROCESS] Successfully allocated parity_page.\n");

    parity_page_ptr = kmap(parity_page);

    // Grab the lock for the metadata buffers. 
    if (mutex_lock_interruptible(&ent_dev->metadata_buffers_lock)) {
        pr_err("Interrupted while waiting for the lock to the metadata buffers.\n");
        return -EINTR;
    }
    printk(KERN_INFO "[WRITE PROCESS] Successfully got the metadata_buffers_lock.\n");

    bio_get(bio);

    data_bio = bio_alloc_clone(ent_dev->dev->bdev, bio, GFP_NOIO, &bioset);
    if (!data_bio) {
        pr_err("Error while cloning bio for write.\n");
        err = -ENOMEM;
        goto err_bio_cloning;
    }
    printk(KERN_INFO "[WRITE PROCESS] Successfully cloned bio.\n");

    bio_get(bio);

    parity_bio = bio_alloc_bioset(ent_dev->dev->bdev, bio_segments(bio), bio->bi_opf, GFP_NOIO, &bioset);
    if (!parity_bio) {
        pr_err("Error while allocating new bio for a parity.\n");
        err = -ENOMEM;
        goto err_bio_allocation;
    }
    printk(KERN_INFO "[WRITE PROCESS] Successfully allocated new bio for the parity.\n");

    // data_buffer = kmalloc(ENT_BLOCK_SIZE, GFP_KERNEL);
    // if (!data_buffer) {
    //     pr_err("Error while allocating buffer for bio data when writing.\n");
    //     err = -ENOMEM;
    //     goto err_biodata_buffer_alloc;
    // }


    parity_buffer = kmalloc(ENT_BLOCK_SIZE, GFP_KERNEL);
    if (!parity_buffer) {
        pr_err("Error while allocating buffer for calculating new parity when writing.\n");
        err = -ENOMEM;
        goto err_parity_buffer_alloc;
    }
    printk(KERN_INFO "[WRITE PROCESS] Successfully allocated parity_buffer.\n");

    if (!bio_data(data_bio)) {
        printk(KERN_INFO "[WRITE PROCESS] NULL returned from bio_data(). No data in the bio, even though I checked it before. WTF???\n");
        goto err_parity_buffer_alloc;
    }
    data_buffer = (char *) bio_data(data_bio);

    // Using this function from utils.h because I had a weird error with memcmp.
    // If this is empty, it means we are at the start of the entanglement, and the first parity is just the first data block copied. 
    if (is_buffer_empty(ent_dev->last_entangled_block, ENT_BLOCK_SIZE)) {
        memcpy(parity_buffer, data_buffer, ENT_BLOCK_SIZE);
    }else {
        for (int i = 0 ; i < ENT_BLOCK_SIZE ; i++) {
            parity_buffer[i] = data_buffer[i] ^ ent_dev->last_entangled_block[i];
        }
    }
    printk(KERN_INFO "[WRITE PROCESS] Successfully calculated the XOR (or copied in case of beginning of entanglement).\n");

    // TODO: Check which of these is correct. Can I just use bio_data, or do I need the bio_for_each_segment.

    // TODO: REEEEALLY check this part!!!!!!! 
    // int curr_index = 0;
    // bio_for_each_segment(bvec, data_bio, iter) {
    //     char *data = kmap(bvec.bv_page) + bvec.bv_offset;
    //     for (int i = 0 ; i < bvec.bv_len && curr_index < sizeof(buffer) ; i++, curr_index++) {
    //         parity_buffer[curr_index] = data[i] ^ ent_dev->last_entangled_block[curr_index];
    //     }
    //     kunmap(bvec.bv_page);
    // }

    memcpy(parity_page_ptr, parity_buffer, sizeof(parity_buffer));

    if (!bio_add_page(parity_bio, parity_page, ENT_BLOCK_SIZE, 0)) {
        pr_err("Catastrophe: could not add page to parity bio! WTF?\n");
        err = -EINVAL;
        goto err_adding_page;
    }
    printk(KERN_INFO "[WRITE PROCESS] Successfully added parity_page to bio.\n");

    // Update bio sectors, such that we have the following in storage: data->parity->data->parity etc.
    // if (bio->bi_iter.bi_sector % 2 == 0) {
    //     data_sector = bio->bi_iter.bi_sector;
    //     parity_sector = data_sector + 1;
    // }else {
    //     parity_sector = bio->bi_iter.bi_sector;
    //     data_sector = parity_sector - 1;
    // }
    data_sector = bio->bi_iter.bi_sector;
    parity_sector = data_sector + ent_dev->write_sector_scale;

    data_bio->bi_iter.bi_sector = data_sector;
    parity_bio->bi_iter.bi_sector = parity_sector;

    // Create new entangled blocks, and add them to the entanglement (list).
    struct entangled_block *new_data_block;
    struct entangled_block *new_parity_block;

    new_data_block = kmalloc(sizeof(struct entangled_block), GFP_KERNEL);
    if (!new_data_block) {
        pr_err("Error while allocating new data block.\n");
        err = -ENOMEM;
        goto err_new_data_allocation;
    }
    printk(KERN_INFO "[WRITE PROCESS] Successfully allocated new_data_block.\n");

    new_parity_block = kmalloc(sizeof(struct entangled_block), GFP_KERNEL);
    if (!new_parity_block) {
        pr_err("Error while allocating new parity block.\n");
        err = -ENOMEM;
        goto err_new_parity_allocation;
    }
    printk(KERN_INFO "[WRITE PROCESS] Successfully allocated new_parity_block.\n");

    // Calculate checksums and add them to the buffer, flushing the buffer if needed. When flushing, update next_checksum. Also update curr_buffer_size.
    uint data_checksum = crc32b(data_buffer);
    uint parity_checksum = crc32b(parity_buffer);

    printk(KERN_INFO "[WRITE PROCESS] Successfully calculated checksums.\n");

    // Add sectors and checksums to blocks. Add them to the entanglement list. 
    new_data_block->block_sector = data_sector;
    new_data_block->block_checksum = data_checksum;
    new_parity_block->block_sector = parity_sector;
    new_parity_block->block_checksum = parity_checksum;
    INIT_LIST_HEAD(&new_data_block->list_node);
    INIT_LIST_HEAD(&new_parity_block->list_node);

    list_add_tail(&new_data_block->list_node, &ent_dev->entanglement);
    list_add_tail(&new_parity_block->list_node, &ent_dev->entanglement);

    printk(KERN_INFO "[WRITE PROCESS] Successfully added new blocks to list.\n");

    // Add the data sector and checksum, and flush buffers if needed.
    if (ent_dev->sector_buffer_size + sizeof(data_sector) < ENT_BLOCK_SIZE) {
        // printk(KERN_INFO "[WRITE PROCESS] Entered the if part of adding data sector to sector buffer.\n");
        memcpy(ent_dev->block_sector_buffer + ent_dev->sector_buffer_size, &data_sector, sizeof(data_sector));
        ent_dev->sector_buffer_size += sizeof(data_sector);
        // printk(KERN_INFO "[WRITE PROCESS] Leaving the if part of adding data sector to sector buffer.\n");
    }else {
        // printk(KERN_INFO "[WRITE PROCESS] Entered the else part of adding data sector to sector buffer.\n");
        err = flush_metadata(ent_dev, SECTOR);
        if (err) {
            goto err_metadata_flush;
        }
        // Put the ongoing write in the buffer.
        memcpy(ent_dev->block_sector_buffer + ent_dev->sector_buffer_size, &data_sector, sizeof(data_sector));
        ent_dev->sector_buffer_size += sizeof(data_sector);
    }
    printk(KERN_INFO "[WRITE PROCESS] Successfully added data sector to sector buffer, and flushed if it was necessary.\n");

    if (ent_dev->checksum_buffer_size + sizeof(data_checksum) < ENT_BLOCK_SIZE) {
        memcpy(ent_dev->block_checksum_buffer + ent_dev->checksum_buffer_size, &data_checksum, sizeof(data_checksum));
        ent_dev->checksum_buffer_size += sizeof(data_checksum);
    }else {
        err = flush_metadata(ent_dev, CHECKSUM);
        if (err) {
            goto err_metadata_flush;
        }

        // Put the ongoing write in the buffer. 
        memcpy(ent_dev->block_checksum_buffer + ent_dev->checksum_buffer_size, data_checksum, sizeof(data_checksum));
        ent_dev->checksum_buffer_size += sizeof(data_checksum);
    }
    printk(KERN_INFO "[WRITE PROCESS] Successfully added data checksum to checksum buffer, and flushed if it was necessary.\n");

    // Add the parity sector and checksum, and flush buffers if needed.
    if (ent_dev->sector_buffer_size + sizeof(parity_sector) < ENT_BLOCK_SIZE) {
        memcpy(ent_dev->block_sector_buffer + ent_dev->sector_buffer_size, &parity_sector, sizeof(parity_sector));
        ent_dev->sector_buffer_size += sizeof(parity_sector);
    }else {
        err = flush_metadata(ent_dev, SECTOR);
        if (err) {
            goto err_metadata_flush;
        }
        // Put the ongoing write in the buffer. 
        memcpy(ent_dev->block_sector_buffer + ent_dev->sector_buffer_size, &parity_sector, sizeof(parity_sector));
        ent_dev->sector_buffer_size += sizeof(parity_sector);
    }
    printk(KERN_INFO "[WRITE PROCESS] Successfully added parity sector to sector buffer, and flushed if it was necessary.\n");

    if (ent_dev->checksum_buffer_size + sizeof(parity_checksum) < ENT_BLOCK_SIZE) {
        memcpy(ent_dev->block_checksum_buffer + ent_dev->checksum_buffer_size, &parity_checksum, sizeof(parity_checksum));
        ent_dev->checksum_buffer_size += sizeof(parity_checksum);
    }else {
        err = flush_metadata(ent_dev, CHECKSUM);
        if (err) {
            goto err_metadata_flush;
        }

        // Put the ongoing write in the buffer. 
        memcpy(ent_dev->block_checksum_buffer + ent_dev->checksum_buffer_size, &parity_checksum, sizeof(parity_checksum));
        ent_dev->checksum_buffer_size += sizeof(parity_checksum);
    }
    printk(KERN_INFO "[WRITE PROCESS] Successfully added parity checksum to checksum buffer, and flushed if it was necessary.\n");

    // Update the last_entangled_block. 
    memcpy(ent_dev->last_entangled_block, parity_buffer, ENT_BLOCK_SIZE);

    // Update the sector-checksum map. 
    ent_dev->sector_checksum_map[data_sector] = data_checksum;
    ent_dev->sector_checksum_map[parity_sector] = parity_checksum;

    // TODO: Check the correctness of this. 
    parity_bio->bi_end_io = ent_dev_write_end_io;
    parity_bio->bi_private = bio;

    data_bio->bi_end_io = ent_dev_write_end_io_clone;
    data_bio->bi_private = bio;

    submit_bio(data_bio);
    submit_bio(parity_bio);

    kunmap(parity_page);
    // kfree(new_data_block);
    // kfree(new_parity_block);
    kfree(parity_buffer);
    mutex_unlock(&ent_dev->metadata_buffers_lock);

    printk(KERN_INFO "[WRITE PROCESS] Submitted bios, freed memory, and returning from process_write_bio without error.\n");

    return 0;

err_metadata_flush:
    list_del(&new_parity_block->list_node);
    list_del(&new_data_block->list_node);
    kfree(new_parity_block);
err_new_parity_allocation:
    kfree(new_data_block);
err_new_data_allocation:
err_adding_page:
    kunmap(parity_page);
    kfree(parity_buffer);
err_parity_buffer_alloc:
err_bio_allocation:
    bio_put(bio);
err_bio_cloning:
    mempool_free(parity_page, page_pool);
    bio_put(bio);
    mutex_unlock(&ent_dev->metadata_buffers_lock);

    bio->bi_status = BLK_STS_IOERR;
    bio_endio(bio);

    printk(KERN_INFO "[WRITE PROCESS] Returning from process_write_bio with error.\n");

    return err;
}

/*
    Map function of this target. Handles the processing of each bio that comes from upper layers. 
*/
static int entanglement_tgt_map(struct dm_target *ti, struct bio *bio) {

    printk(KERN_INFO "Entered the entanglement_tgt_map function.\n");

    if (!bio) {
        printk(KERN_INFO "Bio is NULL????? WTF????? What is going on?\n");
        return DM_MAPIO_KILL;
    }

    if (unlikely(!bio_has_data(bio))) {
        printk(KERN_INFO "Bio has no data, WTF??? What to do here????\n");
        return DM_MAPIO_REMAPPED;
    }

    int err; 

    if (bio_data_dir(bio) == READ) {
        err = process_read_bio(ti->private, bio);
        if (err) {
            pr_err("Error while processing a read bio.\n");
            printk(KERN_INFO "ERROR while processing a READ and returning from tgt_map.\n");
            return DM_MAPIO_KILL;
        }
        printk(KERN_INFO "Successfully processed a READ and returning from tgt_map.\n");
        return DM_MAPIO_SUBMITTED;
    }

    err = process_write_bio(ti->private, bio);
    if (err) {
        pr_err("Error while processing write bio.\n");
        printk(KERN_INFO "ERROR while processing a WRTIE and returning from tgt_map.\n");
        return DM_MAPIO_KILL;
    }

    printk(KERN_INFO "Successfully processed a WRITE and returning from tgt_map.\n");

    return DM_MAPIO_SUBMITTED;
}

/*
    Inform DM about the size of the block, since we are working with 4096-byte blocks. 
*/
static void entanglement_tgt_io_hints(struct dm_target *ti, struct queue_limits *limits) {

    limits->logical_block_size = ENT_BLOCK_SIZE;
	limits->physical_block_size = ENT_BLOCK_SIZE;

	limits->io_min = ENT_BLOCK_SIZE;
	limits->io_opt = ENT_BLOCK_SIZE;
}

static int entanglement_tgt_iterateDevices(struct dm_target *ti, iterate_devices_callout_fn fn,
									void *data)
{
	struct entanglement_device *ent_dev = ti->private;

	if (!fn) {
		return -EINVAL;
	}
    
	return fn(ti, ent_dev->dev, 0, ent_dev->dev_size * ENT_DEV_SECTOR_SCALE, data);
}

struct target_type entanglement_target = {
    .name               = "entanglement", 
    .version            = {1, 0, 0}, 
    .module             = THIS_MODULE, 
    .ctr                = entanglement_tgt_ctr, 
    .dtr                = entanglement_tgt_dtr, 
    .map                = entanglement_tgt_map, 
    .io_hints           = entanglement_tgt_io_hints,
    .iterate_devices    = entanglement_tgt_iterateDevices, 
};

/*
    Functions called when the module is inserted in/removed from the kernel.
*/
int dm_entanglement_init(void) {

    printk(KERN_INFO "Entered the dm_entanglement_init function.\n");

    // Initialize the bioset.
    int err = bioset_init(&bioset, BIOSET_SIZE, 0, BIOSET_NEED_BVECS);
    if (err) {
        pr_err("Error while initializing the bioset: %d\n", err);
        goto err_bioset_alloc;
    }
    printk(KERN_INFO "Successfully initialized the bioset.\n");

    // Initialize the page pool.
	page_pool = mempool_create_page_pool(PAGE_POOL_SIZE, 0);
	if (!page_pool) {
		pr_err("Error while creating the page pool\n");
		err = -ENOMEM;
        goto err_pagepool_creation;
	}
    printk(KERN_INFO "Successfully initialized the page pool.\n");

    err = dm_register_target(&entanglement_target);

    if (err < 0) {
        pr_err("Target registration failed: %d", err);
        goto err_target_registration;
    }

    printk(KERN_INFO "Successfully registered target.\n");

    printk(KERN_INFO "Leaving the dm_entanglement_function without error.\n");

    return err;

err_pagepool_creation:
    bioset_exit(&bioset);
err_bioset_alloc:
    dm_unregister_target(&entanglement_target);
err_target_registration:
    printk(KERN_INFO "Leaving the dm_entanglement_function with error.\n");
    return err;
}

void dm_entanglement_exit(void) {

    dm_unregister_target(&entanglement_target);
    bioset_exit(&bioset);
    mempool_destroy(page_pool);
}

module_init(dm_entanglement_init);
module_exit(dm_entanglement_exit);
MODULE_LICENSE("GPL");