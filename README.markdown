# MoarVM

MoarVM (short for Metamodel On A Runtime Virtual Machine) is a runtime built
for the 6model object system. It is primarily aimed at running NQP and Rakudo
Perl 6, but should be able to serve as a backend for any compilers built using
the NQP compiler toolchain.

## Build It

[![Build Status](https://travis-ci.org/MoarVM/MoarVM.svg?branch=master)](https://travis-ci.org/MoarVM/MoarVM)

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

When pointing the `Configure.pl` script in rakudo's repository at a `--prefix`
that has an `nqp-m` installed in it, it will automatically detect and configure
the MoarVM backend. Alternatively, `--backend=moar,jvm` can be used to force
it to build the MoarVM and JVM backends, for example. Just like in the NQP
`Configure.pl` script, you have the option to supply a `--gen-moar` flag that
will do all the work for you, including creating an `nqp-m`.

## Status

MoarVM is currently in development. It can run all of the NQP test suite, all
of the Rakudo sanity tests, and passes more spectests than any other Rakudo
Perl 6 backend (though some backends pass tests that it does not).

Unlike the JVM backend of NQP, the MoarVM repo is not currently planned to be
integrated into the main NQP source repo http://github.com/perl6/nqp but
instead can be pulled in by `Configure.pl --gen-moar` configure script in the
NQP repo, same as it can `--gen-parrot`.

## Feature overview

Some key features provided by MoarVM include:

* Meta-object programming, using the 6model design
* Precise, generational, and parallel GC
* Unicode support (Unicode database lookup, encodings, normalization)
* First-class code objects, lexical variables and closures
* Exceptions
* Continuations
* Bounded serialization
* Code generation from MAST (MoarVM AST)
* Runtime loading of code
* Big integers
* A range of IO and process support, including asynchronous sockets, signals,
  timers, and processes
* Native calling and native pointer manipulation
* Threads, mutexes, condition variables, semaphores, and blocking queues
* Bytecode specialization by type, and numerous optimizations (including
  resolution of method calls and multiple dispatch, dead code elimination,
  inlining, and on stack replacement)
* JIT compilation
* Instrumentation-based profiling of call frames and allocations

## Contributing

Contributions by pull request are accepted. Commit bits are given to those who
contribute quality work. If you are interested in contributing, drop by the
`#moarvm` channel on freenode.org, or email jnthn@jnthn.net if you're averse
to IRC.

See the LICENSE file in the root directory for information on the license of
the source code in the MoarVM repository.
