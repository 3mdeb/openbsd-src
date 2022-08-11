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
	struct device dev;
};

struct efi_attach_args {
	const char *eaa_name;
};

int	efi_match(struct device *, void *, void *);
void	efi_attach(struct device *, struct device *, void *);

const struct cfattach efi_ca = {
	sizeof(struct efi_esrt), efi_match, efi_attach
};

struct cfdriver efi_cd = {
	NULL, "efi", DV_DULL
};

static bus_space_handle_t ioh_st;
static uintptr_t esrt_paddr;

int
efi_match(struct device *parent, void *match, void *aux)
{
	static EFI_GUID	esrt_guid = ESRT_TABLE_GUID;

	EFI_SYSTEM_TABLE *st;
	EFI_CONFIGURATION_TABLE *ct;
	int i;
	size_t ct_len;

	bus_space_tag_t		 iot = X86_BUS_SPACE_MEM;
	bus_space_handle_t	 ioh_ct;

	struct efi_attach_args *eaa = aux;

	if (strcmp(eaa->eaa_name, efi_cd.cd_name) != 0)
		return 0;

	if (bus_space_map(iot, bios_efiinfo->system_table, sizeof(*st),
	    BUS_SPACE_MAP_PREFETCHABLE | BUS_SPACE_MAP_LINEAR, &ioh_st))
		panic("can't map EFI_SYSTEM_TABLE");
	st = bus_space_vaddr(iot, ioh_st);

	ct_len = sizeof(*st) * st->NumberOfTableEntries;

	if (bus_space_map(iot, (uintptr_t)st->ConfigurationTable, ct_len,
	    BUS_SPACE_MAP_PREFETCHABLE | BUS_SPACE_MAP_LINEAR, &ioh_ct))
		panic("can't map ConfigurationTable");
	ct = bus_space_vaddr(iot, ioh_ct);

	for (i = 0; i < st->NumberOfTableEntries; i++) {
		if (efi_guidcmp(&esrt_guid, &ct[i].VendorGuid) == 0) {
			esrt_paddr = (uintptr_t)ct[i].VendorTable;
			return 1;
		}
	}

	bus_space_unmap(iot, ioh_ct, ct_len);
	return 0;
}

void
efi_attach(struct device *parent, struct device *self, void *aux)
{
	bus_space_tag_t		 iot = X86_BUS_SPACE_MEM;

	EFI_SYSTEM_TABLE *st;
	uint16_t major, minor;

	EFI_SYSTEM_RESOURCE_TABLE *esrt;
	EFI_SYSTEM_RESOURCE_ENTRY *esre;
	int i;

	st = bus_space_vaddr(iot, ioh_st);

	major = st->Hdr.Revision >> 16;
	minor = st->Hdr.Revision & 0xffff;
	printf(": UEFI %d.%d", major, minor / 10);
	if (minor % 10)
		printf(".%d", minor % 10);
	printf("\n");

	esrt = (EFI_SYSTEM_RESOURCE_TABLE *)PMAP_DIRECT_MAP(esrt_paddr);
	esre = (EFI_SYSTEM_RESOURCE_ENTRY *)&esrt[1];

	printf("ESRT FwResourceCount = %d\n", esrt->FwResourceCount);

	for (i = 0; i < esrt->FwResourceCount; i++) {
		printf("ESRT[%d]:\n", i);
		printf("  FwClass: %08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
		    esre[i].FwClass.Data1,
		    esre[i].FwClass.Data2,
		    esre[i].FwClass.Data3,
		    esre[i].FwClass.Data4[0], esre[i].FwClass.Data4[1],
		    esre[i].FwClass.Data4[2], esre[i].FwClass.Data4[3],
		    esre[i].FwClass.Data4[4], esre[i].FwClass.Data4[5],
		    esre[i].FwClass.Data4[6], esre[i].FwClass.Data4[7]);
		printf("  FwType: %08x\n", esre[i].FwType);
		printf("  FwVersion: %08x\n", esre[i].FwVersion);
		printf("  LowestSupportedFwVersion: %08x\n", esre[i].LowestSupportedFwVersion);
		printf("  CapsuleFlags: %08x\n", esre[i].CapsuleFlags);
		printf("  LastAttemptVersion: %08x\n", esre[i].LastAttemptVersion);
		printf("  LastAttemptStatus: %08x\n", esre[i].LastAttemptStatus);
	}
}
