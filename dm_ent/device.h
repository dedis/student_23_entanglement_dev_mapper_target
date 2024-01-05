#ifndef _ENT_DEVICE_H_
#define _ENT_DEVICE_H_

#include <linux/fs.h>
#include <linux/list.h>
#include <linux/blkdev.h>

struct entanglement_device {
    
    // Underlying block device. 
    struct dm_dev *dev;

    // Size of device in 4KB blocks.
    int dev_size;

    // These numbers correspond to the number of 4KB blocks that are needed to store the metadata at the beginning of the disk. 
    uint metadata_size;
    uint metadata_sector_size;
    uint metadata_checksum_size;
    
    sector_t metadata_start_sector;

    // Number used to move parity blocks to the appropriate sector in the other half of the disk. 
    uint write_sector_scale;
    
    // The entanglement represented as a list of struct entangled_block, and its mutex. 
    struct mutex entanglement_lock;
    struct list_head entanglement;

    // Bitmap of corrupted blocks, used in data corruption check/repair. 
    struct mutex corrupted_blocks_lock;
    unsigned long *corrupted_blocks;

    // This is an array that maps the block sector to its checksum. Used to quickly check if checksums match when searching for corrupted blocks. 
    uint *sector_checksum_map;

    // Contents of the last block in the entanglement. Kept in memory to avoid the I/O overhead of reading it every time we write a new block.
    char *last_entangled_block;

    // These sectors are used to know what is the next available sector in the header, which contains the entanglement and block checksums.
    // All these fields are needed to keep track of the metadata blocks: how full the current block is, where should it be written when it is full, etc.
    sector_t next_sector;
    sector_t next_checksum;
    struct mutex metadata_buffers_lock;
    char *block_sector_buffer; 
    char *block_checksum_buffer;
    // Used to know the current free position in the buffers. We cannot use the same pointer, as the size of a sector is larger than the size of a checksum.
    int sector_buffer_size; 
    int checksum_buffer_size;

};


#endif