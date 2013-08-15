# DynCall build script using Makefile.generic on Minix 3.1.8
# ----------------------------------------------------------

# build dyncall, clear CFLAGS (so that '-fPIC' removed, coz not supported)

( cd dyncall ; CC=gcc make -f Makefile.generic clean all CFLAGS= )


# build dyncallback, clear CFLAGS and set explicit link to dyncall include.

( cd dyncallback ; CC=gcc make -f Makefile.generic clean all CFLAGS=-I../dyncall )


# build tests, skip dynload tests.

( cd test ; CC=gcc CFLAGS= CXX=g++ make -f Makefile.generic clean all-dyncall all-dyncallback )


