# Garbage Collection in MoarVM

Garbage collection in MoarVM can be characterized as:

* Generational (two generations, the young one being know as the nursery)
* Parallel (multiple threads may participate in GC)
* Semi-space copying (only in the young generation)
* Mark-compact (only in the old generation)
* Stop the world (all threads are paused while collection takes place)
* Precise (we always know what is a pointer and what is not)

Finalization calls to free non-garbage-collectable resources happen asynchronously
with mutator execution.

## Thread Locality
Every thread has its own semi-space nursery. This is so it can do bump-pointer
allocation without the need for synchronization during execution, and also as
most objects will be thread-local. This doesn't mean objects in the nursery
cannot be accessed by other threads or have their only living reference known
just by an object in another thread's nursery.

The older generation is a shared space. It is organized into pages which will
each hold objects of a certain size.

## How Objects Support Collection
Each object has space for flags, some of which are used for GC-related purposes.
Additionally, objects all have space for a forwarding pointer, which is used
by the GC as it goes about copying or compaction.

## How Collection Is Started
For collection to begin, all threads must be paused. The thread that wishes to
initiate a collection races to set the in_gc flag in the MVM_Instance struct.
If it suceeds, it then visits all other threads and flags that they must suspend
execution and do a GC run. If it fails, then it was at a GC-safe point anyway,
so just waits for everyone else to be.

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
* Scaning the object and puttng any nursery pointers found and not yet copied into
  our work list
* If it has survived a previous nursery collection, move it into the older generation
* Otherwise, copy it to tospace (needs care if the target tospace is not ours - since
  we expect most objects not to survive, probably OK to do synchronized operations to
  bump the tospace pointer)
* Finally, update any pointers we discovered that point to the now-moved objects

## Full Collections
A full collection is triggered if the nursery promotion into the old generation
brings avilable space within a given threshold and it's been long enough since the
last full collection.

XXX Describe compaction.

If compaction fully clears a page, it is returned to the page buffer. In cases
where the page buffer has an excess of pages, it may choose to return memory to
the OS.

## Write Barrier
All writes into an object in the second generation from an object in the nursery
must be added to a remembered set. This is done through a write barrier.
