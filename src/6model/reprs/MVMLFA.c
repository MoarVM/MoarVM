#include "moarvm.h"

/* Lock-free dynamic array. Loosely based on the public-domain
 * lock-free hash in Java by Cliff Click: NonBlockingHashMap.java */

 /* get a fresh lfa from the thread-local cache or allocate */
MVMLFA * MVM_LFA_acquire_lfa(MVMThreadContext *tc) {
    MVMLFA *lfa = tc->lfa_cache;
    if (lfa) {
        tc->lfa_cache = lfa->next_lfa;
        memset(lfa, 0, sizeof(MVMLFA));
        return lfa;
    }
    return calloc(sizeof(MVMLFA), 1);
}

/* We know this lfa has no active handles, so we're releasing
 * it either to our thread-local cache list or freeing it if
 * the cache is full. */
void MVM_LFA_release_lfa(MVMThreadContext *tc, MVMLFA *lfa) {
    MVMLFA *head = tc->lfa_cache;
    if (head) {
        /* XXX test and tune this magic constant... */
        if (head->elems < 20) {
            /* if there's room in the cache */
            lfa->next_lfa = head;
            lfa->elems = head->elems + 1;
            tc->lfa_cache = lfa;
        }
        else {
            /* wow; there must be something very unbalanced
             * going on; this list shouldn't get bigger than
             * several items, since the threads that release
             * lfas are the ones that are the ones that
             * acquire them, though I suppose if there is one
             * very long transaction that blocks the reaping
             * of a bunch of other ones, . */
            free(lfa);
        }
    }
    else { /* add a head */
        lfa->next_lfa = NULL;
        lfa->elems = 0; /* zeroeth in the cache chain */
        tc->lfa_cache = lfa;
    }
}

/* decrement the refcount of an lfa.  */
static AO_t MVM_LFA_release_read_handle(MVMThreadContext *tc, MVMLFA *lfa) {
    if (lfa == NULL)
        MVM_exception_throw_adhoc(tc, "internal failure in MVMLFA");
    return MVM_ATOMIC_DECR(&lfa->refcount);
}

/* increment the refcount of an lfa.  */
static AO_t MVM_LFA_acquire_read_handle(MVMThreadContext *tc, MVMLFA *lfa) {
    return MVM_ATOMIC_INCR(&lfa->refcount);
}

/* decrement the read barrier count of an lfa.  */
static AO_t MVM_LFA_release_read_barrier(MVMThreadContext *tc, MVMLFA *lfa) {
    return MVM_ATOMIC_DECR(&lfa->prev_refcount);
}

/* increment the read barrier count of an lfa.  */
static AO_t MVM_LFA_acquire_read_barrier(MVMThreadContext *tc, MVMLFA *lfa) {
    return MVM_ATOMIC_INCR(&lfa->prev_refcount);
}

/* Read the next LFA. Obtain a read handle (and release
 * the previous one) if requested */
static MVMLFA * MVM_LFA_next_lfa(MVMThreadContext *tc, MVMLFArray *toparr, MVMLFA *lfa, MVMuint8 get_read_handle) {
    MVMLFA *new_lfa = (MVMLFA *)MVM_ATOMIC_GET(&lfa->next_lfa);
    /* if we found an lfa to the right and a read handle was
     * required, try to get one. This always succeeds
     * since we already have a handle on lfa, and nothing will
     * try to recycle or free something to the right of one
     * that's still referenced, because they're always recycled/
     * freed from left to right. */
    if (new_lfa && get_read_handle) {
        MVM_LFA_acquire_read_handle(tc, new_lfa);
        MVM_LFA_release_read_handle(tc, lfa);
    }
    return new_lfa;
}

/* Return the previous LFA. Always obtain a read handle */
static MVMLFA * MVM_LFA_prev_lfa(MVMThreadContext *tc, MVMLFArray *toparr, MVMLFA *lfa) {
    MVMLFA *new_lfa, *first_lfa = (MVMLFA *)MVM_VOLATILE_GET(&lfa->prev_lfa);
    new_lfa = first_lfa;

    if (!new_lfa)
        /* don't bother doing anything if it's already NULL.
         * Also, this ensures no one tries to get a read
         * handle after the reaper set this NULL, so the
         * prev_refcount eventually (quickly!?) drops to zero
         * permanently. */
        return NULL;

    /* explicitly denote that the previous pointer we obtained
     * cannot be trusted to still be valid, since we didn't
     * assert a read barrier yet. */
    new_lfa = NULL;

    /* signal to any reaper threads that we're about to
     * dereference (and then obtain a handle on) the prev.
     * This prevents them from reaping what we dereference.
     * Basically a handle on the pointer prev_lfa. */
    MVM_LFA_acquire_read_barrier(tc, lfa);

    /* read it again in case it was nulled.  This ensures that
     * the read barrier we just asserted pertains to the correct
     * value, since the previous lfa is guaranteed not to be
     * reaped while we have this read barrier. */
    if (new_lfa = (MVMLFA *)MVM_VOLATILE_GET(&lfa->prev_lfa)) {
        /* get a read handle. */
        MVM_LFA_acquire_read_handle(tc, new_lfa);

        /* we're done trying no matter what happened */
        if (MVM_LFA_release_read_barrier(tc, lfa) == 1
        /* if we were the last to decrement it, then prev_lfa
         * has *just* been nulled, and we are in a race with
         * another thread to get rid of our read handle. */
        && MVM_LFA_release_read_handle(tc, new_lfa) == 1) {
            /* we lost the race; we get to reap it. */
            MVM_LFA_release_lfa(tc, new_lfa);
            new_lfa = NULL;
        }
    }
    else {
        /* prev_lfa was just nulled; release the read barrier.
         * We can't do anything with first_lfa since we don't
         * have a lock on it. */
        MVM_LFA_release_read_barrier(tc, lfa);
    }

    return new_lfa;
}

