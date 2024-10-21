/*
 * Copyright (c) 2015, Intel Corporation
 * All rights reserved.
 *
 * Authors: Jeremy Compostella <jeremy.compostella@intel.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <lib.h>
#include <slot.h>

#include "acpi.h"
#ifndef __LP64__
#include "pae.h"
#endif
#include "reader.h"
#include "sparse_format.h"

/* Memory dump shared functions.  These functions do not make any
   dynamic memory allocation to avoid RAM corruption during the
   dump.  */
#define MAX_MEMORY_REGION_NB 256

typedef struct memory_priv {
	BOOLEAN is_in_used;

	/* Memory map */
	UINT8 memmap[MAX_MEMORY_REGION_NB * sizeof(EFI_MEMORY_DESCRIPTOR)];
	UINTN nr_descr;
	UINTN descr_sz;

	/* Boundaries */
	EFI_PHYSICAL_ADDRESS start;
	EFI_PHYSICAL_ADDRESS end;

	/* Current memory region */
	EFI_PHYSICAL_ADDRESS cur;
	EFI_PHYSICAL_ADDRESS cur_end;
} memory_t;

static EFI_STATUS get_sorted_memory_map(memory_t *mem)
{
	EFI_STATUS ret;
	UINT32 descr_ver;
	UINTN key, memmap_sz;

	memmap_sz = sizeof(mem->memmap);
	ret = uefi_call_wrapper(BS->GetMemoryMap, 5, &memmap_sz,
				(EFI_MEMORY_DESCRIPTOR *)mem->memmap,
				&key, &mem->descr_sz, &descr_ver);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to get the current memory map");
		return ret;
	}

	mem->nr_descr = memmap_sz / mem->descr_sz;
	sort_memory_map(mem->memmap, mem->nr_descr, mem->descr_sz);

	return EFI_SUCCESS;
}

static EFI_STATUS memory_open(reader_ctx_t *ctx, memory_t *mem,
			      EFI_STATUS (*init)(reader_ctx_t *, void *),
			      UINTN argc, char **argv)
{
	EFI_STATUS ret = EFI_SUCCESS;
	char *endptr;
	UINT64 length;

	if (argc > 2)
		return EFI_INVALID_PARAMETER;

	if (mem->is_in_used)
		return EFI_ALREADY_STARTED;

	mem->is_in_used = TRUE;
	ctx->private = mem;

	/* Parse argv  */
	if (argc > 0) {
		mem->start = strtoull(argv[0], &endptr, 16);
		if (*endptr != '\0')
			goto err;
	} else
		mem->start = 0;

	if (argc == 2) {
		length = strtoull(argv[1], &endptr, 16);
		if (*endptr != '\0')
			goto err;
		mem->end = mem->start + length;
	} else
		mem->end = 0;

	if (mem->start % EFI_PAGE_SIZE || mem->end % EFI_PAGE_SIZE) {
		error(L"Boundaries must be multiple of %d bytes", EFI_PAGE_SIZE);
		goto err;
	}

	ret = get_sorted_memory_map(mem);
	if (EFI_ERROR(ret))
		return ret;

	ret = init(ctx, mem);
	if (EFI_ERROR(ret))
		goto err;

#ifndef __LP64__
	ret = pae_init(mem->memmap, mem->nr_descr, mem->descr_sz);
	if (EFI_ERROR(ret))
		goto err;
#endif

	return EFI_SUCCESS;

err:
	mem->is_in_used = FALSE;
	return EFI_ERROR(ret) ? ret : EFI_INVALID_PARAMETER;
}

static EFI_STATUS memory_read_current(memory_t *mem, unsigned char **buf, UINT64 *len)
{
#ifndef __LP64__
	EFI_STATUS ret;
#endif

	*len = min(*len, mem->cur_end - mem->cur);
#ifdef __LP64__
	*buf = (unsigned char *)mem->cur;
#else
	ret = pae_map(mem->cur, buf, len);
	if (EFI_ERROR(ret))
		return ret;
#endif
	mem->cur += *len;

	return EFI_SUCCESS;
}

static void memory_close(reader_ctx_t *ctx)
{
	((memory_t *)ctx->private)->is_in_used = FALSE;
#ifndef __LP64__
	pae_exit();
#endif
}

