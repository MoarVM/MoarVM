# MoarVM

MoarVM (short for Metamodel On A Runtime Virtual Machine) is a runtime built
for the 6model object system. It is primarily aimed at running NQP and Rakudo
Perl 6, but should be able to serve as a backend for any compilers built using
the NQP compiler toolchain.

## Build It

Building the VM itself takes just:

    perl Configure.pl
    make

(Or `nmake` on Windows). Currently it is known to build on Windows with MSVC,
with `gcc` and `clang` on Linux & MacOS X.  We're expanding this with time.

Type `perl Configure.pl --help` to see the configure-time options, as well
as some descriptions of the make-time options/targets.

## Building an NQP with MoarVM

After installing MoarVM, you can clone the NQP repository or grab a source
tarball and use the Configure.pl script in it like so:

    perl Configure.pl --backend=moar --prefix=where_your_moarvm_install_lives

Alternatively, the same Configure.pl script in NQP is able to clone, build
and install a copy of MoarVM on its own if you supply the `--gen-moar` flag.

## Building a Rakudo with MoarVM

Currently, the code supporting MoarVM for rakudo lives in the `moar-support`
branch of the rakudo repository. When pointing it at a `--prefix` that has
an `nqp-m` installed in it, it will automatically detect and configure the
MoarVM backend. Alternatively, `--backend=moar,parrot` can be used to force
it to build the MoarVM and Parrot backends, for example.

## Status

MoarVM is currently in development. It can run all of the NQP test suite
and all MoarVM-specific NQP tests (with the exception of continuations)
without needing cross-compilation. The `moar-support` branch of rakudo is
progressing quickly and is expected to be merged into `nom` some time
in or before February 2014.

Unlike the JVM backend of NQP, the MoarVM repo is not currently planned to be
integrated into the main NQP source repo http://github.com/perl6/nqp but
instead can be pulled in by `Configure.pl --gen-moar` configure script in the
NQP repo, same as it can `--gen-parrot`.

## Contributing

Contributions by pull request are accepted. If you are interested in commit
access so you can contribute more easily, drop by the `#moarvm` channel on
freenode.org, or email diakopter@gmail.com if you're averse to IRC.

See the LICENSE file in the root directory for information on the license of
the source code in the MoarVM repository.