/* Return the newest LFA, with read handle and all. See the explanation
 * in the comments in MVMLFArray.h */
static MVMLFA * MVM_LFA_newest_lfa(MVMThreadContext *tc, MVMLFArray *toparr) {
    MVMLFA *second_lfa, *first_lfa = MVM_LFARRAY_LFA(toparr);
    MVMLFA *lfa;
    /* gather whether it's odd */
    MVMuint8 second_odd, first_odd = ((uintptr_t)first_lfa & 1) ? 1 : 0;
    /* increment the proper ref counter */
    MVM_ATOMIC_INCR(first_odd
        ? &toparr->body.odd_refcount
        : &toparr->body.even_refcount);
    /* read lfa again */
    second_lfa = MVM_LFARRAY_LFA(toparr);
    second_odd = ((uintptr_t)second_lfa & 1) ? 1 : 0;

    /* if the oddness changed */
    if (first_odd != second_odd) {
        /* also grab the other lock; we need to hold on to
         * the first one too until we get the other one. */
        MVM_ATOMIC_INCR(second_odd
            ? &toparr->body.odd_refcount
            : &toparr->body.even_refcount);
        /* now we've locked both values; we don't need
         * the first one. */
        /* we don't need to read lfa again because there's
         * no way it could have changed since we read it
         * the second time, because we had the other one
         * locked. */
    }
    /* we've definitely locked at least the right one, and
     * at least the second one is the right one. */
    lfa = (MVMLFA *)CLEAR_LOW2(second_lfa);
    /* get the desired read handle */
    MVM_LFA_acquire_read_handle(tc, lfa);

    /* discard our locks on the references. */
    MVM_ATOMIC_DECR(first_odd
        ? &toparr->body.odd_refcount
        : &toparr->body.even_refcount);
    if (first_odd != second_odd)
        /* also release the other handle */
        MVM_ATOMIC_DECR(second_odd
            ? &toparr->body.odd_refcount
            : &toparr->body.even_refcount);
    return lfa;
}

static AO_t MVM_LFA_attempt_lfa(MVMThreadContext *tc, MVMLFArray *toparr, MVMLFA *lfa, MVMLFA *new_lfa, MVMuint64 idx) {

    /* spin if the last one is still in the process of
     * queueing. */
    /* while (MVM_ATOMIC_GET(&lfa->next_lfa) == 1); */

    if (MVM_CAS(&lfa->next_lfa, NULL, new_lfa)) {
        void *addr;
        /* our new transaction won! */

        /* see the notes in MVMLFArray.h for gory details. */

        /* now we need to atomically/safely update the head
         * lfa pointer on the main array object. */

        /* no one else can possibly be modifying this value
         * since we're the one staging the transaction,
         * so we don't need a volatile read. */
        AO_t even = ((uintptr_t)toparr->body.lfa) & 1 ? 0 : 1;

        /* for help_check_promote below */
        MVM_LFA_acquire_read_handle(tc, new_lfa);

        /* put the new lfa reference in place, flipping its
         * lowest bit */
        MVM_ATOMIC_SET(&toparr->body.lfa, ((uintptr_t)new_lfa | even));

        /* this should be a very short wait; old reader threads
         * (of ->lfa) just need to be scheduled, read a pointer,
         * increment an offset of a dereference of it,
         * then decrement a refcount or two. .. this is *Justin
         * Case* there is something that just happens to be
         * trying to read and dereference lfa at the exact
         * same time we're swapping it out. highly unlikely,
         * imho. Don't yield the thread; just spin. Maybe change
         * it to yield later? */
        addr = even
            ? &toparr->body.even_refcount
            : &toparr->body.odd_refcount;

        while (MVM_VOLATILE_GET(addr));

        /* clear the way for additional transactions. It is
         * crucial to wait until any/all threads have finished
         * getting their read handles on the last head lfa
         * because if we don't, the lfa could be completed and
         * reaped out from under them before they have a chance
         * to dereference, thus giving them a bad pointer. */
        MVM_ATOMIC_SET(&new_lfa->next_lfa, NULL);

        /* now do our transaction */
     //   MVM_LFA_help_and_cleanup(tc, toparr, lfa, new_lfa, idx);
        /* this function released any read handles */

        /* signal success */
        return 1;
    }
    /* signal failure */
    return 0;
}