/* RAM reader */
#define SIZEOF_TOTALSZ		sizeof(((chunk_header_t *)0)->total_sz)
#define MAX_CHUNK_SIZE		(((UINT64)1 << (SIZEOF_TOTALSZ * 8)) - EFI_PAGE_SIZE)

static struct ram_priv {
	memory_t m;

	/* Sparse format */
	UINTN chunk_nb;
	UINTN cur_chunk;
	struct sparse_header sheader;
	struct chunk_header chunks[MAX_MEMORY_REGION_NB];
} ram_priv = {
	.sheader = {
		.magic = SPARSE_HEADER_MAGIC,
		.major_version = 0x1,
		.minor_version = 0,
		.file_hdr_sz = sizeof(struct sparse_header),
		.chunk_hdr_sz = sizeof(struct chunk_header),
		.blk_sz = EFI_PAGE_SIZE
	}
};

static EFI_STATUS ram_add_chunk(reader_ctx_t *ctx, struct ram_priv *priv, UINT16 type, UINT64 size)
{
	EFI_STATUS ret = EFI_SUCCESS;
	struct chunk_header *cur = NULL;

	if (size % EFI_PAGE_SIZE) {
		error(L"chunk size must be multiple of %d bytes", EFI_PAGE_SIZE);
		return EFI_INVALID_PARAMETER;
	}

	if (type == CHUNK_TYPE_RAW) {
		while ((UINT32)(size + sizeof(*cur)) <= size) {
			/* Overflow detected in UINT32 total_sz field */
			ret = ram_add_chunk(ctx, priv, type, MAX_CHUNK_SIZE);
			if (EFI_ERROR(ret))
				return ret;
			size -= MAX_CHUNK_SIZE;
		}
	}

	if (priv->chunk_nb == MAX_MEMORY_REGION_NB) {
		error(L"Failed to allocate a new chunk");
		return EFI_OUT_OF_RESOURCES;
	}

	cur = &priv->chunks[priv->chunk_nb++];

	cur->chunk_type = type;
	cur->chunk_sz = size / EFI_PAGE_SIZE;
	cur->total_sz = sizeof(*cur);
	ctx->len += sizeof(*cur);
	if (type == CHUNK_TYPE_RAW) {
		cur->total_sz += size;
		ctx->len += size;
	}

	priv->sheader.total_chunks++;
	priv->sheader.total_blks += cur->chunk_sz;

	return EFI_SUCCESS;
}

static EFI_STATUS ram_build_chunks(reader_ctx_t *ctx, void *priv_p)
{
	struct ram_priv *priv = priv_p;
	EFI_STATUS ret = EFI_SUCCESS;
	UINT16 type;
	UINTN i;
	EFI_MEMORY_DESCRIPTOR *entry;
	UINT64 entry_len, length;
	EFI_PHYSICAL_ADDRESS entry_end, prev_end;
	UINT8 *entries = priv->m.memmap;

	priv->sheader.total_chunks = priv->sheader.total_blks = 0;
	priv->chunk_nb = priv->cur_chunk = 0;
	prev_end = ctx->cur = ctx->len = 0;

	for (i = 0; i < priv->m.nr_descr; entries += priv->m.descr_sz, i++) {
		entry = (EFI_MEMORY_DESCRIPTOR *)entries;
		entry_len = entry->NumberOfPages * EFI_PAGE_SIZE;
		entry_end = entry->PhysicalStart + entry_len;

		if (priv->m.start >= entry_end)
			goto next;

		/* Memory hole between two memory regions */
		if (prev_end != entry->PhysicalStart) {
			if (prev_end > entry->PhysicalStart) {
				error(L"overlap detected, aborting");
				goto err;
			}

			length = entry->PhysicalStart - prev_end;

			if (priv->m.start > prev_end && priv->m.start < entry->PhysicalStart)
				length -= priv->m.start - prev_end;

			if (priv->m.end && entry->PhysicalStart > priv->m.end)
				length -= entry->PhysicalStart - priv->m.end;

			ret = ram_add_chunk(ctx, priv, CHUNK_TYPE_DONT_CARE, length);
			if (EFI_ERROR(ret))
				goto err;

			if (priv->m.end && priv->m.end < entry->PhysicalStart)
				break;
		}

		length = entry_len;
		if (priv->m.start > entry->PhysicalStart && priv->m.start < entry_end)
			length -= priv->m.start - entry->PhysicalStart;

		if (priv->m.end && priv->m.end < entry_end)
			length -= entry_end - priv->m.end;

		type = entry->Type == EfiConventionalMemory ? CHUNK_TYPE_RAW : CHUNK_TYPE_DONT_CARE;
		ret = ram_add_chunk(ctx, priv, type, length);
		if (EFI_ERROR(ret))
			goto err;

		if (priv->m.end && priv->m.end <= entry_end)
			break;

next:
		prev_end = entry_end;
	}

	if (priv->m.end && i == priv->m.nr_descr) {
		error(L"End boundary is in unreachable memory region (>= 0x%lx)",
		      prev_end);
		return EFI_INVALID_PARAMETER;
	}

	if (!ctx->len) {
		error(L"Start boundary is in unreachable memory region");
		return EFI_INVALID_PARAMETER;
	}

	if (!priv->m.end)
		priv->m.end = prev_end;

	ctx->len += sizeof(priv->sheader);
	return EFI_SUCCESS;

err:
	return EFI_ERROR(ret) ? ret : EFI_INVALID_PARAMETER;
}

