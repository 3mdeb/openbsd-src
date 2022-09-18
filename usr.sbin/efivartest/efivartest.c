/*-
 * Copyright (c) 2016 Netflix, Inc.
 * Copyright (c) 2022 3mdeb <contact@3mdeb.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This is adjusted FreeBSD's libefivar + simple main() to test out EFI
 * variables kernel API with no extra dependencies.
 */

#include <errno.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dev/efi/efi.h>
#include <dev/efi/efiio.h>
#include <uuid.h>

#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define	EFI_VARIABLE_NON_VOLATILE		0x00000001
#define	EFI_VARIABLE_BOOTSERVICE_ACCESS		0x00000002
#define	EFI_VARIABLE_RUNTIME_ACCESS		0x00000004
#define	EFI_VARIABLE_HARDWARE_ERROR_RECORD	0x00000008
#define	EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS	0x00000010
#define	EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS \
							0x00000020
#define	EFI_VARIABLE_APPEND_WRITE		0x00000040

typedef uuid_t efi_guid_t;

static int efi_fd = -2;

/*
 * The two functions below are dumb stubs.
 */

static size_t
utf8_to_ucs2(efi_char **buf, const char utf8[])
{
	size_t len = 0;
	while (utf8[len] != 0)
		++len;

	if (*buf == NULL)
		*buf = malloc(len + 1);

	for (size_t i = 0; i < len; ++i)
		(*buf)[i] = utf8[i];

	(*buf)[len++] = '\0';
	return len*sizeof(efi_char);
}

static char *
ucs2_to_utf8(const efi_char ucs2[])
{
	size_t len = 0;
	while (ucs2[len] != 0)
		++len;

	char *utf8 = malloc(len + 1);

	for (size_t i = 0; i < len + 1; ++i)
		utf8[i] = ucs2[i];

	return utf8;
}

static int
efi_open_dev(void)
{

	if (efi_fd == -2)
		efi_fd = open("/dev/efi", O_RDWR);
	if (efi_fd < 0)
		efi_fd = -1;
	return (efi_fd);
}

static void
efi_var_reset(struct efi_var_ioc *var)
{
	var->name = NULL;
	var->namesize = 0;
	memset(&var->vendor, 0, sizeof(var->vendor));
	var->attrib = 0;
	var->data = NULL;
	var->datasize = 0;
}

static int
rv_to_linux_rv(int rv)
{
	if (rv == 0)
		rv = 1;
	else
		rv = -errno;
	return (rv);
}

int
efi_set_variable(efi_guid_t guid, const char *name,
    uint8_t *data, size_t data_size, uint32_t attributes)
{
	struct efi_var_ioc var;
	int rv = ENOMEM;

	if (efi_open_dev() == -1)
		return -1;

	efi_var_reset(&var);
	var.namesize = utf8_to_ucs2(&var.name, name);
	if (var.namesize == (size_t)-1)
		goto errout;
	var.vendor = guid;
	var.data = data;
	var.datasize = data_size;
	var.attrib = attributes;
	rv = ioctl(efi_fd, EFIIOC_VAR_SET, &var);
errout:
	free(var.name);

	return rv;
}

int
efi_append_variable(efi_guid_t guid, const char *name,
    uint8_t *data, size_t data_size, uint32_t attributes)
{
	return efi_set_variable(guid, name, data, data_size,
	    attributes | EFI_VARIABLE_APPEND_WRITE);
}

int
efi_del_variable(efi_guid_t guid, const char *name)
{
	/* data_size of 0 deletes the variable */
	return efi_set_variable(guid, name, NULL, 0, 0);
}

int
efi_get_variable(efi_guid_t guid, const char *name,
    uint8_t **data, size_t *data_size, uint32_t *attributes)
{
	struct efi_var_ioc var;
	int rv = ENOMEM;
	static uint8_t buf[1024*32];

	if (efi_open_dev() == -1)
		return -1;

	efi_var_reset(&var);
	var.namesize = utf8_to_ucs2(&var.name, name);
	if (var.namesize == (size_t)-1)
		goto errout;
	var.vendor = guid;
	var.data = buf;
	var.datasize = sizeof(buf);
	rv = ioctl(efi_fd, EFIIOC_VAR_GET, &var);
	if (data_size != NULL)
		*data_size = var.datasize;
	if (data != NULL)
		*data = buf;
	if (attributes != NULL)
		*attributes = var.attrib;
errout:
	free(var.name);

	return rv_to_linux_rv(rv);
}