static MVMObject * MVM_LFA_apply_transaction(MVMThreadContext *tc, MVMLFArray *toparr, MVMLFA *lfa, MVMuint64 idx);

/* get the most recent value at a particular index from a LFArray,
 * helping catch up that slot in its transactions if necessary;
 * returns the value and sets lfa_out to the lfa in which the value
 * was found, so put can write there.  lfa_out is guaranteed to be
 * the last lfa in the chain extremely recently before returning.
 * Finding the proper index and
 * table from which to grab the value is tricky. The general strategy
 * is to first find the tail of the lfa chain, then work backwards
 * until a non-null, non-tombstone value is reached, or if it hits
 * the oldest lfa that can contain a value that would have been
 * copied to the slot in question, then work forwards from there to
 * make sure we didn't skip over the value while it was being copied
 * and hit its tombstone in a prior lfa, helping copy the chain if
 * we find a value in either case. We have to help it copy all the
 * way to the end, since the index could disappear and reappear from
 * the table in the course of the chain since we read the last lfa in
 * the chain. */
MVMObject * MVM_LFA_get_slot(MVMThreadContext *tc, MVMLFArray *toparr, MVMLFA *start_lfa, MVMLFA **lfa_out, MVMuint64 idx, MVMuint8 moving_right) {

    /* save the idx to orig_idx */
    MVMuint64 orig_idx = idx, new_idx;
    MVMLFA *lfa, *n, *m, *orig_lfa;
    MVMObject *V;
    /* moving_right == 0 means start off moving left;
     * 1 means moving right left of the orig_lfa;
     * 2 means moving right at or past the orig_lfa-> */

  start:
    /* if we were passed an lfa to start in, we know the lfa contains
     * the index, and we want to move right from there, so don't traverse
     * it all the way to the end */
    if (start_lfa != NULL)
        /* the lfa we've been passed already has a read handle */
        orig_lfa = lfa = start_lfa;

    else { /* single-threaded path */
        lfa = MVM_LFA_newest_lfa(tc, toparr);

        /* traverse the lfa chain to the newest, store in lfa. We
         * will almost always be at the end already, the only time
         * we won't is between the time a new lfa CAS's itself to the
         * right of the newest one and the time it updates lfa of
         * toparr. */
        while (n = MVM_LFA_next_lfa(tc, toparr, lfa, 1))
            lfa = n;

        /* stash the original lfa */
        orig_lfa = lfa;

        /* check whether the requested index exists in the range - 1 */
        if (idx >= lfa->elems) {
            moving_right = 2;
            /* find any lfa that contains the index */
            do {
                /* if we're moving right, the index could exist in a
                 * later lfa, so verify this is still the last one. */
                if (!(n = MVM_LFA_next_lfa(tc, toparr, lfa, 1))) {
                    /* single-threaded path */
                    /* we know for a fact that this is the newest
                     * table and the index doesn't exist in it,
                     * so return NULL */
                    V = NULL;
                    goto done;
                }
                /* the index could exist in a later lfa, so advance
                 * to the right. */
                lfa = n;
                /* try again in the next table */
            } while (idx >= lfa->elems);
        }
    }

    /* the index fits within the range of this lfa-> */
    /* Loop, reading the current value in the prescribed slot,
     * traversing in the prescribed direction, default first left to
     * the end of where the needed data could possibly appear, then
     * right again in case it appeared in the table later. */
    while (1) {
        MVMuint8 is_prime;
        /* read the value in the slot -
         * If it's done, read and use the value read from slots. */

        /* if the transaction is not complete for this index in
         * this lfa, either move left or complete the transaction. */
        if (!MVM_LFA_ISDONE(lfa)) {
            /* single-threaded never does this */
            /* if we're moving left, we're still hunting for the
             * leftmost incomplete lfa */
            if (moving_right == 0)
                /* go ahead and try to move left in case the non-done
                 * section(s) include(s) the index we want. */
                goto move_left;
            /* Since we're moving right, we know that all prior
             * transactions that could possibly have affected this slot
             * have been applied.  So apply this one for this slot. */
            V = MVM_LFA_apply_transaction(tc, toparr, lfa, idx);
        }
        else /* single-threaded path */
            V = MVM_LFA_SLOT(lfa, idx);

        /* if the value is NULL, copying hasn't reached this slot
         * or a specified transaction hasn't yet been applied to the
         * slot, so go backwards in the lfa chain */
        if (V == NULL) {
            /* if we're moving right */
            if (moving_right != 0)
                /* try to move right since we found a NULL */
                goto move_right_continue;

            /* we're moving left; try to move left to find
             * a non-NULL. */
            goto move_left;
        }

        /* if the value is tombstone, read next_lfa again,
         * and if next_lfa is not NULL, try again in the new table.
         * Otherwise, return NULL, because if
         * a tombstone is there, any copying or erasing for that slot
         * in this table has definitely already been accomplished.
         * This is guaranteed because all copying transactions on the
         * slot leave a tombstone here when they're done, and the
         * lfa promotion operation puts a tombstone in all slots in
         * the table. */
        if (MVM_LFH_IS_TOMBSTONE(tc, V))
            goto move_right; /* single threaded never does this */

        /* consider re-enabling the following section after discussion
         * with experts. */
        goto skip_2;

        is_prime = MVM_LFH_ISPRIME(tc, V);
        /* if the value is a prime, copying for this slot has started;
         * help copy the slot, then try again in the new table. Reset
         * the idx to orig_idx if we're moving right past the orig.
         * Or, assignment to the slot is occuring. */

        /* traverse all the way to the right to make sure there isn't
         * a start-changing transaction pending */
        /* cache the first lookup to the right */
        m = lfa;
        if (m = n = MVM_LFA_next_lfa(tc, toparr, m, 0))
            /* single threaded never does this */
            do {
                /* if it's start-changing */
                /* XXX actually this should adjust idx as it goes right
                 * and allow start to change, but test whether the new
                 * index fits in the newer lfa. An optimization. */
                if (m->start != lfa->start
                /* or the idx doesn't exist later in the chain now */
                || idx >= m->elems
                /* or if it's a splice */
                || MVM_LFA_ISSPLICE(m))
                    /* we must restart because there could be a whole
                     * new value chain being written in the later
                     * sections of the chain */
                    goto restart;
            } while (m = MVM_LFA_next_lfa(tc, toparr, m, 0));

        /* if it was prime, it's an assignment or copying
         * transaction here, so just return the value unprimed. */
        if (is_prime)
            V = (MVMObject *)CLEAR_LOW2(V);

      skip_2:
        /* V is the value that was [to be] in the requested slot
         * at the time of the request. There may be later transactions
         * staged to assign to this slot since we started traversing,
         * but right now we're not worrying about those, since it
         * could cause big delays in returning values if the traverser
         * is continually chasing the end of the transaction chain.
         * Even if it's prime, return it like that. */

        /* return the value */
        goto done;


      move_left:
    /* read the prev_lfa */
        if (!(n = MVM_LFA_prev_lfa(tc, toparr, lfa)))
    /* if it's NULL, we have been promoted to the top lfa,
     * but the value could have been moved from a prior lfa to
     * ours or a newer one between reading V=NULL and reading
     * prev_lfa, so start over in the same table, except set the
     * moving_right flag. */
            /* the index slot must exist in this table since we
             * already checked it. */
            goto move_right_begin;

    /* if prev_lfa wasn't NULL, we need to keep traversing them
     * backwards until we find the earliest one that could contain
     * the value that would eventually end up in the desired slot.
     * So, adjust our index (if necessary) to where it would have
     * been copied from the prev_lfa, to new_idx: */

        /* remember, n is the one to the left, the older one */
        if (n->start != lfa->start) {
            /* if start moved left (from lfa to n) or rolled back..
             * (if a shift occurred from n to lfa) */
            if (n->start == lfa->start - 1
            || (n->start == n->ssize - 1
                && lfa->start == 0))
                new_idx = idx + 1;
            else
                /* if idx is zero here, let it overflow, so
                 * that we start moving right below */
                new_idx = idx - 1;
        }
        /* if the newer lfa is a splice, */
        else if (MVM_LFA_ISSPLICE(lfa)) {
            MVMuint64 arr_length = (MVMuint64)
                REPR(lfa->copy_item)->pos_funcs->elems(tc,
                    STABLE(lfa->copy_item), lfa->copy_item,
                    OBJECT_BODY(lfa->copy_item));
            MVMuint64 tail_start = lfa->copy_start
                + arr_length;
            /* if the index is in the tail that possibly moved */
            if (idx >= tail_start)
                if (arr_length > lfa->copy_count)
                    new_idx = (idx - lfa->copy_count) + arr_length;
                else
                    new_idx = idx - lfa->copy_count - arr_length;
        }
    /* Then check to see whether that index can even fit in the
     * range of the prev_lfa->  If so, set lfa to prev_lfa, set
     * idx to new_idx, and goto label2. */
        if (new_idx < n->elems) {
            /* keep the read handle on both lfa and n */
            lfa = n;
            idx = new_idx;
            continue;
        }
        /* release the read handle on n we just obtained */
        if (MVM_LFA_release_read_handle(tc, n) == 1) {
            /* we were the last to release it */
            MVM_LFA_release_lfa(tc, n);
            n = NULL;
        }

        /* fall through to move_right_begin */

    /* If not, we've found the left boundary of what
     * could affect the desired slot, so set the moving-right flag
     * and try again in the same current table (not the prev_lfa
     * we were just analyzing). */
      move_right_begin:
        moving_right = lfa == orig_lfa ? 2 : 1;
        /* This should take us to the apply_transaction then
         * move right, unless it completes before then. */
        continue;

      move_right:
        if (moving_right == 0)
            moving_right = 1; /* at least */

      move_right_continue:
        if (!(n = MVM_LFA_next_lfa(tc, toparr, lfa, 1)))
            /* single-threaded: n is always NULL */
            /* we hit the end of the line on the right;
             * definitely no value in this index... */
            V = NULL;
            goto done; /* single-threaded path! */

        /* mark that we've passed the orig lfa, so don't
         * apply copy offsets from now on; always reset
         * to orig_idx */
        if (n == orig_lfa) {
            moving_right = 2;
            idx = orig_idx;
        }
        else if (moving_right == 1) {
            /* one invariant here is that the items in the
             * lowest part of the represented array never
             * move in a copy. An unshift always inserts
             * a new item to the left of the last start.
             * Start moves only with a shift or unshift.
             * Someday make it move with a splice of an
             * empty array..? */
            /* if start moved */
            if (n->start != lfa->start) {
                /* if start moved right or rolled over..
                 * (if a shift occurred) */
                if (n->start > lfa->start
                || (lfa->start == lfa->ssize - 1
                    && n->start == 0))
                    /* we're safe because idx can never
                     * equal zero here, because we know
                     * that we would have never moved
                     * left past an lfa that couldn't
                     * have contained data leading to
                     * the requested index */
                    idx--;
                else
                    idx++;
            }

            /* if the newer lfa is a splice, */
            else if (MVM_LFA_ISSPLICE(n)) {
                MVMuint64 arr_length = (MVMuint64)
                    REPR(n->copy_item)->pos_funcs->elems(tc,
                        STABLE(n->copy_item), n->copy_item,
                        OBJECT_BODY(n->copy_item));
                MVMuint64 tail_start = n->copy_start
                    + n->copy_count;
                /* if the index is in the tail that possibly moved */
                if (idx >= tail_start)
                    if (arr_length > n->copy_count)
                        idx -= arr_length - n->copy_count;
                    else
                        idx += n->copy_count - arr_length;
            }
            lfa = n;
            continue;
        }
        /* do the move */
        lfa = n;
        /* else, moving_right == 2, so need to loop next_lfa until
         * hitting one that can contain the index, if any. */
        /* if the index doesn't exist in this lfa */
        if (idx >= lfa->elems)
            /* find any lfa that contains the index */
            do {
                if (!(n = MVM_LFA_next_lfa(tc, toparr, lfa, 1))) {
                    /* we know for a fact that this is the newest
                     * table and the index doesn't exist in it,
                     * so return NULL */
                    V = NULL;
                    goto done;
                }
                /* the index could exist in a later lfa, so advance
                 * to the right. */
                lfa = n;
                /* try again in the next table */
            } while (idx >= lfa->elems);
        /* found an lfa that contains the index, so try again
         * in that one */
        /* continue; */
    }
  restart:
    if (!moving_right)
        moving_right = (lfa == orig_lfa) ? 2 : 1;

    /* release any read handles we obtained */
    if (moving_right == 2) {
        /* we have only one read handle */
        if (MVM_LFA_release_read_handle(tc, lfa) == 1)
            MVM_LFA_release_lfa(tc, lfa);
    }
    else
        while (lfa) {
            n = MVM_LFA_next_lfa(tc, toparr, lfa, 0);
            if (MVM_LFA_release_read_handle(tc, lfa) == 1)
                MVM_LFA_release_lfa(tc, lfa);
            lfa = n;
        }
    lfa = n = NULL;
    V = NULL;
    moving_right = 0;
    idx = orig_idx;
    goto start;

  done:
    *lfa_out = lfa;
    /* take an additional read handle on this lfa for the caller */
    MVM_LFA_acquire_read_handle(tc, lfa);

    /* release any read handles we obtained */
    if (moving_right == 2)
        /* we have only one read handle */
        MVM_LFA_release_read_handle(tc, lfa);
    else
        while (lfa) {
            n = MVM_LFA_next_lfa(tc, toparr, lfa, 0);
            MVM_LFA_release_read_handle(tc, lfa);
            lfa = n;
        }

    return V;
}

