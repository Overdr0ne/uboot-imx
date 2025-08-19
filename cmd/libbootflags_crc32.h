#ifndef _CRC32_H
#define _CRC32_H

#include <linux/types.h>

uint32_t bootflags_crc32(uint32_t val, const void *ss, int len);

#endif
