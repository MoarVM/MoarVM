/*

 Package: dyncall
 Library: dyncallback
 File: dyncallback/dyncall_callback_x86.c
 Description: Callback - Implementation for x86
 License:

   Copyright (c) 2007-2011 Daniel Adler <dadler@uni-goettingen.de>,
                           Tassilo Philipp <tphilipp@potion-studios.com>

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


#include "dyncall_callback_x86.h"
#include "dyncall_args_x86.h"

#include "dyncall_alloc_wx.h"
#include "dyncall_signature.h"

/*
 * assembly thunk entry for callbacks
 */

extern void dcCallbackThunkEntry();

/* compute stacksize for callee cleanup calling conventions:
 *
 * stdcall,fastcall_ms,fastcall_gnu
 */

static int dcbCleanupSize_x86_cdecl(const char* signature)
{
  return 0;
}

static int dcbCleanupSize_x86_std(const char* signature)
{
  const char* ptr = signature;
  int size = 0;
  char ch;
  while( (ch = *ptr++) != DC_SIGCHAR_ENDARG ) {
    switch(ch) {
      case DC_SIGCHAR_BOOL:
      case DC_SIGCHAR_CHAR:
      case DC_SIGCHAR_SHORT:
      case DC_SIGCHAR_INT:
      case DC_SIGCHAR_LONG:
      case DC_SIGCHAR_POINTER:
      case DC_SIGCHAR_UCHAR:
      case DC_SIGCHAR_USHORT:
      case DC_SIGCHAR_UINT:
      case DC_SIGCHAR_ULONG:
      case DC_SIGCHAR_STRING:
      case DC_SIGCHAR_FLOAT:
        size += 4;
        break;
      case DC_SIGCHAR_DOUBLE:
      case DC_SIGCHAR_LONGLONG:
      case DC_SIGCHAR_ULONGLONG:
        size += 8;
        break;
    }
  }
  return size;
}

static int dcbCleanupSize_x86_this_ms(const char* signature)
{
  const char* ptr = signature;
  int size = 0;
  char ch;
  while( (ch = *ptr++) != DC_SIGCHAR_ENDARG )
  {
    switch(ch)
    {
    case DC_SIGCHAR_BOOL:
    case DC_SIGCHAR_CHAR:
    case DC_SIGCHAR_SHORT:
    case DC_SIGCHAR_INT:
    case DC_SIGCHAR_LONG:
    case DC_SIGCHAR_POINTER:
    case DC_SIGCHAR_UCHAR:
    case DC_SIGCHAR_USHORT:
    case DC_SIGCHAR_UINT:
    case DC_SIGCHAR_ULONG:
    case DC_SIGCHAR_STRING:
    case DC_SIGCHAR_FLOAT:
      size += 4;
      break;
    case DC_SIGCHAR_DOUBLE:
    case DC_SIGCHAR_LONGLONG:
    case DC_SIGCHAR_ULONGLONG:
      size += 8;
      break;
    }
  }
  return size;
}

static int dcbCleanupSize_x86_fast_ms(const char* signature)
{
  const char* ptr = signature;
  int size = 0;
  int regs = 0;
  char ch;
  while( (ch = *ptr++) != DC_SIGCHAR_ENDARG )
  {
    switch(ch)
    {
    case DC_SIGCHAR_BOOL:
    case DC_SIGCHAR_CHAR:
    case DC_SIGCHAR_SHORT:
    case DC_SIGCHAR_INT:
    case DC_SIGCHAR_LONG:
    case DC_SIGCHAR_POINTER:
    case DC_SIGCHAR_UCHAR:
    case DC_SIGCHAR_USHORT:
    case DC_SIGCHAR_UINT:
    case DC_SIGCHAR_ULONG:
    case DC_SIGCHAR_STRING:
      if (regs < 2) regs++;
      else size += 4;
      break;
    case DC_SIGCHAR_FLOAT:
      size += 4;
      break;
    case DC_SIGCHAR_DOUBLE:
      size += 8;
      break;
    case DC_SIGCHAR_LONGLONG:
    case DC_SIGCHAR_ULONGLONG:
      size += 8;
      break;
    }
  }
  return size;
}

