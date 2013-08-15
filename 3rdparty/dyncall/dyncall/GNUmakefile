# Package: dyncall
# File: dyncall/GNUmakefile
# Description: dyncall library buildsys/gmake GNUmakefile
# License:
#
# Copyright (c) 2007,2011 Daniel Adler <dadler@uni-goettingen.de>, 
#                         Tassilo Philipp <tphilipp@potion-studios.com>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#

TOP ?= ..
GMAKE_TOP ?= $(TOP)/buildsys/gmake
include $(GMAKE_TOP)/prolog.gmake

TARGET_LIB = dyncall

INSTALL_HEADERS	= \
	dyncall_macros.h \
	dyncall_config.h \
	dyncall_types.h \
	dyncall.h \
	dyncall_signature.h \
	dyncall_value.h \
	dyncall_callf.h \
	dyncall_alloc.h

INSTALL_LIB = 1
INSTALL_MAN3 = dyncall.3.gz

UNITS = dyncall_vector dyncall_api dyncall_callvm dyncall_callvm_base dyncall_struct

# @@@ disable dyncall_callf for pcc support temporarly
ifndef BUILD_TOOL_pcc
UNITS += dyncall_callf
endif

ifndef BUILD_TOOL_msvc

UNITS += dyncall_call

else

ifdef BUILD_ARCH_x86

ifdef BUILD_ASM_nasm
UNITS += dyncall_call_x86_nasm
else
UNITS += dyncall_call_x86_generic_masm
endif

endif

ifdef BUILD_ARCH_x64

ifdef BUILD_ASM_nasm
UNITS += dyncall_call_x64_nasm
else
UNITS += dyncall_call_x64_generic_masm
endif

endif

endif

include $(GMAKE_TOP)/epilog.gmake

