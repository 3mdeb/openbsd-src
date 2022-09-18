/*	$OpenBSD$	*/

/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
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

#include <sys/param.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/user.h>

#include <uvm/uvm_extern.h>

#include <machine/biosvar.h>
extern paddr_t cr3_reuse_pcid;
extern void (*cpuresetfn)(void);
extern void (*powerdownfn)(void);

#include <dev/acpi/efi.h>
#include <dev/efi/efiio.h>

#include <dev/clock_subr.h>

extern todr_chip_handle_t todr_handle;

extern EFI_MEMORY_DESCRIPTOR *mmap;

struct efi_softc {
	struct device	sc_dev;
	struct pmap	*sc_pm;
	EFI_RUNTIME_SERVICES *sc_rs;
	EFI_SYSTEM_RESOURCE_TABLE *esrt;
	u_long		sc_psw;
	uint64_t	sc_cr3;

	struct todr_chip_handle sc_todr;
};

int	efi_match(struct device *, void *, void *);
void	efi_attach(struct device *, struct device *, void *);

const struct cfattach efi_ca = {
	sizeof(struct efi_softc), efi_match, efi_attach
};

struct cfdriver efi_cd = {
	NULL, "efi", DV_DULL
};

static int efi_status2err[36] = {
	0,		/* EFI_SUCCESS */
	ENOEXEC,	/* EFI_LOAD_ERROR */
	EINVAL,		/* EFI_INVALID_PARAMETER */
	ENOSYS,		/* EFI_UNSUPPORTED */
	EMSGSIZE, 	/* EFI_BAD_BUFFER_SIZE */
	EOVERFLOW,	/* EFI_BUFFER_TOO_SMALL */
	EBUSY,		/* EFI_NOT_READY */
	EIO,		/* EFI_DEVICE_ERROR */
	EROFS,		/* EFI_WRITE_PROTECTED */
	EAGAIN,		/* EFI_OUT_OF_RESOURCES */
	EIO,		/* EFI_VOLUME_CORRUPTED */
	ENOSPC,		/* EFI_VOLUME_FULL */
	ENXIO,		/* EFI_NO_MEDIA */
	ESTALE,		/* EFI_MEDIA_CHANGED */
	ENOENT,		/* EFI_NOT_FOUND */
	EACCES,		/* EFI_ACCESS_DENIED */
	ETIMEDOUT,	/* EFI_NO_RESPONSE */
	EADDRNOTAVAIL,	/* EFI_NO_MAPPING */
	ETIMEDOUT,	/* EFI_TIMEOUT */
	ENXIO,		/* EFI_NOT_STARTED */
	EALREADY,	/* EFI_ALREADY_STARTED */
	ECANCELED,	/* EFI_ABORTED */
	EPROTO,		/* EFI_ICMP_ERROR */
	EPROTO,		/* EFI_TFTP_ERROR */
	EPROTO,		/* EFI_PROTOCOL_ERROR */
	EFTYPE,		/* EFI_INCOMPATIBLE_VERSION */
	EPERM,          /* EFI_SECURITY_VIOLATION */
	EIO,		/* EFI_CRC_ERROR */
	EIO,		/* EFI_END_OF_MEDIA */
	EILSEQ,		/* - */
	EILSEQ,		/* - */
	EIO,		/* EFI_END_OF_FILE */
	EINVAL,		/* EFI_INVALID_LANGUAGE */
	EIO,		/* EFI_COMPROMISED_DATA */
	EADDRINUSE,	/* EFI_IP_ADDRESS_CONFLICT */
	EPROTO,		/* EFI_HTTP_ERROR */
};

void	efi_enter(struct efi_softc *);
void	efi_leave(struct efi_softc *);
int	efi_gettime(struct todr_chip_handle *, struct timeval *);
int	efi_settime(struct todr_chip_handle *, struct timeval *);
void	efi_reset(void);
void	efi_powerdown(void);

int efiopen(dev_t, int, int, struct proc *);
int eficlose(dev_t, int, int, struct proc *);
int efiioctl(dev_t, u_long, caddr_t, int, struct proc *);
int efiioc_get_table(dev_t, u_long, caddr_t, int, struct proc *);
int efiioc_var_get(dev_t, u_long, caddr_t, int, struct proc *);
int efiioc_var_next(dev_t, u_long, caddr_t, int, struct proc *);
int efiioc_var_set(dev_t, u_long, caddr_t, int, struct proc *);
int efi_adapt_error(EFI_STATUS);

int
efi_match(struct device *parent, void *match, void *aux)
{
	struct bios_attach_args	*ba = aux;
	struct cfdata *cf = match;

	if (strcmp(ba->ba_name, cf->cf_driver->cd_name) == 0 &&
	    bios_efiinfo->system_table != 0)
		return 1;

	return 0;
}

