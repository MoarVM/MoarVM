Build on Darwin:

Building Universal Binaries:

$ ./configure --target-universal

or

$ set TARGET_ARCH="-arch i386 -arch ppc -arch x86_64" 
$ ./configure

Then build with GNU- or BSD-Make.

Notes for Mac OS X 10.4/Darwin 8:

The applications in the test directory might fail with
"XXX.dylib does not contain an architecture that matches the specified ..."

Here is a workaround:

Explicitly set TARGET_ARCH to "" (empty string) or the desired architecture
(e.g. "-arch ppc") before building the applications.

This prevents building universal binary executables, but the static libraries
are still built.