/* put a value in an index in the table if it matches the expected.
 * Returns what was there previously.  The overall strategy here is:
 * if the put transaction will fit in the newest lfa's elems range,
 * follow the get_impl logic to catch up that slot; attempt to CAS
 * the change in the last table, starting over from scratch if it
 * fails. If the put won't fit in the current elems range,
 * but it will fit in the ssize range if elems is extended, prepare
 * a new lfa with the transaction, then if CAS'ing it succeeds,
 * back up and catch up that slot if necessary, then apply the
 * transaction; if CAS'ing it failed, start over from scratch in the
 * new table if there is one, otherwise try the write again in the
 * same table. Note: we do not care about the so-called "ABA
 * problem," because in the scenario described, we do not know (and
 * therefore cannot care) which thread started a transaction first,
 * so both could read the same original before CAS'ing, and the
 * intermediate value from the other thread could be placed there
 * and truly be in the table for some time before it is replaced by
 * the 3rd value, and then reverted to the original - basically, if
 * the table is shared, the user must be aware of the race
 * condition. Therefore, this thread-safe dynamic array
 * implementation is no more susceptible to the ABA prblem than a
 * normal shared dynamic array that uses a lock for synchronization.
 * If it won't fit in ssize, a table copy is needed -
 * prepare a new lfa with the transaction, if CAS succeeds, complete
 * the transaction, else start over from scratch in the new lfa-> */