void
efi_attach(struct device *parent, struct device *self, void *aux)
{
	struct efi_softc *sc = (struct efi_softc *)self;
	struct bios_attach_args *ba = aux;
	uint64_t system_table;
	bus_space_handle_t memh;
	EFI_SYSTEM_TABLE *st;
	EFI_RUNTIME_SERVICES *rs;
	uint32_t mmap_size = bios_efiinfo->mmap_size;
	uint32_t mmap_desc_size = bios_efiinfo->mmap_desc_size;
	uint32_t mmap_desc_ver = bios_efiinfo->mmap_desc_ver;
	EFI_MEMORY_DESCRIPTOR *desc;
	EFI_TIME time;
	EFI_STATUS status;
	uint16_t major, minor;
	int i;

	if (mmap_desc_ver != EFI_MEMORY_DESCRIPTOR_VERSION) {
		printf(": unsupported memory descriptor version %d\n",
		    mmap_desc_ver);
		return;
	}

	system_table = bios_efiinfo->system_table;
	KASSERT(system_table);

	if (bus_space_map(ba->ba_memt, system_table, sizeof(EFI_SYSTEM_TABLE),
	    BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_PREFETCHABLE, &memh)) {
		printf(": can't map system table\n");
		return;
	}

	st = bus_space_vaddr(ba->ba_memt, memh);
	rs = st->RuntimeServices;

	major = st->Hdr.Revision >> 16;
	minor = st->Hdr.Revision & 0xffff;
	printf(": UEFI %d.%d", major, minor / 10);
	if (minor % 10)
		printf(".%d", minor % 10);
	printf("\n");

	if ((bios_efiinfo->flags & BEI_64BIT) == 0)
		return;

	/*
	 * We don't really want some random executable non-OpenBSD
	 * code lying around in kernel space.  So create a separate
	 * pmap and only activate it when we call runtime services.
	 */
	sc->sc_pm = pmap_create();

	desc = mmap;
	for (i = 0; i < mmap_size / mmap_desc_size; i++) {
		/* Need to map EfiACPIMemoryNVS as at least OVMF accesses it to
		 * check for trusted domain. */
		if ((desc->Attribute & EFI_MEMORY_RUNTIME) ||
		    desc->Type == EfiACPIMemoryNVS) {
			vaddr_t va = desc->PhysicalStart;
			paddr_t pa = desc->PhysicalStart;
			int npages = desc->NumberOfPages;
			vm_prot_t prot = PROT_READ | PROT_WRITE;

#define EFI_DEBUG
#ifdef EFI_DEBUG
			printf("type 0x%x pa 0x%llx va 0x%llx pages 0x%llx attr 0x%llx\n",
			    desc->Type, desc->PhysicalStart,
			    desc->VirtualStart, desc->NumberOfPages,
			    desc->Attribute);
#endif

			/*
			 * Normal memory is expected to be "write
			 * back" cacheable.  Everything else is mapped
			 * as device memory.
			 */
			if ((desc->Attribute & EFI_MEMORY_WB) == 0)
				pa |= PMAP_NOCACHE;

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
				pmap_enter(sc->sc_pm, va, pa, prot,
				   prot | PMAP_WIRED | PMAP_EFI);
				va += PAGE_SIZE;
				pa += PAGE_SIZE;
			}
		}
		desc = NextMemoryDescriptor(desc, mmap_desc_size);
	}

	/*
	 * The FirmwareVendor and ConfigurationTable fields have been
	 * converted from a physical pointer to a virtual pointer, so
	 * we have to activate our pmap to access them.
	 */
	efi_enter(sc);
	if (st->FirmwareVendor) {
		printf("%s: ", sc->sc_dev.dv_xname);
		for (i = 0; st->FirmwareVendor[i]; i++)
			printf("%c", st->FirmwareVendor[i]);
		printf(" rev 0x%x\n", st->FirmwareRevision);
	}
	efi_leave(sc);

	sc->esrt = (EFI_SYSTEM_RESOURCE_TABLE *)bios_efiinfo->config_esrt;

	if (rs == NULL)
		return;
	sc->sc_rs = rs;

	cpuresetfn = efi_reset;
	powerdownfn = efi_powerdown;

	efi_enter(sc);
	status = rs->GetTime(&time, NULL);
	efi_leave(sc);
	if (status != EFI_SUCCESS)
		return;

	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = efi_gettime;
	sc->sc_todr.todr_settime = efi_settime;
	todr_handle = &sc->sc_todr;
}

