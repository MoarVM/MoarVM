# Package: dyncall
# File: dyncallback/GNUmakefile
# Description: dyncallback library buildsys/gmake GNUmakefile
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

TARGET_LIB	 = dyncallback
INSTALL_LIB	 = 1

# uses some dyncall headers - no other dependencies.

CPPFLAGS	 += -I$(TOP)/dyncall

# component: thunks

INSTALL_HEADERS	+= \
	dyncall_thunk.h \
	dyncall_thunk_x86.h \
	dyncall_thunk_ppc32.h \
	dyncall_thunk_x64.h \
	dyncall_thunk_arm32_arm.h \
	dyncall_thunk_arm32_thumb.h

UNITS += dyncall_thunk 

# component: write-and-executable memory allocation

# INSTALL_HEADERS	+= dyncall_alloc_wx.h
UNITS += dyncall_alloc_wx

# component: unmarshall arguments at callee-site

INSTALL_HEADERS += dyncall_args.h
UNITS += dyncall_args

# architecture: callback

INSTALL_HEADERS += dyncall_callback.h
UNITS += dyncall_callback

ifdef BUILD_TOOL_gcc
UNITS += dyncall_callback_arch
endif

ifdef BUILD_TOOL_llvm_gcc
UNITS += dyncall_callback_arch
endif

ifdef BUILD_TOOL_pcc
UNITS += dyncall_callback_arch
endif

ifdef BUILD_TOOL_msvc
ifdef BUILD_ARCH_x86
UNITS += dyncall_callback_x86_masm
endif
ifdef BUILD_ARCH_x64
UNITS += dyncall_callback_x64_masm
endif
endif

include $(GMAKE_TOP)/epilog.gmake

