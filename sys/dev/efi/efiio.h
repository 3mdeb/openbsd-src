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

#ifndef _DEV_EFI_EFIIO_H_
#define _DEV_EFI_EFIIO_H_

#include <dev/efi/efi.h>
#include <sys/ioccom.h>
#include <sys/uuid.h>

struct efi_get_table_ioc
{
	void *buf;		/* Pointer to userspace buffer */
	struct uuid uuid;	/* UUID to look up */
	size_t table_len;	/* Table size */
	size_t buf_len;		/* Size of the buffer */
};

struct efi_var_ioc
{
	efi_char *name;		/* User pointer to name, in UCS2 chars */
	size_t namesize;	/* Number of *bytes* in the name including
				   terminator */
	struct uuid vendor;	/* Vendor's UUID for variable */
	uint32_t attrib;	/* Attributes */
	void *data;		/* User pointer to value */
	size_t datasize;	/* Number of *bytes* in the value */
};

#define EFIIOC_GET_TABLE	_IOWR('E',  1, struct efi_get_table_ioc)
#define EFIIOC_VAR_GET          _IOWR('E',  4, struct efi_var_ioc)
#define EFIIOC_VAR_NEXT         _IOWR('E',  5, struct efi_var_ioc)
#define EFIIOC_VAR_SET          _IOWR('E',  6, struct efi_var_ioc)

#endif /* _DEV_EFI_EFIIO_H_ */