void
efi_enter(struct efi_softc *sc)
{
	sc->sc_psw = intr_disable();
	sc->sc_cr3 = rcr3() | cr3_reuse_pcid;
	lcr3(sc->sc_pm->pm_pdirpa | (pmap_use_pcid ? PCID_EFI : 0));

	fpu_kernel_enter();
}

void
efi_leave(struct efi_softc *sc)
{
	fpu_kernel_exit();

	lcr3(sc->sc_cr3);
	intr_restore(sc->sc_psw);
}

int
efi_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct efi_softc *sc = handle->cookie;
	struct clock_ymdhms dt;
	EFI_TIME time;
	EFI_STATUS status;

	efi_enter(sc);
	status = sc->sc_rs->GetTime(&time, NULL);
	efi_leave(sc);
	if (status != EFI_SUCCESS)
		return EIO;

	dt.dt_year = time.Year;
	dt.dt_mon = time.Month;
	dt.dt_day = time.Day;
	dt.dt_hour = time.Hour;
	dt.dt_min = time.Minute;
	dt.dt_sec = time.Second;

	if (dt.dt_sec > 59 || dt.dt_min > 59 || dt.dt_hour > 23 ||
	    dt.dt_day > 31 || dt.dt_day == 0 ||
	    dt.dt_mon > 12 || dt.dt_mon == 0 ||
	    dt.dt_year < POSIX_BASE_YEAR)
		return EINVAL;

	tv->tv_sec = clock_ymdhms_to_secs(&dt);
	tv->tv_usec = 0;
	return 0;
}

int
efi_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct efi_softc *sc = handle->cookie;
	struct clock_ymdhms dt;
	EFI_TIME time;
	EFI_STATUS status;

	clock_secs_to_ymdhms(tv->tv_sec, &dt);

	time.Year = dt.dt_year;
	time.Month = dt.dt_mon;
	time.Day = dt.dt_day;
	time.Hour = dt.dt_hour;
	time.Minute = dt.dt_min;
	time.Second = dt.dt_sec;
	time.Nanosecond = 0;
	time.TimeZone = 0;
	time.Daylight = 0;

	efi_enter(sc);
	status = sc->sc_rs->SetTime(&time);
	efi_leave(sc);
	if (status != EFI_SUCCESS)
		return EIO;
	return 0;
}

void
efi_reset(void)
{
	struct efi_softc *sc = efi_cd.cd_devs[0];

	printf("%s\n", __func__);
	efi_enter(sc);
	sc->sc_rs->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
	efi_leave(sc);
}

void
efi_powerdown(void)
{
	struct efi_softc *sc = efi_cd.cd_devs[0];

	printf("%s\n", __func__);
	efi_enter(sc);
	sc->sc_rs->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
	efi_leave(sc);
}

int
efiopen(dev_t dev, int flag, int mode, struct proc *p)
{
	return (efi_cd.cd_ndevs > 0 ? 0 : ENXIO);
}

int
eficlose(dev_t dev, int flag, int mode, struct proc *p)
{
	return (0);
}

int
efiioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	int status;

	switch (cmd) {
	case EFIIOC_GET_TABLE:
		status = efiioc_get_table(dev, cmd, data, flag, p);
		break;
	case EFIIOC_VAR_GET:
		status = efiioc_var_get(dev, cmd, data, flag, p);
		break;
	case EFIIOC_VAR_NEXT:
		status = efiioc_var_next(dev, cmd, data, flag, p);
		break;
	case EFIIOC_VAR_SET:
		status = efiioc_var_set(dev, cmd, data, flag, p);
		break;
	default:
		status = ENOTTY;
		break;
	}

	return (status);
}

int
efiioc_get_table(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct uuid esrt_guid = EFI_TABLE_ESRT;
	struct efi_softc *sc = efi_cd.cd_devs[0];
	struct efi_get_table_ioc *ioc = (struct efi_get_table_ioc *)data;
	char *buf;
	int status;

	/* Only ESRT is supported at the moment. */
	if (bcmp(&ioc->uuid, &esrt_guid, sizeof(ioc->uuid)) != 0)
		return (EINVAL);

	/* ESRT might not be present. */
	if (sc->esrt == NULL)
		return (ENXIO);

	efi_enter(sc);

	ioc->table_len = sizeof(*sc->esrt) +
	    sizeof(EFI_SYSTEM_RESOURCE_ENTRY) * sc->esrt->FwResourceCount;

	/* Return table length to userspace. */
	if (ioc->buf == NULL) {
		efi_leave(sc);
		return 0;
	}

	/* Refuse to copy only part of the table. */
	if (ioc->buf_len < ioc->table_len) {
		efi_leave(sc);
		return EINVAL;
	}

	buf = malloc(ioc->table_len, M_TEMP, M_WAITOK);
	memcpy(buf, sc->esrt, ioc->table_len);

	efi_leave(sc);

	status = copyout(buf, ioc->buf, ioc->table_len);
	free(buf, M_TEMP, ioc->table_len);

	return status;
}

