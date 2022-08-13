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
void	efi_map_runtime(void);
void	efi_enter(void);
void	efi_leave(void);

const struct cfattach efi_ca = {
	sizeof(struct efi), efi_match, efi_attach
};

struct cfdriver efi_cd = {
	NULL, "efi", DV_DULL
};

EFI_SYSTEM_RESOURCE_TABLE *efi_esrt;
EFI_RUNTIME_SERVICES *efi_rs;
u_long efi_psw;

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

	efi_rs = st->RuntimeServices;

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

	efi_map_runtime();

	EFI_TIME time;
	EFI_STATUS status;

	efi_enter();
	status = efi_rs->GetTime(&time, NULL);
	efi_leave();
}

void
efi_map_runtime(void)
{
	EFI_MEMORY_DESCRIPTOR *desc;
	int i;

	uint32_t mmap_desc_size = bios_efiinfo->mmap_desc_size;
	uint32_t mmap_size = bios_efiinfo->mmap_size;
	uint64_t mmap_start = bios_efiinfo->mmap_start;

	if (bios_efiinfo->mmap_desc_ver != 1) {
		panic("Unsupported version of EFI memory map: %u\n",
		    bios_efiinfo->mmap_desc_ver);
	}

	desc = (EFI_MEMORY_DESCRIPTOR *)PMAP_DIRECT_MAP(mmap_start);
	for (i = 0; i < mmap_size / mmap_desc_size; i++) {
		if (desc->Attribute & EFI_MEMORY_RUNTIME) {
			vaddr_t va = desc->VirtualStart;
			paddr_t pa = desc->PhysicalStart;
			int npages = desc->NumberOfPages;
			vm_prot_t prot = PROT_READ | PROT_WRITE;

#define EFI_DEBUG 
#ifdef EFI_DEBUG
			printf("type 0x%x pa 0x%llx va 0x%llx pages 0x%llx "
			    "attr 0x%llx\n",
			    desc->Type, desc->PhysicalStart,
			    desc->VirtualStart, desc->NumberOfPages,
			    desc->Attribute);
#endif

			/*
			 * If the virtual address is still zero, use
			 * an identity mapping.
			 */
			if (va == 0)
				va = pa;

			/*
			 * Only make pages marked as runtime service code
			 * executable.  This violates the standard but it
			 * seems we can get away with it.
			 */
			if (desc->Type == EfiRuntimeServicesCode)
				prot |= PROT_EXEC;

			if (desc->Attribute & EFI_MEMORY_RP)
				prot &= ~PROT_READ;
			if (desc->Attribute & EFI_MEMORY_XP)
				prot &= ~PROT_EXEC;
			if (desc->Attribute & EFI_MEMORY_RO)
				prot &= ~PROT_WRITE;

			while (npages--) {
				pmap_enter(pmap_kernel(), va, pa, prot,
				    PMAP_WIRED);
				va += PAGE_SIZE;
				pa += PAGE_SIZE;
			}
		}

		desc = NextMemoryDescriptor(desc, mmap_desc_size);
	}

	pmap_update(pmap_kernel());
}

void
efi_enter(void)
{
	efi_psw = intr_disable();
	fpu_kernel_enter();
}

void
efi_leave(void)
{
	fpu_kernel_exit();
	intr_restore(efi_psw);
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
