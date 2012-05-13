/* A GC sync point is a point where we can check if we're being signalled
 * to stop to do a GC run. This is placed at points where it is safe to
 * do such a thing, and hopefully so that it happens often enough; note
 * that every call down to the allocator is also a sync point, so this
 * really only means we need to do this enough to make sure tight native
 * loops trigger it. */
#define GC_SYNC_POINT(tc) \
    if (tc->interupt) { \
    }

/* Different views of a register. */
typedef union _MVM_Register {
    MVMObject         *o;
    struct _MVMString *s;
    MVMint8            i8;
    MVMuint8           ui8;
    MVMint16           i16;
    MVMuint16          ui16;
    MVMint32           i32;
    MVMuint32          ui32;
    MVMint64           i64;
    MVMuint64          ui64;
    MVMnum32           n32;
    MVMnum64           n64;
} MVM_Register;