static MVMObject * MVM_LFA_put_if_match(MVMThreadContext *tc, MVMLFArray *toparr, MVMLFA *lfa, MVMuint64 idx, MVMObject *putval, MVMObject *expVal, MVMuint8 unshift, MVMuint8 try_once, MVMuint8 *success) {
    MVMObject *V;
    MVMLFA *new_lfa = NULL;
    MVMuint8 new_slots = 0;

    while (1) {
        /* find the last lfa in the chain, and the value in it for that
         * index, if any. Get slot catches up all previous transactions
         * on that index up to the latest for that slot on lfa, to which
         * it assigns the latest lfa-> V is guaranteed not to be
         * tombstone or a prime. It can be NULL. */
        /* if lfa is non-null, we have a read handle on it */
        V = MVM_LFA_get_slot(tc, toparr, lfa, &lfa, idx, (lfa ? 2 : 0));
        /* this returned an lfa with a read handle on it. */

        /* if the index fits within the existing elements range, just
         * assign it. See note on "ABA problem" above. */
        if (!unshift && idx < lfa->elems) {
            /* if there's no work to do, don't do any work */
            if (putval == V
            /* Do we care about expected-Value at all? */
            || !MVM_LFH_IS_NO_MATCH_OLD(tc, expVal) &&
            V != expVal && /* No instant match already? */
            (!MVM_LFH_IS_MATCH_ANY(tc, expVal) ||
                MVM_LFH_IS_TOMBSTONE(tc, V) ||
                V == NULL) &&
                /* Match on null/TOMBSTONE combo */
            !(V == NULL && MVM_LFH_IS_TOMBSTONE(tc, expVal)) &&
            (expVal == NULL) ) {
                /* MSW - I took out the original .equals comparison since
                 * it doesn't make sense in moar/p6 land.
                 * Eventually support repr-provided equals */
                goto done;
            }

            if (MVM_CAS(&MVM_LFA_SLOT_LOC(lfa, idx), V, putval)) {
                /* CAS succeeded, if there was a copier also trying to
                 * put a prime here, we beat them, and
                 * they'll have to use the new value. */
                *success = 1;
                goto done;
            }
            /* CAS failed; get the newer value and try again */
            if (try_once)
                goto done;
        }
        else {
            /* V is obviously NULL since it didn't fit in the table */
            if (expVal != NULL && !MVM_LFH_IS_MATCH_ANY(tc, expVal)) {
                V = NULL;
                goto done;
            }

            /* we will need a new lfa transaction record, since we need
             * to extend the represented array's range. */
            new_lfa = new_lfa ? new_lfa : MVM_LFA_acquire_lfa(tc);
            new_lfa->start = lfa->start;
            /* XXX check whether idx is max uint64, I *suppose*.... */
            new_lfa->elems = unshift
                ? lfa->elems + 1
                : idx + 1;
            new_lfa->copy_item = putval;
            new_lfa->copy_start = idx;
            new_lfa->copy_count = 1;
            new_lfa->prev_lfa = lfa;
            /* block the next from being placed until readers of prior
             * one are done getting read handles on it */
            new_lfa->next_lfa = (MVMLFA *)1;
            new_lfa->refcount = 1;

            /* if it will fit in the existing backing array */
            if (!unshift && idx < lfa->ssize
            || unshift && lfa->elems < lfa->ssize) {
                /* share the previous slots, and mark it completed. */
                if (new_slots)
                    free(MVM_LFA_SLOTS(new_lfa));
                new_lfa->slots =
                    (MVMObject **)((uintptr_t)MVM_LFA_SLOTS(lfa) | 1);
                /* initialize the range-extending put transaction */
                new_lfa->ssize = lfa->ssize;
                if (unshift)
                    new_lfa->start = new_lfa->start > 0
                        ? new_lfa->start - 1
                        : new_lfa->ssize - 1;
                new_slots = 0;
            }
            else { /* we need a copying/growth transaction */
                /* XXX don't just double without bounds; after a certain
                 * size, start adding in increments of 10MB or something */
                AO_t new_size = lfa->ssize;
                while (new_size <= new_lfa->elems)
                    new_size *= 2;
                if (new_slots) {
                    /* if we can't reuse the old one */
                    if (new_size > new_lfa->ssize) {
                        free(MVM_LFA_SLOTS(new_lfa));
                        new_lfa->slots = calloc(
                            new_size, sizeof(MVMObject *));
                    }
                    /* otherwise just keep the existing one */
                }
                else
                    new_lfa->slots = calloc(
                        new_size, sizeof(MVMObject *));
                new_lfa->ssize = new_size;
                new_slots = 1;
            }
            if (MVM_LFA_attempt_lfa(tc, toparr, lfa, new_lfa, idx)) {
                *success = 1;
                return NULL;
            }
            if (try_once) {
                V = NULL;
                goto done;
            }
        }
        /* try again, telling get_slot to start from lfa, moving
         * right.. */
    }
  done:
    MVM_LFA_release_read_handle(tc, lfa);
    if (new_lfa) {
        if (new_slots)
            free(MVM_LFA_SLOTS(new_lfa));
        free(new_lfa);
    }
    return V;
}

