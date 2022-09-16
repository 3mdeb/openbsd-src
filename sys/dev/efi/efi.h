/*	$OpenBSD$	*/

/*
 * Copyright (c) 2022 3mdeb <contact@3mdeb.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _DEV_EFI_EFI_H_
#define _DEV_EFI_EFI_H_

#include <sys/uuid.h>

#define EFI_TABLE_ESRT \
	{0xb122a263,0x3661,0x4f68,0x99,0x29,{0x78,0xf8,0xb0,0xd6,0x21,0x80}}

#define ESRT_FIRMWARE_RESOURCE_VERSION 1

struct efi_esrt_table {
	uint32_t	fw_resource_count;
	uint32_t	fw_resource_count_max;
	uint64_t	fw_resource_version;
	uint8_t		entries[];
};

struct efi_esrt_entry_v1 {
	struct uuid	fw_class;
	uint32_t	fw_type;
	uint32_t	fw_version;
	uint32_t	lowest_supported_fw_version;
	uint32_t	capsule_flags;
	uint32_t	last_attempt_version;
	uint32_t	last_attempt_status;
};

#endif /* _DEV_EFI_EFI_H_ */
