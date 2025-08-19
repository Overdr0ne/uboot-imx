// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2023 Witekio
 */

#include <blk.h>
#include <command.h>
#include <common.h>
#include <dm.h>
#include <malloc.h>
#include <mmc.h>
#include <part.h>
#include <version.h>

#include "libbootflags.h"

#if U_BOOT_VERSION_NUM >= 2023
static enum uclass_id get_class_id(const char *name)
{
	if (!strcmp(CONFIG_BOOTFLAGS_INTERFACE, "virtio")) {
		return UCLASS_VIRTIO;
	}
	if (!strcmp(CONFIG_BOOTFLAGS_INTERFACE, "mmc")) {
		return UCLASS_MMC;
	}
	if (!strcmp(CONFIG_BOOTFLAGS_INTERFACE, "usb")) {
		return UCLASS_USB;
	}
	return UCLASS_INVALID;
}
#else
static enum if_type get_if_type(const char *name)
{
	if (!strcmp(CONFIG_BOOTFLAGS_INTERFACE, "virtio"))   return IF_TYPE_VIRTIO;
	else if (!strcmp(CONFIG_BOOTFLAGS_INTERFACE, "mmc")) return IF_TYPE_MMC;
	else if (!strcmp(CONFIG_BOOTFLAGS_INTERFACE, "usb")) return IF_TYPE_USB;
	else                                                 return IF_TYPE_UNKNOWN;
}
#endif /* U_BOOT_VERSION_NUM */