static EFI_STATUS ram_open(reader_ctx_t *ctx, UINTN argc, char **argv)
{
	return memory_open(ctx, &ram_priv.m, ram_build_chunks, argc, argv);
}

static EFI_STATUS ram_read(reader_ctx_t *ctx, unsigned char **buf, UINT64 *len)
{
	struct ram_priv *priv;
	struct chunk_header *chunk;

	priv = (struct ram_priv *)ctx->private;

	/* First byte, send the sparse header */
	if (ctx->cur == 0) {
		if (*len < sizeof(priv->sheader))
			return EFI_INVALID_PARAMETER;

		*buf = (unsigned char *)&priv->sheader;
		*len = sizeof(priv->sheader);
		priv->m.cur = priv->m.cur_end = priv->m.start;
		return EFI_SUCCESS;
	}

	/* Start new chunk */
	if (priv->m.cur == priv->m.cur_end) {
		if (priv->cur_chunk == priv->chunk_nb || *len < sizeof(*priv->chunks)) {
			error(L"Invalid parameter in %a", __func__);
			return EFI_INVALID_PARAMETER;
		}

		if (priv->cur_chunk >= (MAX_MEMORY_REGION_NB-1))
			return EFI_INVALID_PARAMETER;

		chunk = &priv->chunks[priv->cur_chunk++];
		*buf = (unsigned char *)chunk;
		*len = sizeof(*chunk);
		priv->m.cur_end = priv->m.cur + chunk->chunk_sz * EFI_PAGE_SIZE;
		if (chunk->chunk_type != CHUNK_TYPE_RAW)
			priv->m.cur = priv->m.cur_end;
		return EFI_SUCCESS;
	}

	/* Continue to send the current memory region */
	return memory_read_current(&priv->m, buf, len);
}

/* VMCore reader */
#pragma pack(1)
enum elf_ident {
	EI_MAG0,		/* File identification */
	EI_MAG1,
	EI_MAG2,
	EI_MAG3,
	EI_CLASS,		/* File class */
	EI_DATA,		/* Data encoding */
	EI_VERSION,		/* File version */
	EI_OSABI,		/* OS/ABI identification */
	EI_ABIVERSION,		/* ABI version */
	EI_PAD,			/* Start of padding bytes */
	EI_NIDENT = 16		/* Size of ident[] */
};

typedef struct elf64_hdr {
	unsigned char ident[EI_NIDENT];	/* ELF identification */
	UINT16 type;		  	/* Object file type */
	UINT16 machine;	  		/* Machine type */
	UINT32 version;	  		/* Object file version */
	EFI_PHYSICAL_ADDRESS entry;	/* Entry point address */
	UINT64 phoff;		  	/* Program header offset */
	UINT64 shoff;		  	/* Section header offset */
	UINT32 flags;		  	/* Processor-specific flags */
	UINT16 ehsize;		  	/* ELF header size */
	UINT16 phentsize;	  	/* Size of program header entry */
	UINT16 phnum;		  	/* Number of program header entries */
	UINT16 shentsize;	  	/* Size of section header entry */
	UINT16 shnum;		  	/* Number of section header entries */
	UINT16 shstrndx;	  	/* Section name string table index */
} elf64_hdr_t;

enum ident_ei_class {
	ELFCLASS32 = 1,		/* 32-bit objects */
	ELFCLASS64		/* 64-bit objects */
};

