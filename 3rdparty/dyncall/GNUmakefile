# Package: dyncall
# File: GNUmakefile
# Description: Top-level buildsys/gmake GNUmakefile
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

TOP 	   = .
GMAKE_TOP ?= $(TOP)/buildsys/gmake
include $(GMAKE_TOP)/prolog.gmake

DIRS	= dyncall dynload dyncallback

include $(GMAKE_TOP)/epilog.gmake

.PHONY: test doc
test:
	$(MAKE_COMMAND) -C $@
doc:
	$(MAKE_COMMAND) -C $@

