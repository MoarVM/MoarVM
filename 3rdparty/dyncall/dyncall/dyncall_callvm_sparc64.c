/*

 Package: dyncall
 Library: dyncall
 File: dyncall/dyncall_callvm_sparc64.c
 Description: Call VM for sparc64 64-bit processor architecture.
 License:

   Copyright (c) 2011 Daniel Adler <dadler@uni-goettingen.de>

   Permission to use, copy, modify, and distribute this software for any
   purpose with or without fee is hereby granted, provided that the above
   copyright notice and this permission notice appear in all copies.

   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
   WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
   MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
   ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

*/


#include "dyncall_callvm_sparc64.h"
#include "dyncall_call_sparc64.h"
#include "dyncall_utils.h"
#include "dyncall_alloc.h"

#define IREGS 6
#define FREGS 16
#define SREGS 16
#define DHEAD (IREGS+FREGS)*8+SREGS*4

/* Reset argument buffer. */
static void dc_callvm_reset_sparc64(DCCallVM* in_self)
{
  DCCallVM_sparc64* self = (DCCallVM_sparc64*)in_self;
  dcVecResize(&self->mVecHead,DHEAD);
  self->mIntRegs    = 0;
  self->mFloatRegs  = 0;
  self->mSingleRegs = 0;
  self->mUseSingleFlags = 0;
}

/* Construtor. */
/* the six output registers %o0-%o5 are always loaded, thus we need to ensure the argument buffer has space for at least 24 bytes. */
static DCCallVM* dc_callvm_new_sparc64(DCCallVM_vt* vt, DCsize size)
{
  DCCallVM_sparc64* self = (DCCallVM_sparc64*) dcAllocMem(sizeof(DCCallVM_sparc64)+DHEAD+size);
  dc_callvm_base_init(&self->mInterface, vt);
  dcVecInit(&self->mVecHead,DHEAD+size);
  dc_callvm_reset_sparc64(&self->mInterface);
  return (DCCallVM*)self;
}

/* Destructor. */
static void dc_callvm_free_sparc64(DCCallVM* in_self)
{
  dcFreeMem(in_self);
}

/* all integers are promoted to 64-bit. */

static void dc_callvm_argLongLong_sparc64(DCCallVM* in_self, DClonglong x)
{
  DCCallVM_sparc64* self = (DCCallVM_sparc64*)in_self;
  if (self->mIntRegs < IREGS) {
    * ( (DClonglong*) ( dcVecAt(&self->mVecHead, (self->mIntRegs++)*8) ) ) = x;
  } else {
    dcVecAppend(&self->mVecHead, &x, sizeof(DClonglong));
  }
  if (self->mFloatRegs < FREGS) self->mFloatRegs++;
  if (self->mSingleRegs < SREGS) self->mSingleRegs++;
}

static void dc_callvm_argLong_sparc64   (DCCallVM* in_self, DClong    x) { dc_callvm_argLongLong_sparc64(in_self, (DClonglong) x ); }
static void dc_callvm_argInt_sparc64    (DCCallVM* in_self, DCint     x) { dc_callvm_argLongLong_sparc64(in_self, (DClonglong) x ); }
static void dc_callvm_argBool_sparc64   (DCCallVM* in_self, DCbool    x) { dc_callvm_argLongLong_sparc64(in_self, (DClonglong) x ); }
static void dc_callvm_argChar_sparc64   (DCCallVM* in_self, DCchar    x) { dc_callvm_argLongLong_sparc64(in_self, (DClonglong) x ); }
static void dc_callvm_argShort_sparc64  (DCCallVM* in_self, DCshort   x) { dc_callvm_argLongLong_sparc64(in_self, (DClonglong) x ); }
static void dc_callvm_argPointer_sparc64(DCCallVM* in_self, DCpointer x) { dc_callvm_argLongLong_sparc64(in_self, (DClonglong) x ); }

static void dc_callvm_argDouble_sparc64(DCCallVM* in_self, DCdouble x)
{
  DCCallVM_sparc64* self = (DCCallVM_sparc64*)in_self;
  if (self->mFloatRegs < FREGS) {
    * ((double*)dcVecAt(&self->mVecHead,(IREGS+(self->mFloatRegs++))*8)) = x;
    if (self->mSingleRegs < SREGS) self->mSingleRegs++;
  }
  
  if (self->mIntRegs < IREGS) {
    self->mIntRegs++;
  } else {
    dcVecAppend(&self->mVecHead, &x, sizeof(DCdouble));
  }
}

static void dc_callvm_argDouble_sparc64_ellipsis(DCCallVM* in_self, DCdouble x)
{
  union {
    long long l;
    double d;
  } u;
  u.d = x;
  dc_callvm_argLongLong_sparc64(in_self, u.l);
}

static void dc_callvm_argFloat_sparc64_ellipsis(DCCallVM* in_self, DCfloat x)
{
  dc_callvm_argDouble_sparc64_ellipsis(in_self, (DCdouble) x);
}

static void dc_callvm_argFloat_sparc64(DCCallVM* in_self, DCfloat x)
{
  DCCallVM_sparc64* self = (DCCallVM_sparc64*)in_self;
  double y = (DCdouble) x;
  if (self->mSingleRegs < SREGS) {
    self->mUseSingleFlags |= 1<<self->mSingleRegs;
    * ((float*)dcVecAt(&self->mVecHead,(IREGS+FREGS)*8 + (self->mSingleRegs++)*4)) = x;
    if (self->mFloatRegs < FREGS) self->mFloatRegs++;
  } 
  
  if (self->mIntRegs < IREGS) {
    self->mIntRegs++;
  } else {
    union {
      DCdouble d;
      DClonglong l;
      DCfloat f[2];
    } u;
    u.f[1] = x;
    dcVecAppend(&self->mVecHead, &u.l, sizeof(DClonglong));
  }
}

