# Exceptions

Exceptions in MoarVM need to handle a range of cases. There exist both control
exceptions (last/next/redo) where we want to reach the handler in the most
expedient way possible, unwinding the stack as we go. There are also Perl 6
"normal" exceptions where we need to run the handler in the dynamic scope of
the exception. These are actually properties of the handler rather than the
exception; a CONTROL is interested in being run on the stack top when a "next"
reaches it, whereas a normal handler for that doesn't even care for an object
describing the exception to ever be created, so long as control ends up in the
right place.

## Handlers

Handlers are associated with (static) frames. A handler consists of:

* The start of the protected region (an offset from the frame's bytecode start)
* The end of the protected region (an offset from the frame's bytecode start)
* An exception category filter
* A handler kind (0 = unwind and goto address, 1 = unwind and run block, 2 =
  run block then unwind unless exception was flagged as "resume"; bitwise-or
  a 4 to indicate that there is no need to construct any kind of exception
  object)
* In the case of a "goto address" handler, the offset of the handler
* In the case of a block handler, the register in the frame that holds the
  block to invoke. The block should take no parameters.

The category filter is a bitfield of:

* 1 = Catch Exception
* 2 = Control Exception
* 4 = Next
* 8 = Redo
* 16 = Last
* 32 = Return
* 64 = Unwind (triggers if we unwind out of it)

A bitwise and between the category filter and the category of the exception
being thrown is used to check if the handler is applicable.

## Throwing Exceptions
There are two opcodes available for throwing an exception with a particular
payload (which may be the high-level language's exception object):

    dynthrow_o <payload>, <category>
    lexoticthrow_o <payload>, <category>

It is also possible to throw an exception just by category:

    dynthrow <category>
    lexoticthrow <category>

## Handler Operations


## Unwind Handlers


## Exceptions Thrown While Handling Exceptions

