#include "moar.h"
#include "platform/threads.h"

#include <stdbool.h>
#include "cmp.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET Socket;
    #define sa_family_t unsigned int
#else
    #include "unistd.h"
    #include <sys/socket.h>
    #include <sys/un.h>

    typedef int Socket;
    #define closesocket close
#endif

typedef enum {
    MT_MessageTypeNotUnderstood,
    MT_ErrorProcessingMessage,
    MT_OperationSuccessful,
    MT_IsExecutionSuspendedRequest,
    MT_IsExecutionSuspendedResponse,
    MT_SuspendAll,
    MT_ResumeAll,
    MT_SuspendOne,
    MT_ResumeOne,
    MT_ThreadStarted,
    MT_ThreadEnded,
    MT_ThreadListRequest,
    MT_ThreadListResponse,
    MT_ThreadStackTraceRequest,
    MT_ThreadStackTraceResponse,
    MT_SetBreakpointRequest,
    MT_SetBreakpointConfirmation,
    MT_BreakpointNotification,
    MT_ClearBreakpoint,
    MT_ClearAllBreakpoints,
    MT_StepInto,
    MT_StepOver,
    MT_StepOut,
    MT_StepCompleted,
    MT_ReleaseHandles,
    MT_HandleResult,
    MT_ContextHandle,
    MT_ContextLexicalsRequest,
    MT_ContextLexicalsResponse,
    MT_OuterContextRequest,
    MT_CallerContextRequest,
    MT_CodeObjectHandle,
    MT_ObjectAttributesRequest,
    MT_ObjectAttributesResponse,
    MT_DecontainerizeHandle,
    MT_FindMethod,
    MT_Invoke,
    MT_InvokeResult,
    MT_UnhandledException,
} message_type;

typedef enum {
    ArgKind_Handle,
    ArgKind_Integer,
    ArgKind_Num,
    ArgKind_String,
} argument_kind;

typedef struct {
    MVMuint8 arg_kind;
    union {
        MVMint64 i;
        MVMnum64 n;
        char *s;
        MVMint64 o;
    } arg_u;
} argument_data;

typedef enum {
    FS_type      = 1,
    FS_id        = 2,
    FS_thread_id = 4,
    FS_file      = 8,
    FS_line      = 16,
    FS_suspend   = 32,
    FS_stacktrace = 64,
    /* handle_count is just bookkeeping */
    FS_handles    = 128,
    FS_handle_id  = 256,
    FS_frame_number = 512,
    FS_arguments    = 1024,
} fields_set;

typedef struct {
    MVMuint16 type;
    MVMuint64 id;

    MVMuint32 thread_id;

    char *file;
    MVMuint64 line;

    MVMuint8  suspend;
    MVMuint8  stacktrace;

    MVMuint16 handle_count;
    MVMuint64 *handles;

    MVMuint64 handle_id;

    MVMuint32 frame_number;

    MVMuint32 argument_count;
    argument_data *arguments;

    MVMuint8  parse_fail;
    const char *parse_fail_message;

    fields_set fields_set;
} request_data;

static MVMint32 write_stacktrace_frames(MVMThreadContext *dtc, cmp_ctx_t *ctx, MVMThread *thread);

/* Breakpoint stuff */
void MVM_debugserver_register_line(MVMThreadContext *tc, char *filename, MVMuint32 filename_len, MVMuint32 line_no,  MVMuint32 *file_idx) {
    MVMDebugServerData *debugserver = tc->instance->debugserver;
    MVMDebugServerBreakpointTable *table = debugserver->breakpoints;
    MVMDebugServerBreakpointFileTable *found = NULL;
    MVMuint32 index = 0;

    uv_mutex_lock(&debugserver->mutex_breakpoints);

    if (*file_idx < table->files_used) {
        MVMDebugServerBreakpointFileTable *file = &table->files[*file_idx];
        if (file->filename_length == filename_len && memcmp(file->filename, filename, filename_len) == 0)
            found = file;
    }

    for (index = 0; !found && index < table->files_used; index++) {
        MVMDebugServerBreakpointFileTable *file = &table->files[index];
        if (file->filename_length != filename_len)
            continue;
        if (memcmp(file->filename, filename, filename_len) != 0)
            continue;
        found = file;
        *file_idx = index;
        break;
    }

    if (!found) {
        if (table->files_used++ >= table->files_alloc) {
            MVMuint32 old_alloc = table->files_alloc;
            table->files_alloc *= 2;
            table->files = MVM_fixed_size_realloc_at_safepoint(tc, tc->instance->fsa, table->files,
                    old_alloc * sizeof(MVMDebugServerBreakpointFileTable),
                    table->files_alloc * sizeof(MVMDebugServerBreakpointFileTable));
            memset((char *)(table->files + old_alloc), 0, (table->files_alloc - old_alloc) * sizeof(MVMDebugServerBreakpointFileTable) - 1);
            if (tc->instance->debugserver->debugspam_protocol)
                fprintf(stderr, "table for files increased to %d slots\n", filename, table->files_alloc);
        }

        found = &table->files[table->files_used - 1];

        if (tc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "created new file entry at %d for %s\n", table->files_used - 1, filename);

        found->filename = MVM_calloc(filename_len + 1, sizeof(char));
        strncpy(found->filename, filename, filename_len);

        found->filename_length = filename_len;

        found->lines_active_alloc = line_no + 32;
        found->lines_active = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa, found->lines_active_alloc * sizeof(MVMuint8));

        *file_idx = table->files_used - 1;

        found->breakpoints = NULL;
        found->breakpoints_alloc = 0;
        found->breakpoints_used = 0;
    }

    if (found->lines_active_alloc < line_no + 1) {
        MVMuint32 old_size = found->lines_active_alloc;
        found->lines_active_alloc *= 2;
        if (tc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "increasing line number table for %s from %d to %d slots\n", filename, old_size, found->lines_active_alloc);
        found->lines_active = MVM_fixed_size_realloc_at_safepoint(tc, tc->instance->fsa,
                found->lines_active, old_size, found->lines_active_alloc);
        memset((char *)found->lines_active + old_size, 0, found->lines_active_alloc - old_size - 1);
    }

    uv_mutex_unlock(&debugserver->mutex_breakpoints);
}

static void breakpoint_hit(MVMThreadContext *tc, MVMDebugServerBreakpointFileTable *file, MVMuint32 line_no) {
    cmp_ctx_t *ctx = NULL;
    MVMDebugServerBreakpointInfo *info;
    MVMuint32 index;
    MVMuint8 must_suspend = 0;

    if (tc->instance->debugserver && tc->instance->debugserver->messagepack_data) {
        ctx = (cmp_ctx_t*)tc->instance->debugserver->messagepack_data;
    }

    for (index = 0; index < file->breakpoints_used; index++) {
        info = &file->breakpoints[index];

        if (info->line_no == line_no) {
            if (tc->instance->debugserver->debugspam_protocol)
                fprintf(stderr, "hit a breakpoint\n");
            if (ctx) {
                uv_mutex_lock(&tc->instance->debugserver->mutex_network_send);
                cmp_write_map(ctx, 4);
                cmp_write_str(ctx, "id", 2);
                cmp_write_integer(ctx, info->breakpoint_id);
                cmp_write_str(ctx, "type", 4);
                cmp_write_integer(ctx, MT_BreakpointNotification);
                cmp_write_str(ctx, "thread", 6);
                cmp_write_integer(ctx, tc->thread_id);
                cmp_write_str(ctx, "frames", 6);
                if (info->send_backtrace) {
                    write_stacktrace_frames(tc, ctx, tc->thread_obj);
                } else {
                    cmp_write_nil(ctx);
                }
                uv_mutex_unlock(&tc->instance->debugserver->mutex_network_send);
            }
            if (info->shall_suspend) {
                must_suspend = 1;
            }
        }
    }
    if (must_suspend) {
        while (1) {
            if (MVM_cas(&tc->gc_status, MVMGCStatus_NONE, MVMGCStatus_INTERRUPT | MVMSuspendState_SUSPEND_REQUEST)
                    == MVMGCStatus_NONE) {
                break;
            }
            if (MVM_load(&tc->gc_status) == (MVMGCStatus_INTERRUPT | MVMSuspendState_SUSPEND_REQUEST)) {
                break;
            }
        }
        MVM_gc_enter_from_interrupt(tc);
    }
}

