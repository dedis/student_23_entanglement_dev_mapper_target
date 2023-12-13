#include <stdio.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <libdevmapper.h>


#define ENT_BLK_SIZE 4096
// For now, these names are defined here. Maybe later, something about this should be changed. 
#define ENT_DM_TARGET_NAME "entanglement"
#define ENT_DEV_NAME "ent_dev"


#define DEFAULT_SECTOR_VALUE 0xFFFFFFFFFFFFFFFFULL


unsigned int metadata_size;
int64_t disk_size;



#include "log.h"

/**
 * Create a new entanglement virtual device under /dev/mapper.
 *
 * @param virt_dev_name The name of the new virtual device, as it will appear
 *  under /dev/mapper
 * @param num_sectors The size of the virtual device, in 512-byte sectors
 * @param params The string containing the space-separated paramters that will
 *  be passed to the entanglement target constructor in the kernel module
 *
 * @return The error code (0 on success)
 */
int ent_dm_create(char * virt_dev_name, uint64_t num_sectors, char * params)
{
    struct dm_task *dmt;
    uint32_t cookie = 0;
    uint16_t udev_flags = 0;
    int err;

    /* Just to be sure, let's get them on the heap */
    char * dup_virt_dev_name = strdup(virt_dev_name);
    char * dup_params = strdup(params);

    // ent_log_debug("Creating /dev/mapper/%s", dup_virt_dev_name);
    printf("Creating /dev/mapper/%s\n", dup_virt_dev_name);

    /* Instantiate the DM task (with the CREATE ioctl command) */
    if ((dmt = dm_task_create(DM_DEVICE_CREATE)) == NULL) {
        ent_log_error("Cannot create dm_task");
        err = 1;
        goto dup_free;
    }
    // ent_log_debug("Successfully created dm_task");
    printf("Successfully created dm_task\n");

    /* Set the name of the target device (to be created) */
    if (!dm_task_set_name(dmt, dup_virt_dev_name)) {
        ent_log_error("Cannot set device name");
        err = 2;
        goto out;
    }
    // ent_log_debug("Successfully set device name");
    printf("Successfully set device name\n");

    /* State that it is an entanglement device, pass the start and size, and the
     * constructor parameters */
    if (!dm_task_add_target(dmt, 0, num_sectors, ENT_DM_TARGET_NAME, dup_params)) {
    	ent_log_error("Cannot add DM target and parameters");
    	err = 3;
        goto out;
    }
    // ent_log_debug("Successfully added DM target and parameters");
    printf("Successfully added DM target and parameters\n");

    /* Say that we want a new node under /dev/mapper */
    if (!dm_task_set_add_node(dmt, DM_ADD_NODE_ON_CREATE)) {
        ent_log_error("Cannot add /dev/mapper node");
        err = 4;
        goto out;
    }
    // ent_log_debug("Successfully set the ADD_NODE flag");
    printf("Successfully set the ADD_NODE flag\n");

    /* Get a cookie (request ID, basically) to wait for task completion */
    if (!dm_task_set_cookie(dmt, &cookie, udev_flags)) {
        ent_log_error("Cannot get cookie");
        err = 5;
        goto out;
    }
    // ent_log_debug("Successfully got a cookie");
    printf("Successfully got a cookie\n");

    /* Run the task */
    if (!dm_task_run(dmt)) {
        ent_log_error("Cannot issue ioctl");
        err = 6;
        goto out;
    }
    // ent_log_debug("Successfully run DM task");
    printf("Successfully run DM task\n");

    /* Wait for completion */
    dm_udev_wait(cookie);
    // ent_log_debug("Task completed");
    printf("Task completed\n");

    // No prob
    err = 0;

out:
    dm_task_destroy(dmt);
dup_free:
	free(dup_virt_dev_name);
	free(dup_params);

    return err;
}

/**
 * Close an entanglement virtual device under /dev/mapper.
 *
 * @param virt_dev_name the name of the virtual device, as it appears under
 *  /dev/mapper
 *
 * @return error code (0 on success)
 */
int ent_dm_destroy(char * virt_dev_name)
{
    struct dm_task *dmt;
    uint32_t cookie = 0;
    uint16_t udev_flags = 0;
    int err = 0;
    
    /* Just to be sure, let's get it on the heap */
    char * dup_virt_dev_name = strdup(virt_dev_name);

    ent_log_debug("Closing /dev/mapper/%s", dup_virt_dev_name);

    /* Instantiate the DM task (with the REMOVE ioctl command) */
    if (!(dmt = dm_task_create(DM_DEVICE_REMOVE))) {
        ent_log_error("Cannot create dm_task");
        err = 1;
        goto dup_free;
    }
    ent_log_debug("Successfully created dm_task");

    /* Set the name of the target device (to be closed) */
    if (!dm_task_set_name(dmt, dup_virt_dev_name)) {
        ent_log_error("Cannot set device name");
        err = 2;
        goto out;
    }
    ent_log_debug("Successfully set device name");

    /* Get a cookie (request ID, basically) to wait for task completion */
    if (!dm_task_set_cookie(dmt, &cookie, udev_flags)) {
        ent_log_error("Cannot set cookie");
        err = 3;
        goto out;
    }
    ent_log_debug("Successfully got a cookie");

    /* Needed for some reason */
    dm_task_retry_remove(dmt);
    ent_log_debug("Successful retry_remove");

    /* Run the task */
    if (!dm_task_run(dmt)) {
        ent_log_error("Cannot issue ioctl");
        err = 4;
        goto out;
    }
    ent_log_debug("Successfully run task");

    /* Wait for completion */
    dm_udev_wait(cookie);
    ent_log_debug("Task completed");

    // No prob
    err = 0;

out:
    dm_task_destroy(dmt);
dup_free:
	free(dup_virt_dev_name);

    return err;
}


