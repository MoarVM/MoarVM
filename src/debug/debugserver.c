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

static MVMThread *find_thread_by_id(MVMInstance *vm, MVMint32 id) {
    MVMThread *cur_thread = 0;

    fprintf(stderr, "looking for thread number %d\n", id);

    uv_mutex_lock(&vm->mutex_threads);
    cur_thread = vm->threads;
    while (cur_thread) {
        fprintf(stderr, "%d ", cur_thread->body.thread_id);
        if (cur_thread->body.thread_id == id) {
            break;
        }
        cur_thread = cur_thread->body.next;
    }
    fprintf(stderr, "\n");
    uv_mutex_unlock(&vm->mutex_threads);
    return cur_thread;
}

static MVMint32 request_thread_suspends(MVMInstance *vm, MVMuint32 id) {
    MVMThread *to_do = find_thread_by_id(vm, id);
    MVMThreadContext *tc = to_do->body.tc;

    while (1) {
        if (MVM_cas(&tc->gc_status, MVMGCStatus_NONE, MVMGCStatus_INTERRUPT | MVMSuspendState_SUSPEND_REQUEST)
                == MVMGCStatus_NONE) {
            break;
        }
        MVM_platform_thread_yield();
    }

    while (1) {
        if (MVM_load(&tc->gc_status) != (MVMGCStatus_UNABLE | MVMSuspendState_SUSPENDED)) {
            MVM_platform_thread_yield();
        } else {
            break;
        }
    }

    return 1;
}

static MVMint32 request_thread_resumes(MVMInstance *vm, MVMuint32 id) {
    MVMThread *to_do = find_thread_by_id(vm, id);
    MVMThreadContext *tc = to_do->body.tc;

    if (MVM_load(&tc->gc_status) != (MVMGCStatus_UNABLE | MVMSuspendState_SUSPENDED)) {
        return 0xbeefcafe;
    }

    while(1) {
        AO_t current = MVM_cas(&tc->gc_status, MVMGCStatus_UNABLE | MVMSuspendState_SUSPENDED, MVMGCStatus_UNABLE);
        if (current == (MVMGCStatus_UNABLE | MVMSuspendState_SUSPENDED)) {
            /* Success! We signalled the thread and can now tell it to
             * mark itself unblocked, which takes care of any looming GC
             * and related business. */
            uv_cond_broadcast(&vm->debugserver_tell_threads);
            break;
        } else if ((current & MVMGCSTATUS_MASK) == MVMGCStatus_STOLEN) {
            uv_mutex_lock(&tc->instance->mutex_gc_orchestrate);
            if (tc->instance->in_gc) {
                uv_cond_wait(&tc->instance->cond_blocked_can_continue,
                    &tc->instance->mutex_gc_orchestrate);
            }
            uv_mutex_unlock(&tc->instance->mutex_gc_orchestrate);
        }
    }

    return 1;
}

static MVMint32 request_thread_stacktrace(MVMInstance *vm, cmp_ctx_t *ctx, request_data *argument) {
    MVMThread *to_do = find_thread_by_id(vm, argument->id);

    if (!to_do)
        return 0;

    if (to_do->body.tc->gc_status & MVMGCSTATUS_MASK != MVMGCStatus_UNABLE) {
        return 0;
    }
    MVM_dump_backtrace(to_do->body.tc);
    return 1;
}

static void send_thread_info(MVMInstance *vm, cmp_ctx_t *ctx, request_data *argument) {
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
    cmp_write_integer(ctx, argument->type);
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
        cmp_write_bool(ctx, (MVM_load(&cur_thread->body.tc->gc_status) & MVMSUSPENDSTATUS_MASK) == MVMSuspendState_SUSPENDED);

        cmp_write_str(ctx, "num_locks", 9);
        cmp_write_integer(ctx, cur_thread->body.tc->num_locks);

        cur_thread = cur_thread->body.next;
    }
    uv_mutex_unlock(&vm->mutex_threads);
}

typedef struct {
    MVMInstance *vm;
    MVMuint32    port;
} DebugserverWorkerArgs;

static bool socket_reader(cmp_ctx_t *ctx, void *data, size_t limit) {
    if (recv(*((Socket*)ctx->buf), data, limit, 0) == -1)
        return 0;
    return 1;
}

static size_t socket_writer(cmp_ctx_t *ctx, const void *data, size_t count) {
    if (send(*(Socket*)ctx->buf, data, count, 0) == -1)
        return 0;
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
        default:
            return 0;
    }
    return 1;
}

