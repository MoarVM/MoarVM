Cross-compilation with gcc:

set CC,AR,LD explicitly

export CC=$PREFIX-gcc
export AR=$PREFIX-ar
export LD=$PREFIX-ld

# required for tests:
export CXX=$PREFIX-g++

./configure <flags>

useful flags:

  --target-windows      compiling for windows on cygwin
  --target-x86          compiling for x86 on x64

