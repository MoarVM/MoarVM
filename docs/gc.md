# Garbage Collection in MoarVM

Garbage collection in MoarVM can be characterized as:

* Generational (two generations, the young one being know as the nursery)
* Parallel (multiple threads may participate in GC)
* Semi-space copying (only in the young generation)
* Stop the world (all threads are paused while collection takes place)
* Precise (we always know what is a pointer and what is not)

Finalization calls to free non-garbage-collectable resources happen asynchronously
with mutator execution.

## Thread Locality
Every thread has its own semi-space nursery and generation 2 size-separated
area. This is so it can do bump-pointer allocation and promotion to gen-2
without the need for synchronization during execution, and also as most
objects will be thread-local. This doesn't mean objects cannot be accessed
by other threads or have their only living reference known just by an object
in another thread's memory space.

## How Objects Support Collection
Each object has space for flags, some of which are used for GC-related purposes.
Additionally, objects all have space for a forwarding pointer, which is used
by the GC as it goes about copying.

## How Collection Is Started
For collection to begin, all threads must be paused. The thread that wishes to
initiate a collection races to set the in_gc flag in the MVM_Instance struct.
If it succeeds, it then visits all other threads and flags that they must suspend
execution and do a GC run. If it fails, then it was at a GC-safe point anyway,
so it just waits for everyone else to be.

At each GC-safe point, threads check in their thread-context struct to see if a
GC run needs to be started. It indicates that it has paused, and then proceeds
to add any relevant thread-local roots to the thread's work list. Note that any
roots that could possibly be touched by another thread must NOT be scanned at this
point, as another mutator thread could still be running and modify them, creating
potential for lost references.

Once all threads indicate they have stopped execution, the GC run can go ahead.

## Nursery Collections
Processing the worklist involves:

* Taking an item from the list (where an item is a pointer to an object reference,
  that is an `MVMCollectable **`, not just an `MVMCollectable *`)
* Ensuring it didn't already get copied; if so, just update the pointer to the new
  address of the object
* Seeing if it belongs to another thread, and if so putting it into a worklist for
  whatever thread is processing the allocating thread's GC, and continuing to the next
  item
* If it has survived a previous nursery collection, move it into the older generation
* Otherwise, copy it to tospace
* Update the pointer to point to the new location of the object
* Scanning the object and putting any object references that were not yet marked into
  the worklist

## Full Collections
Every so often there will be a full collection, and generation 2 will be collected as
well as the nursery. This is determined by looking at the amount of memory that has
been promoted to generation 2 relative to the overall heap size, and possibly other
factors (this has been tuned over time and will doubtless be tuned more; see the code).

## Write Barrier
All writes into an object in the second generation from an object in the nursery
must be added to a remembered set. This is done through a write barrier.

## MVMROOT

Being able to move objects relies on being able to find and update all of the
references to them. And, since MoarVM is written in C, that includes those
references on the C stack. Consider this bit of code, which is the (general,
unoptimized) path for boxing strings:

    MVMObject * MVM_repr_box_str(
        MVMThreadContext *tc,
        MVMObject *type,
        MVMString *val
    ) {
        MVMObject *res;
        MVMROOT(tc, val) {
            res = MVM_repr_alloc_init(tc, type);
            MVM_repr_set_str(tc, res, val);
        }
        return res;
    }

It receives `val`, which is a string to box. Note that strings are garbage-
collectable objects in MoarVM, and so may move. It then allocates a box of the
specified type (for example, Raku’s `Str`), and puts the string inside of it.
Since MVM_repr_alloc_init allocates an object, it may trigger garbage
collection. And that in turn may move the object pointed to by `val` – meaning
that the `val` pointer needs updating. The `MVMROOT` macro is used in order to add
the memory address of `val` on the C stack to the set of roots that the GC
considers and updates, thus ensuring that even if the allocation of the box
triggers garbage collection, this code won’t end up with an old `val` pointer.

There is also an `MVMROOT2`, `MVMROOT3`, and `MVMROOT4` macro to root 2, 3, or 4
things at once, up to 6, which saves an indentation level (and possibly a little
work, but C compilers are probably smart enough these days for it not to matter).
