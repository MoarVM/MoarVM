# MoarVM: A virtual machine for NQP and Rakudo

Over the course of the last year, we've been working to make both NQP and
Rakudo more portable. This has primarily been done to enable the JVM porting
work. While the JVM is an important platform to target, and initial work
suggests it can give us a faster and more stable Rakudo, there are some use
cases, or users, that the JVM will not cater to so well. Startup time will
likely remain a little high for one-liners and quick scripts. Other potential
users simply don't want to use the JVM, for whatever reason. That's fine:
there's more than one way to do it, and our strategy has been to enable that by
adding support for the JVM without breaking support for Parrot. Additionally,
pmurias will be working on a JavaScript backend for a GSoC project.

Today I'd like to introduce some work that, all being well, will lead to an
additional "way to do it" arriving over the next several months. A small team,
composed of myself, diakopter, japhb and masak, have been quietly working on
taking the design of the 6model object system and building a runtime around it.
Thus, we've created the "Metamodel On A Runtime" Virtual Machine, or the "MOAR
VM", which we've taken to writing as "MoarVM".

This is not a release announcement. At present, MoarVM runs neither NQP nor
Rakudo, though a cross-compiler exists that allows translating and passing
much of the NQP test suite. We're revealing it ahead of YAPC::NA, so it can be
openly discussed by the Perl 6 team. The goal from the start has been to run
NQP, then run Rakudo. The JVM porting work has established the set of steps
that such an effort takes, namely:

1. Build an NQP cross-compiler that targets the desired platform. Make it good
   enough to compile the MOP, built-ins and the classes at the heart of the
   regex/grammar engine. Make it pass most of the NQP test suite.
2. Make the cross-compiler good enough to cross-compile NQP itself.
3. Close the bootstrap loop, making NQP self host on the new platform.
4. Do the Rakudo port.

At the moment, the JVM work is well into the final step. For MoarVM, we've
reached the second step. That is to say, we already have a cross-compiler that
compiles a substantial range of NQP language constructs into MoarVM bytecode,
including the MOP, built-ins and regex-related classes. Around 51 of the NQP
test files (out of a total of 62) pass. Work towards getting the rest of NQP
to cross-compile is ongoing.

Since anybody who has read this far into the post probably has already got a
whole bunch of questions, I'm going to write the rest of it in a
question-and-answer style.

## What are the main goals?

To create a VM backend for Rakudo Perl 6 that:

* Is **lightweight and focused** on doing exactly what Rakudo needs, without any
  prior technical or domain debt to pay off.

* **Supports 6model** and various other needs natively and, hopefully, efficiently.

* Is a **quick and easy build**, with few dependencies. I was rather impressed
  with how quick LuaJIT can be built, and took that as an inspiration.

* Enable the **near-term exploration of JIT compilation in 6model** (exploring
  this through invokedynamic on the JVM is already underway too).

## What's on the inside?

So far, MoarVM has:

* An **implementation of 6model**. In fact, the VM uses 6model as its core object
  system. Even strings, arrays and hashes are really 6model objects (which in
  reality means we have representations for arrays and hashes, which can be
  re-used by high-level types). This is the first time 6model has been built
  up from scratch without re-using existing VM data structures.

* Enough in place to support **a sizable subset of the `nqp::` op space**. The tests
  from the NQP test suite that can be translated by the cross-compiler cover a
  relatively diverse range of features: the boring easy stuff (variables,
  conditionals, loops, subs), OO stuff (classes, methods, roles, mixins, and,
  naturally, meta-programming), multiple dispatch, most of grammars (including
  LTM), and various other bits.

* **Unicode strings**, designed with future NFG support in mind. The VM includes
  the Unicode Character Database, meaning that character name and property
  lookups, case changing and so forth can be supported without any external
  dependencies. Encoding of strings takes place only at the point of I/O or
  when a Buf rather than a Str is requested; the rest of the time, strings
  are opaque (we're working towards NFG and also ropes).

* **Precise, generational GC**. The nursery is managed through semi-space copying,
  with objects that are seen a second time in the nursery being promoted to a
  second generation, which is broken up into sized heaps. Allocations in the
  nursery are thus "bump the pointer", the copying dealing with the resulting
  fragmentation.

* **Bytecode assembly done from an AST**, not from a textual format. MoarVM has
  no textual assembly language or intermediate language. Of course, there's
  a way to dump bytecode to something human-readable for debugging, but nothing
  to go in the opposite direction. This saves us from producing text, only to
  parse it to produce bytecode.

* **IO and other platform stuff** provided by the Apache Portable Runtime, big
  integer support provided by libtommath, and re-use of existing atomic ops
  and hash implementations. We will likely replace the APR with libuv in the
  future. The general principle is to re-use things that we're unlikely to
  be able to recreate ourselves to the same level of quality or on an
  acceptable time scale, enabling us to focus on the core domain.

## What does this mean for the Rakudo on JVM work?

Relatively little. Being on the JVM is an important goal in its own right. The
JVM has a great number of things in its favor: it's a mature, well-optimized,
widely deployed technology, and in some organizations the platform of choice
("use what you like, so long as it runs on the JVM"). No matter how well Moar
turns out, the JVM still has a couple of decades head start.

Additionally, a working NQP on JVM implementation and a fledgling Rakudo on
JVM already exist. Work on making Rakudo run, then run fast, on the JVM will
continue at the same kind of pace. After all, it's already been taking place
concurrently with building MoarVM. :-)

## What does this mean for Rakudo on Parrot?

In the short term, until MoarVM runs Rakudo well, this shouldn't really impact
Rakudo on Parrot. Beyond that is a more interesting question. The JVM is widely
deployed and battle-hardened, and so is interesting in its own right whatever
else Rakudo runs on. That's still not the case for Parrot. Provided MoarVM gets
to a point where it runs Rakudo more efficiently and is at least as stable and
feature complete, it's fairly likely to end up as a more popular choice of
backend. There are no plans to break support for Rakudo on Parrot.

## Why do the initial work in private?

There were a bunch of reasons for doing so. First and foremost, because it was
hard to gauge how long it would take to get anywhere interesting, if indeed it
did. As such, it didn't seem constructive to raise expectations or discourage
work on other paths that may have led somewhere better, sooner. Secondly, this
had to be done on a fairly limited time budget. I certainly didn't have time
for every bit of the design to be bikeshedded and rehashed 10 times, which is
most certainly a risk when design is done in a very public way. Good designs
often come from a single designer. For better or worse, MoarVM gets me.

## Why not keep it private until point X?

The most immediate reason for making this work public now is because a large
number of Perl 6 core developers will be at YAPC::NA, and I want us to be
able to openly discuss MoarVM as part of the larger discussions and planning
with regard to Perl 6, NQP and Rakudo. It's not in any sense "ready" for use
in the real world yet. The benefits of the work being publicly known just hit
the point of outweighing the costs.

## What's the rough timeline?

My current aim is to have the MoarVM backend supported in NQP by the July or
August release of NQP, with Rakudo support to come in the Rakudo compiler
release in August or September. A "Star" distribution release, with modules
and tools, would come after that. For now, the NQP cross-compiler lives in
the MoarVM repository.

After we get Rakudo running and stabilized on MoarVM, the focus will move
towards 6model-aware JIT compilation, improving the stability of the threads
implementation (the parallel GC exists, but needs some love still), asynchronous
IO and full NFG string/rope support.

We'll have a bunch of the right people in the same room at YAPC::NA, so we'll
work on trying to get a more concrete roadmap together there.

## Where is...

* The Git repository: https://github.com/MoarVM/MoarVM
* The IRC channel: #moarvm on freenode.org