void MVM_debugserver_breakpoint_check(MVMThreadContext *tc, MVMuint32 file_idx, MVMuint32 line_no) {
    MVMDebugServerData *debugserver = tc->instance->debugserver;
    if (debugserver->any_breakpoints_at_all) {
        MVMDebugServerBreakpointTable *table = debugserver->breakpoints;
        MVMDebugServerBreakpointFileTable *found = &table->files[file_idx];

        if (found->breakpoints_used && found->lines_active[line_no]) {
            breakpoint_hit(tc, found, line_no);
        }
    }
}


#define REQUIRE(field, message) do { if(!(data->fields_set & (field))) { data->parse_fail = 1; data->parse_fail_message = (message); return 0; }; accepted = accepted | (field); } while (0)

MVMuint8 check_requirements(request_data *data) {
    fields_set accepted = FS_id | FS_type;

    REQUIRE(FS_id, "An id field is required");
    REQUIRE(FS_type, "A type field is required");
    switch (data->type) {
        case MT_IsExecutionSuspendedRequest:
        case MT_SuspendAll:
        case MT_ResumeAll:
        case MT_ThreadListRequest:
        case MT_ClearAllBreakpoints:
            /* All of these messages only take id and type */
            break;

        case MT_SuspendOne:
        case MT_ResumeOne:
        case MT_ThreadStackTraceRequest:
        case MT_StepInto:
        case MT_StepOver:
        case MT_StepOut:
            REQUIRE(FS_thread_id, "A thread field is required");
            break;

        case MT_SetBreakpointRequest:
            REQUIRE(FS_suspend, "A suspend field is required");
            REQUIRE(FS_stacktrace, "A stacktrace field is required");
            /* Fall-Through */
        case MT_ClearBreakpoint:
            REQUIRE(FS_file, "A file field is required");
            REQUIRE(FS_line, "A line field is required");
            break;

        case MT_ReleaseHandles:
            REQUIRE(FS_handles, "A handles field is required");
            break;

        case MT_FindMethod:
            /* TODO we've got to have some name field or something */
            /* Fall-Through */
        case MT_DecontainerizeHandle:
            REQUIRE(FS_thread_id, "A thread field is required");
            /* Fall-Through */
        case MT_ContextLexicalsRequest:
        case MT_OuterContextRequest:
        case MT_CallerContextRequest:
        case MT_ObjectAttributesRequest:
            REQUIRE(FS_handle_id, "A handle field is required");
            break;

        case MT_ContextHandle:
        case MT_CodeObjectHandle:
            REQUIRE(FS_thread_id, "A thread field is required");
            REQUIRE(FS_frame_number, "A frame field is required");
            break;

        case MT_Invoke:
            REQUIRE(FS_handle_id, "A handle field is required");
            REQUIRE(FS_thread_id, "A thread field is required");
            REQUIRE(FS_arguments, "An arguments field is required");
            break;

        default:
            break;
    }

    if (data->fields_set != accepted) {
        data->parse_fail = 1;
        data->parse_fail_message = "Too many keys in message";
    }
}

static void send_greeting(Socket *sock) {
    char buffer[24] = "MOARVM-REMOTE-DEBUG\0";
    MVMuint32 version = htobe16(1);

    MVMuint16 *verptr = (MVMuint16 *)(&buffer[strlen("MOARVM-REMOTE-DEBUG") + 1]);

    *verptr = version;
    verptr++;

    version = htobe16(0);

    *verptr = version;
    send(*sock, buffer, 24, 0);
}

static int receive_greeting(Socket *sock) {
    const char *expected = "MOARVM-REMOTE-CLIENT-OK";
    char buffer[strlen(expected) + 1];
    int received = 0;

    memset(buffer, 0, sizeof(buffer));

    received = recv(*sock, buffer, sizeof(buffer), 0);
    if (received != sizeof(buffer)) {
        return 0;
    }
    if (memcmp(buffer, expected, sizeof(buffer)) == 0) {
        return 1;
    }
    return 0;
}

static void communicate_error(cmp_ctx_t *ctx, request_data *argument) {
    fprintf(stderr, "communicating an error\n");
    cmp_write_map(ctx, 2);
    cmp_write_str(ctx, "id", 2);
    cmp_write_integer(ctx, argument->id);
    cmp_write_str(ctx, "type", 4);
    cmp_write_integer(ctx, MT_ErrorProcessingMessage);
}

static void communicate_success(cmp_ctx_t *ctx, request_data *argument) {
    fprintf(stderr, "communicating success\n");
    cmp_write_map(ctx, 2);
    cmp_write_str(ctx, "id", 2);
    cmp_write_integer(ctx, argument->id);
    cmp_write_str(ctx, "type", 4);
    cmp_write_integer(ctx, MT_OperationSuccessful);
}

/* Send spontaneous events */
void MVM_debugserver_notify_thread_creation(MVMThreadContext *tc) {
    if (tc->instance->debugserver && tc->instance->debugserver->messagepack_data) {
        cmp_ctx_t *ctx = (cmp_ctx_t*)tc->instance->debugserver->messagepack_data;
        MVMuint64 event_id;

        uv_mutex_lock(&tc->instance->debugserver->mutex_network_send);

        event_id = tc->instance->debugserver->event_id;
        tc->instance->debugserver->event_id += 2;

        cmp_write_map(ctx, 5);
        cmp_write_str(ctx, "id", 2);
        cmp_write_integer(ctx, event_id);
        cmp_write_str(ctx, "type", 4);
        cmp_write_integer(ctx, MT_ThreadStarted);

        cmp_write_str(ctx, "thread", 6);
        cmp_write_integer(ctx, tc->thread_obj->body.thread_id);

        cmp_write_str(ctx, "native_id", 9);
        cmp_write_integer(ctx, tc->thread_obj->body.native_thread_id);

        cmp_write_str(ctx, "app_lifetime", 12);
        cmp_write_integer(ctx, tc->thread_obj->body.app_lifetime);

        uv_mutex_unlock(&tc->instance->debugserver->mutex_network_send);
    }
}

void MVM_debugserver_notify_thread_destruction(MVMThreadContext *tc) {
    if (tc->instance->debugserver && tc->instance->debugserver->messagepack_data) {
        cmp_ctx_t *ctx = (cmp_ctx_t*)tc->instance->debugserver->messagepack_data;
        MVMuint64 event_id;

        uv_mutex_lock(&tc->instance->debugserver->mutex_network_send);

        event_id = tc->instance->debugserver->event_id;
        tc->instance->debugserver->event_id += 2;

        cmp_write_map(ctx, 3);
        cmp_write_str(ctx, "id", 2);
        cmp_write_integer(ctx, event_id);
        cmp_write_str(ctx, "type", 4);
        cmp_write_integer(ctx, MT_ThreadEnded);

        cmp_write_str(ctx, "thread", 6);
        cmp_write_integer(ctx, tc->thread_obj->body.thread_id);

        uv_mutex_unlock(&tc->instance->debugserver->mutex_network_send);
    }
}

static MVMuint8 is_thread_id_eligible(MVMInstance *vm, MVMuint32 id) {
    if (id == vm->debugserver->thread_id || id == vm->speshworker_thread_id) {
        return 0;
    }
    return 1;
}

/* Send replies to requests send by the client */

static MVMThread *find_thread_by_id(MVMInstance *vm, MVMint32 id) {
    MVMThread *cur_thread = 0;

    if (!is_thread_id_eligible(vm, id)) {
        return NULL;
    }

    uv_mutex_lock(&vm->mutex_threads);
    cur_thread = vm->threads;
    while (cur_thread) {
        if (cur_thread->body.thread_id == id) {
            break;
        }
        cur_thread = cur_thread->body.next;
    }
    uv_mutex_unlock(&vm->mutex_threads);
    return cur_thread;
}

static MVMint32 request_thread_suspends(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument, MVMThread *thread) {
    MVMThread *to_do = thread ? thread : find_thread_by_id(dtc->instance, argument->thread_id);
    MVMThreadContext *tc = to_do ? to_do->body.tc : NULL;

    if (!tc)
        return 1;

    MVM_gc_mark_thread_blocked(dtc);

    while (1) {
        if (MVM_cas(&tc->gc_status, MVMGCStatus_NONE, MVMGCStatus_INTERRUPT | MVMSuspendState_SUSPEND_REQUEST)
                == MVMGCStatus_NONE) {
            break;
        }
        if (MVM_cas(&tc->gc_status, MVMGCStatus_UNABLE, MVMGCStatus_UNABLE | MVMSuspendState_SUSPEND_REQUEST)
                == MVMGCStatus_UNABLE) {
            break;
        }
        MVM_platform_thread_yield();
    }

    if (argument->type == MT_SuspendOne)
        communicate_success(ctx,  argument);

    MVM_gc_mark_thread_unblocked(dtc);
    if (tc->instance->debugserver->debugspam_protocol)
        fprintf(stderr, "thread %d successfully suspended\n", tc->thread_id);

    return 0;
}

