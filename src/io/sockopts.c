#include "moar.h"

#ifdef _WIN32
#include <ws2def.h>
#else
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#define NUM_SOCKOPTS_WANTED 9

#define SOCKOPTS(X) \
    X( MVM_SO_BROADCAST ) \
    X( MVM_SO_KEEPALIVE ) \
    X( MVM_SO_LINGER    ) \
    X( MVM_SO_REUSEADDR ) \
    X( MVM_SO_DONTROUTE ) \
    X( MVM_SO_SNDBUF    ) \
    X( MVM_SO_RCVBUF    ) \
    X( MVM_SO_OOBINLINE ) \
    X( MVM_TCP_NODELAY  )

#define GEN_ENUMS(v)   v,
#define GEN_STRING(v) #v,

static enum {
    SOCKOPTS(GEN_ENUMS)
} MVM_sockopt_names;

static char const * const SOCKOPTS_WANTED[NUM_SOCKOPTS_WANTED] = {
    SOCKOPTS(GEN_STRING)
};

static void populate_sockopt_values(MVMint32 sockopt_vals[NUM_SOCKOPTS_WANTED]) {
    MVMint8 i;
    for (i = 0; i < NUM_SOCKOPTS_WANTED; ++i) {
        sockopt_vals[i] = 0;
    }

#ifdef SO_BROADCAST
    sockopt_vals[MVM_SO_BROADCAST] = SO_BROADCAST;
#endif
#ifdef SO_KEEPALIVE
    sockopt_vals[MVM_SO_KEEPALIVE] = SO_KEEPALIVE;
#endif
#ifdef SO_LINGER
    sockopt_vals[MVM_SO_LINGER] = SO_LINGER;
#endif
#ifdef SO_REUSEADDR
    sockopt_vals[MVM_SO_REUSEADDR] = SO_REUSEADDR;
#endif
#ifdef SO_DONTROUTE
    sockopt_vals[MVM_SO_DONTROUTE] = SO_DONTROUTE;
#endif
#ifdef SO_SNDBUF
    sockopt_vals[MVM_SO_SNDBUF] = SO_SNDBUF;
#endif
#ifdef SO_RCVBUF
    sockopt_vals[MVM_SO_RCVBUF] = SO_RCVBUF;
#endif
#ifdef SO_OOBINLINE
    sockopt_vals[MVM_SO_OOBINLINE] = SO_OOBINLINE;
#endif
#ifdef TCP_NODELAY
    sockopt_vals[MVM_TCP_NODELAY] = TCP_NODELAY;
#endif
}

static void populate_instance_valid_sockopts(MVMThreadContext *tc, MVMint32 sockopt_vals[NUM_SOCKOPTS_WANTED]) {
    MVMint32 valid_sockopts = 0;
    MVMint8 i;

    if (tc->instance->valid_sockopts) return;

    for (i = 0; i < NUM_SOCKOPTS_WANTED; ++i) {
        if (sockopt_vals[i]) {
            valid_sockopts |= sockopt_vals[i];
        }
    }

    tc->instance->valid_sockopts = valid_sockopts;
}

MVMObject * MVM_io_get_sockopts(MVMThreadContext *tc) {
    MVMInstance  * const instance     = tc->instance;
    MVMHLLConfig *       hll          = MVM_hll_current(tc);
    MVMObject    *       sockopt_arr;

    MVMint32 sockopt_wanted_vals[NUM_SOCKOPTS_WANTED];
    populate_sockopt_values(sockopt_wanted_vals);

    if (instance->sockopt_arr) {
        return instance->sockopt_arr;
    }

    sockopt_arr = MVM_repr_alloc_init(tc, hll->slurpy_array_type);

    MVMROOT(tc, sockopt_arr, {
        MVMint8 i;
        for (i = 0; i < NUM_SOCKOPTS_WANTED; ++i) {
            MVMObject *key      = NULL;
            MVMString *full_key = NULL;
            MVMObject *val      = NULL;

            MVMROOT3(tc, key, full_key, val, {
                full_key = MVM_string_utf8_c8_decode(
                    tc,
                    instance->VMString,
                    SOCKOPTS_WANTED[i],
                    strlen(SOCKOPTS_WANTED[i])
                );

                key = MVM_repr_box_str(
                    tc,
                    hll->str_box_type,
                    MVM_string_substring(tc, full_key, 4, -1)
                );
                val = MVM_repr_box_int(tc, hll->int_box_type, sockopt_wanted_vals[i]);

                MVM_repr_push_o(tc, sockopt_arr, key);
                MVM_repr_push_o(tc, sockopt_arr, val);
            });
        }

        populate_instance_valid_sockopts(tc, sockopt_wanted_vals);
        instance->sockopt_arr = sockopt_arr;
    });

    return sockopt_arr;
}

const char * MVM_io_get_sockopt_name(MVMint32 option) {
    switch (option) {
        case SO_BROADCAST: return "SO_BROADCAST";
        case SO_KEEPALIVE: return "SO_KEEPALIVE";
        case SO_LINGER:    return "SO_LINGER";
        case SO_REUSEADDR: return "SO_REUSEADDR";
        case SO_DONTROUTE: return "SO_DONTROUTE";
        case SO_SNDBUF:    return "SO_SNDBUF";
        case SO_RCVBUF:    return "SO_RCVBUF";
        case SO_OOBINLINE: return "SO_OOBINLINE";
        case TCP_NODELAY:  return "TCP_NODELAY";
        default:           return "unknown";
    }
}
