# JIT documentation

To enable JIT the compiler, run Configure.pl with the option --enable-jit

The preprocessor uses *lua* and uses the
[http://bitop.luajit.org/](BitOp) module. You shouldn't need to use
the preprocessor for compiling JIT support, though.

For now, the JIT compiler only works on x64 CPUs, and is frequently
tested on linux, windows and sometimes on a mac :-).
