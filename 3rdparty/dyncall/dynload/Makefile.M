#include "../buildsys/dynmake/Makefile.base.M"

all: _L(dynload_s)

_L(dynload_s): _O(dynload) _O(dynload_syms)
	_(AR) _(ARFLAGS) _(ARFLAG_OUT_PREFIX)_(TARGET) _(PREREQS)

clean:
        _(RM) _O(*)
        _(RM) _L(dynload_s)