static MVMint32 request_all_threads_suspend(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMInstance *vm = dtc->instance;
    MVMThread *cur_thread = 0;
    MVMuint32 success = 1;

    uv_mutex_lock(&vm->mutex_threads);

    /* TODO track which threads we successfully suspended so we can wake them
     * up again if an error occured */

    cur_thread = vm->threads;
    while (cur_thread) {
        if (is_thread_id_eligible(vm, cur_thread->body.thread_id)) {
            AO_t current = MVM_load(&cur_thread->body.tc->gc_status);
            if (current == MVMGCStatus_NONE || current == MVMGCStatus_UNABLE) {
                MVMint32 result = request_thread_suspends(dtc, ctx, argument, cur_thread);
                if (result == 1) {
                    success = 0;
                    break;
                }
            }
        }
        cur_thread = cur_thread->body.next;
    }

    if (success)
        communicate_success(ctx, argument);
    else
        communicate_error(ctx, argument);

    uv_mutex_unlock(&vm->mutex_threads);
}

static MVMint32 request_thread_resumes(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument, MVMThread *thread) {
    MVMInstance *vm = dtc->instance;
    MVMThread *to_do = thread ? thread : find_thread_by_id(vm, argument->thread_id);
    MVMThreadContext *tc = to_do ? to_do->body.tc : NULL;
    AO_t current;

    if (!tc) {
        return 1;
    }

    current = MVM_load(&tc->gc_status);

    if (current != (MVMGCStatus_UNABLE | MVMSuspendState_SUSPENDED)
            && (current & MVMSUSPENDSTATUS_MASK) != MVMSuspendState_SUSPEND_REQUEST) {
        fprintf(stderr, "wrong state to resume from: %d\n", MVM_load(&tc->gc_status));
        return 1;
    }

    MVM_gc_mark_thread_blocked(dtc);

    while(1) {
        current = MVM_cas(&tc->gc_status, MVMGCStatus_UNABLE | MVMSuspendState_SUSPENDED, MVMGCStatus_UNABLE);
        if (current == (MVMGCStatus_UNABLE | MVMSuspendState_SUSPENDED)) {
            /* Success! We signalled the thread and can now tell it to
             * mark itself unblocked, which takes care of any looming GC
             * and related business. */
            uv_cond_broadcast(&vm->debugserver->tell_threads);
            fprintf(stderr, "thread %d resumed from unable + suspended\n", tc->thread_id);
            break;
        } else if ((current & MVMGCSTATUS_MASK) == MVMGCStatus_STOLEN) {
            uv_mutex_lock(&tc->instance->mutex_gc_orchestrate);
            if (tc->instance->in_gc) {
                uv_cond_wait(&tc->instance->cond_blocked_can_continue,
                    &tc->instance->mutex_gc_orchestrate);
            }
            uv_mutex_unlock(&tc->instance->mutex_gc_orchestrate);
        } else {
            if (current == (MVMGCStatus_UNABLE | MVMSuspendState_SUSPEND_REQUEST)) {
                if (MVM_cas(&tc->gc_status, (MVMGCStatus_UNABLE | MVMSuspendState_SUSPEND_REQUEST), MVMGCStatus_UNABLE) == current) {
                    fprintf(stderr, "thread %d resumed from unable + suspend request\n", tc->thread_id);
                    break;
                }
            }
        }
    }

    MVM_gc_mark_thread_unblocked(dtc);

    if (argument->type == MT_ResumeOne)
        communicate_success(ctx, argument);

    if (tc->instance->debugserver->debugspam_protocol)
        fprintf(stderr, "success resuming thread; its status is now %d\n", MVM_load(&tc->gc_status));

    return 0;
}

static MVMint32 request_all_threads_resume(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMInstance *vm = dtc->instance;
    MVMThread *cur_thread = 0;
    MVMuint8 success = 1;

    uv_mutex_lock(&vm->mutex_threads);
    cur_thread = vm->threads;
    while (cur_thread) {
        if (cur_thread != dtc->thread_obj) {
            AO_t current = MVM_load(&cur_thread->body.tc->gc_status);
            if (current == (MVMGCStatus_UNABLE | MVMSuspendState_SUSPENDED) ||
                    current == (MVMGCStatus_INTERRUPT | MVMSuspendState_SUSPEND_REQUEST) ||
                    current == (MVMGCStatus_STOLEN | MVMSuspendState_SUSPEND_REQUEST)) {
                if (request_thread_resumes(dtc, ctx, argument, cur_thread)) {
                    if (vm->debugserver->debugspam_protocol)
                        fprintf(stderr, "failure to resume thread %d\n", cur_thread->body.thread_id);
                    success = 0;
                    break;
                }
            }
        }
        cur_thread = cur_thread->body.next;
    }

    if (success)
        communicate_success(ctx, argument);
    else
        communicate_error(ctx, argument);

    uv_mutex_unlock(&vm->mutex_threads);

    return !success;
}

static MVMint32 write_stacktrace_frames(MVMThreadContext *dtc, cmp_ctx_t *ctx, MVMThread *thread) {
    MVMThreadContext *tc = thread->body.tc;
    MVMuint64 stack_size = 0;

    MVMFrame *cur_frame = tc->cur_frame;

    while (cur_frame != NULL) {
        stack_size++;
        cur_frame = cur_frame->caller;
    }

    if (tc->instance->debugserver->debugspam_protocol)
        fprintf(stderr, "dumping a stack trace of %d frames\n", stack_size);

    cmp_write_array(ctx, stack_size);

    cur_frame = tc->cur_frame;
    stack_size = 0; /* To check if we've got the topmost frame or not */

    while (cur_frame != NULL) {
        MVMString *bc_filename = cur_frame->static_info->body.cu->body.filename;
        MVMString *name     = cur_frame->static_info->body.name;

        MVMuint8 *cur_op = stack_size != 0 ? cur_frame->return_address : *(tc->interp_cur_op);
        MVMuint32 offset = cur_op - MVM_frame_effective_bytecode(cur_frame);
        MVMBytecodeAnnotation *annot = MVM_bytecode_resolve_annotation(tc, &cur_frame->static_info->body,
                                          offset > 0 ? offset - 1 : 0);

        MVMint32 line_number = annot ? annot->line_number : 1;
        MVMint16 string_heap_index = annot ? annot->filename_string_heap_index : 1;

        char *tmp1 = annot && string_heap_index < cur_frame->static_info->body.cu->body.num_strings
            ? MVM_string_utf8_encode_C_string(tc, MVM_cu_string(tc,
                    cur_frame->static_info->body.cu, string_heap_index))
            : NULL;
        char *filename_c = bc_filename
            ? MVM_string_utf8_encode_C_string(tc, bc_filename)
            : NULL;
        char *name_c = name
            ? MVM_string_utf8_encode_C_string(tc, name)
            : NULL;

        char *debugname = cur_frame->code_ref ? MVM_6model_get_debug_name(tc, cur_frame->code_ref) : "";

        cmp_write_map(ctx, 5);
        cmp_write_str(ctx, "file", 4);
        cmp_write_str(ctx, tmp1, tmp1 ? strlen(tmp1) : 0);
        cmp_write_str(ctx, "line", 4);
        cmp_write_integer(ctx, line_number);
        cmp_write_str(ctx, "bytecode_file", 13);
        if (bc_filename)
            cmp_write_str(ctx, filename_c, strlen(filename_c));
        else
            cmp_write_nil(ctx);
        cmp_write_str(ctx, "name", 4);
        cmp_write_str(ctx, name_c, name_c ? strlen(name_c) : 0);
        cmp_write_str(ctx, "type", 4);
        cmp_write_str(ctx, debugname, strlen(debugname));

        if (bc_filename)
            MVM_free(filename_c);
        if (name)
            MVM_free(name_c);
        if (tmp1)
            MVM_free(tmp1);

        cur_frame = cur_frame->caller;
        stack_size++;
    }
}

static MVMint32 request_thread_stacktrace(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument, MVMThread *thread) {
    MVMThread *to_do = thread ? thread : find_thread_by_id(dtc->instance, argument->thread_id);

    if (!to_do)
        return 1;

    if ((to_do->body.tc->gc_status & MVMGCSTATUS_MASK) != MVMGCStatus_UNABLE) {
        return 1;
    }

    cmp_write_map(ctx, 3);
    cmp_write_str(ctx, "id", 2);
    cmp_write_integer(ctx, argument->id);
    cmp_write_str(ctx, "type", 4);
    cmp_write_integer(ctx, MT_ThreadStackTraceResponse);
    cmp_write_str(ctx, "frames", 6);

    write_stacktrace_frames(dtc, ctx, to_do);

    return 0;
}