int
efiioc_var_get(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct efi_softc *sc = efi_cd.cd_devs[0];
	struct efi_var_ioc *ioc = (struct efi_var_ioc *)data;

	void *value = NULL;
	efi_char *name = NULL;
	size_t valuesize = ioc->datasize;
	int status;

	if (valuesize > 0) {
		value = malloc(valuesize, M_TEMP, M_WAITOK);
		if (value == NULL) {
			status = ENOMEM;
			goto leave;
		}
	}

	name = malloc(ioc->namesize, M_TEMP, M_WAITOK);
	if (name == NULL) {
		status = ENOMEM;
		goto leave;
	}

	status = copyin(ioc->name, name, ioc->namesize);
	if (status != 0)
		goto leave;

	/* NULL-terminated name must fit into namesize bytes. */
	if (name[ioc->namesize / sizeof(*name) - 1] != 0) {
		status = EINVAL;
		goto leave;
	}

	efi_enter(sc);
	status = efi_adapt_error(sc->sc_rs->GetVariable(name,
	    (EFI_GUID *)&ioc->vendor, &ioc->attrib, &ioc->datasize, value));
	efi_leave(sc);

	if (status == 0) {
		status = copyout(value, ioc->data, ioc->datasize);
	} else if (status == EOVERFLOW) {
		/*
		 * Return size of the value, which was set by EFI RT, reporting
		 * no error to match FreeBSD's behaviour.
		 */
		ioc->data = NULL;
		status = 0;
	}

leave:
	free(value, M_TEMP, valuesize);
	free(name, M_TEMP, ioc->namesize);
	return status;
}

int
efiioc_var_next(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct efi_softc *sc = efi_cd.cd_devs[0];
	struct efi_var_ioc *ioc = (struct efi_var_ioc *)data;

	efi_char *name;
	int status;
	size_t namesize = ioc->namesize;

	name = malloc(namesize, M_TEMP, M_WAITOK);
	if (name == NULL) {
		status = ENOMEM;
		goto leave;
	}

	status = copyin(ioc->name, name, namesize);
	if (status)
		goto leave;

	efi_enter(sc);
	status = efi_adapt_error(sc->sc_rs->GetNextVariableName(&ioc->namesize,
	    name, (EFI_GUID *)&ioc->vendor));
	efi_leave(sc);

	if (status == 0) {
		status = copyout(name, ioc->name, ioc->namesize);
	} else if (status == EOVERFLOW) {
		/*
		 * Return size of the name, which was set by EFI RT, reporting
		 * no error to match FreeBSD's behaviour.
		 */
		ioc->name = NULL;
		status = 0;
	}

leave:
	free(name, M_TEMP, namesize);
	return status;
}

int
efiioc_var_set(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct efi_softc *sc = efi_cd.cd_devs[0];
	struct efi_var_ioc *ioc = (struct efi_var_ioc *)data;

	void *value = NULL;
	efi_char *name = NULL;
	int status;

	/* Zero datasize means variable deletion. */
	if (ioc->datasize > 0) {
		value = malloc(ioc->datasize, M_TEMP, M_WAITOK);
		if (value == NULL) {
			status = ENOMEM;
			goto leave;
		}

		status = copyin(ioc->data, value, ioc->datasize);
		if (status)
			goto leave;
	}

	name = malloc(ioc->namesize, M_TEMP, M_WAITOK);
	if (name == NULL) {
		status = ENOMEM;
		goto leave;
	}

	status = copyin(ioc->name, name, ioc->namesize);
	if (status)
		goto leave;

	/* NULL-terminated name must fit into namesize bytes. */
	if (name[ioc->namesize / sizeof(*name) - 1] != 0) {
		status = EINVAL;
		goto leave;
	}

	if (securelevel > 0) {
		status = EPERM;
		goto leave;
	}

	efi_enter(sc);
	status = efi_adapt_error(sc->sc_rs->SetVariable(name,
	    (EFI_GUID *)&ioc->vendor, ioc->attrib, ioc->datasize, value));
	efi_leave(sc);

leave:
	free(value, M_TEMP, ioc->datasize);
	free(name, M_TEMP, ioc->namesize);
	return status;
}

int
efi_adapt_error(EFI_STATUS status)
{
	u_long code = status & 0x3ffffffffffffffful;
	return (code < nitems(efi_status2err) ? efi_status2err[code] : EILSEQ);
}
