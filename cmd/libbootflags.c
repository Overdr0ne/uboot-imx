/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2023 Witekio
 */

#include <common.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <malloc.h>

#include "libbootflags.h"
#include "libbootflags_crc32.h"

void bootflags_init(bootflags_t *bootflags, uint32_t normal_slots)
{
	memset(bootflags, 0, sizeof(bootflags_t));

	strncpy(bootflags->magic, BOOTFLAGS_MAGIC, strlen(BOOTFLAGS_MAGIC));
	bootflags->normal_slots = normal_slots;

	// CRC updated in bootflags_serialize
}

int bootflags_deserialize(const uint8_t *addr, bootflags_t *bootflags)
{
	memcpy(bootflags, addr, sizeof(bootflags_t));
	// Check magic
	if (strcmp(BOOTFLAGS_MAGIC, bootflags->magic)) {
		printf("Invalid magic\n");
		return -ENODATA;
	}

	// Check CRC
	uint32_t crc = bootflags_crc32(0, (const unsigned char *)bootflags, offsetof(bootflags_t, crc));
	if (crc != bootflags->crc) {
		// Invalid CRC
		printf("Invalid CRC 0x%x (should be 0x%x)\n", bootflags->crc, crc);
		return -ENODATA;
	}
	return 0;
}

int bootflags_serialize(uint8_t *addr, const bootflags_t *bootflags)
{
	// Update CRC
	bootflags_t bootflags_copy = *bootflags;
	bootflags_copy.crc = bootflags_crc32(0, (const unsigned char *)&bootflags_copy, offsetof(bootflags_t, crc));

	memcpy(addr, &bootflags_copy, sizeof(bootflags_t));
	return 0;
}


void bootflags_abort_test(bootflags_t *bootflags)
{
	bootflags->test_mode = 0;
	// CRC updated in bootflags_serialize
}

void bootflags_increment_test_count(bootflags_t *bootflags)
{
	bootflags->test_count ++;
	// CRC updated in bootflags_serialize
}

int bootflags_first_active_partition(const bootflags_t *bootflags)
{
	uint32_t slots;
	int partition_index = 1;
	if (bootflags->test_mode) slots = bootflags->test_slots;
	else slots = bootflags->normal_slots;

	while (slots) {
		if (slots & 1) return partition_index;
		slots >>= 1;
		partition_index ++;
	}
	return -ENODATA;
}

int bootflags_segment_size(void)
{
	return sizeof(bootflags_t);
}