#if 0
/* call: delegate to default call kernel */
static void dc_callvm_call_sparc64(DCCallVM* in_self, DCpointer target)
{
  DCCallVM_sparc64* self = (DCCallVM_sparc64*)in_self;
  dcCall_sparc64(target, dcVecSize(&self->mVecHead), dcVecData(&self->mVecHead));
}
#endif

static void dc_callvm_mode_sparc64(DCCallVM* in_self, DCint mode);

DCCallVM_vt gVT_sparc64_ellipsis = 
{
  &dc_callvm_free_sparc64, 
  &dc_callvm_reset_sparc64, 
  &dc_callvm_mode_sparc64, 
  &dc_callvm_argBool_sparc64, 
  &dc_callvm_argChar_sparc64, 
  &dc_callvm_argShort_sparc64, 
  &dc_callvm_argInt_sparc64, 
  &dc_callvm_argLong_sparc64, 
  &dc_callvm_argLongLong_sparc64, 
  &dc_callvm_argFloat_sparc64_ellipsis, 
  &dc_callvm_argDouble_sparc64_ellipsis, 
  &dc_callvm_argPointer_sparc64, 
  NULL /* argStruct */, 
  (DCvoidvmfunc*)       &dcCall_sparc64, 
  (DCboolvmfunc*)       &dcCall_sparc64, 
  (DCcharvmfunc*)       &dcCall_sparc64, 
  (DCshortvmfunc*)      &dcCall_sparc64, 
  (DCintvmfunc*)        &dcCall_sparc64, 
  (DClongvmfunc*)       &dcCall_sparc64, 
  (DClonglongvmfunc*)   &dcCall_sparc64, 
  (DCfloatvmfunc*)      &dcCall_sparc64, 
  (DCdoublevmfunc*)     &dcCall_sparc64, 
  (DCpointervmfunc*)    &dcCall_sparc64, 
  NULL /* callStruct */
};

/* CallVM virtual table. */
DCCallVM_vt gVT_sparc64 =
{
  &dc_callvm_free_sparc64, 
  &dc_callvm_reset_sparc64, 
  &dc_callvm_mode_sparc64, 
  &dc_callvm_argBool_sparc64, 
  &dc_callvm_argChar_sparc64, 
  &dc_callvm_argShort_sparc64, 
  &dc_callvm_argInt_sparc64, 
  &dc_callvm_argLong_sparc64, 
  &dc_callvm_argLongLong_sparc64, 
  &dc_callvm_argFloat_sparc64, 
  &dc_callvm_argDouble_sparc64, 
  &dc_callvm_argPointer_sparc64, 
  NULL /* argStruct */, 
  (DCvoidvmfunc*)       &dcCall_sparc64, 
  (DCboolvmfunc*)       &dcCall_sparc64, 
  (DCcharvmfunc*)       &dcCall_sparc64, 
  (DCshortvmfunc*)      &dcCall_sparc64, 
  (DCintvmfunc*)        &dcCall_sparc64, 
  (DClongvmfunc*)       &dcCall_sparc64, 
  (DClonglongvmfunc*)   &dcCall_sparc64, 
  (DCfloatvmfunc*)      &dcCall_sparc64, 
  (DCdoublevmfunc*)     &dcCall_sparc64, 
  (DCpointervmfunc*)    &dcCall_sparc64, 
  NULL /* callStruct */
};

/* mode: only a single mode available currently. */
static void dc_callvm_mode_sparc64(DCCallVM* in_self, DCint mode)
{
  switch(mode) {
    case DC_CALL_C_DEFAULT:
    case DC_CALL_C_ELLIPSIS:
    case DC_CALL_C_SPARC64:
      in_self->mVTpointer = &gVT_sparc64; 
      break;
    case DC_CALL_C_ELLIPSIS_VARARGS:
      in_self->mVTpointer = &gVT_sparc64_ellipsis; 
      break;
    default:
      in_self->mError = DC_ERROR_UNSUPPORTED_MODE;
      break; 
  }
}


/* Public API. */
DCCallVM* dcNewCallVM(DCsize size)
{
  return dc_callvm_new_sparc64(&gVT_sparc64,size);
}

#if 0
/* Load integer 32-bit. */
static void dc_callvm_argInt_sparc64(DCCallVM* in_self, DCint x)
{
  DCCallVM_sparc64* self = (DCCallVM_sparc64*)in_self;
  dcVecAppend(&self->mVecHead, &x, sizeof(DCint));
}

/* we propagate Bool,Char,Short,Int to LongLong. */

static void dc_callvm_argBool_sparc64(DCCallVM* in_self, DCbool x)   { dc_callvm_argInt_sparc64(in_self, (DCint)x); }
static void dc_callvm_argChar_sparc64(DCCallVM* in_self, DCchar x)   { dc_callvm_argInt_sparc64(in_self, (DCint)x); }
static void dc_callvm_argShort_sparc64(DCCallVM* in_self, DCshort x) { dc_callvm_argInt_sparc64(in_self, (DCint)x); }
#endif

