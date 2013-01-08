# MoarVM

MoarVM (short for Metamodel On A Runtime) is a runtime centered on the
6model meta-programming world view. Like 6model, it is decidedly built
with Perl 6 in mind, but since Perl 6 is designed to be twisted into all
manner of shapes then no doubt MoarVM can be put to work in a bunch of
different situations too.

## Key Principles

MoarVM distinguishes itself in a number of ways:

* MoarVM is gradually typed, rather than having a focus on static or dynamic
  typing. In general, it is built to use type information when it has it,
  but is comfortable operating in its absence too.

* MoarVM has a notion of ubiquitous representation polymorphism; storage
  strategy is not tangled up with the nominal type system.
  
* MoarVM doesn't have any kind of intermediate language or assembly
  language; as input, it takes a low level AST, which is packed in a
  binary form. Of course, you could develop an IL that translates to
  the AST, but the official interface is the AST.

* MoarVM uses NFG internally for strings. Separately from this, it makes
  it possible to talk about encoded buffers - but they need to be turned
  into NFG strings before string operations are performed.

* MoarVM doesn't re-invent wheels when it doesn't have to. As a result, it
  uses the Apache Portable Runtime for a whole bunch of things, libtommath
  for providing big integer support and dyncall for providing calls to C
  native libraries.

* MoarVM also doesn't want to give you dependency hell, so anything that it
  depends on, it includes and builds.
