/* host_shim/elf.h -- the ELF types so_util.h names (sizes only matter). */
#ifndef ABS_HOST_ELF_H
#define ABS_HOST_ELF_H
#include <stdint.h>
typedef struct { unsigned char e_ident[16]; uint16_t e_type, e_machine; uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags; uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx; } Elf32_Ehdr;
typedef struct { uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align; } Elf32_Phdr;
typedef struct { uint32_t sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize; } Elf32_Shdr;
typedef struct { uint32_t st_name, st_value, st_size; unsigned char st_info, st_other; uint16_t st_shndx; } Elf32_Sym;
typedef struct { int32_t d_tag; union { uint32_t d_val, d_ptr; } d_un; } Elf32_Dyn;
typedef struct { uint32_t r_offset, r_info; } Elf32_Rel;
typedef struct { uint32_t r_offset, r_info; int32_t r_addend; } Elf32_Rela;
#endif