static void send_thread_info(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMInstance *vm = dtc->instance;
    MVMint32 threadcount = 0;
    MVMThread *cur_thread;
    char infobuf[32] = "THL";

    uv_mutex_lock(&vm->mutex_threads);
    cur_thread = vm->threads;
    while (cur_thread) {
        threadcount++;
        cur_thread = cur_thread->body.next;
    }

    cmp_write_map(ctx, 3);
    cmp_write_str(ctx, "id", 2);
    cmp_write_integer(ctx, argument->id);
    cmp_write_str(ctx, "type", 4);
    cmp_write_integer(ctx, MT_ThreadListResponse);
    cmp_write_str(ctx, "threads", 7);

    cmp_write_array(ctx, threadcount);

    cur_thread = vm->threads;
    while (cur_thread) {
        cmp_write_map(ctx, 5);

        cmp_write_str(ctx, "thread", 6);
        cmp_write_integer(ctx, cur_thread->body.thread_id);

        cmp_write_str(ctx, "native_id", 9);
        cmp_write_integer(ctx, cur_thread->body.native_thread_id);

        cmp_write_str(ctx, "app_lifetime", 12);
        cmp_write_bool(ctx, cur_thread->body.app_lifetime);

        cmp_write_str(ctx, "suspended", 9);
        cmp_write_bool(ctx, (MVM_load(&cur_thread->body.tc->gc_status) & MVMSUSPENDSTATUS_MASK) != MVMSuspendState_NONE);

        cmp_write_str(ctx, "num_locks", 9);
        cmp_write_integer(ctx, cur_thread->body.tc->num_locks);

        cur_thread = cur_thread->body.next;
    }
    uv_mutex_unlock(&vm->mutex_threads);
}

static MVMuint64 send_is_execution_suspended_info(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMInstance *vm = dtc->instance;
    MVMuint8 result = 1;
    MVMThread *cur_thread;

    uv_mutex_lock(&vm->mutex_threads);
    cur_thread = vm->threads;
    while (cur_thread) {
        if ((MVM_load(&cur_thread->body.tc->gc_status) & MVMSUSPENDSTATUS_MASK) != MVMSuspendState_SUSPENDED
                && cur_thread->body.thread_id != vm->debugserver->thread_id
                && cur_thread->body.thread_id != vm->speshworker_thread_id) {
            result = 0;
            break;
        }
        cur_thread = cur_thread->body.next;
    }

    uv_mutex_unlock(&vm->mutex_threads);

    cmp_write_map(ctx, 3);
    cmp_write_str(ctx, "id", 2);
    cmp_write_integer(ctx, argument->id);
    cmp_write_str(ctx, "type", 4);
    cmp_write_integer(ctx, MT_IsExecutionSuspendedResponse);
    cmp_write_str(ctx, "suspended", 9);
    cmp_write_bool(ctx, result);
}

void MVM_debugserver_add_breakpoint(MVMThreadContext *tc, cmp_ctx_t *ctx, request_data *argument) {
    MVMDebugServerData *debugserver = tc->instance->debugserver;
    MVMDebugServerBreakpointTable *table = debugserver->breakpoints;
    MVMDebugServerBreakpointFileTable *found = NULL;
    MVMDebugServerBreakpointInfo *bp_info = NULL;
    MVMuint32 index = 0;

    if (tc->instance->debugserver->debugspam_protocol)
        fprintf(stderr, "asked to set a breakpoint for file %s line %d to send id %d\n", argument->file, argument->line, argument->id);

    MVM_debugserver_register_line(tc, argument->file, strlen(argument->file), argument->line, &index);

    uv_mutex_lock(&debugserver->mutex_breakpoints);

    found = &table->files[index];

    /* Create breakpoint first so that if a thread breaks on the activated line
     * the breakpoint information already exists */
    if (found->breakpoints_alloc == 0) {
        found->breakpoints_alloc = 4;
        found->breakpoints = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa,
                found->breakpoints_alloc * sizeof(MVMDebugServerBreakpointInfo));
    }
    if (found->breakpoints_used++ >= found->breakpoints_alloc) {
        MVMuint32 old_alloc = found->breakpoints_alloc;
        found->breakpoints_alloc *= 2;
        found->breakpoints = MVM_fixed_size_realloc_at_safepoint(tc, tc->instance->fsa, found->breakpoints,
                old_alloc * sizeof(MVMDebugServerBreakpointInfo),
                found->breakpoints_alloc * sizeof(MVMDebugServerBreakpointInfo));
        if (tc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "table for breakpoints increased to %d slots\n", found->breakpoints_alloc);
    }

    bp_info = &found->breakpoints[found->breakpoints_used - 1];

    bp_info->breakpoint_id = argument->id;
    bp_info->line_no = argument->line;
    bp_info->shall_suspend = argument->suspend;
    bp_info->send_backtrace = argument->stacktrace;

    debugserver->any_breakpoints_at_all++;

    if (tc->instance->debugserver->debugspam_protocol)
        fprintf(stderr, "breakpoint settings: index %d bpid %d lineno %d suspend %d backtrace %d\n", found->breakpoints_used - 1, argument->id, argument->line, argument->suspend, argument->stacktrace);

    found->lines_active[argument->line] = 1;

    cmp_write_map(ctx, 3);
    cmp_write_str(ctx, "id", 2);
    cmp_write_integer(ctx, argument->id);
    cmp_write_str(ctx, "type", 4);
    cmp_write_integer(ctx, MT_SetBreakpointConfirmation);
    cmp_write_str(ctx, "line", 4);
    cmp_write_integer(ctx, argument->line);

    uv_mutex_unlock(&debugserver->mutex_breakpoints);
}

void MVM_debugserver_clear_all_breakpoints(MVMThreadContext *tc, cmp_ctx_t *ctx, request_data *argument) {
    MVMDebugServerData *debugserver = tc->instance->debugserver;
    MVMDebugServerBreakpointTable *table = debugserver->breakpoints;
    MVMDebugServerBreakpointFileTable *found = NULL;
    MVMuint32 index;

    uv_mutex_lock(&debugserver->mutex_breakpoints);

    for (index = 0; index < table->files_used; index++) {
        found = &table->files[index];
        memset(found->lines_active, 0, found->lines_active_alloc * sizeof(MVMuint8));
        found->breakpoints_used = 0;
    }

    debugserver->any_breakpoints_at_all = 0;

    uv_mutex_unlock(&debugserver->mutex_breakpoints);

    /* When a client disconnects, we clear all breakpoints but don't
     * send a confirmation. In this case ctx and argument will be NULL */
    if (ctx && argument)
        communicate_success(ctx, argument);
}

void MVM_debugserver_clear_breakpoint(MVMThreadContext *tc, cmp_ctx_t *ctx, request_data *argument) {
    MVMDebugServerData *debugserver = tc->instance->debugserver;
    MVMDebugServerBreakpointTable *table = debugserver->breakpoints;
    MVMDebugServerBreakpointFileTable *found = NULL;
    MVMuint32 index = 0;
    MVMuint32 bpidx = 0;
    MVMuint32 num_cleared = 0;

    MVM_debugserver_register_line(tc, argument->file, strlen(argument->file), argument->line, &index);

    if (tc->instance->debugserver->debugspam_protocol)
        fprintf(stderr, "asked to clear breakpoints for file %s line %d\n", argument->file, argument->line);

    uv_mutex_lock(&debugserver->mutex_breakpoints);

    found = &table->files[index];

    if (tc->instance->debugserver->debugspam_protocol)
        fprintf(stderr, "dumping all breakpoints\n");
    for (bpidx = 0; bpidx < found->breakpoints_used; bpidx++) {
        MVMDebugServerBreakpointInfo *bp_info = &found->breakpoints[bpidx];
        if (tc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "breakpoint index %d has id %d, is at line %d\n", bpidx, bp_info->breakpoint_id, bp_info->line_no);
    }

    if (tc->instance->debugserver->debugspam_protocol)
        fprintf(stderr, "trying to clear breakpoints\n\n");
    for (bpidx = 0; bpidx < found->breakpoints_used; bpidx++) {
        MVMDebugServerBreakpointInfo *bp_info = &found->breakpoints[bpidx];
        if (tc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "breakpoint index %d has id %d, is at line %d\n", bpidx, bp_info->breakpoint_id, bp_info->line_no);

        if (bp_info->line_no == argument->line) {
            if (tc->instance->debugserver->debugspam_protocol)
                fprintf(stderr, "breakpoint with id %d cleared\n", bp_info->breakpoint_id);
            found->breakpoints[bpidx] = found->breakpoints[--found->breakpoints_used];
            num_cleared++;
            bpidx--;
            debugserver->any_breakpoints_at_all--;
        }
    }

    uv_mutex_unlock(&debugserver->mutex_breakpoints);

    if (num_cleared > 0)
        communicate_success(ctx, argument);
    else
        communicate_error(ctx, argument);
}

