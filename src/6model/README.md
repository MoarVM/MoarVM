## 6model

A great introduction to the 6model system is available here:

    http://jnthn.net/papers/2013-yapceu-moarvm.pdf

Here are some highlights from the text on those slides:

6model provides primitives for building an object system.  Every object in
MoarVM is a 6model object - one object system for the whole VM.  By object,
we mean what you think of as objects (Arrays, Hashes, Boxed integers, floats,
etc., Threads, handles)...

### Inside 6model

An object has a header - STable, Flags, owner, slots for GC stuff...

Which points to an STable (Shared Table, representing a type).  It contains the
HOW (Meta-object), REPR, WHAT (type object), WHO (stash), Method cache, and
Type check cache..., which are objects important to the type.

Which has a "representation" (REPR) that manages the object's body.

### Representations

All about the use of memory by an object

REPR API has a common part (allocation, GC marking) along with several
sub-protocols for different ways of using memory:

Attributes Boxing Positional Associative

Representations are orthogonal to type (and thus disinterested in method
dispatch, type check, etc.) and also non-virtual (if you know the REPR, can
inline stuff).