enum ident_ei_data {
	ELFDATA2LSB = 1,	/* Object file data structures are
				   little-endian */
	ELFDATA2MSB = 2		/* Object-file data structures are
				   big-endian*/
};

enum elf_type {
	ET_NONE,		/* No file type */
	ET_REL,			/* Relocatable object file */
	ET_EXEC,		/* Executable file */
	ET_DYN,			/* Shared object file */
	ET_CORE,		/* Core file */
	ET_LOOS	  = 0xfe00,	/* Environment-specific use */
	ET_HIOS	  = 0xfeff,
	ET_LOPROC = 0xff00,	/* Processor-specific use */
	ET_HIPROC = 0xffff
};

enum elf_machine {
	EM_NONE,		/* No machine */
	EM_X86_64 = 62		/* AMD x86-64 architecture */
};

typedef struct elf64_phdr
{
	UINT32 type;			/* Type of segment */
	UINT32 flags;			/* Segment attributes */
	UINT64 offset;			/* Offset in file */
	EFI_PHYSICAL_ADDRESS vaddr;	/* Virtual address in memory */
	EFI_PHYSICAL_ADDRESS paddr;	/* Reserved */
	UINT64 filesz;			/* Size of segment in file */
	UINT64 memsz;			/* Size of segment in memory */
	UINT64 align;			/* Alignment of segment */
} elf64_phdr_t;

enum elfp_type {
	PT_NULL,		/* Unused entry */
	PT_LOAD, 		/* Loadable segment */
	PT_DYNAMIC,		/* Dynamic linking tables */
	PT_INTERP,		/* Program interpreter path name */
	PT_NOTE			/* Note sections */
};

#define ELF_VERSION		1
#define KERNEL_PAGE_FLAGS	7 /* Executable, writable and readable */
/* The Linux kernel maps all the physical memory from this offset
   (cf. https://www.kernel.org/doc/Documentation/x86/x86_64/mm.txt) */
#define KERNEL_PAGE_OFFSET	0xffff880000000000

static struct vmcore_priv {
	memory_t m;

	/* Current program header */
	INTN cur_phdr;

	/* ELF header and ELF program headers */
	UINTN hdr_sz;
	elf64_hdr_t hdr;
	elf64_phdr_t phdr[MAX_MEMORY_REGION_NB];
} vmcore_priv = {
	.hdr = {
		.ident = {
			[EI_MAG0] = 0x7f,
			[EI_MAG1] = 'E',
			[EI_MAG2] = 'L',
			[EI_MAG3] = 'F',
			[EI_CLASS] = ELFCLASS64,
			[EI_DATA] = ELFDATA2LSB,
			[EI_VERSION] = ELF_VERSION
		},
		.type = ET_CORE,
		.machine = EM_X86_64,
		.version = ELF_VERSION,
		.phoff = sizeof(elf64_hdr_t),
		.ehsize = sizeof(elf64_hdr_t),
		.phentsize = sizeof(elf64_phdr_t)
	},
	.phdr = {
		[0] = { .type = PT_NOTE } /* First program header is
					     reserved to notes */
	}
};
#pragma pack()

static EFI_STATUS vmcore_build_header(reader_ctx_t *ctx, void *priv_p)

{
	struct vmcore_priv *priv = priv_p;
	UINTN i;
	EFI_MEMORY_DESCRIPTOR *entry;
	elf64_phdr_t *phdr;
	UINT8 *entries = priv->m.memmap;
	EFI_PHYSICAL_ADDRESS start, end;
	UINT64 length;

	ctx->cur = 0;
	priv->hdr_sz = sizeof(priv->hdr) + sizeof(priv->phdr[0]);
	priv->hdr.phnum = 1;

	for (i = 0; i < priv->m.nr_descr; entries += priv->m.descr_sz, i++) {
		entry = (EFI_MEMORY_DESCRIPTOR *)entries;
		if (entry->Type != EfiConventionalMemory)
			continue;

		start = entry->PhysicalStart;
		length = entry->NumberOfPages * EFI_PAGE_SIZE;
		end = start + length;

		if (end <= priv->m.start)
			continue;

		if (start < priv->m.start) {
			length -= priv->m.start - start;
			start = priv->m.start;
		}

		if (priv->m.end && end > priv->m.end) {
			length -= end - priv->m.end;
		}

		priv->hdr.phnum++;
		if (priv->hdr.phnum == ARRAY_SIZE(priv->phdr)) {
			error(L"Not enough program headers");
			return EFI_OUT_OF_RESOURCES;
		}

		phdr = &priv->phdr[priv->hdr.phnum - 1];
		phdr->type = PT_LOAD;
		phdr->paddr = start;
		phdr->vaddr = KERNEL_PAGE_OFFSET + start;
		phdr->filesz = phdr->memsz = length;
		phdr->flags = KERNEL_PAGE_FLAGS;

		priv->hdr_sz += sizeof(*phdr);
	}

	if (priv->hdr.phnum == 1) {
		error(L"No memory region to dump found");
		return EFI_INVALID_PARAMETER;
	}

	ctx->len = priv->hdr_sz;
	for (i = 1; i < priv->hdr.phnum; i++) {
		phdr = &priv->phdr[i];
		phdr->offset = ctx->len;
		ctx->len += phdr->memsz;
	}

	return EFI_SUCCESS;
}

