# Exceptions

Exceptions in MoarVM need to handle a range of cases. There exist both control
exceptions (last/next/redo) where we want to reach the handler in the most
expedient way possible, unwinding the stack as we go, and probably just do a
goto instruction. In these cases, we don't expect to need any kind of exception
object. At the other end of the scale, there are Perl 6 exceptions. These want
to run the handler in the dynamic scope of the exception, and potentially resume
rather than unwinding. These differences are properties of the handler rather than
the exception; a CONTROL is interested in being run on the stack top when a "next"
reaches it, whereas a while loop's handler for that just wants control delivered
to the appropriate place.

## Handlers

Handlers are associated with (static) frames. A handler consists of:

* The start of the protected region (an offset from the frame's bytecode start)
* The end of the protected region (an offset from the frame's bytecode start)
* An exception category filter:
    * 1 = Catch Exception
    * 2 = Control Exception
    * 4 = Next
    * 8 = Redo
    * 16 = Last
    * 32 = Return
    * 64 = Unwind (triggers if we unwind out of it due to an exception being
      thrown; normal block exits do not cause this)
* A handler action
    * 0 = Unwind any required frames, then goto the specified address. It is
      not possible to get any exception object or do any kind of rethrow.
    * 1 = Unwind any required frames, then goto the specified address. An
      exception object is available. This kind of handler leaves a handler
      record active on the stack, which the handler should remove by doing
      a rethrow or making the exception handled.
    * 2 = Invoke the specified block, and unwind unless it chooses to resume.
      Once the block returns, the handler is over.
* In the case of a goto address handler, the offset of the handler
* In the case of a block handler, the register in the frame that holds the
  block to invoke. The block should take no parameters.

A bitwise `and` between the category filter and the category of the exception
being thrown is used to check if the handler is applicable.

Note that an Unwind handler is never actually set as the category of an
exception; these are just for triggering actions during unwinds due to
other exceptions. In the case of an unwind handler, the current exception
is thus the one to blame for the unwinding. It is expected that an unwind
handler will always rethrow once it's done what is needed.

## Handler representation in MAST

The MAST::HandlerScope node indicates the instructions covered by handler
and details of the kind of handler it is. See the MAST node definitions for
more.

## Handler representation in bytecode

Handlers are stored per frame and listed in a table. It is important that
more deeply nested handlers appear in the table earlier than those lexically
outer to them. This is really a job for the MAST to Bytecode compiler, since
the MAST encodes the structure as nested nodes. Really, though, it's just a
case of writing an entry into the frame's table after the node has been
processed. See the bytecode specification for details.

## Exception Objects

Some opcodes exist for creating exception objects and working with them. An
exception object is anything with the VMException representation. Note that
most HLLs will wish to attach their own objects as the payload.

### exception w(obj)
Gets the exception currently being handled. Only valid in the scope of handler.

### handled r(obj)
Marks the specified exception as handled. Only valid in the scope of a handler
for the specified exception. Also, only required for goto handlers that also
include an exception object.

### newexception w(obj)
Creates a new exception object, based on the current HLL's configured exception
type object or using BOOTException otherwise. By default it has an empty message
and a category of 1 (a catch exception).

### bindexmessage r(obj), r(str)
Sets the exception object's string message.

### bindexpayload r(obj), r(obj)
Sets the exception object's payload (some other object).

### bindexcategory r(obj), r(int64)
Sets the exception object's category

### getexmessage w(str), r(obj)
Gets the exception object's string message.

### getexpayload W(obj), r(obj)
Gets the exception object's payload.

### getexcategory w(int64), r(obj)
Gets the exception object's category

## Throwing Exceptions
There are various instructions for throwing a new exception object.

    throwdyn w(obj) r(obj)
    throwlex w(obj) r(obj)
    throwlexotic w(obj) r(obj)

There are also instructions for throwing a particular category of exception
without first creating an exception object.

    throwcatdyn w(obj) int64
    throwcatlex w(obj) int64
    throwcatlexotic w(obj) int64

These will only produce an exception object for handlers that need it. The
object that is produced will have a null message and payload, so only its
category will be of interest. These are mostly intended for control exceptions.

Finally, for convenience, there is also:

    die w(obj) r(str)

Which creates a catch exception with a string message and throws it.

One may wonder why all of these throw instructions take a register to write into.
This is because a handler that invokes in the dynamic scope of the throw has the
option to prevent stack unwinding by instead indicating that execution be resumed.
When it does so, it specifies an argument for the resumption; this argument is then
written into the register should resumption take place.

As for the dyn/lex/lexotic difference:

* dyn means "search caller"
* lex means "search outer", with the caveat that the outer must also be on the caller
  chain too
* lexotic combines the two; for each entry in the dynamic scope, we scan all outers
  from that point; note that such an outer should also be in the call chain

## Rethrowing

Sometimes, a handler may want to look at an exception, see if it's what it expects to
handle, and if not pass it along as if the handler never saw it. This is the job of
rethrow. A rethrow may only be used on the exception currently being handled. It is
a simple instruction:

    rethrow

Since it's always about the exception for the current handler, there's no need to say
what should be rethrown.

## Goto handlers that access exception objects and may rethrow

A goto handler that is allowed to get the exception object and/or rethrow it must mark
the point they consider the handler over in the case they do not rethrow. The op for
this is simply:

    handled r(obj)

Note that if, while the handler is active, another exception is thrown and unwinds the
stack past this handler, that's fine.

## Overall mechanism

A stack of current handlers is maintained. Note that this is handlers we've actually
invoked as the result of an exception being thrown (there may be many handler scopes
that we are in, but only those that are presently handling exceptions get an entry on
the stack).

When we search for handlers to invoke, any active handler is automatically skipped,
so that a handler can never catch an exception thrown within it. Otherwise, you can
easily imagine a mass of hangs.

When an exception is thrown, some pieces of information are initially needed:

* The category, CAT
* The exception object, OBJ
* How to search (dyn, lex, lexotic), MODE
* The current scope, SCOPE
* The curent thread's active handler stack, HSTACK

Here is the overall algorithm in pseudo-code.

XXX TODO: Finish this up. :-)

    search_frame_handlers(f, cat):
        for h in f.handlers
            if h.category_mask & cat
                if f.pc >= h.from && f.pc < h.to
                    if !in_handler_stack(HSTACK, h)
                        return h
        return NULL

    search_for_handler_from(f, mode, cat)
        if mode == LEXOTIC
            while f != NULL
                h = search_for_handler_from(f, LEX, cat)
                if h != NULL
                    return h
                f = f.caller
        else
            while f != NULL
                h = search_frame_handlers(f, cat)
                if h != NULL
                    return h
                if mode == DYN
                    f = f.caller
                else if f == LEX
                    f_maybe = f.outer
                    while f_maybe != NULL && !is_in_caller_chain(f, f_maybe)
                        f_maybe = f_maybe.outer
                    f = f_maybe
    return NULL

    run_handler(h, target_scope)
        if h.mode == 0
            unwind_to(target_scope)
            pc = h.goto
            return_to_runloop
        if h.mode == 1
            unwind_to(target_scope)
            pc = h.goto
            push_handler(h, target_scope)
            return_to_runloop
        if h.mode == 2
            unwind_to(target_scope)
            push_handler(h, target_scope)
            SCOPE.return_special = ...
            SCOPE.return_special_data = ...
            invoke(get_reg(target_scope, h.local_idx))
            return_to_runloop
        if h.mode == 3
            push_handler(h, target_scope)
            SCOPE.return_special = ...
            SCOPE.return_special_data = ...
            invoke(get_reg(target_scope, h.local_idx))
            return_to_runloop

    panic_unhandled(scope, obj):
        note "Unahndled exception: " + obj.message
        note backtrace(scope)
        exit 1

    panic_unhandled_cat(scope, cat):
        note "Unahndled exception of category " + category_name(cat)
        note backtrace(scope)
        exit 1

    throw(mode):
        (h, target_scope) = search_for_handler_from(SCOPE, mode, CAT)
        if h == NULL
            panic_unhandled_cat(SCOPE, CAT)
        run_handler_(h, target_scope, obj)

    throwcat(mode):
        (h, target_scope) = search_for_handler_from(SCOPE, mode, CAT)
        if h == NULL
            panic_unhandled_cat(SCOPE, CAT)
        run_handler_(h, target_scope, NULL)

    handled():
        HSTACK.pop()
