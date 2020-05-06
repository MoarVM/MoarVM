#include "moar.h"

#define DEBUGSERVER_MAJOR_PROTOCOL_VERSION 1
#define DEBUGSERVER_MINOR_PROTOCOL_VERSION 3

#define bool int
#define true TRUE
#define false FALSE

#include "cmp.h"
#include "platform/socket.h"

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
    MT_OperationUnsuccessful,
    MT_ObjectMetadataRequest,
    MT_ObjectMetadataResponse,
    MT_ObjectPositionalsRequest,
    MT_ObjectPositionalsResponse,
    MT_ObjectAssociativesRequest,
    MT_ObjectAssociativesResponse,
    MT_HandleEquivalenceRequest,
    MT_HandleEquivalenceResponse,
    MT_HLLSymbolRequest,
    MT_HLLSymbolResponse,
} message_type;

typedef enum {
    ArgKind_Handle,
    ArgKind_Integer,
    ArgKind_Num,
    ArgKind_String,
} argument_kind;

typedef struct {
    MVMuint8 arg_kind;
    /* In order to pass an existing object we have a handle to as a string (as
     * defined by the callsite we generate) we need to differentiate between
     * kind set to string and the .o entry being set. */
    MVMuint8 str_uses_handle;
    /* If a string was passed via messagepack, store the full length here
     * (since strings are not necessarily null-terminated) */
    MVMuint64 string_length;
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
    FS_name       = 2048,
    FS_hll        = 4096,
} fields_set;

typedef struct {
    MVMuint16 type;
    MVMuint64 id;

    MVMuint32 thread_id;

    char *file;
    MVMuint32 line;

    MVMuint8  suspend;
    MVMuint8  stacktrace;

    MVMuint16 handle_count;
    MVMuint64 *handles;

    MVMuint64 handle_id;

    MVMuint32 frame_number;

    char *name;

    MVMuint32 argument_count;
    argument_data *arguments;

    MVMuint8  parse_fail;
    const char *parse_fail_message;

    char *hll;

    fields_set fields_set;
} request_data;

static void write_stacktrace_frames(MVMThreadContext *dtc, cmp_ctx_t *ctx, MVMThread *thread);
static MVMint32 request_all_threads_suspend(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument);
static MVMuint64 allocate_handle(MVMThreadContext *dtc, MVMObject *target);

/* Breakpoint stuff */
MVM_PUBLIC void MVM_debugserver_register_line(MVMThreadContext *tc, char *filename, MVMuint32 filename_len, MVMuint32 line_no,  MVMuint32 *file_idx) {
    MVMDebugServerData *debugserver = tc->instance->debugserver;
    MVMDebugServerBreakpointTable *table = debugserver->breakpoints;
    MVMDebugServerBreakpointFileTable *found = NULL;
    MVMuint32 index = 0;

    char *open_paren_pos = (char *)memchr(filename, '(', filename_len);

    if (open_paren_pos) {
        if (open_paren_pos[-1] == ' ') {
            filename_len = open_paren_pos - filename - 1;
        }
    }

    uv_mutex_lock(&debugserver->mutex_breakpoints);

    if (*file_idx < table->files_used) {
        MVMDebugServerBreakpointFileTable *file = &table->files[*file_idx];
        if (file->filename_length == filename_len && memcmp(file->filename, filename, filename_len) == 0)
            found = file;
    }

    if (!found) {
        for (index = 0; index < table->files_used; index++) {
            MVMDebugServerBreakpointFileTable *file = &table->files[index];
            if (file->filename_length != filename_len)
                continue;
            if (memcmp(file->filename, filename, filename_len) != 0)
                continue;
            found = file;
            *file_idx = index;
            break;
        }
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
                fprintf(stderr, "table for files increased to %u slots\n", table->files_alloc);
        }

        found = &table->files[table->files_used - 1];

        found->filename = MVM_calloc(filename_len + 1, sizeof(char));
        strncpy(found->filename, filename, filename_len);

        if (tc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "created new file entry at %u for %s\n", table->files_used - 1, found->filename);

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
            fprintf(stderr, "increasing line number table for %s from %u to %u slots\n", found->filename, old_size, found->lines_active_alloc);
        found->lines_active = MVM_fixed_size_realloc_at_safepoint(tc, tc->instance->fsa,
                found->lines_active, old_size, found->lines_active_alloc);
        memset((char *)found->lines_active + old_size, 0, found->lines_active_alloc - old_size - 1);
    }

    uv_mutex_unlock(&debugserver->mutex_breakpoints);
}

static void stop_point_hit(MVMThreadContext *tc) {
    tc->debugserver_can_invoke_here = 1;

    while (1) {
        /* We're in total regular boring execution. Set ourselves to
         * interrupted for suspend reasons */
        if (MVM_cas(&tc->gc_status, MVMGCStatus_NONE, MVMGCStatus_INTERRUPT | MVMSuspendState_SUSPEND_REQUEST)
                == MVMGCStatus_NONE) {
            break;
        }
        /* Looks like another thread just interrupted us; join in on GC and
         * then this loop will store the suspend request flag when we're back
         * to MVMGCStatus_NONE. */
        if (MVM_load(&tc->gc_status) == MVMGCStatus_INTERRUPT) {
            MVM_gc_enter_from_interrupt(tc);
        }
        /* Perhaps the debugserver just asked us to suspend, too. It's not
         * important for our suspend request flag to survive or something. */
        if (MVM_load(&tc->gc_status) == (MVMGCStatus_INTERRUPT | MVMSuspendState_SUSPEND_REQUEST)) {
            break;
        }
    }
    MVM_gc_enter_from_interrupt(tc);

    tc->debugserver_can_invoke_here = 0;
}

static MVMuint8 breakpoint_hit(MVMThreadContext *tc, MVMDebugServerBreakpointFileTable *file, MVMuint32 line_no) {
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

    return must_suspend;
}
static void step_point_hit(MVMThreadContext *tc) {
    cmp_ctx_t *ctx = (cmp_ctx_t*)tc->instance->debugserver->messagepack_data;

    uv_mutex_lock(&tc->instance->debugserver->mutex_network_send);
    cmp_write_map(ctx, 4);
    cmp_write_str(ctx, "id", 2);
    cmp_write_integer(ctx, tc->step_message_id);
    cmp_write_str(ctx, "type", 4);
    cmp_write_integer(ctx, MT_StepCompleted);
    cmp_write_str(ctx, "thread", 6);
    cmp_write_integer(ctx, tc->thread_id);
    cmp_write_str(ctx, "frames", 6);
    write_stacktrace_frames(tc, ctx, tc->thread_obj);
    uv_mutex_unlock(&tc->instance->debugserver->mutex_network_send);

    tc->step_mode = MVMDebugSteppingMode_NONE;
    tc->step_mode_frame = NULL;
}

MVM_PUBLIC MVMint32 MVM_debugserver_breakpoint_check(MVMThreadContext *tc, MVMuint32 file_idx, MVMuint32 line_no) {
    MVMDebugServerData *debugserver = tc->instance->debugserver;
    MVMuint8 shall_suspend = 0;

    if (debugserver->any_breakpoints_at_all && (file_idx != tc->cur_file_idx || line_no != tc->cur_line_no)) {
        MVMDebugServerBreakpointTable *table = debugserver->breakpoints;
        MVMDebugServerBreakpointFileTable *found = &table->files[file_idx];

        if (debugserver->any_breakpoints_at_all && found->breakpoints_used && found->lines_active[line_no]) {
            shall_suspend |= breakpoint_hit(tc, found, line_no);
        }
    }

    tc->cur_line_no = line_no;
    tc->cur_file_idx = file_idx;

    if (tc->step_mode) {
        if (tc->step_mode == MVMDebugSteppingMode_STEP_OVER) {
            if (line_no != tc->step_mode_line_no && tc->step_mode_frame == tc->cur_frame) {

                if (tc->instance->debugserver->debugspam_protocol)
                    fprintf(stderr, "hit a stepping point: step over; %u != %u, %p == %p\n", line_no, tc->step_mode_line_no, tc->step_mode_frame, tc->cur_frame);
                step_point_hit(tc);
                shall_suspend = 1;
            }
        }
        else if (tc->step_mode == MVMDebugSteppingMode_STEP_INTO) {
            if ((line_no != tc->step_mode_line_no && tc->step_mode_frame == tc->cur_frame)
                    || tc->step_mode_frame != tc->cur_frame) {
                if (tc->instance->debugserver->debugspam_protocol) {
                    if (line_no != tc->step_mode_line_no && tc->step_mode_frame == tc->cur_frame)
                        fprintf(stderr, "hit a stepping point: step into; %u != %u, %p == %p\n",
                                line_no, tc->step_mode_line_no, tc->step_mode_frame, tc->cur_frame);
                    else
                        fprintf(stderr, "hit a stepping point: step into; %u,   %u, %p != %p\n",
                                line_no, tc->step_mode_line_no, tc->step_mode_frame, tc->cur_frame);
                }
                step_point_hit(tc);
                shall_suspend = 1;
            }
        }
        /* Nothing to do for STEP_OUT. */
        /* else if (tc->step_mode == MVMDebugSteppingMode_STEP_OUT) { } */
    }

    if (shall_suspend)
        stop_point_hit(tc);

    return 0;
}


#define REQUIRE(field, message) do { if (!(data->fields_set & (field))) { data->parse_fail = 1; data->parse_fail_message = (message); return 0; }; accepted = accepted | (field); } while (0)

MVMuint8 check_requirements(MVMThreadContext *tc, request_data *data) {
    fields_set accepted = FS_id | FS_type;

    MVMuint8 allow_optional = 0;

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

        case MT_HandleEquivalenceRequest:
        case MT_ReleaseHandles:
            REQUIRE(FS_handles, "A handles field is required");
            break;

        case MT_FindMethod:
            REQUIRE(FS_name, "A name field is required");
            /* Fall-Through */
        case MT_DecontainerizeHandle:
            REQUIRE(FS_thread_id, "A thread field is required");
            /* Fall-Through */
        case MT_ContextLexicalsRequest:
        case MT_OuterContextRequest:
        case MT_CallerContextRequest:
        case MT_ObjectAttributesRequest:
        case MT_ObjectMetadataRequest:
        case MT_ObjectPositionalsRequest:
        case MT_ObjectAssociativesRequest:
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

        case MT_HLLSymbolRequest:
            allow_optional = 1;
            break;

        default:
            break;
    }

    if (data->fields_set != accepted && !allow_optional) {
        if (tc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "debugserver: too many fields in message of type %d: accepted 0x%x, got 0x%x\n", data->type, accepted, data->fields_set);
    }

    return 1;
}

static MVMuint16 big_endian_16(MVMuint16 number) {
#ifdef MVM_BIGENDIAN
    return number;
#else
    char *bytes = (char *)&number;
    char tmp;
    tmp = bytes[1];
    bytes[1] = bytes[0];
    bytes[0] = tmp;
    return *((MVMuint16 *)bytes);
#endif
}

static void send_greeting(MVMSocket *sock) {
    char buffer[24] = "MOARVM-REMOTE-DEBUG\0";
    MVMuint16 version = big_endian_16(DEBUGSERVER_MAJOR_PROTOCOL_VERSION);
    MVMuint16 *verptr = (MVMuint16 *)(&buffer[strlen("MOARVM-REMOTE-DEBUG") + 1]);

    *verptr = version;
    verptr++;

    version = big_endian_16(DEBUGSERVER_MINOR_PROTOCOL_VERSION);

    *verptr = version;
    send(*sock, buffer, 24, 0);
}