static void release_all_handles(MVMThreadContext *dtc) {
    MVMDebugServerHandleTable *dht = dtc->instance->debugserver->handle_table;
    dht->used = 0;
    dht->next_id = 0;
}

static MVMuint64 release_handles(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMDebugServerHandleTable *dht = dtc->instance->debugserver->handle_table;

    MVMuint16 handle_index = 0;
    MVMuint16 id_index = 0;
    MVMuint16 handles_cleared = 0;
    for (handle_index = 0; handle_index < dht->used; handle_index++) {
        for (id_index = 0; id_index < argument->handle_count; id_index++) {
            if (argument->handles[id_index] == dht->entries[handle_index].id) {
                dht->used--;
                dht->entries[handle_index].id = dht->entries[dht->used].id;
                dht->entries[handle_index].target = dht->entries[dht->used].target;
                handle_index--;
                handles_cleared++;
                break;
            }
        }
    }
    if (handles_cleared != argument->handle_count) {
        return 1;
    } else {
        return 0;
    }
}

static MVMuint64 allocate_handle(MVMThreadContext *dtc, MVMObject *target) {
    if (!target) {
        return 0;
    } else {
        MVMDebugServerHandleTable *dht = dtc->instance->debugserver->handle_table;

        MVMuint64 id = dht->next_id++;

        if (dht->used + 1 > dht->allocated) {
            if (dht->allocated < 8192)
                dht->allocated *= 2;
            else
                dht->allocated += 1024;
            dht->entries = MVM_realloc(dht->entries, sizeof(MVMDebugServerHandleTableEntry) * dht->allocated);
        }

        dht->entries[dht->used].id = id;
        dht->entries[dht->used].target = target;
        dht->used++;

        return id;
    }
}

static MVMuint64 allocate_and_send_handle(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument, MVMObject *target) {
    MVMuint64 id = allocate_handle(dtc, target);
    cmp_write_map(ctx, 3);
    cmp_write_str(ctx, "id", 2);
    cmp_write_integer(ctx, argument->id);
    cmp_write_str(ctx, "type", 4);
    cmp_write_integer(ctx, MT_HandleResult);
    cmp_write_str(ctx, "handle", 6);
    cmp_write_integer(ctx, id);

    return id;
}

static MVMObject *find_handle_target(MVMThreadContext *dtc, MVMuint64 id) {
    MVMDebugServerHandleTable *dht = dtc->instance->debugserver->handle_table;
    MVMuint32 index;

    for (index = 0; index < dht->used; index++) {
        if (dht->entries[index].id == id)
            return dht->entries[index].target;
    }
    return NULL;
}

static MVMint32 create_context_or_code_obj_debug_handle(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument, MVMThread *thread) {
    MVMInstance *vm = dtc->instance;
    MVMThread *to_do = thread ? thread : find_thread_by_id(vm, argument->thread_id);

    if (!to_do) {
        if (dtc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "no thread found for context/code obj handle (or thread not eligible)\n");
        return 1;
    }

    if ((to_do->body.tc->gc_status & MVMGCSTATUS_MASK) != MVMGCStatus_UNABLE) {
        if (dtc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "can only retrieve a context or code obj handle if thread is 'UNABLE' (is %d)\n", to_do->body.tc->gc_status);
        return 1;
    }

    {

    MVMFrame *cur_frame = to_do->body.tc->cur_frame;
    MVMuint32 frame_idx;

    for (frame_idx = 0;
            cur_frame && frame_idx < argument->frame_number;
            frame_idx++, cur_frame = cur_frame->caller) { }

    if (!cur_frame) {
        if (dtc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "couldn't create context/coderef handle: no such frame %d\n", argument->frame_number);
        return 1;
    }

    if (argument->type == MT_ContextHandle) {
        MVMROOT(dtc, cur_frame, {
            allocate_and_send_handle(dtc, ctx, argument, MVM_frame_context_wrapper(to_do->body.tc, cur_frame));
        });
    } else if (argument->type == MT_CodeObjectHandle) {
        allocate_and_send_handle(dtc, ctx, argument, cur_frame->code_ref);
    } else {
        if (dtc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "Did not expect to see create_context_or_code_obj_debug_handle called with a %d type\n", argument->type);
        return 1;
    }

    }

    return 0;
}

static MVMint32 create_caller_or_outer_context_debug_handle(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument, MVMThread *thread) {
    MVMObject *this_ctx = argument->handle_id
        ? find_handle_target(dtc, argument->handle_id)
        : dtc->instance->VMNull;

    MVMFrame *frame;
    if (!IS_CONCRETE(this_ctx) || REPR(this_ctx)->ID != MVM_REPR_ID_MVMContext) {
        fprintf(stderr, "outer/caller context handle must refer to a definite MVMContext object\n");
        return 1;
    }
    if (argument->type == MT_OuterContextRequest) {
        if ((frame = ((MVMContext *)this_ctx)->body.context->outer))
            this_ctx = MVM_frame_context_wrapper(dtc, frame);
    } else if (argument->type == MT_CallerContextRequest) {
        if ((frame = ((MVMContext *)this_ctx)->body.context->caller))
            this_ctx = MVM_frame_context_wrapper(dtc, frame);
    }

    allocate_and_send_handle(dtc, ctx, argument, this_ctx);
    return 0;
}

static MVMint32 request_context_lexicals(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMObject *this_ctx = argument->handle_id
        ? find_handle_target(dtc, argument->handle_id)
        : dtc->instance->VMNull;
    MVMStaticFrame *static_info;
    MVMLexicalRegistry *lexical_names;

    MVMFrame *frame;
    if (MVM_is_null(dtc, this_ctx) || !IS_CONCRETE(this_ctx) || REPR(this_ctx)->ID != MVM_REPR_ID_MVMContext) {
        fprintf(stderr, "getting lexicals: context handle must refer to a definite MVMContext object\n");
        return 1;
    }
    if (!(frame = ((MVMContext *)this_ctx)->body.context)) {
        fprintf(stderr, "context doesn't have a frame?!\n");
        return 1;
    }

    static_info = frame->static_info;
    lexical_names = static_info->body.lexical_names;
    if (lexical_names) {
        MVMLexicalRegistry *entry, *tmp;
        unsigned bucket_tmp;
        MVMuint64 lexcount = HASH_CNT(hash_handle, lexical_names);
        MVMuint64 lexical_index = 0;

        cmp_write_map(ctx, 3);
        cmp_write_str(ctx, "id", 2);
        cmp_write_integer(ctx, argument->id);
        cmp_write_str(ctx, "type", 4);
        cmp_write_integer(ctx, MT_ContextLexicalsResponse);

        cmp_write_str(ctx, "lexicals", 8);
        cmp_write_map(ctx, lexcount);

        if (dtc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "will write %d lexicals\n", lexcount);

        HASH_ITER(hash_handle, lexical_names, entry, tmp, bucket_tmp) {
            MVMuint16 lextype = static_info->body.lexical_types[entry->value];
            MVMRegister *result = &frame->env[entry->value];
            char *c_key_name;

            if (entry->key && IS_CONCRETE(entry->key))
                c_key_name = MVM_string_utf8_encode_C_string(dtc, entry->key);
            else {
                c_key_name = MVM_malloc(12 + 16);
                sprintf(c_key_name, "<lexical %d>", lexical_index);
            }

            cmp_write_str(ctx, c_key_name, strlen(c_key_name));

            MVM_free(c_key_name);

            if (lextype == MVM_reg_obj) { /* Object */
                char *debugname;

                if (!result->o) {
                    /* XXX this can't allocate? */
                    MVM_frame_vivify_lexical(dtc, frame, entry->value);
                }

                cmp_write_map(ctx, 5);

                cmp_write_str(ctx, "kind", 4);
                cmp_write_str(ctx, "obj", 3);

                cmp_write_str(ctx, "handle", 6);
                cmp_write_integer(ctx, allocate_handle(dtc, result->o));

                debugname = MVM_6model_get_debug_name(dtc, result->o);

                cmp_write_str(ctx, "type", 4);
                cmp_write_str(ctx, debugname, strlen(debugname));

                cmp_write_str(ctx, "concrete", 8);
                cmp_write_bool(ctx, IS_CONCRETE(result->o));

                cmp_write_str(ctx, "container", 9);
                cmp_write_bool(ctx, STABLE(result->o)->container_spec == NULL ? 0 : 1);
            } else {
                cmp_write_map(ctx, 2);

                cmp_write_str(ctx, "kind", 4);
                cmp_write_str(ctx,
                        lextype == MVM_reg_int64 ? "int" :
                        lextype == MVM_reg_num32 ? "num" :
                        lextype == MVM_reg_str   ? "str" :
                        "???", 3);

                cmp_write_str(ctx, "value", 5);
                if (lextype == MVM_reg_int64) {
                    cmp_write_integer(ctx, result->i64);
                } else if (lextype == MVM_reg_num64) {
                    cmp_write_double(ctx, result->n64);
                } else if (lextype == MVM_reg_str) {
                    if (result->s && IS_CONCRETE(result->s)) {
                        char *c_value = MVM_string_utf8_encode_C_string(dtc, result->s);
                        cmp_write_str(ctx, c_value, strlen(c_value));
                        MVM_free(c_value);
                    } else {
                        cmp_write_nil(ctx);
                    }
                } else {
                    fprintf(stderr, "what lexical type is %d supposed to be?\n", lextype);
                    cmp_write_nil(ctx);
                }
            }
            if (dtc->instance->debugserver->debugspam_protocol)
                fprintf(stderr, "wrote a lexical\n");
            lexical_index++;
        }
    } else {
        cmp_write_map(ctx, 3);
        cmp_write_str(ctx, "id", 2);
        cmp_write_integer(ctx, argument->id);
        cmp_write_str(ctx, "type", 4);
        cmp_write_integer(ctx, MT_ContextLexicalsResponse);

        cmp_write_str(ctx, "lexicals", 8);
        cmp_write_map(ctx, 0);
    }
    if (dtc->instance->debugserver->debugspam_protocol)
        fprintf(stderr, "done writing lexicals\n");
    return 0;
}

