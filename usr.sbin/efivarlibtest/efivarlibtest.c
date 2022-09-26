#include <errno.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <efivar/efivar.h>

int
main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Not enough arguments, try 'efitest help'\n");
		return 1;
	}

	if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0 ||
            strcmp(argv[1], "--help") == 0) {
		printf("Usage: efitest op opargs...\n");
		printf("\n");
		printf("  Read variable:\n");
		printf("       efitest get GUID name\n\n");
		printf("  Set variable:\n");
		printf("       efitest set GUID name value\n\n");
		printf("  Remove variable:\n");
		printf("       efitest delete GUID name\n\n");
		printf("  List variables:\n");
		printf("       efitest list\n");
	} else if (strcmp(argv[1], "get") == 0) {
		if (argc != 4) {
			fprintf(stderr, "Wrong number of arguments\n");
			return 1;
		}

		efi_guid_t uuid;
		if (efi_id_guid_to_guid(argv[2], &uuid) < 0) {
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

		efi_guid_t uuid;
		if (efi_id_guid_to_guid(argv[2], &uuid) < 0) {
			fprintf(stderr, "Wrong UUID: %s\n", argv[2]);
			return 1;
		}

		uint8_t *data = argv[4];
		size_t data_size = strlen(argv[4]);
		uint32_t attrs = EFI_VARIABLE_NON_VOLATILE
			       | EFI_VARIABLE_BOOTSERVICE_ACCESS
			       | EFI_VARIABLE_RUNTIME_ACCESS;
		if (efi_set_variable(uuid, argv[3], data, data_size, attrs, 0666) < 0) {
			fprintf(stderr, "Failed to set var: %s\n", strerror(errno));
			return 1;
		}
	} else if (strcmp(argv[1], "delete") == 0) {
		if (argc != 4) {
			fprintf(stderr, "Wrong number of arguments\n");
			return 1;
		}

		efi_guid_t uuid;
		if (efi_id_guid_to_guid(argv[2], &uuid) < 0) {
			fprintf(stderr, "Wrong UUID: %s\n", argv[2]);
			return 1;
		}

		uint8_t *data = NULL;
		size_t data_size = 0;
		uint32_t attrs = 0;
		if (efi_set_variable(uuid, argv[3], data, data_size, attrs, 0666) < 0) {
			fprintf(stderr, "Failed to delete var: %s\n", strerror(errno));
			return 1;
		}
	} else if (strcmp(argv[1], "list") == 0) {
		efi_guid_t *guid = NULL;
		char *name = NULL;
		int rc;
		while ((rc = efi_get_next_variable_name(&guid, &name)) > 0) {
			char *uuid = NULL;
			efi_guid_to_id_guid(guid, &uuid);
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