/* push */
void MVM_LFA_push(MVMThreadContext *tc, MVMLFArray *toparr, MVMObject *item) {
    MVMuint8 success = 0;
    while (1) {
        MVMLFA *lfa = MVM_LFA_newest_lfa(tc, toparr);
        MVM_LFA_put_if_match(tc, toparr, lfa, lfa->elems, item,
            MVM_LFH_MATCH_ANY, 0, 1, &success);
        if (success)
            break;
        MVM_LFA_release_read_handle(tc, lfa);
    }
}

/* unshift */
void MVM_LFA_unshift(MVMThreadContext *tc, MVMLFArray *toparr, MVMObject *item) {
    MVMuint8 success = 0;
    MVMLFA *lfa = MVM_LFA_newest_lfa(tc, toparr);
    MVM_LFA_put_if_match(tc, toparr, lfa, 0, item,
        MVM_LFH_MATCH_ANY, 1, 0, &success);
}

/* pop */
MVMObject * MVM_LFA_pop(MVMThreadContext *tc, MVMLFArray *toparr) {
    MVMLFA *last_lfa = (MVMLFA *)1, *new_lfa = NULL;
    while (1) {
        MVMLFA *lfa = MVM_LFA_newest_lfa(tc, toparr);
        MVMObject *V = (MVMObject *)CLEAR_LOW2(
            MVM_LFA_get_slot(tc, toparr, lfa, &lfa, lfa->elems - 1, 2));
        if (last_lfa != lfa) {
            new_lfa = new_lfa ? new_lfa : MVM_LFA_acquire_lfa(tc);
            new_lfa->start = lfa->start;
            new_lfa->elems = lfa->elems - 1;
            new_lfa->ssize = lfa->ssize;
            new_lfa->slots = (MVMObject **)((uintptr_t)MVM_LFA_SLOTS(lfa) | 1);
            new_lfa->prev_lfa = lfa;
            /* block the next from being placed until readers of prior
             * one are done getting read handles on it */
            new_lfa->next_lfa = (MVMLFA *)1;
            new_lfa->refcount = 1;
        }
        if (MVM_LFA_attempt_lfa(tc, toparr, lfa, new_lfa, new_lfa->elems - 1))
            return V;
        last_lfa = lfa;
    }
}

