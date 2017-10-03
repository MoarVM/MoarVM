# Runtime Configuration

The JIT compiler can be switched off using the environment variable:

    MVM_JIT_DISABLE=1

The JIT compiler can write a log file, which is useful only for
debugging JIT problems. The path to the log file can be given
as an environment variable:

   MVM_JIT_LOG=path/to/logfile.txt

Finally, the JIT compiler can write binary dumps of compiled frames
in a directory. These binaries are also primarily useful as a
debugging aid.

   MVM_JIT_BYTECODE_DIR=a/dir

The following command serves to disassemable a JIT compiled frame
(assuming you have GNU objdump installed as objdump):

   objdump -b binary -D -m i386:x86-64 -M intel $frame-name-jit-code.bin


