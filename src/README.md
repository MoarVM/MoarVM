## MoarVM Source Code

MoarVM is written in C, and is designed to (eventually) have a couple of build
targets: a dynamic library (so it can be loaded by other VMs or programs that
embed it) with a small executable front-end wrapper, but also a fully
statically-built standalone executable that can run .moarvm files only.
Another option could be for incorporating programs (such as a perl6 build) to
statically link the moarvm library so it can be self-contained itself.

moarvm.c will contain the main embedding API, and main.c will utilize that API.
This is not yet fully realized.  It will eventually be compiled to libmoarvm,
or similar.

moarvm.h is the primary header file that embedders should include to gain
access to the publicly exported MVM_ routines and macros.  It includes all the
other .h in the src/ tree.  Some individual .c files also include other .h
files, but the symbols from those files aren't necessarily intended to be part
of MoarVM's public API.

main.c is currently compiled to the ./moarvm executable, which is able to run
or dump .moarvm bytecode files only.  Use the --help flag to see the options
available to the moarvm (moarvm.exe on Windows) executable.