MVM_STATIC_INLINE MVMObject * get_obj_at_offset(void *data, MVMint64 offset) {
    void *location = (char *)data + offset;
    return *((MVMObject **)location);
}
static MVMint32 request_object_attributes(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMObject *target = argument->handle_id
        ? find_handle_target(dtc, argument->handle_id)
        : dtc->instance->VMNull;

    if (MVM_is_null(dtc, target)) {
        return 1;
    }

    if (!IS_CONCRETE(target)) {
        return 1;
    }

    cmp_write_map(ctx, 3);
    cmp_write_str(ctx, "id", 2);
    cmp_write_integer(ctx, argument->id);
    cmp_write_str(ctx, "type", 4);
    cmp_write_integer(ctx, MT_ObjectAttributesResponse);

    cmp_write_str(ctx, "attributes", 10);

    if (REPR(target)->ID != MVM_REPR_ID_P6opaque) {
        cmp_write_array(ctx, 0);
        return 0;
    } else {
        MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)STABLE(target)->REPR_data;
        MVMP6opaqueBody *data = MVM_p6opaque_real_data(dtc, OBJECT_BODY(target));
        if (repr_data) {
            MVMint16 const num_attributes = repr_data->num_attributes;
            MVMP6opaqueNameMap * const name_to_index_mapping = repr_data->name_to_index_mapping;

            cmp_write_array(ctx, num_attributes);

            if (name_to_index_mapping != NULL) {
                MVMint16 i;
                MVMP6opaqueNameMap *cur_map_entry = name_to_index_mapping;

                while (cur_map_entry->class_key != NULL) {
                    MVMint16 i;
                    MVMint64 slot;
                    char *class_name = MVM_6model_get_stable_debug_name(dtc, cur_map_entry->class_key->st);

                    for (i = 0; i < cur_map_entry->num_attrs; i++) {
                        char * name = MVM_string_utf8_encode_C_string(dtc, cur_map_entry->names[i]);

                        slot = cur_map_entry->slots[i];
                        if (slot >= 0) {
                            MVMuint16 const offset = repr_data->attribute_offsets[slot];
                            MVMSTable * const attr_st = repr_data->flattened_stables[slot];
                            if (attr_st == NULL) {
                                MVMObject *value = get_obj_at_offset(data, offset);
                                char *value_debug_name = value ? MVM_6model_get_debug_name(dtc, value) : "VMNull";

                                cmp_write_map(ctx, 7);

                                cmp_write_str(ctx, "name", 4);
                                cmp_write_str(ctx, name, strlen(name));

                                cmp_write_str(ctx, "class", 5);
                                cmp_write_str(ctx, class_name, strlen(class_name));

                                cmp_write_str(ctx, "kind", 4);
                                cmp_write_str(ctx, "obj", 3);

                                cmp_write_str(ctx, "handle", 6);
                                cmp_write_integer(ctx, allocate_handle(dtc, value));

                                cmp_write_str(ctx, "type", 4);
                                cmp_write_str(ctx, value_debug_name, strlen(value_debug_name));

                                cmp_write_str(ctx, "concrete", 8);
                                cmp_write_bool(ctx, !MVM_is_null(dtc, value) && IS_CONCRETE(value));

                                cmp_write_str(ctx, "container", 9);
                                if (MVM_is_null(dtc, value))
                                    cmp_write_bool(ctx, 0);
                                else
                                    cmp_write_bool(ctx, STABLE(value)->container_spec == NULL ? 0 : 1);
                            }
                            else {
                                const MVMStorageSpec *attr_storage_spec = attr_st->REPR->get_storage_spec(dtc, attr_st);

                                cmp_write_map(ctx, 4);

                                cmp_write_str(ctx, "name", 4);
                                cmp_write_str(ctx, name, strlen(name));

                                cmp_write_str(ctx, "class", 5);
                                cmp_write_str(ctx, class_name, strlen(class_name));

                                cmp_write_str(ctx, "kind", 4);

                                switch (attr_storage_spec->boxed_primitive) {
                                    case MVM_STORAGE_SPEC_BP_INT:
                                        cmp_write_integer(ctx, attr_st->REPR->box_funcs.get_int(dtc, attr_st, target, (char *)data + offset));
                                        break;
                                    case MVM_STORAGE_SPEC_BP_NUM:
                                        cmp_write_double(ctx, attr_st->REPR->box_funcs.get_num(dtc, attr_st, target, (char *)data + offset));
                                        break;
                                    case MVM_STORAGE_SPEC_BP_STR: {
                                        MVMString * const s = attr_st->REPR->box_funcs.get_str(dtc, attr_st, target, (char *)data + offset);
                                        char * const str = MVM_string_utf8_encode_C_string(dtc, s);
                                        cmp_write_str(ctx, str, strlen(str));
                                        MVM_free(str);
                                        break;
                                    }
                                }
                            }
                        }

                        MVM_free(name);
                    }
                    cur_map_entry++;
                }
            }
        }
        else {
            cmp_write_str(ctx, "error: not composed yet", 22);
            return 0;
        }
    }

    return 0;
}

MVMuint8 debugspam_network;

static bool socket_reader(cmp_ctx_t *ctx, void *data, size_t limit) {
    size_t idx;
    size_t total_read = 0;
    size_t read;
    MVMuint8 *orig_data = (MVMuint8 *)data;
    if (debugspam_network)
        fprintf(stderr, "asked to read %d bytes\n", limit);
    while (total_read < limit) {
        if ((read = recv(*((Socket*)ctx->buf), data, limit, 0)) == -1) {
            if (debugspam_network)
                fprintf(stderr, "minus one\n");
            return 0;
        } else if (read == 0) {
            if (debugspam_network)
                fprintf(stderr, "end of file - socket probably closed\nignore warnings about parse errors\n");
            return 0;
        }
        if (debugspam_network)
            fprintf(stderr, "%d ", read);
        data = (void *)(((MVMuint8*)data) + read);
        total_read += read;
    }

    if (debugspam_network) {
        fprintf(stderr, "... recv received %d bytes\n", total_read);
        fprintf(stderr, "cmp read: ");
        for (idx = 0; idx < limit; idx++) {
            fprintf(stderr, "%x ", orig_data[idx]);
        }
        fprintf(stderr, "\n");
    }
    return 1;
}

