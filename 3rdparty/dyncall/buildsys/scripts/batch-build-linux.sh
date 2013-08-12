# Build libraries.

make -f Makefile.generc clean all

# Build tests: dynload tests need '-ldl'.

( cd test ; make -f Makefile.generic clean )
( cd test ; LDFLAGS=-ldl make -f Makefile.generic all-dynload )
( cd test ; make -f Makefile.generic all )


