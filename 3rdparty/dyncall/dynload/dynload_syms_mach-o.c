/*

 Package: dyncall
 Library: dynload
 File: dynload/dynload_syms_mach-o.c
 Description: 
 License:

   Copyright (c) 2007-2011 Olivier Chafik <olivier.chafik@gmail.com>

   Permission to use, copy, modify, and distribute this software for any
   purpose with or without fee is hereby granted, provided that the above
   copyright notice and this permission notice appear in all copies.

   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
   WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
   MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
   ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

*/


/*
 
 dynamic symbol resolver for Mach-O

*/

#include "dynload.h"
#include "dynload_alloc.h"

#include <mach-o/dyld.h>
#include <mach-o/nlist.h>
#include <dlfcn.h>
#include <string.h>

#if defined(ARCH_X64)
#define MACH_HEADER_TYPE mach_header_64
#define SEGMENT_COMMAND segment_command_64
#define NLIST_TYPE nlist_64
#else
#define MACH_HEADER_TYPE mach_header
#define SEGMENT_COMMAND segment_command
#define NLIST_TYPE nlist
#endif


struct DLLib_
{
	char* libPath;
	void* handle;
};


struct DLSyms_
{
	const char* pStringTable;
	const struct NLIST_TYPE* pSymbolTable;
	uint32_t symbolCount;
};


DLSyms* dlSymsInit(const char* libPath) 
{
	DLSyms* pSyms = NULL;
	uint32_t iImage, nImages;
	for (iImage = 0, nImages = _dyld_image_count(); iImage < nImages; iImage++)
	{
		const char* name = _dyld_get_image_name(iImage);
		if (name && !strcmp(name, libPath))
		{
			const struct MACH_HEADER_TYPE* pHeader = (const struct MACH_HEADER_TYPE*) _dyld_get_image_header(iImage);
			const char* pBase = ((const char*)pHeader);
			if (pHeader->filetype != MH_DYLIB)
				return NULL;
			if (pHeader->flags & MH_SPLIT_SEGS)
				return NULL;

			if (pHeader)
			{
				uint32_t iCmd, nCmds = pHeader->ncmds;
				const struct load_command* cmd = (const struct load_command*)(pBase + sizeof(struct MACH_HEADER_TYPE));
				
				for (iCmd = 0; iCmd < nCmds; iCmd++) 
				{
					if (cmd->cmd == LC_SYMTAB) 
					{
						const struct symtab_command* scmd = (const struct symtab_command*)cmd;
					
						pSyms = (DLSyms*)( dlAllocMem(sizeof(DLSyms)) );
						pSyms->symbolCount = scmd->nsyms;
						pSyms->pStringTable = pBase + scmd->stroff;
						pSyms->pSymbolTable = (struct NLIST_TYPE*)(pBase + scmd->symoff);
						
						return pSyms;
					}
					cmd = (const struct load_command*)(((char*)cmd) + cmd->cmdsize);
				}
			}
			break;
		}
	}
	return NULL;
}


void dlSymsCleanup(DLSyms* pSyms)
{
	if (!pSyms)
		return;
	
	dlFreeMem(pSyms);
}

int dlSymsCount(DLSyms* pSyms)
{
	if (!pSyms)
		return 0;
	return pSyms->symbolCount;
}

static const struct NLIST_TYPE* get_nlist(DLSyms* pSyms, int index)
{
	const struct NLIST_TYPE* nl;
	if (!pSyms)
		return NULL;
	
	nl = pSyms->pSymbolTable + index;
	if (nl->n_un.n_strx <= 1)
		return NULL; // would be empty string anyway
	
	//TODO skip more symbols based on nl->n_desc and nl->n_type ?
	return nl;
}


const char* dlSymsName(DLSyms* pSyms, int index)
{
	const struct NLIST_TYPE* nl = get_nlist(pSyms, index);
	if (!nl)
		return NULL;
	
	return pSyms->pStringTable + nl->n_un.n_strx;
}


void* dlSymsValue(DLSyms* pSyms, int index)
{
	const struct NLIST_TYPE* nl = get_nlist(pSyms, index);
	if (!nl)
		return NULL;
	
	return (void*) (ptrdiff_t) (nl->n_value);
}


const char* dlSymsNameFromValue(DLSyms* pSyms, void* value)
{
	Dl_info info;
	if (!dladdr(value, &info) || (value != info.dli_saddr))
		return NULL;
	
	return info.dli_sname;
}