static EFI_STATUS vmcore_open(reader_ctx_t *ctx, UINTN argc, char **argv)
{
	return memory_open(ctx, &vmcore_priv.m, vmcore_build_header, argc, argv);
}

static EFI_STATUS vmcore_read(reader_ctx_t *ctx, unsigned char **buf, UINT64 *len)
{
	struct vmcore_priv *priv = ctx->private;

	/* First byte, send the ELF headers */
	if (ctx->cur == 0) {
		if (*len < priv->hdr_sz)
			return EFI_INVALID_PARAMETER;

		*buf = (unsigned char *)&priv->hdr;
		*len = priv->hdr_sz;

		priv->m.cur = priv->m.cur_end = 0;
		priv->cur_phdr = 0;
		return EFI_SUCCESS;
	}

	/* Start new memory region */
	if (priv->m.cur == priv->m.cur_end) {
		if (priv->cur_phdr >= priv->hdr.phnum - 1) {
			error(L"Invalid parameter in %a", __func__);
			return EFI_INVALID_PARAMETER;
		}

		priv->cur_phdr++;
		priv->m.cur = priv->phdr[priv->cur_phdr].paddr;
		priv->m.cur_end = priv->m.cur + priv->phdr[priv->cur_phdr].memsz;
	}

	/* Continue to send the current memory region */
	return memory_read_current(&priv->m, buf, len);
}

/* Partition reader */
#define PART_READER_BUF_SIZE (10 * 1024 * 1024)

struct part_priv {
	struct gpt_partition_interface gparti;
	BOOLEAN need_more_data;
	unsigned char buf[PART_READER_BUF_SIZE];
	UINTN buf_cur;
	UINTN buf_len;
	UINT64 offset;
};

static EFI_STATUS _part_open(reader_ctx_t *ctx, UINTN argc, char **argv, logical_unit_t log_unit)
{
	EFI_STATUS ret = EFI_SUCCESS;
	struct gpt_partition_interface *gparti;
	struct part_priv *priv;
	CHAR16 *partname;
	UINT64 length;

	if (argc < 1 || argc > 3)
		return EFI_INVALID_PARAMETER;

	priv = ctx->private = AllocatePool(sizeof(*priv));
	if (!priv)
		return EFI_OUT_OF_RESOURCES;


	partname = stra_to_str((CHAR8 *)argv[0]);
	if (!partname) {
		error(L"Failed to convert partition name to CHAR16");
		goto err;
	}

	gparti = &priv->gparti;
	ret = gpt_get_partition_by_label(slot_label(partname), gparti, log_unit);
	FreePool(partname);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Cannot access partition '%a'", argv[0]);
		goto err;
	}

	priv->offset = gparti->part.starting_lba * gparti->bio->Media->BlockSize;
	length = (gparti->part.ending_lba + 1 - gparti->part.starting_lba) *
		gparti->bio->Media->BlockSize;

	ctx->cur = 0;
	ctx->len = length;

	if (argc > 1) {
		ctx->cur = strtoull(argv[1], NULL, 16);
		if (ctx->cur >= length)
			goto err;
	}

	if (argc == 3) {
		ctx->len = strtoull(argv[2], NULL, 16);
		if (ctx->len == 0 || ctx->len > length || ctx->cur >= length - ctx->len)
			goto err;
	}

	priv->buf_cur = 0;
	priv->buf_len = 0;
	priv->need_more_data = TRUE;

	return EFI_SUCCESS;

