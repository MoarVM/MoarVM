#include "../buildsys/dynmake/Makefile.base.M"

all: _L(dyncallback_s)

_L(dyncallback_s): _O(dyncall_thunk) _O(dyncall_alloc_wx) _O(dyncall_args) _O(dyncall_callback) _O(dyncall_callback_arch)
	_(AR) _(ARFLAGS) _(ARFLAG_OUT_PREFIX)_(TARGET) _(PREREQS)

clean:
        _(RM) _O(*)
        _(RM) _L(dyncallback_s)
