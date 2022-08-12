#ifndef _MACHINE_EFIVAR_H_
#define _MACHINE_EFIVAR_H_

struct efi_attach_args {
	const char		*eaa_name;
};

__BEGIN_DECLS

int	efi_get_esrt(void **, unsigned int *);

__END_DECLS

#endif /* _MACHINE_EFIVAR_H_ */
