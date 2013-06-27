# MoarVM

MoarVM (short for Metamodel On A Runtime Virtual Machine) is a runtime built
for the 6model object system. It is primarily aimed at running NQP and Rakudo
Perl 6, but should be able to serve as a backend for any compilers built using
the NQP compiler toolchain.

## Build It

Building the VM itself takes just:

    perl Configure.pl
    make

Or `nmake` on Windows. Currently it is known to build on Windows with MSVC,
and with `gcc` and `clang` on Linux. We'll work on expanding this with time.

You need to install libuuid on linux(It's required by libapr).

## Build the NQP Cross-Compiler

To run some NQP code, or tests, then:

    cd nqp-cc
    perl Configure.pl
    make

Then run some NQP code with:

    nqp nqp-moar-cc.nqp -e "say('Hello, MoarVM')"

To run some VM-centric tests, do:

    make test

To run what is passing of the NQP test suite so far, do:

    make nqptest

Note that at present, you need a working Parrot and NQP in order to run the
cross-compiler. In the future, NQP will be self-hosting on MoarVM and that
shall not be needed. In order to obtain these, you can replace the initial
configure line with:

    perl Configure.pl --gen-parrot

## Status

MoarVM is currently in development. It is capable of having much of the NQP
test suite cross-compiled and run on it, but does not yet host NQP, nor Rakudo
Perl 6.

## Contributing

Contributions by pull request are accepted. If you are interested in commit
access so you can contribute more easily, drop by the #moarvm channel on
freenode.org.
