#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <machine/biosvar.h>
#include <machine/cpufunc.h>
#include <machine/bus.h>
#include <machine/fpu.h>

#include <dev/acpi/efi.h>

#include <dev/clock_subr.h>

struct efi_esrt {
};

int	efi_match(struct device *, void *, void *);
void	efi_attach(struct device *, struct device *, void *);

const struct cfattach efi_ca = {
	sizeof(struct efi_esrt), efi_match, efi_attach
};

struct cfdriver efi_cd = {
	NULL, "efi", DV_DULL
};

int
efi_match(struct device *parent, void *match, void *aux)
{
	static EFI_GUID	esrt_guid = ESRT_TABLE_GUID;

	EFI_SYSTEM_TABLE *st;
	EFI_CONFIGURATION_TABLE *ct;
	int i;
	size_t ct_len;

	bus_space_tag_t		 iot = X86_BUS_SPACE_MEM;
	bus_space_handle_t	 ioh_st;
	bus_space_handle_t	 ioh_ct;

	if (bus_space_map(iot, bios_efiinfo->system_table, sizeof(*st),
	    BUS_SPACE_MAP_PREFETCHABLE | BUS_SPACE_MAP_LINEAR, &ioh_st))
		panic("can't map EFI_SYSTEM_TABLE");
	st = bus_space_vaddr(iot, ioh_st);

        ct_len = sizeof(*st) * st->NumberOfTableEntries;

	if (bus_space_map(iot, (uintptr_t)st->ConfigurationTable, ct_len,
	    BUS_SPACE_MAP_PREFETCHABLE | BUS_SPACE_MAP_LINEAR, &ioh_ct))
		panic("can't map ConfigurationTable");
	ct = bus_space_vaddr(iot, ioh_ct);

	printf(": ST %p\n", st);
	printf(": ST# %lu\n", st->NumberOfTableEntries);

	for (i = 0; i < st->NumberOfTableEntries; i++) {
		if (efi_guidcmp(&esrt_guid, &ct[i].VendorGuid) == 0)
			return 1;
	}

	bus_space_unmap(iot, ioh_st, sizeof(*st));
	bus_space_unmap(iot, ioh_ct, ct_len);

	printf(": Didn't find ESRT!\n");
	return 0;
}

void
efi_attach(struct device *parent, struct device *self, void *aux)
{
	uint64_t system_table;
	EFI_SYSTEM_TABLE *st;
	uint16_t major, minor;

	system_table = bios_efiinfo->system_table;
	KASSERT(system_table);

	st = (void *)system_table;

	major = st->Hdr.Revision >> 16;
	minor = st->Hdr.Revision & 0xffff;
	printf(": UEFI %d.%d", major, minor / 10);
	if (minor % 10)
		printf(".%d", minor % 10);
	printf("\n");
	return; // XXX
}