static int dcbCleanupSize_x86_fast_gnu(const char* signature)
{
  const char* ptr = signature;
  char ch;
  int size = 0;
  int regs = 0;
  while( (ch = *ptr++) != DC_SIGCHAR_ENDARG ) {
    switch(ch) {
      case DC_SIGCHAR_FLOAT:
        size += 4;
        break;
      case DC_SIGCHAR_DOUBLE:
        size += 8;
        break;
      case DC_SIGCHAR_LONGLONG:
      case DC_SIGCHAR_ULONGLONG:
        regs  = 2;
        size += 8;
        break;
      default:
        if (regs < 2) regs++;
        else size += 4;
        break;
    }
  }
  return size;
}

void dcbInitCallback(DCCallback* pcb, const char* signature, DCCallbackHandler* handler, void* userdata)
{
  const char* ptr;
  char  ch;
  int mode;
  pcb->handler = handler;
  pcb->userdata = userdata;

  ptr = signature;
  ch = *ptr;

  /* x86 hints: */

  mode = DC_CALL_C_X86_CDECL;

  if(ch == DC_SIGCHAR_CC_PREFIX)
  {
    ptr++;
    ch = *ptr++;
    switch(ch) {
      case DC_SIGCHAR_CC_STDCALL:      mode = DC_CALL_C_X86_WIN32_STD;      break;
      case DC_SIGCHAR_CC_THISCALL_MS:  mode = DC_CALL_C_X86_WIN32_THIS_MS;  break;
      case DC_SIGCHAR_CC_FASTCALL_GNU: mode = DC_CALL_C_X86_WIN32_FAST_GNU; break;
      case DC_SIGCHAR_CC_FASTCALL_MS:  mode = DC_CALL_C_X86_WIN32_FAST_MS;  break;
    }
  }

  /* x86 configuration: */

  switch(mode) {
    case DC_CALL_C_X86_CDECL:
      pcb->args_vt = &dcArgsVT_default;
      pcb->stack_cleanup = dcbCleanupSize_x86_cdecl(ptr);
      break;
    case DC_CALL_C_X86_WIN32_STD:
      pcb->args_vt = &dcArgsVT_default;
      pcb->stack_cleanup = dcbCleanupSize_x86_std(ptr);
      break;
    case DC_CALL_C_X86_WIN32_THIS_MS:
      pcb->args_vt = &dcArgsVT_this_ms;
      pcb->stack_cleanup = dcbCleanupSize_x86_this_ms(ptr);
      break;
    case DC_CALL_C_X86_WIN32_FAST_MS:
      pcb->args_vt = &dcArgsVT_fast_ms;
      pcb->stack_cleanup = dcbCleanupSize_x86_fast_ms(ptr);
      break;
    case DC_CALL_C_X86_WIN32_FAST_GNU:
      pcb->args_vt = &dcArgsVT_fast_gnu;
      pcb->stack_cleanup = dcbCleanupSize_x86_fast_gnu(ptr);
      break;
  }
}

/*
 * callback constructor
 */

DCCallback* dcbNewCallback(const char* signature, DCCallbackHandler* handler, void* userdata)
{
  int err;
  DCCallback* pcb;
  err = dcAllocWX(sizeof(DCCallback), (void**) &pcb);
  if (err != 0) return 0;

  dcbInitThunk(&pcb->thunk, dcCallbackThunkEntry);
  dcbInitCallback(pcb, signature, handler, userdata);
  return pcb;
}

/*
 * free
 */

void dcbFreeCallback(DCCallback* pcb)
{
  dcFreeWX(pcb, sizeof(DCCallback));
}

