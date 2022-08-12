#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <machine/biosvar.h>
#include <machine/bus.h>
#include <machine/efivar.h>
#include <machine/pmap.h>

#include <dev/acpi/efi.h>

struct efi {
	struct	device efi_dev;
};

int	efi_match(struct device *, void *, void *);
void	efi_attach(struct device *, struct device *, void *);

const struct cfattach efi_ca = {
	sizeof(struct efi), efi_match, efi_attach
};

struct cfdriver efi_cd = {
	NULL, "efi", DV_DULL
};

EFI_SYSTEM_RESOURCE_TABLE *efi_esrt;

int
efi_match(struct device *parent, void *match, void *aux)
{
	struct efi_attach_args *eaa = aux;

	if (strcmp(eaa->eaa_name, efi_cd.cd_name) != 0)
		return (0);

	return (bios_efiinfo->config_esrt != 0);
}

void
efi_attach(struct device *parent, struct device *self, void *aux)
{
	bus_space_handle_t ioh_st;
	bus_space_tag_t iot = X86_BUS_SPACE_MEM;

	EFI_SYSTEM_TABLE *st;
	EFI_SYSTEM_RESOURCE_ENTRY *esre;

	int i;
	uint16_t major, minor;

	if (bus_space_map(iot, bios_efiinfo->system_table, sizeof(*st),
	    BUS_SPACE_MAP_PREFETCHABLE | BUS_SPACE_MAP_LINEAR, &ioh_st))
		panic("can't map EFI_SYSTEM_TABLE");

	st = bus_space_vaddr(iot, ioh_st);

	major = st->Hdr.Revision >> 16;
	minor = st->Hdr.Revision & 0xffff;
	printf(": UEFI %d.%d", major, minor / 10);
	if (minor % 10)
		printf(".%d", minor % 10);
	printf("\n");

	efi_esrt = (EFI_SYSTEM_RESOURCE_TABLE *)
	    PMAP_DIRECT_MAP(bios_efiinfo->config_esrt);
	esre = (EFI_SYSTEM_RESOURCE_ENTRY *)&efi_esrt[1];

	printf("ESRT FwResourceCount = %d\n", efi_esrt->FwResourceCount);

	for (i = 0; i < efi_esrt->FwResourceCount; i++) {
		printf("ESRT[%d]:\n", i);
		printf("  FwClass: %08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
		    esre[i].FwClass.Data1,
		    esre[i].FwClass.Data2,
		    esre[i].FwClass.Data3,
		    esre[i].FwClass.Data4[0], esre[i].FwClass.Data4[1],
		    esre[i].FwClass.Data4[2], esre[i].FwClass.Data4[3],
		    esre[i].FwClass.Data4[4], esre[i].FwClass.Data4[5],
		    esre[i].FwClass.Data4[6], esre[i].FwClass.Data4[7]);
		printf("  FwType: 0x%08x\n", esre[i].FwType);
		printf("  FwVersion: 0x%08x\n", esre[i].FwVersion);
		printf("  LowestSupportedFwVersion: 0x%08x\n", esre[i].LowestSupportedFwVersion);
		printf("  CapsuleFlags: 0x%08x\n", esre[i].CapsuleFlags);
		printf("  LastAttemptVersion: 0x%08x\n", esre[i].LastAttemptVersion);
		printf("  LastAttemptStatus: 0x%08x\n", esre[i].LastAttemptStatus);
	}
}

int
efi_get_esrt(void **table, unsigned int *size)
{
	if (efi_esrt == NULL)
		return (1);

	*table = efi_esrt;
	*size = sizeof(efi_esrt) +
	    sizeof(EFI_SYSTEM_RESOURCE_ENTRY) * efi_esrt->FwResourceCount;
	return (0);
}
