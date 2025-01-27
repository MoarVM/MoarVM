# MoarVM

MoarVM (short for Metamodel On A Runtime Virtual Machine) is a runtime built
for the 6model object system. It is primarily aimed at running NQP and
Rakudo, but should be able to serve as a backend for any compilers
built using the NQP compiler toolchain.

## Get It
Either download it from [the MoarVM site](https://www.moarvm.org/) or clone it from GitHub:
```
git clone https://github.com/MoarVM/MoarVM.git
```
If you use the automatically generated release tarballs or zip files please note that they don't
contain the third party libraries needed to successfully build MoarVM.

## Build It
[![Build Status](https://dev.azure.com/MoarVM/MoarVM/_apis/build/status/MoarVM.MoarVM?branchName=master)](https://dev.azure.com/MoarVM/MoarVM/_build/latest?definitionId=1&branchName=master)

Building and installing the VM itself takes just:

    perl Configure.pl
    make install

(Or `nmake`/`gmake` on Windows). Currently it is known to build on Windows
with MSVC and gcc, and with `gcc` and `clang` on Linux & MacOS X.  We're
expanding this with time.

Type `perl Configure.pl --help` to see the configure-time options, as well
as some descriptions of the make-time options/targets.

## Building an NQP with MoarVM

After installing MoarVM, you can clone the NQP repository or grab a source
tarball and use the Configure.pl script in it like so:

    perl Configure.pl --backend=moar --prefix=where_your_moarvm_install_lives

Alternatively, the same Configure.pl script in NQP is able to clone, build
and install a copy of MoarVM on its own if you supply the `--gen-moar` flag.

> Please bear in mind that this will be the prefix to the `/bin`, `/lib` and other directories where the `moar` executable and other files are going to be installed, so you'll have to use `/usr` if you want `moar` to be copied to `/usr/bin`.

## Building a Rakudo with MoarVM

When pointing the `Configure.pl` script in rakudo's repository at a `--prefix`
that has an `nqp-m` installed in it, it will automatically detect and configure
the MoarVM backend. Alternatively, `--backend=moar,jvm` can be used to force
it to build the MoarVM and JVM backends, for example. Just like in the NQP
`Configure.pl` script, you have the option to supply a `--gen-moar` flag that
will do all the work for you, including creating an `nqp-m`.

## Status

MoarVM is currently in development. It can run all of the NQP test suite, all
of the Rakudo sanity tests, and passes more spectests than any other
Rakudo backend.

Unlike the JVM or JS backend of NQP, the MoarVM repo is not integrated into the
[NQP source repo](http://github.com/perl6/nqp) but instead can be pulled
in by running `Configure.pl --gen-moar` configure script in the NQP repo.

## Feature overview

Some key features provided by MoarVM include:

* Meta-object programming, using the 6model design
* Precise, generational, and parallel GC
* Unicode 15 support (Unicode Character Database, encodings, normalization)
* First-class code objects, lexical variables and closures
* Exceptions
* Continuations
* Bounded serialization
* Runtime loading of code
* Big integers
* A range of IO and process support, including asynchronous sockets, signals,
  timers, and processes
* Native calling and native pointer manipulation
* Threads, mutexes, condition variables, semaphores, and blocking queues
* Bytecode specialization by type, and numerous optimizations (including
  resolution of method calls and multiple dispatch, dead code elimination,
  inlining, on stack replacement, scalar replacement, and partial escape
  analysis)
* JIT compilation
* Instrumentation-based profiling of call frames and allocations
* Heap snapshotting
* Remote Debugging with single stepping and variable/object introspection

## Contributing

Contributions by pull request are accepted. Commit bits are given to those who
contribute quality work. If you are interested in contributing, drop by the
`#moarvm` channel on libera.chat.

See the LICENSE file in the root directory for information on the license of
the source code in the MoarVM repository.

## Troubleshooting

### Linker can't find appropriate symbols on macOS

If MoarVM fails to build, and the error looks something like this:

> ld: symbol(s) not found for architecture x86_64

you likely have an incompatible mix of build and bin utils.

While it is common to have toolchains installed from third party repositories in macOS, they aren't all compatible. In the event you run into this issue, please try these steps.

 1. Unlink your tools in homebrew: `brew unlink binutils`
 2. Destroy and re-clone MoarVM or rakudobrew
 3. Attempt the build again from scratch

If you _want_ to use a GNU toolchain, and you get an error telling you to see this file, simply supply the `--toolchain=gnu` flag and this package will configure and build with a GNU toolchain.

**Please note:** If you use mixed Xcode and non-Xcode tools, you are likely to run into trouble. As such, this configuration is unsupported.

### You need different code for `gcc` versus `clang`

Note both compilers define macro `__GNUC__`, so macro `__clang__` needs to be tested first to disambiguate the two.
