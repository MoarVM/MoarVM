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

Very soon, you will be able to run some NQP code by doing:

    cd nqp-cc
    ../moarvm nqp.moarvm -e 'say("Alive!")'

Note that at present, you need a working Parrot and NQP in order to run the
cross-compiler. In the future, NQP will be self-hosting on MoarVM and that
will not be needed. In order to obtain these, you can replace the initial
configure line with:

    perl Configure.pl --gen-parrot --gen-nqp

Then you will need to have nqp-cc/install/bin in your PATH to safely build.

## Status

MoarVM is currently in development. It is capable of running much of the NQP
test suite when it's cross-compiled, but does not yet host NQP, nor Rakudo
Perl 6.  We hope these to occur (conservatively) in 2013.

Unlike the JVM backend of NQP, the MoarVM repo is not currently planned to be
integrated into the main NQP source repo http://github.com/perl6/nqp but
instead will be pulled in by a future ConfigureMoarVM.pl (or similarly named)
backend configure script in the NQP repo, similarly to how the original one
automatically pulls in the Parrot VM and builds it from its repo.

## Contributing

Contributions by pull request are accepted. If you are interested in commit
access so you can contribute more easily, drop by the `#moarvm` channel on
freenode.org, or email diakopter@gmail.com if you're averse to IRC.

See the LICENSE file in the root directory for information on the license of
the source code in the MoarVM repository.