err:
	FreePool(priv);
	return EFI_ERROR(ret) ? ret : EFI_INVALID_PARAMETER;
}

static EFI_STATUS part_open(reader_ctx_t *ctx, UINTN argc, char **argv)
{
	return _part_open(ctx, argc, argv, LOGICAL_UNIT_USER);
}

static EFI_STATUS factory_part_open(reader_ctx_t *ctx, UINTN argc, char **argv)
{
	return _part_open(ctx, argc, argv, LOGICAL_UNIT_FACTORY);
}

static EFI_STATUS part_read(reader_ctx_t *ctx, unsigned char **buf, UINT64 *len)
{
	EFI_STATUS ret;
	struct part_priv *priv = ctx->private;

	if (priv->need_more_data) {
		priv->buf_len = min(sizeof(priv->buf), ctx->len - ctx->cur);
		ret = uefi_call_wrapper(priv->gparti.dio->ReadDisk, 5, priv->gparti.dio,
					priv->gparti.bio->Media->MediaId,
					priv->offset + ctx->cur, priv->buf_len, priv->buf);
		if (EFI_ERROR(ret)) {
			efi_perror(ret, L"Failed to read partition");
			return ret;
		}

		priv->need_more_data = FALSE;
		priv->buf_cur = 0;
	}

	*len = min(*len, priv->buf_len - priv->buf_cur);
	*buf = priv->buf + priv->buf_cur;
	priv->buf_cur += *len;
	if (priv->buf_cur == priv->buf_len)
		priv->need_more_data = TRUE;

	return EFI_SUCCESS;
}

/* ACPI table reader */
static EFI_STATUS acpi_open(reader_ctx_t *ctx, UINTN argc, char **argv)
{
	EFI_STATUS ret;
	struct ACPI_DESC_HEADER *table;

	if (argc != 1)
		return EFI_INVALID_PARAMETER;

	ret = get_acpi_table((CHAR8 *)argv[0], (VOID **)&table);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Cannot access ACPI table %a", argv[0]);
		return ret;
	}

	ctx->private = table;
	ctx->cur = 0;
	ctx->len = table->length;

	return EFI_SUCCESS;
}

/* EFI variable reader */
static EFI_STATUS efivar_find(CHAR16 *varname, EFI_GUID *guid_p)
{
	EFI_STATUS ret;
	UINTN bufsize, namesize;
	CHAR16 *name;
	EFI_GUID guid={0};
	BOOLEAN found = FALSE;
	EFI_GUID found_guid;

	bufsize = 64;		/* Initial size large enough to handle
				   usual variable names length and
				   avoid the ReallocatePool as much as
				   possible.  */
	name = AllocateZeroPool(bufsize);
	if (!name) {
		error(L"Failed to re-allocate variable name buffer");
		return EFI_OUT_OF_RESOURCES;
	}

	for (;;) {
		namesize = bufsize;
		ret = uefi_call_wrapper(RT->GetNextVariableName, 3, &namesize,
					name, &guid);
		if (ret == EFI_NOT_FOUND) {
			ret = EFI_SUCCESS;
			break;
		}
		if (ret == EFI_BUFFER_TOO_SMALL) {
			name = ReallocatePool(name, bufsize, namesize);
			if (!name) {
				error(L"Failed to re-allocate variable name buffer");
				return EFI_OUT_OF_RESOURCES;
			}
			bufsize = namesize;
			continue;
		}
		if (EFI_ERROR(ret)) {
			efi_perror(ret, L"GetNextVariableName failed");
			break;
		}

		if (!StrCmp(name, varname)) {
			if (found) {
				error(L"Found 2 variables named %s", varname);
				ret = EFI_UNSUPPORTED;
				break;
			}
			found = TRUE;
			found_guid = guid;
		}
	}

	FreePool(name);

	if (EFI_ERROR(ret))
		return ret;

	if (!found)
		return EFI_NOT_FOUND;

	*guid_p = found_guid;
	return EFI_SUCCESS;
}