#define CHECK(operation, message) do { if(!(operation)) { data->parse_fail = 1; data->parse_fail_message = (message);fprintf(stderr, "%s", cmp_strerror(ctx)); return 0; } } while(0)
#define FIELD_FOUND(field, duplicated_message) do { if(data->fields_set & (field)) { data->parse_fail = 1; data->parse_fail_message = duplicated_message;  return 0; }; field_to_set = (field); } while (0)

MVMint32 parse_message_map(cmp_ctx_t *ctx, request_data *data) {
    MVMuint32 map_size = 0;
    MVMuint32 i;

    memset(data, 0, sizeof(request_data));

    CHECK(cmp_read_map(ctx, &map_size), "Couldn't read envelope map");

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
        } else {
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
                default:
                    data->parse_fail = 1;
                    data->parse_fail_message = "Field to set NYI";
                    return 0;
            }
            data->fields_set = data->fields_set | field_to_set;
        }
    }

    return check_requirements(data);
}

static void communicate_error(cmp_ctx_t *ctx, request_data *argument) {
    cmp_write_map(ctx, 2);
    cmp_write_str(ctx, "id", 2);
    cmp_write_integer(ctx, argument->id);
    cmp_write_str(ctx, "type", 4);
    cmp_write_integer(ctx, 0);
}

void *debugserver_worker(DebugserverWorkerArgs *args)
{
    int continue_running = 1;
    MVMint32 command_serial;
    Socket listensocket;

    {
        char portstr[16];
        struct addrinfo *res;
        int error;

        snprintf(portstr, 16, "%d", args->port);

        getaddrinfo("localhost", portstr, NULL, &res);

        listensocket = socket(res->ai_family, SOCK_STREAM, 0);

#ifndef _WIN32
        {
            int one = 1;
            setsockopt(listensocket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        }
#endif

        bind(listensocket, res->ai_addr, res->ai_addrlen);

        freeaddrinfo(res);

        listen(listensocket, 1);
    }

    while(continue_running) {
        Socket clientsocket = accept(listensocket, NULL, NULL);
        int len;
        char *buffer[32];
        cmp_ctx_t ctx;

        send_greeting(&clientsocket);

        if (!receive_greeting(&clientsocket)) {
            fprintf(stderr, "did not receive greeting properly\n");
            close(clientsocket);
            continue;
        }

        cmp_init(&ctx, &clientsocket, socket_reader, NULL, socket_writer);

        while (clientsocket) {
            request_data argument;

            parse_message_map(&ctx, &argument);

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

            fprintf(stderr, "debugserver received packet %d, command %d\n", argument.id, argument.type);

            switch (argument.type) {
                case MT_SuspendOne:
                    if (!request_thread_suspends(args->vm, argument.thread_id)) {
                        communicate_error(&ctx, &argument);
                    }
                    break;
                case MT_ResumeOne:
                    if (!request_thread_resumes(args->vm, argument.thread_id)) {
                        communicate_error(&ctx, &argument);
                    }
                    break;
                case MT_ThreadListRequest:
                    send_thread_info(args->vm, &ctx, &argument);
                    break;
                case MT_ThreadStackTraceRequest:
                    if (!request_thread_stacktrace(args->vm, &ctx, &argument)) {
                        communicate_error(&ctx, &argument);
                    }
                    break;
                default: /* Unknown command or NYI */
                    cmp_write_map(&ctx, 2);
                    cmp_write_str(&ctx, "id", 2);
                    cmp_write_integer(&ctx, argument.id);
                    cmp_write_str(&ctx, "type", 4);
                    cmp_write_integer(&ctx, 0);
                    break;

            }
        }
    }

    return NULL;
}

MVM_PUBLIC void MVM_debugserver_init(MVMInstance *vm, MVMuint32 port)
{
    int threadCreateError;

    DebugserverWorkerArgs *args = MVM_malloc(sizeof(DebugserverWorkerArgs));

    args->vm = vm;
    args->port = port;

    threadCreateError = uv_thread_create(&vm->debugserver_thread, (uv_thread_cb)debugserver_worker, (void *)args);
    if (threadCreateError != 0)  {
        fprintf(stderr, "MoarVM: Could not initialize telemetry: %s\n", uv_strerror(threadCreateError));
    }
}

/*MVM_PUBLIC void MVM_telemetry_finish()*/
/*{*/
    /*continueBackgroundSerialization = 0;*/
    /*uv_thread_join(&backgroundSerializationThread);*/
/*}*/

