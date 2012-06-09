# Argument Passing
Every invocation in MoarVM consists of two pieces:

* A static Callsite descriptor (MVMCallsite), which incorporates the
  number of arguments along with a set of flags about what is being
  passed.
* An array of MVMArg, which is a union type. This contains the actual
  arguments being passed. It's the static descriptor that indicates how
  it can be read.

## Memory management
Callsite descriptors are kept at a compilation unit level, and thus their
lifetime is simply that of the compilation unit. (An alternative way would
be to keep a global store of these and intern them, fixing up all of the
references to them at bytecode loading time.)

The argument data itself is more sutble. A simple approach would be to
allocate the argument data array per call, but that's too much allocation
for the common case. Thus each callframe, along with its working space,
also allocates an amount of space equal to that required by the most
demanding callsite (that is, the one that implies the most storage). This
space is populated with arguments per call, and a pointer passed to the
invocation target.

This area of memory will clearly never be safe to consider beyond the point
that the callee yields control. Control is yielded by:

* Returning
* Yielding co-routine style
* Calling anything that will potentially do co-routine or continuation stuff

Typically, though, an argument processor immediately copies what was passed
into locals, lexicals and so forth. It may need to make its own copy of the
original arguments if:

* It wants to keep them around and present them "first class" (a bit like
  Perl 6 does with Captures, in order to do nextsame et al.)
* Part way through processing them, some other code needs to be run in order
  to obtain default values, build up data structures, etc.

A language that doesn't need to worry about such matters will generally be
able to avoid any of the copying. A language like NQP will be able to avoid
it perhaps entirely, by handling default values after the initial binding.
A language like Perl 6 will generally need to default to copying, but a
decent optimizer should be able to whitelist routines where it's OK not to.
Many simple built-in operators should make the cut, which will take the edge
off the cases where compile-time inlining isn't possible.

## Call Code Generation
A call looks something like:

    prepargs   callsite  # set the callsite
    argconst_i 0, 42     # set arg slot 0 to native int arg 42
    arg_o      1, r0     # set arg slot 1 to object in register 0
    call r1              # invoke the object in r1

The bytecode loader will analyse the callsite info, and ensure that between
it and the call all of the required slots are given values, and that these are
of the correct types. This is verified once when the bytecode is loaded, and
at execution time needs no further consideration.

Note that there's no reason you can't have things like:

    prepargs callsite       # set the callsite
    argconst_i 0, 42        # set arg slot 0 to native int arg 42
    get_lex_named r0, '$x'  # load lexical "$x" by name into register 0
    arg_o 1, r0)            # set arg slot 1 to object in register 0
    call r1                 # invoke the object in r1

That is, do lookups or computations of arguments while building the callsite.
However, it is NOT allowable to nest prepargs/call sequences in the bytecode.
There is only one chunk of memory available to store the arguments. Thus a
call like:

    foo(bar(42))
    
Will have to store the result of bar(42) in a register, then prepargs..call
for foo(...) afterwards.