static size_t socket_writer(cmp_ctx_t *ctx, const void *data, size_t limit) {
    size_t idx;
    size_t total_sent = 0;
    size_t sent;
    MVMuint8 *orig_data = (MVMuint8 *)data;
    if (debugspam_network)
        fprintf(stderr, "asked to send %3d bytes: ", limit);
    while (total_sent < limit) {
        if ((sent = send(*(Socket*)ctx->buf, data, limit, 0)) == -1) {
            if (debugspam_network)
                fprintf(stderr, "but couldn't (socket disconnected?)\n");
            return 0;
        } else if (sent == 0) {
            if (debugspam_network)
                fprintf(stderr, "send encountered end of file\n");
            return 0;
        }
        if (debugspam_network)
            fprintf(stderr, "%2d ", sent);
        data = (void *)(((MVMuint8*)data) + sent);
        total_sent += sent;
    }
    if (debugspam_network)
        fprintf(stderr, "... send sent %3d bytes\n", total_sent);
    return 1;
}

static bool is_valid_int(cmp_object_t *obj, MVMint64 *result) {
    switch (obj->type) {
        case CMP_TYPE_POSITIVE_FIXNUM:
        case CMP_TYPE_UINT8:
            *result = obj->as.u8;
            break;
        case CMP_TYPE_UINT16:
            *result = obj->as.u16;
            break;
        case CMP_TYPE_UINT32:
            *result = obj->as.u32;
            break;
        case CMP_TYPE_UINT64:
            *result = obj->as.u64;
            break;
        case CMP_TYPE_NEGATIVE_FIXNUM:
        case CMP_TYPE_SINT8:
            *result = obj->as.s8;
            break;
        case CMP_TYPE_SINT16:
            *result = obj->as.s16;
            break;
        case CMP_TYPE_SINT32:
            *result = obj->as.s32;
            break;
        case CMP_TYPE_SINT64:
            *result = obj->as.s64;
            break;
        case CMP_TYPE_BOOLEAN:
            *result = obj->as.boolean;
            break;
        default:
            return 0;
    }
    return 1;
}

#define CHECK(operation, message) do { if(!(operation)) { data->parse_fail = 1; data->parse_fail_message = (message);fprintf(stderr, "CMP error: %s\n", cmp_strerror(ctx)); return 0; } } while(0)
#define FIELD_FOUND(field, duplicated_message) do { if(data->fields_set & (field)) { data->parse_fail = 1; data->parse_fail_message = duplicated_message;  return 0; }; field_to_set = (field); } while (0)

MVMint32 parse_message_map(cmp_ctx_t *ctx, request_data *data) {
    MVMuint32 map_size = 0;
    MVMuint32 i;
    cmp_object_t obj;

    memset(data, 0, sizeof(request_data));

    CHECK(cmp_read_object(ctx, &obj), "couldn't read envelope object!");

    switch (obj.type) {
        case CMP_TYPE_FIXMAP:
        case CMP_TYPE_MAP16:
        case CMP_TYPE_MAP32:
            map_size = obj.as.map_size;
            break;
        default:
            fprintf(stderr, "expected a map, but got %d\n", obj.type);
            data->parse_fail = 1;
            data->parse_fail_message = "expected a map as the envelope";
            return 0;
    }

    for (i = 0; i < map_size; i++) {
        char key_str[16];
        MVMuint32 str_size = 16;

        fields_set field_to_set = 0;
        MVMuint32  type_to_parse = 0;

        CHECK(cmp_read_str(ctx, key_str, &str_size), "Couldn't read string key");

        if (strncmp(key_str, "type", 15) == 0) {
            FIELD_FOUND(FS_type, "type field duplicated");
            type_to_parse = 1;
        } else if (strncmp(key_str, "id", 15) == 0) {
            FIELD_FOUND(FS_id, "id field duplicated");
            type_to_parse = 1;
        } else if (strncmp(key_str, "thread", 15) == 0) {
            FIELD_FOUND(FS_thread_id, "thread field duplicated");
            type_to_parse = 1;
        } else if (strncmp(key_str, "frame", 15) == 0) {
            FIELD_FOUND(FS_frame_number, "frame number field duplicated");
            type_to_parse = 1;
        } else if (strncmp(key_str, "handle", 15) == 0) {
            FIELD_FOUND(FS_handle_id, "handle field duplicated");
            type_to_parse = 1;
        } else if (strncmp(key_str, "line", 15) == 0) {
            FIELD_FOUND(FS_line, "line field duplicated");
            type_to_parse = 1;
        } else if (strncmp(key_str, "suspend", 15) == 0) {
            FIELD_FOUND(FS_suspend, "suspend field duplicated");
            type_to_parse = 1;
        } else if (strncmp(key_str, "stacktrace", 15) == 0) {
            FIELD_FOUND(FS_stacktrace, "stacktrace field duplicated");
            type_to_parse = 1;
        } else if (strncmp(key_str, "file", 15) == 0) {
            FIELD_FOUND(FS_file, "file field duplicated");
            type_to_parse = 2;
        } else if (strncmp(key_str, "handles", 15) == 0) {
            FIELD_FOUND(FS_handles, "handles field duplicated");
            type_to_parse = 3;
        } else {
            fprintf(stderr, "the hell is a %s?\n", key_str);
            data->parse_fail = 1;
            data->parse_fail_message = "Unknown field encountered (NYI or protocol violation)";
            return 0;
        }

        if (type_to_parse == 1) {
            cmp_object_t object;
            MVMuint64 result;
            CHECK(cmp_read_object(ctx, &object), "Couldn't read value for a key");
            CHECK(is_valid_int(&object, &result), "Couldn't read integer value for a key");
            switch (field_to_set) {
                case FS_type:
                    data->type = result;
                    break;
                case FS_id:
                    data->id = result;
                    break;
                case FS_thread_id:
                    data->thread_id = result;
                    break;
                case FS_frame_number:
                    data->frame_number = result;
                    break;
                case FS_handle_id:
                    data->handle_id = result;
                    break;
                case FS_line:
                    data->line = result;
                    break;
                case FS_suspend:
                    data->suspend = result;
                    break;
                case FS_stacktrace:
                    data->stacktrace = result;
                    break;
                default:
                    data->parse_fail = 1;
                    data->parse_fail_message = "Int field to set NYI";
                    return 0;
            }
            data->fields_set = data->fields_set | field_to_set;
        } else if (type_to_parse == 2) {
            uint32_t strsize = 1024;
            char *string = MVM_calloc(strsize, sizeof(char));
            fprintf(stderr, "reading a string for %s\n", key_str);
            CHECK(cmp_read_str(ctx, string, &strsize), "Couldn't read string for a key");

            switch (field_to_set) {
                case FS_file:
                    data->file = string;
                    break;
                default:
                    data->parse_fail = 1;
                    data->parse_fail_message = "Str field to set NYI";
                    return 0;
            }
            data->fields_set = data->fields_set | field_to_set;
        } else if (type_to_parse == 3) {
            uint32_t arraysize = 0;
            uint32_t index;
            CHECK(cmp_read_array(ctx, &arraysize), "Couldn't read array for a key");
            data->handle_count = arraysize;
            data->handles = MVM_malloc(arraysize * sizeof(MVMuint64));
            for (index = 0; index < arraysize; index++) {
                cmp_object_t object;
                MVMuint64 result;
                CHECK(cmp_read_object(ctx, &object), "Couldn't read value for a key");
                CHECK(is_valid_int(&object, &result), "Couldn't read integer value for a key");
                data->handles[index] = result;
            }
        }
    }

    return check_requirements(data);
}

#define COMMUNICATE_RESULT(operation) do { if((operation)) { communicate_error(&ctx, &argument); } else { communicate_success(&ctx, &argument); } } while (0)
#define COMMUNICATE_ERROR(operation) do { if((operation)) { communicate_error(&ctx, &argument); } } while (0)