static EFI_STATUS efivar_open(reader_ctx_t *ctx, UINTN argc, char **argv)
{
	EFI_STATUS ret;
	UINT32 flags;
	UINTN size;
	CHAR16 *varname = NULL;
	EFI_GUID guid;

	if (argc != 1 && argc != 2)
		return EFI_INVALID_PARAMETER;

	if (argc == 2) {
		ret = stra_to_guid(argv[1], &guid);
		if (EFI_ERROR(ret))
			return ret;
	}

	varname = stra_to_str((CHAR8 *)argv[0]);
	if (!varname)
		return EFI_OUT_OF_RESOURCES;

	if (argc == 1) {
		ret = efivar_find(varname, &guid);
		if (EFI_ERROR(ret))
			goto exit;
	}

	ret = get_efi_variable(&guid, varname, &size, &ctx->private, &flags);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Cannot access EFI variable %a %g", argv[0], &guid);
		goto exit;
	}

	ctx->cur = 0;
	ctx->len = size;

exit:
	FreePool(varname);
	return ret;
}

/* MBR */
static EFI_STATUS mbr_open(reader_ctx_t *ctx, UINTN argc,
			   __attribute__((__unused__)) char **argv)
{
	struct gpt_partition_interface gparti;
	EFI_STATUS ret;

	if (argc != 0)
		return EFI_INVALID_PARAMETER;

	ret = gpt_get_root_disk(&gparti, LOGICAL_UNIT_USER);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to get disk information");
		return ret;
	}

	ctx->private = AllocatePool(MBR_CODE_SIZE);
	if (!ctx->private) {
		error(L"Failed to allocate MBR buffer");
		return EFI_OUT_OF_RESOURCES;
	}

	ret = uefi_call_wrapper(gparti.dio->ReadDisk, 5, gparti.dio,
				gparti.bio->Media->MediaId,
				0, MBR_CODE_SIZE, ctx->private);
	if (EFI_ERROR(ret)) {
		FreePool(ctx->private);
		efi_perror(ret, L"Failed to read partition");
		return ret;
	}

	ctx->len = MBR_CODE_SIZE;
	ctx->cur = 0;

	return EFI_SUCCESS;
}

/* GPT-HEADER and GPT-FACTORY-HEADER */
static EFI_STATUS _gpt_header_open(reader_ctx_t *ctx, logical_unit_t log_unit)
{
	UINTN size;
	EFI_STATUS ret;

	ret = gpt_get_header((struct gpt_header **)&ctx->private, &size, log_unit);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to get GPT header");
		return ret;
	}

	ctx->len = size;
	ctx->cur = 0;

	return EFI_SUCCESS;
}

static EFI_STATUS gpt_header_open(reader_ctx_t *ctx, UINTN argc,
				  __attribute__((__unused__)) char **argv)
{
	if (argc != 0)
		return EFI_INVALID_PARAMETER;

	return _gpt_header_open(ctx, LOGICAL_UNIT_USER);
}

static EFI_STATUS gpt_factory_header_open(reader_ctx_t *ctx, UINTN argc,
					  __attribute__((__unused__)) char **argv)
{
	if (argc != 0)
		return EFI_INVALID_PARAMETER;

	return _gpt_header_open(ctx, LOGICAL_UNIT_FACTORY);
}

/* GPT-PARTS and GPT-FACTORY-PARTS */
static EFI_STATUS _gpt_parts_open(reader_ctx_t *ctx, logical_unit_t log_unit)
{
	UINTN size;
	EFI_STATUS ret;

	ret = gpt_get_partitions((struct gpt_partition **)&ctx->private,
				 &size, log_unit);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to get GPT partition table");
		return ret;
	}

	ctx->len = size;
	ctx->cur = 0;

	return EFI_SUCCESS;
}

static EFI_STATUS gpt_parts_open(reader_ctx_t *ctx, UINTN argc,
				 __attribute__((__unused__)) char **argv)
{
	if (argc != 0)
		return EFI_INVALID_PARAMETER;

	return _gpt_parts_open(ctx, LOGICAL_UNIT_USER);
}

static EFI_STATUS gpt_factory_parts_open(reader_ctx_t *ctx, UINTN argc,
					 __attribute__((__unused__)) char **argv)
{
	if (argc != 0)
		return EFI_INVALID_PARAMETER;

	return _gpt_parts_open(ctx, LOGICAL_UNIT_FACTORY);
}

/* BERT Region reader */
static const char BERR_MAGIC[4] = "BERR"; /* Boot Error Record Region */

