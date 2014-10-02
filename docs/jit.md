# JIT documentation

To enable JIT the compiler, run Configure.pl with the option --enable-jit

The preprocessor uses *lua* and uses the
[http://bitop.luajit.org/](BitOp) module. You shouldn't need to use
the preprocessor for compiling JIT support, though.

For now, the JIT compiler only works on x64 CPUs, and is frequently
tested on linux, windows and sometimes on a mac :-).

## Configuration

You can configure the behavior of the JIT compile with some nice
environment variables.

    MVM_JIT_DISABLE=1

Disables the JIT compiler.

   MVM_JIT_LOG=path/to/logfile.txt

Instructs the VM to log JIT actions to the given logfile.

   MVM_JIT_BYTECODE_DIR=a/dir

Instruct the JIT compiler to store the JIT-ed bytecode file. For now,
the most recent file is simply stored in a file named
"jit-code.bin". I'll work on changing that yet. You can look at what
that code means with the following command line (assuming you have gnu
objdump):

   objdump -b binary -D -m i386:x86-64 -M intel jit-code.bin

If you find that moarvm crashes where you'd expect the JIT to run,
please send me a copy of the output of this command, along with the
code (nqp or perl6) that triggered the problem.