static int receive_greeting(MVMSocket *sock) {
    const char *expected = "MOARVM-REMOTE-CLIENT-OK";
    char buffer[24];
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

static void communicate_error(MVMThreadContext *tc, cmp_ctx_t *ctx, request_data *argument) {
    if (argument) {
        if (tc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "communicating an error\n");
        cmp_write_map(ctx, 2);
        cmp_write_str(ctx, "id", 2);
        cmp_write_integer(ctx, argument->id);
        cmp_write_str(ctx, "type", 4);
        cmp_write_integer(ctx, MT_ErrorProcessingMessage);
    }
}

static void communicate_success(MVMThreadContext *tc, cmp_ctx_t *ctx, request_data *argument) {
    if (argument) {
        if (tc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "communicating success\n");
        cmp_write_map(ctx, 2);
        cmp_write_str(ctx, "id", 2);
        cmp_write_integer(ctx, argument->id);
        cmp_write_str(ctx, "type", 4);
        cmp_write_integer(ctx, MT_OperationSuccessful);
    }
}

/* Send spontaneous events */
MVM_PUBLIC void MVM_debugserver_notify_thread_creation(MVMThreadContext *tc) {
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

MVM_PUBLIC void MVM_debugserver_notify_thread_destruction(MVMThreadContext *tc) {
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

MVM_PUBLIC void MVM_debugserver_notify_unhandled_exception(MVMThreadContext *tc, MVMException *ex) {
    if (tc->instance->debugserver && tc->instance->debugserver->messagepack_data) {
        cmp_ctx_t *ctx = (cmp_ctx_t*)tc->instance->debugserver->messagepack_data;
        MVMuint64 event_id;

        uv_mutex_lock(&tc->instance->debugserver->mutex_network_send);

        MVMROOT(tc, ex, {
            request_all_threads_suspend(tc, ctx, NULL);
        });

        event_id = tc->instance->debugserver->event_id;
        tc->instance->debugserver->event_id += 2;

        cmp_write_map(ctx, 5);
        cmp_write_str(ctx, "id", 2);
        cmp_write_integer(ctx, event_id);
        cmp_write_str(ctx, "type", 4);
        cmp_write_integer(ctx, MT_UnhandledException);

        cmp_write_str(ctx, "handle", 6);
        cmp_write_integer(ctx, allocate_handle(tc, (MVMObject *)ex));

        cmp_write_str(ctx, "thread", 6);
        cmp_write_integer(ctx, tc->thread_obj->body.thread_id);

        cmp_write_str(ctx, "frames", 6);
        write_stacktrace_frames(tc, ctx, tc->thread_obj);

        uv_mutex_unlock(&tc->instance->debugserver->mutex_network_send);

        MVM_gc_enter_from_interrupt(tc);
    }
}

static MVMuint8 is_thread_id_eligible(MVMInstance *vm, MVMuint32 id) {
    if (id == vm->debugserver->thread_id || id == vm->speshworker_thread_id) {
        return 0;
    }
    return 1;
}

/* Send replies to requests send by the client */

static MVMThread *find_thread_by_id(MVMInstance *vm, MVMuint32 id) {
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
        /* Is the thread currently doing completely ordinary code execution? */
        if (MVM_cas(&tc->gc_status, MVMGCStatus_NONE, MVMGCStatus_INTERRUPT | MVMSuspendState_SUSPEND_REQUEST)
                == MVMGCStatus_NONE) {
            break;
        }
        /* Is the thread in question currently blocked, i.e. spending time in
         * some long-running piece of C code, waiting for I/O, etc.?
         * If so, just store the suspend request bit so when it unblocks itself
         * it'll suspend execution. */
        if (MVM_cas(&tc->gc_status, MVMGCStatus_UNABLE, MVMGCStatus_UNABLE | MVMSuspendState_SUSPEND_REQUEST)
                == MVMGCStatus_UNABLE) {
            break;
        }
        /* Was the thread faster than us? For example by running into
         * a breakpoint, completing a step, or encountering an
         * unhandled exception? If so, we're done here. */
        if ((MVM_load(&tc->gc_status) & MVMSUSPENDSTATUS_MASK) == MVMSuspendState_SUSPEND_REQUEST) {
            break;
        }
        MVM_platform_thread_yield();
    }

    if (argument && argument->type == MT_SuspendOne)
        communicate_success(tc, ctx,  argument);

    MVM_gc_mark_thread_unblocked(dtc);
    if (tc->instance->debugserver->debugspam_protocol)
        fprintf(stderr, "thread %u successfully suspended\n", tc->thread_id);

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
        communicate_success(dtc, ctx, argument);
    else
        communicate_error(dtc, ctx, argument);

    uv_mutex_unlock(&vm->mutex_threads);

    return success;
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
        fprintf(stderr, "wrong state to resume from: %zu\n", MVM_load(&tc->gc_status));
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
                if (MVM_cas(&tc->gc_status, current, MVMGCStatus_UNABLE) == current) {
                    break;
                }
            }
        }
    }

    MVM_gc_mark_thread_unblocked(dtc);

    if (argument && argument->type == MT_ResumeOne)
        communicate_success(tc, ctx, argument);

    if (tc->instance->debugserver->debugspam_protocol)
        fprintf(stderr, "success resuming thread; its status is now %zu\n", MVM_load(&tc->gc_status));

    return 0;
}

static MVMint32 request_all_threads_resume(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMInstance *vm = dtc->instance;
    MVMThread *cur_thread = 0;
    MVMuint8 success = 1;

    uv_mutex_lock(&vm->mutex_threads);
    cur_thread = vm->threads;
    MVMROOT(dtc, cur_thread, {
        while (cur_thread) {
            if (cur_thread != dtc->thread_obj) {
                AO_t current = MVM_load(&cur_thread->body.tc->gc_status);
                if (current == (MVMGCStatus_UNABLE | MVMSuspendState_SUSPENDED) ||
                        current == (MVMGCStatus_INTERRUPT | MVMSuspendState_SUSPEND_REQUEST) ||
                        current == (MVMGCStatus_STOLEN | MVMSuspendState_SUSPEND_REQUEST)) {
                    if (request_thread_resumes(dtc, ctx, argument, cur_thread)) {
                        if (vm->debugserver->debugspam_protocol)
                            fprintf(stderr, "failure to resume thread %u\n", cur_thread->body.thread_id);
                        success = 0;
                        break;
                    }
                }
            }
            cur_thread = cur_thread->body.next;
        }
    });

    if (success)
        communicate_success(dtc, ctx, argument);
    else
        communicate_error(dtc, ctx, argument);

    uv_mutex_unlock(&vm->mutex_threads);

    return !success;
}

static void write_stacktrace_frames(MVMThreadContext *dtc, cmp_ctx_t *ctx, MVMThread *thread) {
    MVMThreadContext *tc = thread->body.tc;
    MVMuint64 stack_size = 0;

    MVMFrame *cur_frame = tc->cur_frame;

    while (cur_frame != NULL) {
        stack_size++;
        cur_frame = cur_frame->caller;
    }

    if (tc->instance->debugserver->debugspam_protocol)
        fprintf(stderr, "dumping a stack trace of %"PRIu64" frames\n", stack_size);

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
        MVMuint16 string_heap_index = annot ? annot->filename_string_heap_index : 1;

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

        MVMObject *code_ref = cur_frame->code_ref;
        MVMCode *code_obj = code_ref && REPR(code_ref)->ID == MVM_REPR_ID_MVMCode ? (MVMCode*)code_ref : NULL;
        char *debugname = code_obj && code_obj->body.code_object ? MVM_6model_get_debug_name(tc, code_obj->body.code_object) : "";

        MVM_free(annot);

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
        char *threadname = NULL;
#if MVM_HAS_PTHREAD_SETNAME_NP
        threadname = MVM_malloc(16);
        if (pthread_getname_np((pthread_t)cur_thread->body.native_thread_id, threadname, 16) != 0) {
            MVM_free_null(threadname);
        }
#endif

        cmp_write_map(ctx, 5 + (threadname != NULL && strlen(threadname)));

        cmp_write_str(ctx, "thread", 6);
        cmp_write_integer(ctx, cur_thread->body.thread_id);

        cmp_write_str(ctx, "native_id", 9);
        cmp_write_integer(ctx, cur_thread->body.native_thread_id);

        cmp_write_str(ctx, "app_lifetime", 12);
        cmp_write_bool(ctx, cur_thread->body.app_lifetime);

        cmp_write_str(ctx, "suspended", 9);
        cmp_write_bool(ctx, (MVM_load(&cur_thread->body.tc->gc_status) & MVMSUSPENDSTATUS_MASK) != MVMSuspendState_NONE);

        cmp_write_str(ctx, "num_locks", 9);
        cmp_write_integer(ctx, MVM_thread_lock_count(dtc, (MVMObject *)cur_thread));

        if (threadname != NULL && strlen(threadname)) {
            cmp_write_str(ctx, "name", 4);
            cmp_write_str(ctx, threadname, strlen(threadname));
        }
        MVM_free(threadname);

        cur_thread = cur_thread->body.next;
    }
    uv_mutex_unlock(&vm->mutex_threads);
}

static void send_is_execution_suspended_info(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
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

MVMuint8 setup_step(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument, MVMDebugSteppingMode mode, MVMThread *thread) {
    MVMThread *to_do = thread ? thread : find_thread_by_id(dtc->instance, argument->thread_id);
    MVMThreadContext *tc;

    if (!to_do)
        return 1;

    if ((to_do->body.tc->gc_status & MVMGCSTATUS_MASK) != MVMGCStatus_UNABLE) {
        return 1;
    }

    tc = to_do->body.tc;
    tc->step_mode_frame = tc->cur_frame;
    tc->step_message_id = argument->id;
    tc->step_mode_line_no = tc->cur_line_no;
    tc->step_mode_file_idx = tc->cur_file_idx;

    tc->step_mode = mode;

    request_thread_resumes(dtc, ctx, NULL, to_do);

    return 0;
}

void MVM_debugserver_add_breakpoint(MVMThreadContext *tc, cmp_ctx_t *ctx, request_data *argument) {
    MVMDebugServerData *debugserver = tc->instance->debugserver;
    MVMDebugServerBreakpointTable *table = debugserver->breakpoints;
    MVMDebugServerBreakpointFileTable *found = NULL;
    MVMDebugServerBreakpointInfo *bp_info = NULL;
    MVMuint32 index = 0;

    if (tc->instance->debugserver->debugspam_protocol)
        fprintf(stderr, "asked to set a breakpoint for file %s line %"PRIu32" to send id %"PRIu64"\n", argument->file, argument->line, argument->id);

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
            fprintf(stderr, "table for breakpoints increased to %"PRIu32" slots\n", found->breakpoints_alloc);
    }

    bp_info = &found->breakpoints[found->breakpoints_used - 1];

    bp_info->breakpoint_id = argument->id;
    bp_info->line_no = argument->line;
    bp_info->shall_suspend = argument->suspend;
    bp_info->send_backtrace = argument->stacktrace;

    debugserver->any_breakpoints_at_all++;

    if (tc->instance->debugserver->debugspam_protocol)
        fprintf(stderr, "breakpoint settings: index %"PRIu32" bpid %"PRIu64" lineno %"PRIu32" suspend %"PRIu32" backtrace %"PRIu32"\n", found->breakpoints_used - 1, argument->id, argument->line, argument->suspend, argument->stacktrace);

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
        communicate_success(tc, ctx, argument);
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
        fprintf(stderr, "asked to clear breakpoints for file %s line %"PRIu32"\n", argument->file, argument->line);

    uv_mutex_lock(&debugserver->mutex_breakpoints);

    found = &table->files[index];

    if (tc->instance->debugserver->debugspam_protocol) {
        fprintf(stderr, "dumping all breakpoints\n");
        for (bpidx = 0; bpidx < found->breakpoints_used; bpidx++) {
            MVMDebugServerBreakpointInfo *bp_info = &found->breakpoints[bpidx];
            fprintf(stderr, "breakpoint index %"PRIu32" has id %"PRIu64", is at line %"PRIu32"\n", bpidx, bp_info->breakpoint_id, bp_info->line_no);
        }
    }

    if (tc->instance->debugserver->debugspam_protocol)
        fprintf(stderr, "trying to clear breakpoints\n\n");
    for (bpidx = 0; bpidx < found->breakpoints_used; bpidx++) {
        MVMDebugServerBreakpointInfo *bp_info = &found->breakpoints[bpidx];
        if (tc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "breakpoint index %"PRIu32" has id %"PRIu64", is at line %"PRIu32"\n", bpidx, bp_info->breakpoint_id, bp_info->line_no);

        if (bp_info->line_no == argument->line) {
            if (tc->instance->debugserver->debugspam_protocol)
                fprintf(stderr, "breakpoint with id %"PRIu64" cleared\n", bp_info->breakpoint_id);
            found->breakpoints[bpidx] = found->breakpoints[--found->breakpoints_used];
            num_cleared++;
            bpidx--;
            debugserver->any_breakpoints_at_all--;
        }
    }

    uv_mutex_unlock(&debugserver->mutex_breakpoints);

    if (num_cleared > 0)
        communicate_success(tc, ctx, argument);
    else
        communicate_error(tc, ctx, argument);
}

