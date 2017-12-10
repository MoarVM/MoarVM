#include "moar.h"
#include "endian.h"

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

void send_greeting(Socket *sock) {
    char buffer[24] = "MOARVM-REMOTE-DEBUG\0";
    MVMuint32 version = htobe16(1);

    MVMuint16 *verptr = (MVMuint16 *)(&buffer[strlen("MOARVM-REMOTE-DEBUG") + 1]);
    *verptr = version;
    verptr++;
    *verptr = version;
    send(*sock, buffer, 24, 0);
}

int receive_greeting(Socket *sock) {
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

typedef struct {
    MVMInstance *vm;
    MVMuint32    port;
} DebugserverWorkerArgs;

void *debugserver_worker(DebugserverWorkerArgs *args)
{
    int continue_running = 1;
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

        send(clientsocket, "oh my", 5, 0);
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

