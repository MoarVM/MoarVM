#include "../buildsys/dynmake/Makefile.base.M"

all: _L(dyncall_s)

_L(dyncall_s): _O(dyncall_vector) _O(dyncall_api) _O(dyncall_callvm) _O(dyncall_callvm_base) _O(dyncall_call) _O(dyncall_callf) _O(dyncall_struct)
	_(AR) _(ARFLAGS) _(ARFLAG_OUT_PREFIX)_(TARGET) _(PREREQS)

clean:
	_(RM) _O(*)
	_(RM) _L(dyncall_s)