static EFI_STATUS bert_region_open(reader_ctx_t *ctx, UINTN argc,
				   __attribute__((__unused__)) char **argv)
{
	EFI_STATUS ret;
	struct BERT_TABLE *bert_table;

	if (argc != 0)
		return EFI_INVALID_PARAMETER;

	ret = get_acpi_table((CHAR8 *)"BERT", (VOID **)&bert_table);
	if (ret == EFI_NOT_FOUND) {
		debug(L"BERT ACPI table not available");
		return ret;
	}
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Cannot access ACPI table BERT");
		return ret;
	}

	ctx->private = bert_table;
	ctx->cur = 0;
	ctx->len = sizeof(BERR_MAGIC) + bert_table->region_length;

	return EFI_SUCCESS;
}

static EFI_STATUS bert_region_read(reader_ctx_t *ctx, unsigned char **buf, UINT64 *len)
{
	struct BERT_TABLE *bert_table = ctx->private;

	/* First byte, send the BERR magic */
	if (ctx->cur == 0) {
		if (*len < sizeof(BERR_MAGIC))
			return EFI_INVALID_PARAMETER;

		*buf = (unsigned char *)BERR_MAGIC;
		*len = sizeof(BERR_MAGIC);
		return EFI_SUCCESS;
	}

	*len = min(*len, ctx->len - ctx->cur);
	*buf = (unsigned char *)(UINTN)bert_table->region + ctx->cur - sizeof(BERR_MAGIC);

	return EFI_SUCCESS;
}

/* Interface */
static EFI_STATUS read_from_private(reader_ctx_t *ctx, unsigned char **buf,
				    __attribute__((__unused__)) UINT64 *len)
{
	*buf = (unsigned char *)ctx->private + ctx->cur;
	return EFI_SUCCESS;
}

static void free_private(reader_ctx_t *ctx)
{
	FreePool(ctx->private);
}

struct reader {
	const char *name;
	EFI_STATUS (*open)(reader_ctx_t *ctx, UINTN argc, char **argv);
	EFI_STATUS (*read)(reader_ctx_t *ctx, unsigned char **buf, UINT64 *len);
	void (*close)(reader_ctx_t *ctx);
} READERS[] = {
	{ "ram",		ram_open,			ram_read,		memory_close },
	{ "vmcore",		vmcore_open,			vmcore_read,		memory_close },
	{ "acpi",		acpi_open,			read_from_private,	NULL },
	{ "part",		part_open,			part_read,		free_private },
	{ "factory-part",	factory_part_open,		part_read,		free_private },
	{ "efivar",		efivar_open,			read_from_private,	free_private },
	{ "mbr",		mbr_open,			read_from_private,	free_private },
	{ "gpt-header",		gpt_header_open,		read_from_private,	free_private },
	{ "gpt-parts",		gpt_parts_open,			read_from_private,	free_private },
	{ "gpt-factory-header",	gpt_factory_header_open,	read_from_private,	free_private },
	{ "gpt-factory-parts",	gpt_factory_parts_open,		read_from_private,	free_private },
	{ "bert-region",	bert_region_open,		bert_region_read,	NULL }
};

#define MAX_ARGS		8

EFI_STATUS reader_open(reader_ctx_t *ctx, char *args)
{
	EFI_STATUS ret;
	INTN argc;
	UINTN i;
	char *argv[MAX_ARGS];
	struct reader *reader = NULL;

	if (!args || !ctx)
		return EFI_INVALID_PARAMETER;

	ret = string_to_argv(args, &argc, (CHAR8 **)argv,
			     ARRAY_SIZE(argv), ":", ":");
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"Failed to split string into argv");
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(READERS); i++)
		if (!strcmp((CHAR8 *)argv[0], (CHAR8 *)READERS[i].name)) {
			reader = &READERS[i];
			break;
		}

	if (!reader)
		return EFI_UNSUPPORTED;

	ctx->reader = reader;
	return reader->open(ctx, argc - 1, argv + 1);
}

EFI_STATUS reader_read(reader_ctx_t *ctx, unsigned char **buf, UINT64 *len)
{
	EFI_STATUS ret;

	if (!ctx || !len || !*len || !ctx->reader)
		return EFI_INVALID_PARAMETER;

	*len = min(*len, ctx->len - ctx->cur);
	if (*len == 0)
		return EFI_SUCCESS;

	ret = ctx->reader->read(ctx, buf, len);
	if (EFI_ERROR(ret))
		return ret;

	ctx->cur += *len;

	return EFI_SUCCESS;
}

void reader_close(reader_ctx_t *ctx)
{
	if (!ctx || !ctx->reader)
		return;

	if (ctx->reader->close)
		ctx->reader->close(ctx);
}
