#include "moar.h"
#include "platform/threads.h"
#include <endian.h>

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

static MVMint32 request_thread_stacktrace(MVMInstance *vm, MVMuint32 id) {
    MVMThread *to_do = find_thread_by_id(vm, id);

    if (!to_do)
        return 0xbeefcafe;

    if (to_do->body.tc->gc_status & MVMGCSTATUS_MASK != MVMGCStatus_UNABLE) {
        return 0xc0ffee;
    }
    MVM_dump_backtrace(to_do->body.tc);
    return 1;
}

static void send_thread_info(MVMInstance *vm, Socket *sock) {
    MVMint32 threadcount = 0;
    MVMThread *cur_thread;
    char infobuf[32] = "THL";

    uv_mutex_lock(&vm->mutex_threads);
    cur_thread = vm->threads;
    while (cur_thread) {
        threadcount++;
        fprintf(stderr, "thread found: %d\n", cur_thread->body.thread_id);
        cur_thread = cur_thread->body.next;
    }
    fprintf(stderr, "writing threadcount of %d\n", threadcount);
    ((MVMint32*)infobuf)[1] = htobe32(threadcount);
    send(*sock, infobuf, 8, 0);
    cur_thread = vm->threads;
    while (cur_thread) {
        memset(infobuf, 0, 16);
        ((MVMint32*)infobuf)[0] = htobe32(cur_thread->body.thread_id);
        ((MVMint32*)infobuf)[1] = htobe32(MVM_load(&cur_thread->body.stage));
        ((MVMint32*)infobuf)[2] = htobe32(MVM_load(&cur_thread->body.tc->gc_status));
        ((MVMint32*)infobuf)[4] = htobe32(cur_thread->body.app_lifetime);
        send(*sock, infobuf, 16, 0);
        cur_thread = cur_thread->body.next;
    }
    uv_mutex_unlock(&vm->mutex_threads);
}

typedef struct {
    MVMInstance *vm;
    MVMuint32    port;
} DebugserverWorkerArgs;

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

        send_greeting(&clientsocket);

        if (!receive_greeting(&clientsocket)) {
            close(clientsocket);
        }

        while (clientsocket) {
            char command;
            MVMuint32 txn_id;
            MVMuint32 argument;

            if (recv(clientsocket, &txn_id, 1, 0) == 0) {
                close(clientsocket);
                clientsocket = 0;
            }

            if (recv(clientsocket, &command, 1, 0) == 0 || command == 0) {
                close(clientsocket);
                clientsocket = 0;
            }
            fprintf(stderr, "debugserver received packet %d, command %d\n", be32toh(txn_id), command);

            switch (command) {
                case 1: /* Suspend a thread */
                    recv(clientsocket, &argument, 4, 0);
                    argument = htobe32(request_thread_suspends(args->vm, be32toh(argument)));
                    send(clientsocket, &txn_id, 4, 0);
                    send(clientsocket, &command, 1, 0);
                    send(clientsocket, &argument, 4, 0);
                    break;
                case 2: /* Resume a thread */
                    recv(clientsocket, &argument, 4, 0);
                    argument = htobe32(request_thread_resumes(args->vm, be32toh(argument)));
                    send(clientsocket, &txn_id, 4, 0);
                    send(clientsocket, &command, 1, 0);
                    send(clientsocket, &argument, 4, 0);
                    break;
                case 3: /* Get thread list again */
                    send(clientsocket, &txn_id, 4, 0);
                    send(clientsocket, &command, 1, 0);
                    send_thread_info(args->vm, &clientsocket);
                    break;
                case 9: /* Ask thread to dump stacktrace to stderr */
                    recv(clientsocket, &argument, 4, 0);
                    argument = htobe32(request_thread_stacktrace(args->vm, be32toh(argument)));
                    send(clientsocket, &txn_id, 4, 0);
                    send(clientsocket, &command, 1, 0);
                    send(clientsocket, &argument, 4, 0);
                    break;
                default: /* Unknown command */
                    argument = 0xc0ffee;

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