/**
 * Writes a single 4096-byte sector to the disk.
 *
 * @param bdev_path The path of the block device
 * @param sector The index of the desired sector
 * @param The caller-allocated buffer (must hold 4096 bytes) where the data
 *  comes from
 *
 * @return The error code (0 on success)
 */
int ent_disk_writeSector(char * bdev_path, uint64_t sector, char * buf)
{
	return ent_disk_writeManySectors(bdev_path, sector, buf, 1);
}


/**
 * Writes many 4096-byte sectors to the disk.
 *
 * @param bdev_path The path of the block device
 * @param sector The index of the starting sector
 * @param The caller-allocated buffer where the data
 *  comes from
 * @param num_sectors The number of sectors to write
 *
 * @return The error code (0 on success)
 */
int ent_disk_writeManySectors(char * bdev_path, uint64_t sector, char * buf, size_t num_sectors)
{
    int fd;
    int err;
    
    /* Open file */
    fd = open(bdev_path, O_WRONLY);
    if (fd < 0) {
        ent_log_error("Could not open file %s", bdev_path);
        perror("Cause: ");
        err = errno;
        goto bad_open;
    }
    ent_log_debug("Opened file %s", bdev_path);

    /* Set offset in bytes */
    if (lseek(fd, sector * ENT_BLK_SIZE, SEEK_CUR) < 0) {
        ent_log_error("Could not lseek file %s to sector %lu", bdev_path, sector);
        perror("Cause: ");
        err = errno;
        goto bad_lseek;
    }
    ent_log_debug("Successful lseek on file %s to sector %lu", bdev_path, sector);

    /* Write in a loop */
    size_t bytes_to_write = ENT_BLK_SIZE * num_sectors;
    while (bytes_to_write > 0) {
    	/* Write syscall */
		ssize_t bytes_written = write(fd, buf, bytes_to_write);
		if (bytes_written < 0) {
			ent_log_red("Could not write file %s at sector %lu", bdev_path, sector);
			perror("Cause: ");
			err = errno;
			goto bad_write;
		}

		/* Partial write? No problem just log */
		if (bytes_written < bytes_to_write) {
			ent_log_debug("Partial write to file %s at sector %lu: %ld bytes instead of %ld",
					bdev_path, sector, bytes_written, bytes_to_write);
		}

		/* Advance loop */
		bytes_to_write -= bytes_written;
		buf += bytes_written;
    }

    // No prob
    err = 0;

bad_write:
bad_lseek:
    close(fd);
bad_open:
    return err;
}


/**
 * Returns the size in 4096-byte sectors (or < 0 if error).
 *
 * @param bdev_path The path of the block device
 *
 * @return The size (in 4096-byte sectors) of the disk, or -errno if error
 */
int64_t get_disk_size(char * bdev_path)
{
    int fd;
    uint64_t size_bytes;
    int64_t ret;
    
    /* Open file */
    fd = open(bdev_path, O_RDONLY);
    if (fd < 0) {
        perror("Error while opening file when getting disk size.\n");
        ret = -errno;
        goto bad_open;
    }

    /* Get size in bytes */
    if (ioctl(fd, BLKGETSIZE64, &size_bytes) < 0) {
        perror("Error: could not ioctl file while getting disk size.\n");
        ret = -errno;
        goto bad_ioctl;
    }

    /* Compute size in ent_block_size sectors */
    ret = (size_bytes / ENT_BLK_SIZE);

bad_ioctl:
    close(fd);
bad_open:
    return ret;
}

int main(int argc, char const *argv[])
{
    /*
        Get the size of the disk, and send it to the device-mapper as a parameter. Send the size as number of 4KB blocks.
    */

    char *dev_path;
    char *command;
    int redundancy_flag = 0;
    char params[1024];

    int err;

    if (argc != 3 && argc != 4) {
        printf("Wrong number of arguments. Usage: ./entanglement_app <command(open/close)> <dev_path> [<redundancy>]\n");
        return 1;
    }
    
    command = argv[1];
    dev_path = argv[2];
    disk_size = get_disk_size(dev_path);
    if (argc == 4) {
        redundancy_flag = 1;
    }

    /* Build param list */
	sprintf(params, "%s %lu %d", dev_path, disk_size, redundancy_flag);

    metadata_size = (disk_size * 3U) >> 10;
    printf("%u\n", metadata_size);

    // The two 8s in the end is to properly align the size for the block size of 4096 bytes. 
    unsigned int virtual_device_size = (((disk_size-metadata_size)*8) / 2) / 8 * 8;

    // This was just for testing purposes.     
    printf("%s\n", params);

    
    if (strcmp(command, "init") == 0) {
        char buf[ENT_BLK_SIZE];
        memset(buf, 0xFF, sizeof(buf));
        ent_disk_writeSector(dev_path, virtual_device_size, buf);
    }else if (strcmp(command, "open") == 0) {
        err = ent_dm_create(ENT_DEV_NAME, virtual_device_size, params);
        if (err) {
            perror("Error while creating dm target.\n");
            return err;
        }
    }else if (strcmp(command, "close") == 0) {
        err = ent_dm_destroy(ENT_DEV_NAME);
        if (err) {
            perror("Error while destroying dm target.\n");
            return err;
        }
    }else {
        printf("Wrong command. Usage: ./entanglement_app <command(open/close)> <dev_path> [<redundancy>]\n");
        return 2;
    }
    

    return 0;
}
