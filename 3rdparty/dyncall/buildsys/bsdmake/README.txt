dyncall bsdmake build system manual
Copyright (c) 2007-2009 Daniel Adler <dadler@uni-goettingen.de>, 
                        Tassilo Philipp <tphilipp@potion-studios.com>

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

configuration:

Prior to running "make" the library, one should run "configure" first.
This will create the file 'ConfigVars' defining some variables identifying
the system to build the library for.

BUILD_OS   - "{Free,Net,Open,DragonFly}BSD","SunOS","Darwin"
BUILD_ARCH - "X86","PPC32","X64"
BUILD_TOOL - "GCC","PCC"
BUILD_ASM
  default: as
  on x86 and x64 arch: nasm, as


site configuration:

  file 'SiteVars' overrides 'ConfigVars'


targets:

  all       - build dirs and targets
  clean     - clean up targets and objects
  distclean - cleans targets, objects and auto-generated files


build settings:

  ASM - specifies assembler; defaults to as@@@


project settings:

TOP       points relative to source root
DIRS      directories to sub-process for targets depending on subdirs
APP       name of application (w/o suffix of prefix) - will be built if set
DLL       name of dynamic library (w/o suffix of prefix) - will be built if set
LIBRARY   name of static library (w/o suffix of prefix) - will be built if set
UNITS     specifies the units to link
MODS      specifies modules to link (deprecated - use UNITS instead)
LDLIBS    link libraries
LDFLAGS   link flags


changes from daniel:

- added UNITS variable that deprecates MODS
- added .S.o implied rule 


urgent issues from daniel:

- no TARGET_ARCH variable in implied build rules
  needed to make universal binaries work, and also helpful for 
  gcc cross-compilation and target fine-tuning in general.
  
- handle arch "universal" (for darwin) .. means:
    TARGET_ARCH = -arch i386 -arch ppc -arch x86_64 [ -arch ppc64 ]

- no CPPFLAGS variable in implied build rules
  one place for all pre-processor issues (regardless of C, C++, Objective-C...)

- force -fPIC in general for x64
  
    
low-prio issues (wishlist) from daniel:  

- cross-compilation through overloading CC,CXX,... is this possible on bsdmake?
  
- sync with gmake variable names
  rename 
    APP -> TARGET_APP
    DLL -> TARGET_DLL
    LIBRARY -> TARGET_LIB
    
- separate build directories
  
- not an issue, but helpful:
  introduce BSDMAKE_TOP to allow bsdmake to life externally - see GMAKE_TOP variable for example