static void release_all_handles(MVMThreadContext *dtc) {
    MVMDebugServerHandleTable *dht = dtc->instance->debugserver->handle_table;
    dht->used = 0;
    dht->next_id = 1;
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
    if (!target || MVM_is_null(dtc, target)) {
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

static MVMuint64 find_representant(MVMuint16 *representant, MVMuint64 index) {
    MVMuint64 last = index;

    while (representant[last] != last) {
        last = representant[last];
    }

    return last;
}

static void send_handle_equivalence_classes(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMuint16  *representant = MVM_calloc(argument->handle_count, sizeof(MVMuint64));
    MVMObject **objects      = MVM_calloc(argument->handle_count, sizeof(MVMObject *));
    MVMuint16  *counts       = MVM_calloc(argument->handle_count, sizeof(MVMuint16));
    MVMuint16   idx;
    MVMuint16   classes_count = 0;

    /* Set up our sentinel values. Any object that represents itself
     * is the end of a chain. At the beginning, everything represents
     * itself.
     * Critically, this allows us to use 0 as a valid representant. */
    for (idx = 0; idx < argument->handle_count; idx++) {
        representant[idx] = idx;
    }

    for (idx = 0; idx < argument->handle_count; idx++) {
        MVMuint16 other_idx;
        objects[idx] = find_handle_target(dtc, argument->handles[idx]);

        for (other_idx = 0; other_idx < idx; other_idx++) {
            if (representant[other_idx] != other_idx)
                continue;
            if (objects[idx] == objects[other_idx]) {
                representant[other_idx] = idx;
            }
        }
    }

    /* First, we have to count how many distinct classes there are.
     * Whenever we hit 2, we know we've found a class. */
    for (idx = 0; idx < argument->handle_count; idx++) {
        MVMuint16 the_repr = find_representant(representant, idx);
        counts[the_repr]++;
        if (counts[the_repr] == 2)
            classes_count++;
    }

    /* Send the header of the message */
    cmp_write_map(ctx, 3);
    cmp_write_str(ctx, "id", 2);
    cmp_write_integer(ctx, argument->id);
    cmp_write_str(ctx, "type", 4);
    cmp_write_integer(ctx, MT_HandleEquivalenceResponse);
    cmp_write_str(ctx, "classes", 7);

    /* Now we can write out the classes by following the representant
     * chain until we find one whose representant is itself. */
    cmp_write_array(ctx, classes_count);
    for (idx = 0; idx < argument->handle_count; idx++) {
        if (representant[idx] != idx) {
            MVMuint16 count = counts[find_representant(representant, idx)];
            MVMuint16 pointer = idx;
            cmp_write_array(ctx, count);
            do {
                MVMuint16 current_representant = representant[pointer];
                representant[pointer] = pointer;
                cmp_write_integer(ctx, argument->handles[pointer]);
                pointer = current_representant;
            } while (representant[pointer] != pointer);

            cmp_write_integer(ctx, argument->handles[pointer]);
        }
    }

    MVM_free(representant);
    MVM_free(objects);
    MVM_free(counts);
}

static MVMuint64 request_hll_symbol_data(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMInstance *vm = dtc->instance;

    MVMHash *hll_syms;
    MVMHash *hll_hash;
    MVMStrHashTable *hashtable;

    MVMString *hll_name_str = NULL;
    MVMString *key_str = NULL;
    
    if (argument->fields_set & FS_hll) {
        hll_name_str = MVM_string_utf8_decode(dtc, vm->VMString, argument->hll, strlen(argument->hll));
    }
    MVM_gc_root_temp_push(dtc, (MVMCollectable **)&hll_name_str);

    if (argument->fields_set & FS_name) {
        key_str  = MVM_string_utf8_decode(dtc, vm->VMString, argument->name, strlen(argument->name));
    }
    MVM_gc_root_temp_push(dtc, (MVMCollectable **)&key_str);

    if (!(vm->hll_syms) || !IS_CONCRETE(vm->hll_syms)) {
        if (dtc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "No HLL syms hash found in instance ?!?\n");
        
        MVM_gc_root_temp_pop_n(dtc, 2);
        return 1;
    }

    uv_mutex_lock(&vm->mutex_hll_syms);

    hll_syms = (MVMHash *)vm->hll_syms;
    hashtable = &(hll_syms->body.hashtable);

    if (!(argument->fields_set & FS_hll)) {
        /* First variant: return all knows HLLs */
        MVMuint64 num_hlls = MVM_str_hash_count(dtc, hashtable);
        MVMStrHashIterator iterator = MVM_str_hash_first(dtc, hashtable);

        cmp_write_map(ctx, 3);

        cmp_write_str(ctx, "type", 4);
        cmp_write_integer(ctx, MT_HLLSymbolResponse);

        cmp_write_str(ctx, "id", 2);
        cmp_write_integer(ctx, argument->id);

        cmp_write_str(ctx, "keys", 4);
        cmp_write_array(ctx, num_hlls);
        while (!MVM_str_hash_at_end(dtc, hashtable, iterator)) {
            MVMHashEntry *current = MVM_str_hash_current_nocheck(dtc, hashtable, iterator);
            MVMString *key = current->hash_handle.key;
            char *key_cstr = MVM_string_utf8_encode_C_string(dtc, key);

            cmp_write_str(ctx, key_cstr, strlen(key_cstr));
            MVM_free(key_cstr);

            iterator = MVM_str_hash_next_nocheck(dtc, hashtable, iterator);
        }

        MVM_gc_root_temp_pop_n(dtc, 2);
        uv_mutex_unlock(&vm->mutex_hll_syms);
        return 0;
    }

    if (!MVM_repr_exists_key(dtc, (MVMObject *)hll_syms, hll_name_str)) {
        if (dtc->instance->debugserver->debugspam_protocol) {
            fprintf(stderr, "No HLL registered for this name: %s\n", argument->hll);
        }

        MVM_gc_root_temp_pop_n(dtc, 2);
        uv_mutex_unlock(&vm->mutex_hll_syms);
        return 1;
    }

    hll_hash = (MVMHash *)MVM_repr_at_key_o(dtc, (MVMObject *)hll_syms, hll_name_str);
    hashtable = &(hll_hash->body.hashtable);

    if (!(argument->fields_set & FS_name)) {
        /* Second variant: return all keys of this HLLs */
        MVMuint64 num_keys = MVM_str_hash_count(dtc, hashtable);
        MVMStrHashIterator iterator = MVM_str_hash_first(dtc, hashtable);

        cmp_write_map(ctx, 3);

        cmp_write_str(ctx, "type", 4);
        cmp_write_integer(ctx, MT_HLLSymbolResponse);

        cmp_write_str(ctx, "id", 2);
        cmp_write_integer(ctx, argument->id);

        cmp_write_str(ctx, "keys", 4);
        cmp_write_array(ctx, num_keys);
        while (!MVM_str_hash_at_end(dtc, hashtable, iterator)) {
            MVMHashEntry *current = MVM_str_hash_current_nocheck(dtc, hashtable, iterator);
            MVMString *key = current->hash_handle.key;
            char *key_cstr = MVM_string_utf8_encode_C_string(dtc, key);

            cmp_write_str(ctx, key_cstr, strlen(key_cstr));
            MVM_free(key_cstr);

            iterator = MVM_str_hash_next_nocheck(dtc, hashtable, iterator);
        }

        MVM_gc_root_temp_pop_n(dtc, 2);
        uv_mutex_unlock(&vm->mutex_hll_syms);
        return 0;
    }

    if (!MVM_repr_exists_key(dtc, (MVMObject *)hll_hash, key_str)) {
        if (dtc->instance->debugserver->debugspam_protocol) {
            fprintf(stderr, "This HLL has nothing for key %s\n", argument->name);
        }

        MVM_gc_root_temp_pop_n(dtc, 2);
        uv_mutex_unlock(&vm->mutex_hll_syms);
        return 1;
    }
    else {
        MVMObject *value = MVM_repr_at_key_o(dtc, (MVMObject *)hll_hash, key_str);
        allocate_and_send_handle(dtc, ctx, argument, value);

        MVM_gc_root_temp_pop_n(dtc, 2);
        uv_mutex_unlock(&vm->mutex_hll_syms);
        return 0;
    }
}

static MVMuint64 request_find_method(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMInstance *vm = dtc->instance;
    MVMThread *to_do = find_thread_by_id(vm, argument->thread_id);
    MVMObject *target = find_handle_target(dtc, argument->handle_id);
    MVMThreadContext *tc;
    MVMString *method_name = NULL;

    if (!to_do) {
        if (dtc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "no thread found for context/code obj handle (or thread not eligible)\n");
        return 1;
    }

    tc = to_do->body.tc;

    if ((to_do->body.tc->gc_status & MVMGCSTATUS_MASK) != MVMGCStatus_UNABLE) {
        if (dtc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "can only retrieve a context or code obj handle if thread is 'UNABLE' (is %zu)\n", to_do->body.tc->gc_status);
        return 1;
    }

    if (!target) {
        if (dtc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "could not retrieve object of handle %"PRId64, argument->handle_id);
        return 1;
    }

    MVM_gc_allocate_gen2_default_set(tc);
    method_name = MVM_string_utf8_decode(tc, tc->instance->VMString, argument->name, strlen(argument->name));
    MVM_gc_allocate_gen2_default_clear(tc);

    allocate_and_send_handle(dtc, ctx, argument, MVM_spesh_try_find_method(tc, target, method_name));

    return 0;
}

typedef struct {
    MVMuint64 id;
    MVMRegister return_target;
} DebugserverInvocationSpecialReturnData;

static void debugserver_invocation_special_return(MVMThreadContext *tc, void *data_in) {
    DebugserverInvocationSpecialReturnData *data = (DebugserverInvocationSpecialReturnData *)data_in;
    cmp_ctx_t *ctx = (cmp_ctx_t*)tc->instance->debugserver->messagepack_data;

    uv_mutex_lock(&tc->instance->debugserver->mutex_network_send);

    switch (tc->cur_frame->return_type) {
        case MVM_RETURN_VOID:
            cmp_write_map(ctx, 4);
            cmp_write_str(ctx, "type", 4);
            cmp_write_int(ctx, MT_InvokeResult);
            cmp_write_str(ctx, "id", 2);
            cmp_write_int(ctx, data->id);
            cmp_write_str(ctx, "crashed", 7);
            cmp_write_false(ctx);
            cmp_write_str(ctx, "kind", 4);
            cmp_write_str(ctx, "void", 4);
            break;
        case MVM_RETURN_OBJ: {
            char *typename = MVM_6model_get_debug_name(tc, data->return_target.o);
            cmp_write_map(ctx, 8);
            cmp_write_str(ctx, "type", 4);
            cmp_write_int(ctx, MT_InvokeResult);
            cmp_write_str(ctx, "id", 2);
            cmp_write_int(ctx, data->id);
            cmp_write_str(ctx, "crashed", 7);
            cmp_write_false(ctx);
            cmp_write_str(ctx, "kind", 4);
            cmp_write_str(ctx, "obj", 3);
            cmp_write_str(ctx, "handle", 6);
            cmp_write_int(ctx, allocate_handle(tc, data->return_target.o));
            cmp_write_str(ctx, "obj_type", 8);
            cmp_write_str(ctx, typename, strlen(typename));
            cmp_write_str(ctx, "concrete", 8);
            cmp_write_bool(ctx, IS_CONCRETE(data->return_target.o));
            cmp_write_str(ctx, "container", 9);
            cmp_write_bool(ctx, STABLE(data->return_target.o)->container_spec == NULL ? 0 : 1);
            break;
        }
        case MVM_RETURN_INT:
            cmp_write_map(ctx, 5);
            cmp_write_str(ctx, "type", 4);
            cmp_write_int(ctx, MT_InvokeResult);
            cmp_write_str(ctx, "id", 2);
            cmp_write_int(ctx, data->id);
            cmp_write_str(ctx, "crashed", 7);
            cmp_write_false(ctx);
            cmp_write_str(ctx, "kind", 4);
            cmp_write_str(ctx, "int", 3);
            cmp_write_str(ctx, "value", 5);
            cmp_write_int(ctx, data->return_target.i64);
            break;
        case MVM_RETURN_NUM:
            cmp_write_map(ctx, 5);
            cmp_write_str(ctx, "type", 4);
            cmp_write_int(ctx, MT_InvokeResult);
            cmp_write_str(ctx, "id", 2);
            cmp_write_int(ctx, data->id);
            cmp_write_str(ctx, "crashed", 7);
            cmp_write_false(ctx);
            cmp_write_str(ctx, "kind", 4);
            cmp_write_str(ctx, "num", 3);
            cmp_write_str(ctx, "value", 5);
            cmp_write_float(ctx, data->return_target.n64);
            break;
        case MVM_RETURN_STR: {
            /* TODO handle strings with null bytes in them */
            char *str_result = MVM_string_utf8_encode_C_string(tc, data->return_target.s);
            MVMuint64 handle = allocate_handle(tc, (MVMObject *)data->return_target.s);
            cmp_write_map(ctx, 6);
            cmp_write_str(ctx, "type", 4);
            cmp_write_int(ctx, MT_InvokeResult);
            cmp_write_str(ctx, "id", 2);
            cmp_write_int(ctx, data->id);
            cmp_write_str(ctx, "crashed", 7);
            cmp_write_false(ctx);
            cmp_write_str(ctx, "kind", 4);
            cmp_write_str(ctx, "str", 3);
            cmp_write_str(ctx, "value", 5);
            cmp_write_str(ctx, str_result, strlen(str_result));
            cmp_write_str(ctx, "handle", 6);
            cmp_write_int(ctx, handle);
            MVM_free(str_result);
            break;
        }
        default:
            MVM_panic(1, "Debugserver: Did not understand return type of invoked frame.");
    }

    uv_mutex_unlock(&tc->instance->debugserver->mutex_network_send);

    request_thread_suspends(tc, NULL, NULL, tc->thread_obj);
    // tc->cur_frame->caller->return_type = data->orig_return_type;
}

static void debugserver_invocation_special_unwind(MVMThreadContext *tc, void *data_in) {
    // DebugserverInvocationSpecialReturnData *data = (DebugserverInvocationSpecialReturnData *)data_in;
    MVM_panic(1, "Debugserver: Handling exceptions thrown in invoked code NYI.");
}


static MVMuint64 request_invoke_code(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMInstance *vm = dtc->instance;
    MVMThread *to_do = find_thread_by_id(vm, argument->thread_id);
    MVMObject *target = find_handle_target(dtc, argument->handle_id);
    MVMThreadContext *tc;
    MVMCallsite *cs = NULL;
    MVMRegister *arguments_to_pass = NULL;
    MVMDebugServerData *debugserver = vm->debugserver;

    if (!to_do) {
        if (vm->debugserver->debugspam_protocol)
            fprintf(stderr, "no thread found for context/code obj handle (or thread not eligible)\n");
        return 1;
    }

    tc = to_do->body.tc;

    if ((to_do->body.tc->gc_status & MVMGCSTATUS_MASK) != MVMGCStatus_UNABLE) {
        if (vm->debugserver->debugspam_protocol)
            fprintf(stderr, "can only retrieve a context or code obj handle if thread is 'UNABLE' (is %zu)\n", to_do->body.tc->gc_status);
        return 1;
    }

    if (!target) {
        if (vm->debugserver->debugspam_protocol)
            fprintf(stderr, "could not retrieve object of handle %"PRId64, argument->handle_id);
        return 1;
    }

    if (REPR(target)->ID != MVM_REPR_ID_MVMCode) {
        if (vm->debugserver->debugspam_protocol)
            fprintf(stderr, "object of handle %"PRId64" is not an MVMCode, it's a %s", argument->handle_id, REPR(target)->name);
        return 1;
    }

    if (debugserver->request_data.kind != MVM_DebugRequest_empty) {
        if (vm->debugserver->debugspam_protocol)
            fprintf(stderr, "can't start a debug request when another is currently active.");
        return 1;
    }

    if (!tc->debugserver_can_invoke_here) {
        if (vm->debugserver->debugspam_protocol)
            fprintf(stderr, "can't request an invocation unless execution is halted at a breakpoint or from stepping.");

        cmp_write_map(ctx, 3);

        cmp_write_str(ctx, "id", 2);
        cmp_write_integer(ctx, argument->id);

        cmp_write_str(ctx, "type", 4);
        cmp_write_integer(ctx, MT_ErrorProcessingMessage);

        cmp_write_str(ctx, "reason", 6);
        cmp_write_str(ctx, "execution not halted at a break/step point", 42);

        return 2;
    }

    {
        /* Time to create a callsite from the arguments we've received */
        MVMuint64 index;
        cs = MVM_malloc(sizeof(MVMCallsite));
        cs->flag_count = argument->argument_count;
        cs->arg_count  = argument->argument_count;
        cs->num_pos    = argument->argument_count;
        cs->has_flattening = 0;
        cs->is_interned = 0;
        cs->with_invocant = NULL;
        cs->arg_names = NULL;

        cs->arg_flags = MVM_malloc(sizeof(MVMCallsiteEntry) * cs->flag_count);
        arguments_to_pass = MVM_malloc(sizeof(MVMRegister) * cs->num_pos);

        for (index = 0; index < cs->flag_count; index++)
        {
            if (argument->arguments[index].arg_kind == MVM_reg_int64) {
                cs->arg_flags[index] = MVM_CALLSITE_ARG_INT;
                arguments_to_pass[index].i64 = argument->arguments[index].arg_u.i;
            }
            else if (argument->arguments[index].arg_kind == MVM_reg_num64) {
                cs->arg_flags[index] = MVM_CALLSITE_ARG_NUM;
                arguments_to_pass[index].n64 = argument->arguments[index].arg_u.n;
            }
            else if (argument->arguments[index].arg_kind == MVM_reg_str) {
                if (argument->arguments[index].str_uses_handle) {
                    MVMObject *target = find_handle_target(dtc, argument->arguments[index].arg_u.o);
                    cs->arg_flags[index] = MVM_CALLSITE_ARG_STR;
                    arguments_to_pass[index].s = (MVMString *)target;
                }
                else {
                    MVMString *target;
                    /* NYI, errors out in parse_message_map already */
                    MVM_gc_allocate_gen2_default_set(dtc);

                    /* TODO support for null bytes in strings */
                    target = MVM_string_utf8_decode(dtc, vm->VMString, argument->arguments[index].arg_u.s, strlen(argument->arguments[index].arg_u.s));

                    arguments_to_pass[index].s = target;
                    cs->arg_flags[index] = MVM_CALLSITE_ARG_STR;

                    MVM_gc_allocate_gen2_default_clear(dtc);
                }
            }
            else if (argument->arguments[index].arg_kind == MVM_reg_obj) {
                MVMObject *target = find_handle_target(dtc, argument->arguments[index].arg_u.o);
                cs->arg_flags[index] = MVM_CALLSITE_ARG_OBJ;
                arguments_to_pass[index].o = target;
            }
        }

        tc->cur_frame->return_value = NULL;
        tc->cur_frame->return_type  = MVM_RETURN_VOID;

        DebugserverInvocationSpecialReturnData *srd = MVM_calloc(sizeof(DebugserverInvocationSpecialReturnData), 1);

        srd->id = argument->id;

        MVM_args_setup_thunk(tc, &srd->return_target, MVM_RETURN_ALLOMORPH, cs);
        MVM_frame_special_return(tc, tc->cur_frame,
            debugserver_invocation_special_return,
            debugserver_invocation_special_unwind,
            (void *)srd, NULL);
        /* XXX how to find out how much space there is?
           Or maybe always point to our own arguments_to_pass and mark and delete it
           using the special return mechanism? */
        memcpy(tc->cur_frame->args, arguments_to_pass, sizeof(MVMRegister) * cs->flag_count);

        debugserver->request_data.kind = MVM_DebugRequest_invoke;
        debugserver->request_data.target_tc = tc;
        debugserver->request_data.data.invoke.target = target;
        debugserver->request_data.request_id = argument->id;

        MVM_store(&debugserver->request_data.status, MVM_DebugRequestStatus_sender_is_waiting);

        uv_cond_broadcast(&debugserver->tell_threads);

        while (1) {
            if (MVM_cas(&debugserver->request_data.status,
                    MVM_DebugRequestStatus_receiver_acknowledged,
                    MVM_DebugRequestStatus_sender_is_waiting) == MVM_DebugRequestStatus_receiver_acknowledged) {
                if (vm->debugserver->debugspam_protocol)
                    fprintf(stderr, "debugserver acknowledges thread's acknowledgement.\n");
                break;
            }
        }

        // STABLE(target)->invoke(tc, target, cs, tc->cur_frame->args);

        communicate_success(dtc, ctx, argument);

        return 0;
    }
}

static MVMuint64 request_object_decontainerize(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMInstance *vm = dtc->instance;
    MVMThread *to_do = find_thread_by_id(vm, argument->thread_id);
    MVMObject *target = find_handle_target(dtc, argument->handle_id);

    if (!to_do) {
        if (dtc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "no thread found for context/code obj handle (or thread not eligible)\n");
        return 1;
    }

    if ((to_do->body.tc->gc_status & MVMGCSTATUS_MASK) != MVMGCStatus_UNABLE) {
        if (dtc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "can only retrieve a context or code obj handle if thread is 'UNABLE' (is %zu)\n", to_do->body.tc->gc_status);
        return 1;
    }

    if (!target) {
        if (dtc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "could not retrieve object of handle %"PRId64, argument->handle_id);
        return 1;
    }

    if (STABLE(target)->container_spec && STABLE(target)->container_spec->fetch_never_invokes) {
        MVMRegister r;
        STABLE(target)->container_spec->fetch(dtc, target, &r);
        allocate_and_send_handle(dtc, ctx, argument, r.o);
    }

    return 0;
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
            fprintf(stderr, "can only retrieve a context or code obj handle if thread is 'UNABLE' (is %zu)\n", to_do->body.tc->gc_status);
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
            if (MVM_FRAME_IS_ON_CALLSTACK(dtc, cur_frame)) {
                cur_frame = MVM_frame_debugserver_move_to_heap(dtc, to_do->body.tc, cur_frame);
            }
            allocate_and_send_handle(dtc, ctx, argument, MVM_context_from_frame(dtc, cur_frame));
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
    if (!this_ctx || !IS_CONCRETE(this_ctx) || REPR(this_ctx)->ID != MVM_REPR_ID_MVMContext) {
        if (dtc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "outer/caller context handle must refer to a definite MVMContext object\n");
        return 1;
    }
    if (argument->type == MT_OuterContextRequest) {
        if ((frame = ((MVMContext *)this_ctx)->body.context->outer))
            this_ctx = MVM_context_from_frame(dtc, frame);
    } else if (argument->type == MT_CallerContextRequest) {
        if ((frame = ((MVMContext *)this_ctx)->body.context->caller))
            this_ctx = MVM_context_from_frame(dtc, frame);
    }

    allocate_and_send_handle(dtc, ctx, argument, this_ctx);
    return 0;
}

static void write_one_context_lexical(MVMThreadContext *dtc, cmp_ctx_t *ctx, const char *c_key_name,
        MVMuint16 lextype, MVMRegister *result) {
    cmp_write_str(ctx, c_key_name, strlen(c_key_name));

    if (lextype == MVM_reg_obj) { /* Object */
        char *debugname;

        if (!result->o)
            result->o = dtc->instance->VMNull;

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
            if (dtc->instance->debugserver->debugspam_protocol)
                fprintf(stderr, "what lexical type is %d supposed to be?\n", lextype);
            cmp_write_nil(ctx);
        }
    }
}

static MVMint32 request_context_lexicals(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMObject *this_ctx = argument->handle_id
        ? find_handle_target(dtc, argument->handle_id)
        : dtc->instance->VMNull;
    MVMStaticFrame *static_info;
    MVMStrHashTable *debug_locals;

    MVMFrame *frame;
    if (MVM_is_null(dtc, this_ctx) || !IS_CONCRETE(this_ctx) || REPR(this_ctx)->ID != MVM_REPR_ID_MVMContext) {
        if (dtc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "getting lexicals: context handle must refer to a definite MVMContext object\n");
        return 1;
    }
    if (!(frame = ((MVMContext *)this_ctx)->body.context)) {
        if (dtc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "context doesn't have a frame?!\n");
        return 1;
    }

    static_info = frame->static_info;
    MVMuint32 num_lexicals = static_info->body.num_lexicals;
    debug_locals = static_info->body.instrumentation
        ? &static_info->body.instrumentation->debug_locals
        : NULL;
    if (num_lexicals || debug_locals) {
        MVMuint64 lexical_index = 0;

        /* Count up total number of symbols; that is, the lexicals plus the
         * debug names where the names to not overlap with the lexicals. */
        MVMuint64 lexcount = static_info->body.num_lexicals;
        if (debug_locals) {
            MVMStrHashIterator iterator = MVM_str_hash_first(dtc, debug_locals);
            while (!MVM_str_hash_at_end(dtc, debug_locals, iterator)) {
                MVMStaticFrameDebugLocal *debug_entry
                    = MVM_str_hash_current_nocheck(dtc, debug_locals, iterator);
                MVMuint32 idx = MVM_get_lexical_by_name(dtc, static_info, debug_entry->hash_handle.key);
                if (idx != MVM_INDEX_HASH_NOT_FOUND)
                    lexcount++;
                iterator = MVM_str_hash_next_nocheck(dtc, debug_locals, iterator);
            }
        }

        cmp_write_map(ctx, 3);
        cmp_write_str(ctx, "id", 2);
        cmp_write_integer(ctx, argument->id);
        cmp_write_str(ctx, "type", 4);
        cmp_write_integer(ctx, MT_ContextLexicalsResponse);

        cmp_write_str(ctx, "lexicals", 8);
        cmp_write_map(ctx, lexcount);

        if (dtc->instance->debugserver->debugspam_protocol)
            fprintf(stderr, "will write %"PRIu64" lexicals\n", lexcount);

        MVMString **lexical_names_list = static_info->body.lexical_names_list;

        for (MVMuint32 j = 0; j < num_lexicals; j++) {
            MVMString *name = lexical_names_list[j];
            MVMuint16 lextype = static_info->body.lexical_types[j];
            MVMRegister *result = &frame->env[j];
            MVMint32 was_from_local = 0;
            /* Lexical has to have a name - to get here it has already been added
               to the lookup hash, and that would have failed unless the key
               exists and is a concrete MVMString. */
            assert(name);
            assert(IS_CONCRETE(name));
            /* Check there is no debug local override for it (which means the lexical
             * was lowered into a local, but preserved for some reason). */
            MVMStaticFrameDebugLocal *debug_entry = debug_locals
                ? MVM_str_hash_fetch_nocheck(dtc, debug_locals, name)
                : NULL;
            if (debug_entry && static_info->body.local_types[debug_entry->local_idx] == lextype) {
                result = &frame->work[debug_entry->local_idx];
                was_from_local = 1;
            }

            if (!was_from_local && lextype == MVM_reg_obj && !result->o) {
                /* XXX this can't allocate? */
                MVM_frame_vivify_lexical(dtc, frame, j);
            }
            char *c_key_name = MVM_string_utf8_encode_C_string(dtc, name);
            write_one_context_lexical(dtc, ctx, c_key_name, lextype, result);
            MVM_free(c_key_name);
            if (dtc->instance->debugserver->debugspam_protocol)
                fprintf(stderr, "wrote a lexical\n");
            lexical_index++;
        };

        if (debug_locals) {
            MVMStrHashIterator iterator = MVM_str_hash_first(dtc, debug_locals);
            while (!MVM_str_hash_at_end(dtc, debug_locals, iterator)) {
                MVMStaticFrameDebugLocal *debug_entry
                    = MVM_str_hash_current_nocheck(dtc, debug_locals, iterator);
                MVMuint32 idx = MVM_get_lexical_by_name(dtc, static_info, debug_entry->hash_handle.key);
                if (idx != MVM_INDEX_HASH_NOT_FOUND) {
                    char *c_key_name = MVM_string_utf8_encode_C_string(dtc, lexical_names_list[idx]);
                    MVMRegister *result = &frame->work[debug_entry->local_idx];
                    MVMuint16 lextype = static_info->body.local_types[debug_entry->local_idx];
                    write_one_context_lexical(dtc, ctx, c_key_name, lextype, result);
                }
                iterator = MVM_str_hash_next_nocheck(dtc, debug_locals, iterator);
            }
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
    MVMInstance *vm = dtc->instance;
    MVMObject *target = argument->handle_id
        ? find_handle_target(dtc, argument->handle_id)
        : dtc->instance->VMNull;

    if (MVM_is_null(dtc, target)) {
        if (vm->debugserver->debugspam_protocol)
            fprintf(stderr, "target of attributes request is null\n");
        return 1;
    }

    if (!IS_CONCRETE(target)) {
        if (vm->debugserver->debugspam_protocol)
            fprintf(stderr, "target of attributes request is not concrete\n");
        return 1;
    }

    if (dtc->instance->debugserver->debugspam_protocol)
        fprintf(stderr, "writing attributes of a %s\n", MVM_6model_get_debug_name(dtc, target));

    cmp_write_map(ctx, 3);
    cmp_write_str(ctx, "id", 2);
    cmp_write_integer(ctx, argument->id);
    cmp_write_str(ctx, "type", 4);
    cmp_write_integer(ctx, MT_ObjectAttributesResponse);

    cmp_write_str(ctx, "attributes", 10);

    if (REPR(target)->ID == MVM_REPR_ID_P6opaque) {
        MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)STABLE(target)->REPR_data;
        MVMP6opaqueBody *data = MVM_p6opaque_real_data(dtc, OBJECT_BODY(target));
        if (repr_data) {
            MVMint16 num_attributes = 0;
            MVMP6opaqueNameMap * name_to_index_mapping = repr_data->name_to_index_mapping;

            while (name_to_index_mapping != NULL && name_to_index_mapping->class_key != NULL) {
                num_attributes += name_to_index_mapping->num_attrs;
                name_to_index_mapping++;
            }

            name_to_index_mapping = repr_data->name_to_index_mapping;

            cmp_write_array(ctx, num_attributes);

            if (vm->debugserver->debugspam_protocol)
                fprintf(stderr, "going to write out %d attributes\n", num_attributes);

            if (name_to_index_mapping != NULL) {
                MVMP6opaqueNameMap *cur_map_entry = name_to_index_mapping;

                while (cur_map_entry->class_key != NULL) {
                    MVMuint16 i;
                    MVMint64 slot;
                    char *class_name = MVM_6model_get_stable_debug_name(dtc, cur_map_entry->class_key->st);

                    if (vm->debugserver->debugspam_protocol)
                        fprintf(stderr, "class %s has %d attributes\n", class_name, cur_map_entry->num_attrs);

                    for (i = 0; i < cur_map_entry->num_attrs; i++) {
                        char * name = MVM_string_utf8_encode_C_string(dtc, cur_map_entry->names[i]);

                        slot = cur_map_entry->slots[i];
                        if (slot >= 0) {
                            MVMuint16 const offset = repr_data->attribute_offsets[slot];
                            MVMSTable * const attr_st = repr_data->flattened_stables[slot];
                            if (attr_st == NULL) {
                                MVMObject *value = get_obj_at_offset(data, offset);
                                char *value_debug_name = value ? MVM_6model_get_debug_name(dtc, value) : "VMNull";

                                if (vm->debugserver->debugspam_protocol)
                                    fprintf(stderr, "Writing an object attribute\n");

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

                                if (vm->debugserver->debugspam_protocol)
                                    fprintf(stderr, "Writing a native attribute\n");

                                cmp_write_map(ctx, 4);

                                cmp_write_str(ctx, "name", 4);
                                cmp_write_str(ctx, name, strlen(name));

                                cmp_write_str(ctx, "class", 5);
                                cmp_write_str(ctx, class_name, strlen(class_name));

                                cmp_write_str(ctx, "kind", 4);

                                switch (attr_storage_spec->boxed_primitive) {
                                    case MVM_STORAGE_SPEC_BP_INT:
                                        cmp_write_str(ctx, "int", 3);
                                        cmp_write_str(ctx, "value", 5);
                                        cmp_write_integer(ctx, attr_st->REPR->box_funcs.get_int(dtc, attr_st, target, (char *)data + offset));
                                        break;
                                    case MVM_STORAGE_SPEC_BP_NUM:
                                        cmp_write_str(ctx, "num", 3);
                                        cmp_write_str(ctx, "value", 5);
                                        cmp_write_double(ctx, attr_st->REPR->box_funcs.get_num(dtc, attr_st, target, (char *)data + offset));
                                        break;
                                    case MVM_STORAGE_SPEC_BP_STR: {
                                        MVMString * const s = attr_st->REPR->box_funcs.get_str(dtc, attr_st, target, (char *)data + offset);
                                        char * str;
                                        if (s)
                                            str = MVM_string_utf8_encode_C_string(dtc, s);
                                        cmp_write_str(ctx, "str", 3);
                                        cmp_write_str(ctx, "value", 5);
                                        if (s) {
                                            cmp_write_str(ctx, str, strlen(str));
                                            MVM_free(str);
                                        }
                                        else {
                                            cmp_write_nil(ctx);
                                        }
                                        break;
                                    }
                                    default:
                                        cmp_write_str(ctx, "error", 5);
                                        cmp_write_str(ctx, "value", 5);
                                        cmp_write_str(ctx, "error", 5);
                                        break;
                                }
                            }
                        }

                        MVM_free(name);
                    }
                    cur_map_entry++;
                }
            }
            return 0;
        }
        else {
            if (vm->debugserver->debugspam_protocol)
                fprintf(stderr, "This class isn't composed yet!\n");
            cmp_write_str(ctx, "error: not composed yet", 22);
            return 0;
        }
    } else {
        cmp_write_array(ctx, 0);
        return 0;
    }

    return 1;
}
static void write_object_features(MVMThreadContext *tc, cmp_ctx_t *ctx, MVMuint8 attributes, MVMuint8 positional, MVMuint8 associative) {
    cmp_write_str(ctx, "attr_features", 13);
    cmp_write_bool(ctx, attributes);
    cmp_write_str(ctx, "pos_features", 12);
    cmp_write_bool(ctx, positional);
    cmp_write_str(ctx, "ass_features", 12);
    cmp_write_bool(ctx, associative);
}
static void write_vmarray_slot_type(MVMThreadContext *tc, cmp_ctx_t *ctx, MVMuint8 slot_type) {
    char *text = "unknown";
    switch (slot_type) {
        case MVM_ARRAY_OBJ: text = "obj"; break;
        case MVM_ARRAY_STR: text = "str"; break;
        case MVM_ARRAY_I64: text = "i64"; break;
        case MVM_ARRAY_I32: text = "i32"; break;
        case MVM_ARRAY_I16: text = "i16"; break;
        case MVM_ARRAY_I8:  text = "i8"; break;
        case MVM_ARRAY_N64: text = "n64"; break;
        case MVM_ARRAY_N32: text = "n32"; break;
        case MVM_ARRAY_U64: text = "u64"; break;
        case MVM_ARRAY_U32: text = "u32"; break;
        case MVM_ARRAY_U16: text = "u16"; break;
        case MVM_ARRAY_U8:  text = "u8";  break;
        case MVM_ARRAY_U4:  text = "u4"; break;
        case MVM_ARRAY_U2:  text = "u2"; break;
        case MVM_ARRAY_U1:  text = "u1"; break;
        case MVM_ARRAY_I4:  text = "i4"; break;
        case MVM_ARRAY_I2:  text = "i2"; break;
        case MVM_ARRAY_I1:  text = "i1"; break;
    }
    cmp_write_str(ctx, text, strlen(text));
}
static MVMuint16 write_vmarray_slot_kind(MVMThreadContext *tc, cmp_ctx_t *ctx, MVMuint8 slot_type) {
    char *text = "unknown";
    MVMuint16 kind = 0;
    switch (slot_type) {
        case MVM_ARRAY_OBJ: text = "obj"; kind = MVM_reg_obj; break;
        case MVM_ARRAY_STR: text = "str"; kind = MVM_reg_str; break;
        case MVM_ARRAY_I64:
        case MVM_ARRAY_I32:
        case MVM_ARRAY_I16:
        case MVM_ARRAY_I8:  text = "int"; kind = MVM_reg_int64; break;
        case MVM_ARRAY_N64:
        case MVM_ARRAY_N32: text = "num"; kind = MVM_reg_num64; break;
        case MVM_ARRAY_U64:
        case MVM_ARRAY_U32:
        case MVM_ARRAY_U16:
        case MVM_ARRAY_U8:
        case MVM_ARRAY_U4:
        case MVM_ARRAY_U2:
        case MVM_ARRAY_U1:
        case MVM_ARRAY_I4:
        case MVM_ARRAY_I2:
        case MVM_ARRAY_I1:  text = "int"; kind = MVM_reg_int64; break;
    }
    cmp_write_str(ctx, text, strlen(text));
    return kind;
}
static MVMint32 request_object_metadata(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMObject *target = argument->handle_id
        ? find_handle_target(dtc, argument->handle_id)
        : dtc->instance->VMNull;

    MVMint64 slots = 2; /* Always have the repr name and debug name */
    MVMuint32 repr_id;

    if (MVM_is_null(dtc, target)) {
        return 1;
    }

    cmp_write_map(ctx, 3);
    cmp_write_str(ctx, "id", 2);
    cmp_write_integer(ctx, argument->id);
    cmp_write_str(ctx, "type", 4);
    cmp_write_integer(ctx, MT_ObjectMetadataResponse);

    cmp_write_str(ctx, "metadata", 8);

    if (IS_CONCRETE(target)) {
        slots++;
        if (REPR(target)->unmanaged_size)
            slots++;
    }

    repr_id = REPR(target)->ID;

    if (repr_id == MVM_REPR_ID_P6opaque) {
        MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData*)(STABLE(target)->REPR_data);
        MVMP6opaqueBody *body = &((MVMP6opaque *)target)->body;
        if (IS_CONCRETE(target)) {
            slots += 1; /* Replaced? */
        }

        slots += 5; /* pos/ass del slots, int/num/str unbox slots */
        /*slots++;    [> storage spec <]*/
        slots += 3; /* features */
        cmp_write_map(ctx, slots);

        cmp_write_str(ctx, "p6opaque_pos_delegate_slot", 21);
        cmp_write_int(ctx, repr_data->pos_del_slot);
        cmp_write_str(ctx, "p6opaque_ass_delegate_slot", 21);
        cmp_write_int(ctx, repr_data->ass_del_slot);

        cmp_write_str(ctx, "p6opaque_unbox_int_slot", 23);
        cmp_write_int(ctx, repr_data->unbox_int_slot);
        cmp_write_str(ctx, "p6opaque_unbox_num_slot", 23);
        cmp_write_int(ctx, repr_data->unbox_num_slot);
        cmp_write_str(ctx, "p6opaque_unbox_str_slot", 23);
        cmp_write_int(ctx, repr_data->unbox_str_slot);

        if (IS_CONCRETE(target)) {
            cmp_write_str(ctx, "p6opaque_body_replaced", 22);
            cmp_write_bool(ctx, !!body->replaced);
        }

        write_object_features(dtc, ctx, 1, 0, 0);

        /* TODO maybe output additional unbox slots, too? */

        /*cmp_write_str(ctx, "storage_spec", 12);*/
        /*write_storage_spec(dtc, ctx, repr_data->storage_spec);*/
    }
    else if (repr_id == MVM_REPR_ID_VMArray) {
        MVMArrayREPRData *repr_data = (MVMArrayREPRData *)(STABLE(target)->REPR_data);
        char *debugname = repr_data->elem_type ? MVM_6model_get_debug_name(dtc, repr_data->elem_type) : NULL;
        if (IS_CONCRETE(target)) {
            slots += 3; /* slots allocated / used, storage size */
        }
        slots += 3;
        slots += 3; /* features */
        cmp_write_map(ctx, slots);

        cmp_write_str(ctx, "vmarray_elem_size", 17);
        cmp_write_int(ctx, repr_data->elem_size);

        cmp_write_str(ctx, "vmarray_slot_type", 17);
        write_vmarray_slot_type(dtc, ctx, repr_data->slot_type);

        cmp_write_str(ctx, "vmarray_elem_type", 17);
        if (debugname)
            cmp_write_str(ctx, debugname, strlen(debugname));
        else
            cmp_write_nil(ctx);

        if (IS_CONCRETE(target)) {
            MVMArrayBody *body = (MVMArrayBody *)OBJECT_BODY(target);

            cmp_write_str(ctx, "positional_elems", 16);
            cmp_write_int(ctx, body->elems);
            cmp_write_str(ctx, "vmarray_start", 13);
            cmp_write_int(ctx, body->start);
            cmp_write_str(ctx, "vmarray_ssize", 13);
            cmp_write_int(ctx, body->ssize);
        }

        write_object_features(dtc, ctx, 0, 1, 0);
    }
    else if (repr_id == MVM_REPR_ID_MVMHash) {
        if (IS_CONCRETE(target)) {
            slots += 1; /* num_items */
        }
        slots += 3; /* features */
        cmp_write_map(ctx, slots);

        if (IS_CONCRETE(target)) {
            MVMStrHashTable *hashtable = &(((MVMHashBody *)OBJECT_BODY(target))->hashtable);

            /* FIXME. What stats should we generate? Some are O(1),
             * some are O(n) (like mean probe length, and its SD) */
            cmp_write_str(ctx, "mvmhash_num_items", 17);
            cmp_write_int(ctx, MVM_str_hash_count(dtc, hashtable));
        }

        write_object_features(dtc, ctx, 0, 0, 1);
    }
    else if (repr_id == MVM_REPR_ID_MVMContext) {
        MVMFrame *frame = ((MVMContext *)target)->body.context;
        MVMArgProcContext *argctx = &frame->params;
        MVMCallsite *cs = argctx->version == MVM_ARGS_LEGACY
                ? argctx->legacy.callsite
                : argctx->arg_info.callsite;
        MVMCallsiteEntry *cse = argctx->version == MVM_ARGS_LEGACY && argctx->legacy.arg_flags
                ? argctx->legacy.arg_flags
                : cs->arg_flags;
        MVMuint16 flag_idx;
        MVMuint16 flag_count = argctx->version == MVM_ARGS_LEGACY && argctx->legacy.arg_flags
                ? argctx->legacy.flag_count
                : cs->flag_count;
        char *name = MVM_string_utf8_encode_C_string(dtc, frame->static_info->body.name);
        char *cuuid = MVM_string_utf8_encode_C_string(dtc, frame->static_info->body.cuuid);

        slots += 8;
        slots += 3; /* features */

        cmp_write_map(ctx, slots);

        cmp_write_str(ctx, "frame_on_heap", 13);
        cmp_write_bool(ctx, !MVM_FRAME_IS_ON_CALLSTACK(dtc, frame));

        cmp_write_str(ctx, "frame_work_size", 15);
        cmp_write_int(ctx, frame->allocd_work);
        cmp_write_str(ctx, "frame_env_size", 15);
        cmp_write_int(ctx, frame->allocd_env);

        cmp_write_str(ctx, "frame_name", 10);
        cmp_write_str(ctx, name, strlen(name));
        cmp_write_str(ctx, "frame_cuuid", 11);
        cmp_write_str(ctx, cuuid, strlen(cuuid));

        cmp_write_str(ctx, "frame_num_locals", 16);
        cmp_write_int(ctx, frame->static_info->body.num_locals);

        cmp_write_str(ctx, "frame_num_lexicals", 18);
        cmp_write_int(ctx, frame->static_info->body.num_lexicals);

        cmp_write_str(ctx, "callsite_flags", 14);
        cmp_write_array(ctx, flag_count);

        for (flag_idx = 0; flag_idx < flag_count; flag_idx++) {
            MVMCallsiteEntry entry = cse[flag_idx];
            MVMuint8 entry_count =
                !!(entry & MVM_CALLSITE_ARG_OBJ)
                + !!(entry & MVM_CALLSITE_ARG_INT)
                + !!(entry & MVM_CALLSITE_ARG_NUM)
                + !!(entry & MVM_CALLSITE_ARG_STR)
                + !!(entry & MVM_CALLSITE_ARG_NAMED)
                + !!(entry & MVM_CALLSITE_ARG_FLAT)
                + !!(entry & MVM_CALLSITE_ARG_FLAT_NAMED);
            cmp_write_array(ctx, entry_count ? entry_count : 0);
            if (entry & MVM_CALLSITE_ARG_OBJ)
                cmp_write_str(ctx, "obj", 3);
            if (entry & MVM_CALLSITE_ARG_INT)
                cmp_write_str(ctx, "int", 3);
            if (entry & MVM_CALLSITE_ARG_NUM)
                cmp_write_str(ctx, "num", 3);
            if (entry & MVM_CALLSITE_ARG_STR)
                cmp_write_str(ctx, "str", 3);
            if (entry & MVM_CALLSITE_ARG_NAMED)
                cmp_write_str(ctx, "named", 5);
            if (entry & MVM_CALLSITE_ARG_FLAT)
                cmp_write_str(ctx, "flat", 4);
            if (entry & MVM_CALLSITE_ARG_FLAT_NAMED)
                cmp_write_str(ctx, "flat&named", 10);
            if (!entry_count)
                cmp_write_str(ctx, "nothing", 7);
        }



        MVM_free(name);
        MVM_free(cuuid);

        write_object_features(dtc, ctx, 0, 0, 0);
    }
    else if (repr_id == MVM_REPR_ID_P6str && IS_CONCRETE(target)) {
        MVMP6strBody *body = (MVMP6strBody *)OBJECT_BODY(target);
        MVMString *string = body->value;

        char *value = MVM_string_utf8_encode_C_string(dtc, string);
        slots += 2;
        slots += 3; /* features */

        if (string->body.storage_type == MVM_STRING_STRAND)
            slots += 5;

        cmp_write_map(ctx, slots);

        cmp_write_str(ctx, "string_value", 12);
        cmp_write_str(ctx, value, strlen(value));

        cmp_write_str(ctx, "string_storage_kind", 19);
        switch (string->body.storage_type) {
            case MVM_STRING_GRAPHEME_32:    cmp_write_str(ctx, "grapheme32", 10); break;
            case MVM_STRING_GRAPHEME_ASCII: cmp_write_str(ctx, "graphemeASCII", 13); break;
            case MVM_STRING_GRAPHEME_8:     cmp_write_str(ctx, "grapheme8", 9); break;
            case MVM_STRING_STRAND:         cmp_write_str(ctx, "strands", 7); break;
            default: cmp_write_str(ctx, "???", 3);
        }

        if (string->body.storage_type == MVM_STRING_STRAND) {
            MVMuint16 num_strands = string->body.num_strands;
            MVMuint16 strand_idx;
            cmp_write_str(ctx, "string_strand_count", 19);
            cmp_write_int(ctx, num_strands);

            cmp_write_str(ctx, "string_strand_starts", 20);
            cmp_write_array(ctx, num_strands);
            for (strand_idx = 0; strand_idx < num_strands; strand_idx++) {
                cmp_write_int(ctx, string->body.storage.strands[strand_idx].start);
            }
            cmp_write_str(ctx, "string_strand_ends", 18);
            cmp_write_array(ctx, num_strands);
            for (strand_idx = 0; strand_idx < num_strands; strand_idx++) {
                cmp_write_int(ctx, string->body.storage.strands[strand_idx].end);
            }
            cmp_write_str(ctx, "string_strand_repetitions", 25);
            cmp_write_array(ctx, num_strands);
            for (strand_idx = 0; strand_idx < num_strands; strand_idx++) {
                cmp_write_int(ctx, string->body.storage.strands[strand_idx].repetitions);
            }
            cmp_write_str(ctx, "string_strand_target_length", 27);
            cmp_write_array(ctx, num_strands);
            for (strand_idx = 0; strand_idx < num_strands; strand_idx++) {
                cmp_write_int(ctx, string->body.storage.strands[strand_idx].blob_string->body.num_graphs);
            }
        }

        write_object_features(dtc, ctx, 0, 0, 0);

        MVM_free(value);
    }
    else if (repr_id == MVM_REPR_ID_ReentrantMutex && IS_CONCRETE(target)) {
        MVMReentrantMutexBody *body = (MVMReentrantMutexBody *)OBJECT_BODY(target);

        slots += 3;
        slots += 3; /* features */

        cmp_write_map(ctx, slots);

        cmp_write_str(ctx, "mutex_identity", 14);
        cmp_write_int(ctx, (uintptr_t)body->mutex);
        cmp_write_str(ctx, "mutex_holder", 12);
        cmp_write_int(ctx, MVM_load(&body->holder_id));
        cmp_write_str(ctx, "mutex_lock_count", 16);
        cmp_write_int(ctx, MVM_load(&body->lock_count));

        write_object_features(dtc, ctx, 0, 0, 0);
    }
    else if (repr_id == MVM_REPR_ID_Semaphore && IS_CONCRETE(target)) {
        MVMSemaphoreBody *body = (MVMSemaphoreBody *)OBJECT_BODY(target);

        slots += 1;
        slots += 3; /* features */

        cmp_write_map(ctx, slots);

        cmp_write_str(ctx, "semaphore_identity", 14);
        cmp_write_int(ctx, (uintptr_t)body->sem);

        write_object_features(dtc, ctx, 0, 0, 0);
    }
    else {
        cmp_write_map(ctx, slots);
    }

    if (REPR(target)->unmanaged_size && IS_CONCRETE(target)) {
        cmp_write_str(ctx, "unmanaged_size", 14);
        cmp_write_int(ctx, REPR(target)->unmanaged_size(dtc, STABLE(target), OBJECT_BODY(target)));
    }

    if (IS_CONCRETE(target)) {
        cmp_write_str(ctx, "size", 4);
        cmp_write_int(ctx, target->header.size);
    }

    cmp_write_str(ctx, "repr_name", 9);
    cmp_write_str(ctx, REPR(target)->name, strlen(REPR(target)->name));

    {
        char *debug_name = MVM_6model_get_debug_name(dtc, target);
        cmp_write_str(ctx, "debug_name", 10);
        cmp_write_str(ctx, debug_name, strlen(debug_name));
    }

    return 0;
}

static MVMint32 request_object_positionals(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMObject *target = argument->handle_id
        ? find_handle_target(dtc, argument->handle_id)
        : dtc->instance->VMNull;

    if (MVM_is_null(dtc, target)) {
        return 1;
    }

    if (REPR(target)->ID == MVM_REPR_ID_VMArray) {
        MVMArrayBody *body = (MVMArrayBody *)OBJECT_BODY(target);
        MVMArrayREPRData *repr_data = (MVMArrayREPRData *)STABLE(target)->REPR_data;
        MVMuint16 kind;
        MVMuint64 index;

        cmp_write_map(ctx, 5);
        cmp_write_str(ctx, "id", 2);
        cmp_write_integer(ctx, argument->id);
        cmp_write_str(ctx, "type", 4);
        cmp_write_integer(ctx, MT_ObjectPositionalsResponse);

        cmp_write_str(ctx, "kind", 4);
        kind = write_vmarray_slot_kind(dtc, ctx, repr_data->slot_type);

        cmp_write_str(ctx, "start", 5);
        cmp_write_int(ctx, 0);

        cmp_write_str(ctx, "contents", 8);
        cmp_write_array(ctx, body->elems);

        for (index = 0; index < body->elems; index++) {
            MVMRegister target_reg;
            REPR(target)->pos_funcs.at_pos(dtc, STABLE(target), target, body, index, &target_reg, kind);

            switch (kind) {
                case MVM_reg_obj: {
                    MVMObject *value = target_reg.o;
                    char *value_debug_name = value ? MVM_6model_get_debug_name(dtc, value) : "VMNull";
                    cmp_write_map(ctx, 4);

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
                    break;
                }
                case MVM_reg_int64: {
                    cmp_write_int(ctx, target_reg.i64);
                    break;
                }
                case MVM_reg_num64:
                    cmp_write_double(ctx, target_reg.n64);
                    break;
                case MVM_reg_str: {
                    char *c_value = MVM_string_utf8_encode_C_string(dtc, target_reg.s);
                    cmp_write_str(ctx, c_value, strlen(c_value));
                    MVM_free(c_value);
                    break;
                }
                default:
                    cmp_write_nil(ctx);
            }
        }

        return 0;
    }

    return 1;
}

static MVMint32 request_object_associatives(MVMThreadContext *dtc, cmp_ctx_t *ctx, request_data *argument) {
    MVMObject *target = argument->handle_id
        ? find_handle_target(dtc, argument->handle_id)
        : dtc->instance->VMNull;

    if (MVM_is_null(dtc, target)) {
        return 1;
    }
    if (!IS_CONCRETE(target)) {
        return 1;
    }

    if (REPR(target)->ID == MVM_REPR_ID_MVMHash) {
        MVMStrHashTable *hashtable = &(((MVMHashBody *)OBJECT_BODY(target))->hashtable);
        MVMuint64 count = MVM_str_hash_count(dtc, hashtable);

        cmp_write_map(ctx, 4);
        cmp_write_str(ctx, "id", 2);
        cmp_write_integer(ctx, argument->id);
        cmp_write_str(ctx, "type", 4);
        cmp_write_integer(ctx, MT_ObjectAssociativesResponse);

        cmp_write_str(ctx, "kind", 4);
        cmp_write_str(ctx, "obj", 3);

        cmp_write_str(ctx, "contents", 8);
        cmp_write_map(ctx, count);

        MVMStrHashIterator iterator = MVM_str_hash_first(dtc, hashtable);
        while (!MVM_str_hash_at_end(dtc, hashtable, iterator)) {
            MVMHashEntry *entry = MVM_str_hash_current_nocheck(dtc, hashtable, iterator);
            char *key = MVM_string_utf8_encode_C_string(dtc, entry->hash_handle.key);
            MVMObject *value = entry->value;
            char *value_debug_name = value ? MVM_6model_get_debug_name(dtc, value) : "VMNull";

            cmp_write_str(ctx, key, strlen(key));

            cmp_write_map(ctx, 4);

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

            MVM_free(key);

            iterator = MVM_str_hash_next_nocheck(dtc, hashtable, iterator);
        }
    }

    return 0;
}

MVMuint8 debugspam_network;

static bool socket_reader(cmp_ctx_t *ctx, void *data, size_t limit) {
    size_t idx;
    size_t total_read = 0;
    ssize_t read;
    MVMuint8 *orig_data = (MVMuint8 *)data;
    if (debugspam_network)
        fprintf(stderr, "asked to read %zu bytes\n", limit);
    while (total_read < limit) {
        if ((read = recv(*((MVMSocket*)ctx->buf), data, limit, 0)) == -1) {
            if (debugspam_network)
                fprintf(stderr, "minus one\n");
            return 0;
        } else if (read == 0) {
            if (debugspam_network)
                fprintf(stderr, "end of file - socket probably closed\nignore warnings about parse errors\n");
            return 0;
        }
        if (debugspam_network)
            fprintf(stderr, "%zu ", read);
        data = (void *)(((MVMuint8*)data) + read);
        total_read += (size_t)read;
    }

    if (debugspam_network) {
        fprintf(stderr, "... recv received %zu bytes\n", total_read);
        fprintf(stderr, "cmp read: ");
        for (idx = 0; idx < limit; idx++) {
            fprintf(stderr, "%x ", orig_data[idx]);
        }
        fprintf(stderr, "\n");
    }
    return 1;
}

static size_t socket_writer(cmp_ctx_t *ctx, const void *data, size_t limit) {
    size_t total_sent = 0;
    ssize_t sent;
    if (debugspam_network)
        fprintf(stderr, "asked to send %3zu bytes: ", limit);
    while (total_sent < limit) {
        if ((sent = send(*(MVMSocket*)ctx->buf, data, limit, 0)) == -1) {
            if (debugspam_network)
                fprintf(stderr, "but couldn't (socket disconnected?)\n");
            return 0;
        } else if (sent == 0) {
            if (debugspam_network)
                fprintf(stderr, "send encountered end of file\n");
            return 0;
        }
        if (debugspam_network)
            fprintf(stderr, "%2zu ", sent);
        data = (void *)(((MVMuint8*)data) + sent);
        total_sent += (size_t)sent;
    }
    if (debugspam_network)
        fprintf(stderr, "... send sent %3zu bytes\n", total_sent);
    return 1;
}

static bool is_valid_int(cmp_object_t *obj, MVMuint64 *result) {
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

static bool is_valid_num(cmp_object_t *obj, MVMnum64 *result) {
    switch (obj->type) {
        case CMP_TYPE_FLOAT:
            *result = obj->as.flt;
            break;
        case CMP_TYPE_DOUBLE:
            *result = obj->as.dbl;
            break;
        default:
            return 0;
    }
    return 1;
}

#define CHECK(operation, message) do { if(!(operation)) { data->parse_fail = 1; data->parse_fail_message = (message); if (tc->instance->debugserver->debugspam_protocol) fprintf(stderr, "CMP error: %s; %s\n", cmp_strerror(ctx), message); return 0; } } while(0)
#define FIELD_FOUND(field, duplicated_message) do { if(data->fields_set & (field)) { data->parse_fail = 1; data->parse_fail_message = duplicated_message;  return 0; }; field_to_set = (field); } while (0)

MVMint8 skip_all_read_data(cmp_ctx_t *ctx, MVMuint32 size) {
    char dump[1024];

    while (size > 1024) {
        if (!socket_reader(ctx, dump, 1024)) {
            return 0;
        }
        size -= 1024;
    }
    if (!socket_reader(ctx, dump, size)) {
        return 0;
    }
    return 1;
}

MVMint8 skip_whole_object(MVMThreadContext *tc, cmp_ctx_t *ctx, request_data *data) {
    cmp_object_t obj;
    MVMuint32 obj_size = 0;
    MVMuint32 index;

    CHECK(cmp_read_object(ctx, &obj), "couldn't skip object from unknown key");

    switch (obj.type) {
        case CMP_TYPE_FIXMAP:
        case CMP_TYPE_MAP16:
        case CMP_TYPE_MAP32:
            obj_size = obj.as.map_size * 2;

            for (index = 0; index < obj_size; index++) {
                if (!skip_whole_object(tc, ctx, data)) {
                    return 0;
                }
            }
            break;
        case CMP_TYPE_FIXARRAY:
        case CMP_TYPE_ARRAY16:
        case CMP_TYPE_ARRAY32:
            obj_size = obj.as.array_size;

            for (index = 0; index < obj_size; index++) {
                if (!skip_whole_object(tc, ctx, data)) {
                    return 0;
                }
            }
            break;
        case CMP_TYPE_FIXSTR:
        case CMP_TYPE_STR8:
        case CMP_TYPE_STR16:
        case CMP_TYPE_STR32:
            obj_size = obj.as.str_size;
            CHECK(skip_all_read_data(ctx, obj_size), "could not skip string data");
            break;
        case CMP_TYPE_BIN8:
        case CMP_TYPE_BIN16:
        case CMP_TYPE_BIN32:
            obj_size = obj.as.bin_size;
            CHECK(skip_all_read_data(ctx, obj_size), "could not skip string data");
            break;
        case CMP_TYPE_EXT8:
        case CMP_TYPE_EXT16:
        case CMP_TYPE_EXT32:
        case CMP_TYPE_FIXEXT1:
        case CMP_TYPE_FIXEXT2:
        case CMP_TYPE_FIXEXT4:
        case CMP_TYPE_FIXEXT8:
        case CMP_TYPE_FIXEXT16:
            obj_size = obj.as.ext.size;
            CHECK(skip_all_read_data(ctx, obj_size), "could not skip string data");
            break;
        case CMP_TYPE_POSITIVE_FIXNUM:
        case CMP_TYPE_NIL:
        case CMP_TYPE_BOOLEAN:
        case CMP_TYPE_FLOAT:
        case CMP_TYPE_DOUBLE:
        case CMP_TYPE_NEGATIVE_FIXNUM:
        case CMP_TYPE_UINT8:
        case CMP_TYPE_UINT16:
        case CMP_TYPE_UINT32:
        case CMP_TYPE_UINT64:
        case CMP_TYPE_SINT8:
        case CMP_TYPE_SINT16:
        case CMP_TYPE_SINT32:
        case CMP_TYPE_SINT64:
            break;
        default:
            CHECK(0, "could not skip object: unhandled type");
    }
    return 1;
}

MVMint32 parse_message_map(MVMThreadContext *tc, cmp_ctx_t *ctx, request_data *data) {
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
            if (tc->instance->debugserver->debugspam_protocol)
                fprintf(stderr, "expected a map, but got %d\n", obj.type);
            data->parse_fail = 1;
            data->parse_fail_message = "expected a map as the envelope";
            return 0;
    }

    for (i = 0; i < map_size; i++) {
        char key_str[16];
        MVMuint32 str_size = 16;

        fields_set field_to_set = 0;
        MVMint32   type_to_parse = 0;

        CHECK(cmp_read_str(ctx, key_str, &str_size), "Couldn't read string key");

        if (strncmp(key_str, "type", 15) == 0) {
            FIELD_FOUND(FS_type, "type field duplicated");
            type_to_parse = 1;
        }
        else if (strncmp(key_str, "id", 15) == 0) {
            FIELD_FOUND(FS_id, "id field duplicated");
            type_to_parse = 1;
        }
        else if (strncmp(key_str, "thread", 15) == 0) {
            FIELD_FOUND(FS_thread_id, "thread field duplicated");
            type_to_parse = 1;
        }
        else if (strncmp(key_str, "frame", 15) == 0) {
            FIELD_FOUND(FS_frame_number, "frame number field duplicated");
            type_to_parse = 1;
        }
        else if (strncmp(key_str, "handle", 15) == 0) {
            FIELD_FOUND(FS_handle_id, "handle field duplicated");
            type_to_parse = 1;
        }
        else if (strncmp(key_str, "line", 15) == 0) {
            FIELD_FOUND(FS_line, "line field duplicated");
            type_to_parse = 1;
        }
        else if (strncmp(key_str, "suspend", 15) == 0) {
            FIELD_FOUND(FS_suspend, "suspend field duplicated");
            type_to_parse = 1;
        }
        else if (strncmp(key_str, "stacktrace", 15) == 0) {
            FIELD_FOUND(FS_stacktrace, "stacktrace field duplicated");
            type_to_parse = 1;
        }
        else if (strncmp(key_str, "file", 15) == 0) {
            FIELD_FOUND(FS_file, "file field duplicated");
            type_to_parse = 2;
        }
        else if (strncmp(key_str, "handles", 15) == 0) {
            FIELD_FOUND(FS_handles, "handles field duplicated");
            type_to_parse = 3;
        }
        else if (strncmp(key_str, "name", 4) == 0) {
            FIELD_FOUND(FS_name, "name field duplicated");
            type_to_parse = 2;
        }
        else if (strncmp(key_str, "hll", 3) == 0) {
            FIELD_FOUND(FS_hll, "hll field duplicated");
            type_to_parse = 2;
        }
        else if (strncmp(key_str, "arguments", 15) == 0) {
            FIELD_FOUND(FS_arguments, "arguments field duplicated");
            type_to_parse = 4;
        }
        else {
            if (tc->instance->debugserver->debugspam_protocol)
                fprintf(stderr, "the hell is a %s?\n", key_str);
            type_to_parse = -1;
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
        }
        else if (type_to_parse == 2) {
            uint32_t strsize;
            char *string;

            CHECK(cmp_read_str_size(ctx, &strsize), "Couldn't read string size for a key");

            string = MVM_calloc(strsize + 1, sizeof(char));
            if (tc->instance->debugserver->debugspam_protocol)
                fprintf(stderr, "reading a string for %s size %u\n", key_str, strsize);
            CHECK(ctx->read(ctx, string, strsize), "Couldn't read string for a key");
            if (tc->instance->debugserver->debugspam_protocol)
                fprintf(stderr, "Value is \"%s\"\n", string);

            switch (field_to_set) {
                case FS_file:
                    data->file = string;
                    break;
                case FS_name:
                    data->name = string;
                    break;
                case FS_hll:
                    data->hll = string;
                    break;
                default:
                    data->parse_fail = 1;
                    data->parse_fail_message = "Str field to set NYI";
                    return 0;
            }
            data->fields_set = data->fields_set | field_to_set;
        }
        else if (type_to_parse == 3) {
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
            data->fields_set = data->fields_set | field_to_set;
        }
        else if (type_to_parse == 4) {
            uint32_t arraysize = 0;
            uint32_t index;
            CHECK(cmp_read_array(ctx, &arraysize), "Couldn't read array for a key");
            data->argument_count = arraysize;
            data->arguments = MVM_malloc(arraysize * sizeof(argument_data));
            for (index = 0; index < arraysize; index++) {
                char key_str[16];
                MVMuint32 str_size = 15;
                uint32_t map_size;
                uint32_t map_index;

                MVMuint8 kind_set = 0;
                MVMint32 kind = -1;
                MVMuint8 handle_is_set = 0;
                MVMuint8 str_uses_handle = 0;

                CHECK(cmp_read_map(ctx, &map_size), "Couldn't read map inside an array for arguments");
                CHECK(map_size >= 2, "map inside of array for arguments is too small.");

                for (map_index = 0; map_index < map_size; map_index++) {
                    str_size = 15;
                    CHECK(cmp_read_str(ctx, key_str, &str_size), "Couldn't read a key string for an argument");
                    if (strncmp(key_str, "kind", 15) == 0) {
                        CHECK(kind_set == 0, "kind value duplicated for an argument");
                        str_size = 15;
                        CHECK(cmp_read_str(ctx, key_str, &str_size), "Couldn't read a kind for an argument");
                        if (strncmp(key_str, "obj", 15) == 0) {
                            CHECK(kind == -1 || kind == MVM_reg_obj, "kind for argument doesn't match passed value");
                            kind = MVM_reg_obj;
                            kind_set = 1;
                        }
                        else if (strncmp(key_str, "int", 15) == 0) {
                            CHECK(kind == -1 || kind == MVM_reg_int64, "kind for argument doesn't match passed value");
                            kind = MVM_reg_int64;
                            kind_set = 1;
                        }
                        else if (strncmp(key_str, "num", 15) == 0) {
                            CHECK(kind == -1 || kind == MVM_reg_num64, "kind for argument doesn't match passed value");
                            kind = MVM_reg_num64;
                            kind_set = 1;
                        }
                        else if (strncmp(key_str, "str", 15) == 0) {
                            CHECK(kind == -1 || kind == MVM_reg_str || kind == MVM_reg_obj, "kind for argument doesn't match passed value");
                            if (handle_is_set) {
                                str_uses_handle = 1;
                                data->arguments[index].str_uses_handle = 1;
                            }
                            kind = MVM_reg_str;
                            kind_set = 1;
                            /* TODO: allow a handle rather than literal string passed here. */
                        }
                        else {
                            CHECK(0, "unknown kind for argument in invoke");
                        }
                    }
                    else if (strncmp(key_str, "handle", 15) == 0) {
                        cmp_object_t object;
                        MVMuint64 result;
                        CHECK(kind_set == 0 || kind == MVM_reg_obj || kind == MVM_reg_str, "kind for argument inappropriate for handle argument");
                        CHECK(handle_is_set == 0, "handle field duplicated in argument");
                        CHECK(cmp_read_object(ctx, &object), "Couldn't read value for a key");
                        CHECK(is_valid_int(&object, &result), "Couldn't read integer value for a handle");
                        data->arguments[index].arg_u.o = result;
                        handle_is_set = 1;
                        if (kind == MVM_reg_str) {
                            str_uses_handle = 1;
                            data->arguments[index].str_uses_handle = 1;
                        } else {
                            kind = MVM_reg_obj;
                        }
                    }
                    else if (strncmp(key_str, "value", 15) == 0) {
                        CHECK(kind_set == 0 || kind != MVM_reg_obj, "kind for argument inappropriate for non-handle argument");
                        CHECK(handle_is_set == 0, "cannot have both handle and value entry in argument");
                        cmp_object_t object;
                        MVMuint64 int_result;
                        MVMnum64 num_result;
                        CHECK(cmp_read_object(ctx, &object), "Couldn't read value for a key");
                        if (is_valid_int(&object, &int_result)) {
                            CHECK(kind_set == 0 || kind == MVM_reg_int64, "kind for argument inappropriate");
                            data->arguments[index].arg_u.i = int_result;
                            kind = MVM_reg_int64;
                        }
                        else if (is_valid_num(&object, &num_result)) {
                            CHECK(kind_set == 0 || kind == MVM_reg_num64, "kind for argument inappropriate");
                            data->arguments[index].arg_u.n = num_result;
                            kind = MVM_reg_num64;
                        }
                        else if (object.type == CMP_TYPE_STR32 || object.type == CMP_TYPE_STR16 || object.type == CMP_TYPE_STR8 || object.type == CMP_TYPE_FIXSTR) {
                            MVMuint32 len = object.as.str_size;;
                            char *string_target;
                            string_target = MVM_calloc(len + 1, 1);

                            CHECK(ctx->read(ctx, string_target, len), "could not read string data");

                            string_target[len] = 0;
                            data->arguments[index].arg_u.s = string_target;
                            data->arguments[index].str_uses_handle = 0;
                            kind = MVM_reg_str;
                        }
                        else {
                            CHECK(0, "inappropriate messagepack type for argument value");
                        }
                    }
                    else if (strncmp(key_str, "name", 15) == 0) {
                        CHECK(0, "named arguments for invocation NYI");
                    }
                    data->arguments[index].arg_kind = kind;
                    /* as per the specification, unknown arguments should be ignored. */
                }

                CHECK(kind_set, "argument map did not have a 'kind' key.");
                CHECK(kind != MVM_reg_obj || handle_is_set || (kind == MVM_reg_str && str_uses_handle), "kind is set to obj or str+handle, but no handle passed");
            }
            data->fields_set = data->fields_set | field_to_set;
        }

        else if (type_to_parse == -1) {
            skip_whole_object(tc, ctx, data);
        }
    }

    return check_requirements(tc, data);
}

#define COMMUNICATE_RESULT(operation) do { if ((operation)) { communicate_error(tc, &ctx, &argument); } else { communicate_success(tc, &ctx, &argument); } } while (0)
#define COMMUNICATE_ERROR(operation) do { if ((operation)) { communicate_error(tc, &ctx, &argument); } } while (0)

static void debugserver_worker(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    int continue_running = 1;
    MVMSocket listensocket;
    MVMInstance *vm = tc->instance;
    MVMuint64 port = vm->debugserver->port;

    vm->debugserver->thread_id = tc->thread_obj->body.thread_id;

#if MVM_HAS_PTHREAD_SETNAME_NP
    pthread_setname_np(pthread_self(), "debugserver");
#endif

    {
#ifdef _WIN32
        WORD wVersionRequested;
        WSADATA wsaData;
        int error;
#endif
        char portstr[16];
        struct addrinfo *res;

        snprintf(portstr, 16, "%"PRIu64, port);

#ifdef _WIN32
        wVersionRequested = MAKEWORD(2, 2);

        error = WSAStartup(wVersionRequested, &wsaData);
        if (error != 0) {
            MVM_panic(1, "Debugserver: WSAStartup failed with error: %n", error);
        }
#endif

        if (getaddrinfo("localhost", portstr, NULL, &res) != 0) {
            MVM_panic(1, "Debugserver: Could not get addrinfo for localhost / port %"PRIu64": %s", port, strerror(errno));
        }

        listensocket = socket(res->ai_family, SOCK_STREAM, 0);
        if (listensocket == -1)
            MVM_panic(1, "Debugserver: Could not create file descriptor for socket: %s", strerror(errno));

#ifndef _WIN32
        {
            int one = 1;
            setsockopt(listensocket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        }
#endif

        if (bind(listensocket, res->ai_addr, res->ai_addrlen) == -1) {
            MVM_panic(1, "Debugserver: Could not bind to socket: %s", strerror(errno));
        }

        freeaddrinfo(res);

        if (listen(listensocket, 1) == -1) {
            MVM_panic(1, "Debugserver: Could not listen on socket: %s", strerror(errno));
        }
    }

    while(continue_running) {
        MVMSocket clientsocket;
        cmp_ctx_t ctx;

        MVM_gc_mark_thread_blocked(tc);
        clientsocket = accept(listensocket, NULL, NULL);
        MVM_gc_mark_thread_unblocked(tc);

        send_greeting(&clientsocket);

        if (!receive_greeting(&clientsocket)) {
            if (tc->instance->debugserver->debugspam_protocol)
                fprintf(stderr, "Debugserver: did not receive greeting properly\n");
            MVM_platform_close_socket(clientsocket);
            continue;
        }

        cmp_init(&ctx, &clientsocket, socket_reader, NULL, socket_writer);

        vm->debugserver->messagepack_data = (void*)&ctx;

        while (clientsocket) {
            request_data argument;

            MVM_gc_mark_thread_blocked(tc);
            parse_message_map(tc, &ctx, &argument);
            MVM_gc_mark_thread_unblocked(tc);

            uv_mutex_lock(&vm->debugserver->mutex_network_send);

            if (argument.parse_fail) {
                if (tc->instance->debugserver->debugspam_protocol)
                    fprintf(stderr, "failed to parse this message: %s\n", argument.parse_fail_message);
                cmp_write_map(&ctx, 3);

                cmp_write_str(&ctx, "id", 2);
                cmp_write_integer(&ctx, argument.id);

                cmp_write_str(&ctx, "type", 4);
                cmp_write_integer(&ctx, MT_ErrorProcessingMessage);

                cmp_write_str(&ctx, "reason", 6);
                cmp_write_str(&ctx, argument.parse_fail_message, strlen(argument.parse_fail_message));
                MVM_platform_close_socket(clientsocket);
                uv_mutex_unlock(&vm->debugserver->mutex_network_send);
                break;
            }

            if (vm->debugserver->debugspam_protocol)
                fprintf(stderr, "debugserver received packet %"PRIu64", command %"PRIu32"\n", argument.id, argument.type);

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
                        communicate_error(tc, &ctx, &argument);
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
                case MT_StepInto:
                    COMMUNICATE_RESULT(setup_step(tc, &ctx, &argument, MVMDebugSteppingMode_STEP_INTO, NULL));
                    break;
                case MT_StepOver:
                    COMMUNICATE_RESULT(setup_step(tc, &ctx, &argument, MVMDebugSteppingMode_STEP_OVER, NULL));
                    break;
                case MT_StepOut:
                    COMMUNICATE_RESULT(setup_step(tc, &ctx, &argument, MVMDebugSteppingMode_STEP_OUT, NULL));
                    break;
                case MT_Invoke:
                    /* a return value of 2 means the function already sent an
                     * error message of its own. */
                    if (request_invoke_code(tc, &ctx, &argument) == 1) {
                        communicate_error(tc, &ctx, &argument);
                    }
                    break;
                case MT_FindMethod:
                    if (request_find_method(tc, &ctx, &argument)) {
                        communicate_error(tc, &ctx, &argument);
                    }
                    break;
                case MT_DecontainerizeHandle:
                    if (request_object_decontainerize(tc, &ctx, &argument)) {
                        communicate_error(tc, &ctx, &argument);
                    }
                    break;
                case MT_ObjectAttributesRequest:
                    if (request_object_attributes(tc, &ctx, &argument)) {
                        communicate_error(tc, &ctx, &argument);
                    }
                    break;
                case MT_ReleaseHandles:
                    COMMUNICATE_RESULT(release_handles(tc, &ctx, &argument));
                    MVM_free(argument.handles);
                    break;
                case MT_ContextHandle:
                case MT_CodeObjectHandle:
                    if (create_context_or_code_obj_debug_handle(tc, &ctx, &argument, NULL)) {
                        communicate_error(tc, &ctx, &argument);
                    }
                    break;
                case MT_CallerContextRequest:
                case MT_OuterContextRequest:
                    if (create_caller_or_outer_context_debug_handle(tc, &ctx, &argument, NULL)) {
                        communicate_error(tc, &ctx, &argument);
                    }
                    break;
                case MT_ContextLexicalsRequest:
                    if (request_context_lexicals(tc, &ctx, &argument)) {
                        communicate_error(tc, &ctx, &argument);
                    }
                    break;
                case MT_ObjectMetadataRequest:
                    if (request_object_metadata(tc, &ctx, &argument)) {
                        communicate_error(tc, &ctx, &argument);
                    }
                    break;
                case MT_ObjectPositionalsRequest:
                    if (request_object_positionals(tc, &ctx, &argument)) {
                        communicate_error(tc, &ctx, &argument);
                    }
                    break;
                case MT_ObjectAssociativesRequest:
                    if (request_object_associatives(tc, &ctx, &argument)) {
                        communicate_error(tc, &ctx, &argument);
                    }
                    break;
                case MT_HandleEquivalenceRequest:
                    send_handle_equivalence_classes(tc, &ctx, &argument);
                    break;
                case MT_HLLSymbolRequest:
                    COMMUNICATE_ERROR(request_hll_symbol_data(tc, &ctx, &argument));
                    break;
                default: /* Unknown command or NYI */
                    if (tc->instance->debugserver->debugspam_protocol)
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
MVM_PUBLIC void MVM_debugserver_init(MVMThreadContext *tc, MVMuint32 port) {
    MVMInstance *vm = tc->instance;
    MVMDebugServerData *debugserver = MVM_calloc(1, sizeof(MVMDebugServerData));
    MVMObject *worker_entry_point;
    int init_stat;

    tc->instance->instrumentation_level++; /* So we insert breakpoint instructions. */

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
    debugserver->breakpoints->files       =
        MVM_fixed_size_alloc_zeroed(tc, vm->fsa, debugserver->breakpoints->files_alloc * sizeof(MVMDebugServerBreakpointFileTable));

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

MVM_PUBLIC void MVM_debugserver_mark_handles(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot) {
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