/* pop */
MVMObject * MVM_LFA_shift(MVMThreadContext *tc, MVMLFArray *toparr) {
    MVMLFA *last_lfa = (MVMLFA *)1, *new_lfa = NULL;
    while (1) {
        MVMLFA *lfa = MVM_LFA_newest_lfa(tc, toparr);
        MVMObject *V = (MVMObject *)CLEAR_LOW2(
            MVM_LFA_get_slot(tc, toparr, lfa, &lfa, 0, 2));
        if (last_lfa != lfa) {
            new_lfa = new_lfa ? new_lfa : MVM_LFA_acquire_lfa(tc);
            new_lfa->start = lfa->start == lfa->ssize - 1
                ? 0
                : lfa->start + 1;
            new_lfa->elems = lfa->elems - 1;
            new_lfa->ssize = lfa->ssize;
            new_lfa->slots = (MVMObject **)((uintptr_t)MVM_LFA_SLOTS(lfa) | 1);
            new_lfa->prev_lfa = lfa;
            /* block the next from being placed until readers of prior
             * one are done getting read handles on it */
            new_lfa->next_lfa = (MVMLFA *)1;
            new_lfa->refcount = 1;
        }
        if (MVM_LFA_attempt_lfa(tc, toparr, lfa, new_lfa, new_lfa->elems - 1))
            return V;
        last_lfa = lfa;
    }
}

/* at_pos */
MVMObject * MVM_LFA_at_pos(MVMThreadContext *tc, MVMLFArray *toparr, MVMuint64 idx) {
    MVMLFA *lfa = NULL;
    MVMObject *V = (MVMObject *)CLEAR_LOW2(
        MVM_LFA_get_slot(tc, toparr, lfa,
            &lfa, (MVMuint64)idx, 0));
    return V;
}

/* at_pos */
void MVM_LFA_bind_pos(MVMThreadContext *tc, MVMLFArray *toparr, MVMuint64 idx, MVMObject *value) {
    MVMLFA *lfa = NULL;
    MVMuint8 success;
    MVM_LFA_put_if_match(tc, toparr, lfa, idx, value,
        MVM_LFH_MATCH_ANY, 0, 0, &success);
}

/* elems */
MVMuint64 MVM_LFA_elems(MVMThreadContext *tc, MVMLFArray *toparr) {
    MVMLFA *lfa = MVM_LFA_newest_lfa(tc, toparr);
    MVMuint64 elems = lfa->elems;
    MVM_LFA_release_read_handle(tc, lfa);
    return elems;
}

/* set_elems */
void MVM_LFA_set_elems(MVMThreadContext *tc, MVMLFArray *root, MVMuint64 count) {

}