static void debugserver_worker(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    int continue_running = 1;
    MVMint32 command_serial;
    Socket listensocket;
    MVMInstance *vm = tc->instance;
    MVMuint64 port = vm->debugserver->port;

    vm->debugserver->thread_id = tc->thread_obj->body.thread_id;

    {
        char portstr[16];
        struct addrinfo *res;
        int error;

        snprintf(portstr, 16, "%d", port);

        getaddrinfo("localhost", portstr, NULL, &res);

        listensocket = socket(res->ai_family, SOCK_STREAM, 0);

#ifndef _WIN32
        {
            int one = 1;
            setsockopt(listensocket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        }
#endif

        if (bind(listensocket, res->ai_addr, res->ai_addrlen) == -1) {
            MVM_panic(1, "Could not bind to socket: %s", strerror(errno));
        }

        freeaddrinfo(res);

        if (listen(listensocket, 1) == -1) {
            MVM_panic(1, "Could not listen on socket: %s", strerror(errno));
        }
    }

    while(continue_running) {
        Socket clientsocket;
        int len;
        char *buffer[32];
        cmp_ctx_t ctx;

        MVM_gc_mark_thread_blocked(tc);
        clientsocket = accept(listensocket, NULL, NULL);
        MVM_gc_mark_thread_unblocked(tc);

        send_greeting(&clientsocket);

        if (!receive_greeting(&clientsocket)) {
            fprintf(stderr, "did not receive greeting properly\n");
            close(clientsocket);
            continue;
        }

        cmp_init(&ctx, &clientsocket, socket_reader, NULL, socket_writer);

        vm->debugserver->messagepack_data = (void*)&ctx;

        while (clientsocket) {
            request_data argument;

            MVM_gc_mark_thread_blocked(tc);
            parse_message_map(&ctx, &argument);
            MVM_gc_mark_thread_unblocked(tc);

            uv_mutex_lock(&vm->debugserver->mutex_network_send);

            if (argument.parse_fail) {
                fprintf(stderr, "failed to parse this message: %s\n", argument.parse_fail_message);
                cmp_write_map(&ctx, 3);

                cmp_write_str(&ctx, "id", 2);
                cmp_write_integer(&ctx, argument.id);

                cmp_write_str(&ctx, "type", 4);
                cmp_write_integer(&ctx, 1);

                cmp_write_str(&ctx, "reason", 6);
                cmp_write_str(&ctx, argument.parse_fail_message, strlen(argument.parse_fail_message));
                close(clientsocket);
                break;
            }

            if (vm->debugserver->debugspam_protocol)
                fprintf(stderr, "debugserver received packet %d, command %d\n", argument.id, argument.type);

            switch (argument.type) {
                case MT_IsExecutionSuspendedRequest:
                    send_is_execution_suspended_info(tc, &ctx, &argument);
                    break;
                case MT_SuspendAll:
                    COMMUNICATE_ERROR(request_all_threads_suspend(tc, &ctx, &argument));
                    break;
                case MT_ResumeAll:
                    COMMUNICATE_ERROR(request_all_threads_resume(tc, &ctx, &argument));
                    break;
                case MT_SuspendOne:
                    COMMUNICATE_ERROR(request_thread_suspends(tc, &ctx, &argument, NULL));
                    break;
                case MT_ResumeOne:
                    COMMUNICATE_ERROR(request_thread_resumes(tc, &ctx, &argument, NULL));
                    break;
                case MT_ThreadListRequest:
                    send_thread_info(tc, &ctx, &argument);
                    break;
                case MT_ThreadStackTraceRequest:
                    if (request_thread_stacktrace(tc, &ctx, &argument, NULL)) {
                        communicate_error(&ctx, &argument);
                    }
                    break;
                case MT_SetBreakpointRequest:
                    MVM_debugserver_add_breakpoint(tc, &ctx, &argument);
                    break;
                case MT_ClearBreakpoint:
                    MVM_debugserver_clear_breakpoint(tc, &ctx, &argument);
                    break;
                case MT_ClearAllBreakpoints:
                    MVM_debugserver_clear_all_breakpoints(tc, &ctx, &argument);
                    break;
                case MT_ObjectAttributesRequest:
                    if (request_object_attributes(tc, &ctx, &argument)) {
                        communicate_error(&ctx, &argument);
                    }
                    break;
                case MT_ReleaseHandles:
                    COMMUNICATE_RESULT(release_handles(tc, &ctx, &argument));
                    MVM_free(argument.handles);
                    break;
                case MT_ContextHandle:
                case MT_CodeObjectHandle:
                    if (create_context_or_code_obj_debug_handle(tc, &ctx, &argument, NULL)) {
                        communicate_error(&ctx, &argument);
                    }
                    break;
                case MT_CallerContextRequest:
                case MT_OuterContextRequest:
                    if (create_caller_or_outer_context_debug_handle(tc, &ctx, &argument, NULL)) {
                        communicate_error(&ctx, &argument);
                    }
                    break;
                case MT_ContextLexicalsRequest:
                    if (request_context_lexicals(tc, &ctx, &argument)) {
                        communicate_error(&ctx, &argument);
                    }
                    break;
                default: /* Unknown command or NYI */
                    fprintf(stderr, "unknown command type (or NYI)\n");
                    cmp_write_map(&ctx, 2);
                    cmp_write_str(&ctx, "id", 2);
                    cmp_write_integer(&ctx, argument.id);
                    cmp_write_str(&ctx, "type", 4);
                    cmp_write_integer(&ctx, 0);
                    break;
            }

            uv_mutex_unlock(&vm->debugserver->mutex_network_send);
        }
        MVM_debugserver_clear_all_breakpoints(tc, NULL, NULL);
        release_all_handles(tc);
        vm->debugserver->messagepack_data = NULL;
    }
}

/* XXX stolen verbatim from src/moar.c; maybe put into a header somewhere */
#define init_mutex(loc, name) do { \
    if ((init_stat = uv_mutex_init(&loc)) < 0) { \
        fprintf(stderr, "MoarVM: Initialization of " name " mutex failed\n    %s\n", \
            uv_strerror(init_stat)); \
        exit(1); \
    } \
} while (0)
#define init_cond(loc, name) do { \
    if ((init_stat = uv_cond_init(&loc)) < 0) { \
        fprintf(stderr, "MoarVM: Initialization of " name " condition variable failed\n    %s\n", \
            uv_strerror(init_stat)); \
        exit(1); \
    } \
} while (0)
void MVM_debugserver_init(MVMThreadContext *tc, MVMuint32 port) {
    MVMInstance *vm = tc->instance;
    MVMDebugServerData *debugserver = MVM_calloc(1, sizeof(MVMDebugServerData));
    MVMObject *worker_entry_point;
    int threadCreateError;
    int init_stat;

    init_mutex(debugserver->mutex_cond, "debug server orchestration");
    init_mutex(debugserver->mutex_network_send, "debug server network socket lock");
    init_mutex(debugserver->mutex_request_list, "debug server request list lock");
    init_mutex(debugserver->mutex_breakpoints, "debug server breakpoint management lock");
    init_cond(debugserver->tell_threads, "debugserver signals threads");
    init_cond(debugserver->tell_worker, "threads signal debugserver");

    debugserver->handle_table = MVM_malloc(sizeof(MVMDebugServerHandleTable));

    debugserver->handle_table->allocated = 32;
    debugserver->handle_table->used      = 0;
    debugserver->handle_table->next_id   = 1;
    debugserver->handle_table->entries   = MVM_calloc(debugserver->handle_table->allocated, sizeof(MVMDebugServerHandleTableEntry));

    debugserver->breakpoints = MVM_malloc(sizeof(MVMDebugServerBreakpointTable));

    debugserver->breakpoints->files_alloc = 32;
    debugserver->breakpoints->files_used  = 0;
    debugserver->breakpoints->files       = MVM_calloc(debugserver->breakpoints->files_alloc, sizeof(MVMDebugServerBreakpointFileTable));

    debugserver->event_id = 2;
    debugserver->port = port;

    if (getenv("MDS_NETWORK")) {
        debugspam_network = 1;
        debugserver->debugspam_network = 1;
    } else {
        debugspam_network = 0;
    }
    if (getenv("MDS_PROTOCOL")) {
        debugserver->debugspam_protocol = 1;
    }

    vm->debugserver = debugserver;

    worker_entry_point = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTCCode);
    ((MVMCFunction *)worker_entry_point)->body.func = debugserver_worker;
    MVM_thread_run(tc, MVM_thread_new(tc, worker_entry_point, 1));
}

void MVM_debugserver_mark_handles(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot) {
    MVMInstance *vm = tc->instance;
    if (vm->debugserver) {
        MVMDebugServerHandleTable *table = vm->debugserver->handle_table;
        MVMuint32 idx;

        if (table == NULL)
            return;

        for (idx = 0; idx < table->used; idx++) {
            if (worklist)
                MVM_gc_worklist_add(tc, worklist, &(table->entries[idx].target));
            else
                MVM_profile_heap_add_collectable_rel_const_cstr(tc, snapshot,
                    (MVMCollectable *)table->entries[idx].target, "Debug Handle");
        }
    }
}