int
efi_get_next_variable_name(efi_guid_t **guid, char **name)
{
	struct efi_var_ioc var;
	int rv;
	static efi_char *buf;
	static size_t buflen = 256 * sizeof(efi_char);
	static efi_guid_t retguid;
	size_t size;

	if (efi_open_dev() == -1)
		return -1;

	/*
	 * Always allocate enough for an extra NUL on the end, but don't tell
	 * the IOCTL about it so we can NUL terminate the name before converting
	 * it to UTF8.
	 */
	if (buf == NULL)
		buf = malloc(buflen + sizeof(efi_char));

again:
	efi_var_reset(&var);
	var.name = buf;
	var.namesize = buflen;
	if (*name == NULL) {
		*buf = 0;
		/* GUID zeroed in var_reset */
	} else {
		size = utf8_to_ucs2(&var.name, *name);
		if (size == (size_t)-1)
			goto errout;
		var.vendor = **guid;
	}
	rv = ioctl(efi_fd, EFIIOC_VAR_NEXT, &var);
	if (rv == 0 && var.name == NULL) {
		/*
		 * Variable name not long enough, so allocate more space for the
		 * name and try again. As above, mind the NUL we add.
		 */
		void *new = realloc(buf, var.namesize + sizeof(efi_char));
		if (new == NULL) {
			rv = -1;
			errno = ENOMEM;
			goto done;
		}
		buflen = var.namesize;
		buf = new;
		goto again;
	}

	if (rv == 0) {
		free(*name);			/* Free last name, to avoid leaking */
		*name = NULL;			/* Force ucs2_to_utf8 to malloc new space */
		var.name[var.namesize / sizeof(efi_char)] = 0;	/* EFI doesn't NUL terminate */
		*name = ucs2_to_utf8(var.name);
		if (*name == NULL)
			goto errout;
		retguid = var.vendor;
		*guid = &retguid;
	}
errout:

	/* XXX The linux interface expects name to be a static buffer -- fix or leak memory? */
	/* XXX for the moment, we free just before we'd leak, but still leak last one */
done:
	if (rv != 0 && errno == ENOENT) {
		errno = 0;
		free(*name);			/* Free last name, to avoid leaking */
		return 0;
	}

	return (rv_to_linux_rv(rv));
}

int
main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Not enough arguments, try 'efivartest help'\n");
		return 1;
	}

	if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0 ||
            strcmp(argv[1], "--help") == 0) {
		printf("Usage: efivartest op opargs...\n");
		printf("\n");
		printf("  Read variable:\n");
		printf("       efivartest get GUID name\n\n");
		printf("  Set variable:\n");
		printf("       efivartest set GUID name value\n\n");
		printf("  Remove variable:\n");
		printf("       efivartest delete GUID name\n\n");
		printf("  List variables:\n");
		printf("       efivartest list\n");
	} else if (strcmp(argv[1], "get") == 0) {
		if (argc != 4) {
			fprintf(stderr, "Wrong number of arguments\n");
			return 1;
		}

		uuid_t uuid;
		uint32_t status;
		uuid_from_string(argv[2], &uuid, &status);
		if (status != uuid_s_ok) {
			fprintf(stderr, "Wrong UUID: %s\n", argv[2]);
			return 1;
		}

		uint8_t *data;
		size_t data_size;
		uint32_t attrs;
		if (efi_get_variable(uuid, argv[3], &data, &data_size, &attrs) < 0) {
			fprintf(stderr, "Failed to get var: %s\n", strerror(errno));
			return 1;
		}

		printf("%s-%s (%zu):\n", argv[2], argv[3], data_size);
		printf(" HEX: ");
		for (size_t i = 0; i < data_size; ++i) {
			printf("0x%02x ", data[i]);
		}
		printf("\n");
		printf("ASCII: ");
		for (size_t i = 0; i < data_size; ++i) {
			printf("%c", isprint(data[i]) ? data[i] : '?');
		}
		printf("\n");
	} else if (strcmp(argv[1], "set") == 0) {
		if (argc != 5) {
			fprintf(stderr, "Wrong number of arguments\n");
			return 1;
		}

		uuid_t uuid;
		uint32_t status;
		uuid_from_string(argv[2], &uuid, &status);
		if (status != uuid_s_ok) {
			fprintf(stderr, "Wrong UUID: %s\n", argv[2]);
			return 1;
		}

		uint8_t *data = argv[4];
		size_t data_size = strlen(argv[4]);
		uint32_t attrs = EFI_VARIABLE_NON_VOLATILE
			       | EFI_VARIABLE_BOOTSERVICE_ACCESS
			       | EFI_VARIABLE_RUNTIME_ACCESS;
		if (efi_set_variable(uuid, argv[3], data, data_size, attrs) < 0) {
			fprintf(stderr, "Failed to set var: %s\n", strerror(errno));
			return 1;
		}
	} else if (strcmp(argv[1], "delete") == 0) {
		if (argc != 4) {
			fprintf(stderr, "Wrong number of arguments\n");
			return 1;
		}

		uuid_t uuid;
		uint32_t status;
		uuid_from_string(argv[2], &uuid, &status);
		if (status != uuid_s_ok) {
			fprintf(stderr, "Wrong UUID: %s\n", argv[2]);
			return 1;
		}

		uint8_t *data = NULL;
		size_t data_size = 0;
		uint32_t attrs = 0;
		if (efi_set_variable(uuid, argv[3], data, data_size, attrs) < 0) {
			fprintf(stderr, "Failed to delete var: %s\n", strerror(errno));
			return 1;
		}
	} else if (strcmp(argv[1], "list") == 0) {
		efi_guid_t *guid = NULL;
		char *name = NULL;
		int rc;
		while ((rc = efi_get_next_variable_name(&guid, &name)) > 0) {
			char *uuid = NULL;
			uuid_to_string(guid, &uuid, NULL);
			printf("%s-%s\n", uuid, name);
			free(uuid);
		}

		if (rc < 0) {
			fprintf(stderr, "Error listing variables: %s\n", strerror(errno));
			return 1;
		}
	} else {
		fprintf(stderr, "Unknown command: %s\n", argv[1]);
		return 1;
	}

	return 0;
}