/* splice - injects an array into another array at a certain point,
 * possibly replacing any amount of the existing items. This can be
 * done without a copying transaction, by first copying *either the
 * head or tail* (which one's shorter) into position, RTL or LTR
 * depending on which direction it's moving. Note this copying must
 * know *which* copy_index it's attempting to update, so it
 * doesn't double-count the slot copy if the source object matches
 * what's already in the destination slot and two threads notice
 * this at the same time and try to increment copy_index.  This
 * helps elucidate a general principle of all of the transactions
 * applied in these transaction records - each type has a well-
 * defined, monotonic sequence of operations, and a reader or
 * writer can always know whether a given transaction's activity
 * has been recorded for a given cell. */
void MVM_LFA_splice(MVMThreadContext *tc, MVMLFArray *toparr, MVMObject *from, MVMuint64 offset, MVMuint64 count) {
    MVMLFA *lfa, *new_lfa, *n;
    MVMObject *nah = NULL;
    MVMuint64 from_elems = REPR(from)->pos_funcs->elems(
        tc, STABLE(from), from, OBJECT_BODY(from));

    new_lfa = MVM_LFA_acquire_lfa(tc);

    new_lfa->copy_item = from;
    new_lfa->copy_start = offset;
    new_lfa->copy_count = count;

    while (1) {
        lfa = MVM_LFA_newest_lfa(tc, toparr);

        new_lfa->start = lfa->start;
        new_lfa->prev_lfa = lfa;

        new_lfa->elems = offset >= lfa->elems
            /* the insertion point is at or past the end of the array,
             * so count is really irrelevant; just assume it's
             * replacing NULLs. */
            ? offset + from_elems
            : lfa->elems - count + from_elems;

        new_lfa->ssize = lfa->ssize;
        if (new_lfa->elems > new_lfa->ssize) {
            /* ugh; need to grow the array *and* splice */
            do { /* XXX don't just blindly double eventually */
                new_lfa->ssize *= 2;
            } while (new_lfa->elems >= new_lfa->ssize);
            new_lfa->slots = calloc(
                new_lfa->ssize, sizeof(MVMObject *));
        }
        else {
            new_lfa->slots = MVM_LFA_SLOTS(lfa);
        }
        /* mark it as a splice */
        new_lfa->slots = (MVMObject **)(
            (uintptr_t)new_lfa->slots | 2);

        /* attempt the cas */
        if (MVM_LFA_attempt_lfa(tc, toparr, lfa, new_lfa, new_lfa->elems - 1))
            return;
    }
}

/* Apply the current LFA's transaction(s) enough to get the value at the given
 * index. */
static MVMObject * MVM_LFA_apply_transaction(MVMThreadContext *tc, MVMLFArray *toparr, MVMLFA *lfa, MVMuint64 idx) {

    /* Get a handle on the previous lfa, if there is one. */
    MVMLFA *prev = MVM_LFA_prev_lfa(tc, toparr, lfa);
    MVMObject *copy_item;

    if (prev) {
        /* if the storage size changed */
        if (prev->ssize != lfa->ssize) {
            /* it's a copying transaction */
            MVMuint64 i, oldlen = prev->elems, workdone,
                copy_index = MVM_HIGHEST1, panic_start = MVM_HIGHEST1;
            MVMuint64 MIN_COPY_WORK = oldlen < 1024 ? oldlen : 1024;
            /* Still needing to copy? */
            while ((MVMuint64)MVM_VOLATILE_GET(&lfa->copy_done) < oldlen) {

                if (panic_start == MVM_HIGHEST1) {
                    copy_index = (MVMint64)MVM_VOLATILE_GET(&lfa->copy_index);

                    while (copy_index <= (oldlen<<1)
                    && !MVM_CAS(&lfa->copy_index,
                            copy_index, copy_index + MIN_COPY_WORK))
                        copy_index =
                            (MVMint64)MVM_VOLATILE_GET(&lfa->copy_index);
                    if (copy_index >= (oldlen<<1))
                        panic_start = copy_index;
                }
                workdone = 0;
                for (i = 0; i < MIN_COPY_WORK; i++) {
                    /*if (MVM_LFA_copy_slot(tc, toparr,
                        (copy_index+i)&(oldlen-1), prev, lfa))
                            workdone++;*/
                }
                if (workdone > 0)
            //        MVM_LFA_help_and_cleanup(tc,
            //            toparr, prev, lfa, workdone);
                copy_index += MIN_COPY_WORK;
            }
            MVM_LFA_release_read_handle(tc, prev);
        }
        /* <whew> it's not a copying operation; breathe a huge
         * sigh of relief. */
        else if (MVM_LFA_ISSPLICE(lfa)) {
            /* splice operation, which involves one, possibly two,
             * (if the replaced span is a different size from the
             * replacement span), different range copies. */

        }
        else if (copy_item = (MVMObject *)MVM_VOLATILE_GET(&lfa->copy_item)) {
            /* it's an operation that assigns an item, like a push
             * or unshift. */


        }
    }
}