static int bootflags_write_offset(const bootflags_t *bootflags, uint64_t offset)
{
#if U_BOOT_VERSION_NUM >= 2023
	enum uclass_id class_id = get_class_id(CONFIG_BOOTFLAGS_INTERFACE);
	if (class_id == UCLASS_INVALID) {
#else
	enum if_type if_type = get_if_type(CONFIG_BOOTFLAGS_INTERFACE);
	if (IF_TYPE_UNKNOWN == if_type) {
#endif /* U_BOOT_VERSION_NUM */
		printf("Unsupported bootflags interface: %s\n", CONFIG_BOOTFLAGS_INTERFACE);
		return -EFAULT;
	}

	struct blk_desc *desc;
#if U_BOOT_VERSION_NUM >= 2023
	desc = blk_get_devnum_by_uclass_id(class_id, CONFIG_BOOTFLAGS_DEVICE);
	if (!desc) {
		printf("blk_get_devnum_by_uclass_id(%d, %d) error\n", class_id, CONFIG_BOOTFLAGS_DEVICE);
		return -ENODEV;
	}
#else
	desc = blk_get_devnum_by_type(if_type, CONFIG_BOOTFLAGS_DEVICE);
	if (!desc) {
		printf("blk_get_devnum_by_type(%d, %d) error\n", if_type, CONFIG_BOOTFLAGS_DEVICE);
		return -ENODEV;
	}
#endif /* U_BOOT_VERSION_NUM */
	// We assume that bootflags are located in a single block
	// We assume that no other data is contained within this block (or it will be erased)

	// block start: the block containing the offset
	lbaint_t blk_start = offset / desc->blksz;
	int bf_offset = offset % desc->blksz;
	if (bf_offset) {
		printf("Warning: offset 0x%llx not at the beginning of a block (block size %lu)\n", offset, desc->blksz);
	}

	if (bf_offset + bootflags_segment_size() > desc->blksz) {
		printf("Error: bootflags beyond single block: offset=0x%llx, block-size=%lu, bootflags-size=%d\n",
		       offset, desc->blksz, bootflags_segment_size());
		return -EMSGSIZE;
	}

	const uint8_t n_blocks = 1; // Work on 1 single block
	uint8_t *buffer = malloc(desc->blksz * n_blocks);
	if (!buffer) {
		printf("Cannot malloc(%lu)\n", desc->blksz * n_blocks);
		return -ENOMEM;
	}

	memset(buffer, 0, desc->blksz * n_blocks);
	bootflags_serialize(buffer+bf_offset, bootflags);
#if U_BOOT_VERSION_NUM >= 2023
	ulong n = blk_dwrite(desc, blk_start, n_blocks, buffer);
#else
	ulong n = blk_write_devnum(if_type, CONFIG_BOOTFLAGS_DEVICE, blk_start, n_blocks, buffer);
#endif /* U_BOOT_VERSION_NUM */
	free(buffer);
	if (n != n_blocks) {
		printf("blk_write_devnum error: %ld\n", n);
		return -EIO;
	}
	return 0;
}

/* Write the bootflags on NVM (main copy and duplicate) */
static int bootflags_write(const bootflags_t *bootflags)
{
	int err = bootflags_write_offset(bootflags, CONFIG_BOOTFLAGS_OFFSET);
	if (!err) err = bootflags_write_offset(bootflags, CONFIG_BOOTFLAGS_OFFSET_COPY);
	return err;
}

static void bootflags_env_update(const bootflags_t *bootflags)
{
	int err;
	char string_hex_first_partition[20] = {"\0"}; /*Allocate 16 char for max 64-bit number, 2 chars for "0x", and 1 null*/
	// Store the value in some environment variables
	err = env_set_ulong("bootflags_test_mode", bootflags->test_mode);
	if (!err) printf("bootflags_test_mode=%d\n", bootflags->test_mode);

	err = env_set_ulong("bootflags_test_count", bootflags->test_count);
	if (!err) printf("bootflags_test_count=%d\n", bootflags->test_count);

	int first_active_partition = bootflags_first_active_partition(bootflags);
	sprintf(string_hex_first_partition, "0x%x", first_active_partition);
	err = env_set("bootflags_first_active_partition", string_hex_first_partition);
	if (!err) printf("bootflags_first_active_partition=0x%x\n", first_active_partition);
}

int bootflags_cmd_init(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	bootflags_t bootflags;

	if (argc < 1) {
		printf("Missing argument NORMAL-SLOTS\n");
		return CMD_RET_FAILURE;
	}

	uint32_t normal_slots = simple_strtoul(argv[0], NULL, 0);
	printf("do_init: normal_slots=0x%x\n", normal_slots);

	bootflags_init(&bootflags, normal_slots);

	int err = bootflags_write(&bootflags);
	if (err) return CMD_RET_FAILURE;

	bootflags_env_update(&bootflags);

	return CMD_RET_SUCCESS;
}

static int bootflags_read_offset(bootflags_t *bootflags, uint64_t offset)
{
#if U_BOOT_VERSION_NUM >= 2023
	enum uclass_id class_id = get_class_id(CONFIG_BOOTFLAGS_INTERFACE);
	if (class_id == UCLASS_INVALID) {
#else
	enum if_type if_type = get_if_type(CONFIG_BOOTFLAGS_INTERFACE);
	if (IF_TYPE_UNKNOWN == if_type) {
#endif /* U_BOOT_VERSION_NUM */
		printf("Unsupported bootflags interface: %s\n", CONFIG_BOOTFLAGS_INTERFACE);
		return -EFAULT;
	}

	struct blk_desc *desc;

#if U_BOOT_VERSION_NUM >= 2023
	desc = blk_get_devnum_by_uclass_id(class_id, CONFIG_BOOTFLAGS_DEVICE);
	if (!desc) {
		printf("blk_get_devnum_by_uclass_id(%d, %d) error\n", class_id, CONFIG_BOOTFLAGS_DEVICE);
		return -ENODEV;
	}
#else
	desc = blk_get_devnum_by_type(if_type, CONFIG_BOOTFLAGS_DEVICE);
	if (!desc) {
		printf("blk_get_devnum_by_type(%d, %d) error\n", if_type, CONFIG_BOOTFLAGS_DEVICE);
		return -ENODEV;
	}
#endif /* U_BOOT_VERSION_NUM */
	// block start: the block containing the offset
	lbaint_t blk_start = offset / desc->blksz;
	int bf_offset = offset % desc->blksz;

	// Bootflags must be located within a single block
	if (bf_offset + bootflags_segment_size() > desc->blksz) {
		printf("Error: bootflags beyond single block: offset=0x%llx, block-size=%lu, bootflags-size=%d\n",
		       offset, desc->blksz, bootflags_segment_size());
		return -EMSGSIZE;
	}

	uint8_t *buffer = malloc(desc->blksz);
	if (!buffer) {
		printf("Cannot malloc(%lu)\n", desc->blksz);
		return -ENOMEM;
	}
#if U_BOOT_VERSION_NUM >= 2023
	ulong n = blk_dread(desc, blk_start, 1, buffer);
#else
	ulong n = blk_read_devnum(if_type, CONFIG_BOOTFLAGS_DEVICE, blk_start, 1, buffer);
#endif /* U_BOOT_VERSION_NUM */
	if (n != 1) {
		printf("blk_read_devnum error: %ld\n", n);
		free(buffer);
		return -EIO;
	}

	int err = bootflags_deserialize(buffer+bf_offset, bootflags);
	free(buffer);

	if (err) {
		printf("bootflags_read error %d\n", err);
		return err;
	}
	return 0;
}

static int bootflags_read(bootflags_t *bootflags) {
	return bootflags_read_offset(bootflags, CONFIG_BOOTFLAGS_OFFSET);
}


int bootflags_cmd_read(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	bootflags_t bootflags;
	printf("Reading bootflags from %s %d at 0x%x\n",
	       CONFIG_BOOTFLAGS_INTERFACE,
	       CONFIG_BOOTFLAGS_DEVICE,
	       CONFIG_BOOTFLAGS_OFFSET);

	int err = bootflags_read(&bootflags);
	if (err) return CMD_RET_FAILURE;

	bootflags_env_update(&bootflags);

	printf("bootflags.normal_slots=0x%x\n", bootflags.normal_slots);
	printf("bootflags.test_slots=0x%x\n", bootflags.test_slots);
	printf("bootflags.crc=0x%x\n", bootflags.crc);

	return CMD_RET_SUCCESS;
}

/*
 * Recovery environment is used if bootflags are
 * not set (e.g first boot) or corrupted.
 * This environment starts up the platform that
 * initialize bootlfags in the next boot stages
 */
int bootflags_cmd_recovery_env(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	int err;
	/* Set bootflags_test_mode to 0 */
	err = env_set_ulong("bootflags_test_mode", 0);
	if (!err) {
		printf("bootflags_test_mode=0\n");
	}
	/* Set bootflags_test_count to 0 */
	err = env_set_ulong("bootflags_test_count", 0);
	if (!err) {
		printf("bootflags_test_count=0\n");
	}
	/* Set first boot partition as active */
	err = env_set_ulong("bootflags_first_active_partition", CONFIG_BOOTFLAGS_FIRST_ACTIVE_PARTITION);
	if (!err) {
		printf("bootflags_first_active_partition=%d\n", CONFIG_BOOTFLAGS_FIRST_ACTIVE_PARTITION);
	}
	return CMD_RET_SUCCESS;
}

int bootflags_cmd_check(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	bootflags_t bootflags1;
	bootflags_t bootflags2;
	int finalerr = 0;
	printf("Checking bootflags at %s %d at 0x%x and 0x%x\n",
	       CONFIG_BOOTFLAGS_INTERFACE,
	       CONFIG_BOOTFLAGS_DEVICE,
	       CONFIG_BOOTFLAGS_OFFSET,
	       CONFIG_BOOTFLAGS_OFFSET_COPY);

	int err1 = bootflags_read_offset(&bootflags1, CONFIG_BOOTFLAGS_OFFSET);
	int err2 = bootflags_read_offset(&bootflags2, CONFIG_BOOTFLAGS_OFFSET_COPY);
	if (err1 && err2) {
		// Both segments are corrupted
		printf("Bootflags check: failed\n");
		finalerr = CMD_RET_FAILURE;
	} else if (!err1 && !err2) {
		// Both segment ok
		printf("Bootflags check: ok\n");
		finalerr = CMD_RET_SUCCESS;
	} else {
		if (err1) {
			// Segment 1 corrupted. Write segment 2 to segment 1
			printf("Bootflags check: segment 1 needs recovery\n");
			finalerr = bootflags_write_offset(&bootflags2, CONFIG_BOOTFLAGS_OFFSET);
		} else {
			// The opposite
			printf("Bootflags check: segment 2 needs recovery\n");
			finalerr = bootflags_write_offset(&bootflags1, CONFIG_BOOTFLAGS_OFFSET_COPY);
		}
		if (finalerr) {
			printf("Bootflags check: recovery failed\n");
		} else {
			printf("Bootflags check: recovered\n");
		}
	}

	return finalerr;
}

int bootflags_cmd_abort_test(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	bootflags_t bootflags;
	int err = bootflags_read(&bootflags);
	if (err) return CMD_RET_FAILURE;

	bootflags_abort_test(&bootflags);

	err = bootflags_write(&bootflags);
	if (err) return CMD_RET_FAILURE;

	bootflags_env_update(&bootflags);

	return CMD_RET_SUCCESS;
}

int bootflags_cmd_increment_test_count(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	bootflags_t bootflags;
	int err;

	err = bootflags_read(&bootflags);
	if (err) return CMD_RET_FAILURE;

	bootflags_increment_test_count(&bootflags);

	err = bootflags_write(&bootflags);
	if (err) return CMD_RET_FAILURE;

	bootflags_env_update(&bootflags);

	return CMD_RET_SUCCESS;
}

static struct cmd_tbl cmd_sub[] = {
	U_BOOT_CMD_MKENT(check, CONFIG_SYS_MAXARGS, 0, bootflags_cmd_check,
	                 "bootflags check",
	                 "Check bootflags ingtegrity and restore a corrupted segment"),
	U_BOOT_CMD_MKENT(init, CONFIG_SYS_MAXARGS, 0, bootflags_cmd_init,
	                 "bootflags init",
	                 "Initialize bootflags with default values"),
	U_BOOT_CMD_MKENT(read, CONFIG_SYS_MAXARGS, 0, bootflags_cmd_read,
	                 "bootflags read",
	                 "Load bootflags in environment variables:\n"
	                 "  bootflags_test_mode\n"
	                 "  bootflags_test_count\n"
	                 "  bootflags_first_active_partition\n"),
	U_BOOT_CMD_MKENT(abort-test, CONFIG_SYS_MAXARGS, 0, bootflags_cmd_abort_test,
	                 "bootflags abort-test",
	                 "Abort test mode and revert to normal mode\n"),
	U_BOOT_CMD_MKENT(increment-test-count, CONFIG_SYS_MAXARGS, 0, bootflags_cmd_increment_test_count,
	                 "bootflags increment-test-count",
	                 "Increment test count by one\n"),
	U_BOOT_CMD_MKENT(set-recovery-env, CONFIG_SYS_MAXARGS, 0, bootflags_cmd_recovery_env,
	                 "bootflags set-recovery-env",
	                 "Set bootflags recovery environment values\n"),
};

static int do_bootflags(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	struct cmd_tbl *cp;

	if (argc < 2) return CMD_RET_USAGE;

	/* drop main-command argument */
	argc--;
	argv++;

	cp = find_cmd_tbl(argv[0], cmd_sub, ARRAY_SIZE(cmd_sub));

	/* drop sub-command argument */
	argc--;
	argv++;

	if (cp) return cp->cmd(cmdtp, flag, argc, argv);

	return CMD_RET_USAGE;
}

U_BOOT_CMD(
    bootflags,	3,	0,	do_bootflags,
    "Manage bootflags",
    "check\n"
    "bootflags init NORMAL-SLOTS\n"
    "bootflags read\n"
    "bootflags abort-test\n"
    "bootflags increment-test-count\n"
    "bootflags set-recovery-env\n"
);
