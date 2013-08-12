#include "../../dyncall/dyncall_macros.h"

#if defined(DC_WINDOWS) && defined(MAKE_CMD_nmake)

/* Abstractions */
#define _(X)      $(X)         /* Standard variables */
#define _L(X)     X.lib
#define _O(X)     X.obj

#define TARGET   @
#define PREREQS  **

/* Makefile internal vars for platform abstraction */
MAKEFLAGS_USER = /NOLOGO
MAKEFLAG_FILE = /f
MAKE_DEFAULT_FILE = dynMakefile
SHELL_COMMAND_SEPARATOR = &&

AR = lib
ARFLAGS_USER = /NOLOGO
ARFLAGS = 
ARFLAG_OUT_PREFIX = /OUT:

CFLAGS_USER      = /nologo /I..\dyncall /I..\dyncallback
CFLAGS           = _(CFLAGS) _(CFLAGS_USER)

LDFLAGS_USER      = /nologo /L..\dyncall /L..\dyncallback
LDFLAGS           = _(LDFLAGS) _(LDFLAGS_USER)
LDFLAG_IN_PREFIX  =
LDFLAG_OUT_PREFIX = /OUT:

ASFLAGS_USER = /nologo
AFLAGS       = _(AFLAGS) _(ASFLAGS_USER)  /* Set AFLAGS (without 'S'), which is the standard nmake predefined macro for MASM */
ASFLAGS      = _(AFLAGS)

RM = del

.SUFFIXES : .S

.S.obj:
	cl /nologo /EP $< > $*.asm
	_(AS) _(ASFLAGS) /c $*.asm
	del $*.asm


#else

/* Abstractions */
#define _(X)      ${X}         /* Standard variables */
#define _L(X)     lib##X.a
#define _O(X)     X.o

#define TARGET   @
#if defined(MAKE_CMD_gmake) || (defined(DC__OS_Linux) && !defined(MAKE_CMD_bsdmake)) || (defined(DC__OS_Darwin) && !defined(MAKE_CMD_bsdmake)) || (defined(DC__OS_SunOS) && !defined(MAKE_CMD_bsdmake))
#  define PREREQS  ^
#else
#  define PREREQS  >
#endif


/* Makefile internal vars for platform abstraction */
MAKEFLAGS_USER = 
MAKEFLAG_FILE = -f
MAKE_DEFAULT_FILE = dynMakefile
SHELL_COMMAND_SEPARATOR = ;

ARFLAG_OUT_PREFIX = 

CFLAGS_USER = -I ../dyncall -I ../dyncallback
CFLAGS     += _(CFLAGS_USER)

LDFLAGS_USER      = -L../../dyncall -L../../dyncallback
LDFLAGS          += _(LDFLAGS_USER)
LDFLAG_IN_PREFIX  = -l
LDFLAG_OUT_PREFIX = -o

ASFLAGS_USER = 

RM = rm -f

#endif

