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

* Taking an item from the list
* Ensuring it didn't already get copied; if so, ignore it
* Racing to write a busy value into the forwarding pointer
* If we lose the race, go to the next object in the list
* Scanning the object and putting any nursery pointers found and not yet copied into
  our work list
* If it has survived a previous nursery collection, move it into the older generation
* Otherwise, copy it to tospace (needs to care if the target tospace is not ours - since
  we expect most objects not to survive, probably OK to do synchronized operations to
  bump the tospace pointer)
* Finally, update any pointers we discovered that point to the now-moved objects

## Full Collections
Every N GC runs will be a full collection, and generation 2 will be collected as
well as generation 1.

## Write Barrier
All writes into an object in the second generation from an object in the nursery
must be added to a remembered set. This is done through a write barrier.
