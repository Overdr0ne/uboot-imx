/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2023 Witekio
 */

#ifndef __BOOTFLAGS_H
#define __BOOTFLAGS_H

#include <linux/types.h>

#define BOOTFLAGS_MAGIC "bootflags"
#define BOOTFLAGS_MAGIC_SIZE 16

struct bootflags {
	char magic[BOOTFLAGS_MAGIC_SIZE];
	uint8_t test_mode;     /* 0 = boot on normal slots, 1 = boot on test slots */
	uint32_t normal_slots; /* host byte order, each bit represents a partition */
	uint32_t test_slots;   /* host byte order, each bit represents a partition */
	uint8_t test_count;
	char reserved[16];
	uint32_t crc;          /* computed on all the above */
};

typedef struct bootflags bootflags_t;

void bootflags_init(bootflags_t *bootflags, uint32_t normal_slots);
int bootflags_deserialize(const uint8_t *addr, bootflags_t *bootflags);
int bootflags_serialize(uint8_t *addr, const bootflags_t *bootflags);
void bootflags_abort_test(bootflags_t *bootflags);
void bootflags_increment_test_count(bootflags_t *bootflags);
int bootflags_first_active_partition(const bootflags_t *bootflags);
int bootflags_segment_size(void);

#endif
