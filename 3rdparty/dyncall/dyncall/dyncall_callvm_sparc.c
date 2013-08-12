/*

 Package: dyncall
 Library: dyncall
 File: dyncall/dyncall_callvm_sparc.c
 Description: Call VM for sparc processor architecture.
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


#include "dyncall_callvm_sparc.h"
#include "dyncall_call_sparc.h"
#include "dyncall_utils.h"
#include "dyncall_alloc.h"
#define DEFAULT_STACK_ALIGN	16
/* Construtor. */
/* the six output registers %o0-%o5 are always loaded, thus we need to ensure the argument buffer has space for at least 24 bytes. */
static DCCallVM* dc_callvm_new_sparc(DCCallVM_vt* vt, DCsize size)
{
  size=DC_MAX(size,sizeof(void*)*(6+1));
  DCCallVM_sparc* self = (DCCallVM_sparc*) dcAllocMem(sizeof(DCCallVM_sparc)+size);
  dc_callvm_base_init(&self->mInterface, vt);
  dcVecInit(&self->mVecHead,size);
  return (DCCallVM*)self;
}

/* Destructor. */
static void dc_callvm_free_sparc(DCCallVM* in_self)
{
  dcFreeMem(in_self);
}

/* Reset argument buffer. */
static void dc_callvm_reset_sparc(DCCallVM* in_self)
{
  DCCallVM_sparc* self = (DCCallVM_sparc*)in_self;
  dcVecReset(&self->mVecHead);
}

/* Load integer 32-bit. */
static void dc_callvm_argInt_sparc(DCCallVM* in_self, DCint x)
{
  DCCallVM_sparc* self = (DCCallVM_sparc*)in_self;
  dcVecAppend(&self->mVecHead, &x, sizeof(DCint));
}

/* we propagate Bool,Char,Short to Int. */

static void dc_callvm_argBool_sparc(DCCallVM* in_self, DCbool x) { dc_callvm_argInt_sparc(in_self, (DCint)x); }
static void dc_callvm_argChar_sparc(DCCallVM* in_self, DCchar x) { dc_callvm_argInt_sparc(in_self, (DCint)x); }
static void dc_callvm_argShort_sparc(DCCallVM* in_self, DCshort x) { dc_callvm_argInt_sparc(in_self, (DCint)x); }

/* handle others Pointer, Long, LongLong, Float and Double as-is. */

static void dc_callvm_argPointer_sparc(DCCallVM* in_self, DCpointer x) 
{ 
  DCCallVM_sparc* self = (DCCallVM_sparc*)in_self;
  dcVecAppend(&self->mVecHead, &x, sizeof(DCpointer));
}

static void dc_callvm_argLong_sparc(DCCallVM* in_self, DClong x) 
{ 
  DCCallVM_sparc* self = (DCCallVM_sparc*)in_self;
  dcVecAppend(&self->mVecHead, &x, sizeof(DClong));
}
static void dc_callvm_argLongLong_sparc(DCCallVM* in_self, DClonglong x)
{
  DCCallVM_sparc* self = (DCCallVM_sparc*)in_self;
  dcVecAppend(&self->mVecHead, &x, sizeof(DClonglong));
}
static void dc_callvm_argFloat_sparc(DCCallVM* in_self, DCfloat x)
{
  DCCallVM_sparc* self = (DCCallVM_sparc*)in_self;
  dcVecAppend(&self->mVecHead, &x, sizeof(DCfloat));
}
static void dc_callvm_argDouble_sparc(DCCallVM* in_self, DCdouble x)
{
  DCCallVM_sparc* self = (DCCallVM_sparc*)in_self;
  dcVecAppend(&self->mVecHead, &x, sizeof(DCdouble));
}
  
/* mode: only a single mode available currently. */
static void dc_callvm_mode_sparc(DCCallVM* in_self, DCint mode)
{
  switch(mode) {
    case DC_CALL_C_DEFAULT:
    case DC_CALL_C_ELLIPSIS:
    case DC_CALL_C_SPARC32:
      break;
    default:
      in_self->mError = DC_ERROR_UNSUPPORTED_MODE;
      break; 
  }
}

/* we call directly with 'RTYPE dcCall(DCCallVM* in_self, DCpointer target)' */
#if 0
/* call: delegate to default call kernel */
static void dc_callvm_call_sparc(DCCallVM* in_self, DCpointer target)
{
  DCCallVM_sparc* self = (DCCallVM_sparc*)in_self;
  dcCall_sparc(target, dcVecSize(&self->mVecHead), dcVecData(&self->mVecHead));
}
#endif

/* CallVM virtual table. */
DCCallVM_vt gVT_sparc =
{
  &dc_callvm_free_sparc, 
  &dc_callvm_reset_sparc, 
  &dc_callvm_mode_sparc, 
  &dc_callvm_argBool_sparc, 
  &dc_callvm_argChar_sparc, 
  &dc_callvm_argShort_sparc, 
  &dc_callvm_argInt_sparc, 
  &dc_callvm_argLong_sparc, 
  &dc_callvm_argLongLong_sparc, 
  &dc_callvm_argFloat_sparc, 
  &dc_callvm_argDouble_sparc, 
  &dc_callvm_argPointer_sparc, 
  NULL /* argStruct */, 
  (DCvoidvmfunc*)       &dcCall_sparc,  
  (DCboolvmfunc*)       &dcCall_sparc,  
  (DCcharvmfunc*)       &dcCall_sparc,  
  (DCshortvmfunc*)      &dcCall_sparc,  
  (DCintvmfunc*)        &dcCall_sparc, 
  (DClongvmfunc*)       &dcCall_sparc, 
  (DClonglongvmfunc*)   &dcCall_sparc,  
  (DCfloatvmfunc*)      &dcCall_sparc,  
  (DCdoublevmfunc*)     &dcCall_sparc,  
  (DCpointervmfunc*)    &dcCall_sparc,
  NULL /* callStruct */
};

/* Public API. */
DCCallVM* dcNewCallVM(DCsize size)
{
  return dc_callvm_new_sparc(&gVT_sparc,size);
}

