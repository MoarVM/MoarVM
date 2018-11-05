#include "moar.h"

#ifdef _WIN32
#include <ws2def.h>
#else
#include <sys/socket.h>
#endif

#define NUM_SOCKOPTS_WANTED 25

#define SOCKOPTS(X) \
   X( MVM_SO_DEBUG       ) \
   X( MVM_SO_ACCEPTCONN  ) \
   X( MVM_SO_REUSEADDR   ) \
   X( MVM_SO_KEEPALIVE   ) \
   X( MVM_SO_DONTROUTE   ) \
   X( MVM_SO_BROADCAST   ) \
   X( MVM_SO_USELOOPBACK ) \
   X( MVM_SO_LINGER      ) \
   X( MVM_SO_OOBINLINE   ) \
   X( MVM_SO_REUSEPORT   ) \
   X( MVM_SO_TIMESTAMP   ) \
   X( MVM_SO_BINDANY     ) \
   X( MVM_SO_ZEROIZE     ) \
   X( MVM_SO_SNDBUF      ) \
   X( MVM_SO_RCVBUF      ) \
   X( MVM_SO_SNDLOWAT    ) \
   X( MVM_SO_RCVLOWAT    ) \
   X( MVM_SO_SNDTIMEO    ) \
   X( MVM_SO_RCVTIMEO    ) \
   X( MVM_SO_ERROR       ) \
   X( MVM_SO_TYPE        ) \
   X( MVM_SO_NETPROC     ) \
   X( MVM_SO_RTABLE      ) \
   X( MVM_SO_PEERCRED    ) \
   X( MVM_SO_SPLICE      )

#define GEN_ENUMS(v)   v,
#define GEN_STRING(v) #v,

static enum {
    SOCKOPTS(GEN_ENUMS)
} MVM_sockopt_names;

static char const * const SOCKOPTS_WANTED[NUM_SOCKOPTS_WANTED] = {
    SOCKOPTS(GEN_STRING)
};

static void populate_sockopt_values(MVMint16 sockopt_vals[NUM_SOCKOPTS_WANTED]) {
    MVMint8 i;
    for (i = 0; i < NUM_SOCKOPTS_WANTED; ++i) {
        sockopt_vals[i] = 0;
    }

#ifdef SO_DEBUG
    sockopt_vals[MVM_SO_DEBUG]       = SO_DEBUG;
#endif
#ifdef SO_ACCEPTCONN
    sockopt_vals[MVM_SO_ACCEPTCONN]  = SO_ACCEPTCONN;
#endif
#ifdef SO_REUSEADDR
    sockopt_vals[MVM_SO_REUSEADDR]   = SO_REUSEADDR;
#endif
#ifdef SO_KEEPALIVE
    sockopt_vals[MVM_SO_KEEPALIVE]   = SO_KEEPALIVE;
#endif
#ifdef SO_DONTROUTE
    sockopt_vals[MVM_SO_DONTROUTE]   = SO_DONTROUTE;
#endif
#ifdef SO_BROADCAST
    sockopt_vals[MVM_SO_BROADCAST]   = SO_BROADCAST;
#endif
#ifdef SO_USELOOPBACK
    sockopt_vals[MVM_SO_USELOOPBACK] = SO_USELOOPBACK;
#endif
#ifdef SO_LINGER
    sockopt_vals[MVM_SO_LINGER]      = SO_LINGER;
#endif
#ifdef SO_OOBINLINE
    sockopt_vals[MVM_SO_OOBINLINE]   = SO_OOBINLINE;
#endif
#ifdef SO_REUSEPORT
    sockopt_vals[MVM_SO_REUSEPORT]   = SO_REUSEPORT;
#endif
#ifdef SO_TIMESTAMP
    sockopt_vals[MVM_SO_TIMESTAMP]   = SO_TIMESTAMP;
#endif
#ifdef SO_BINDANY
    sockopt_vals[MVM_SO_BINDANY]     = SO_BINDANY;
#endif
#ifdef SO_ZEROIZE
    sockopt_vals[MVM_SO_ZEROIZE]     = SO_ZEROIZE;
#endif
#ifdef SO_SNDBUF
    sockopt_vals[MVM_SO_SNDBUF]      = SO_SNDBUF;
#endif
#ifdef SO_RCVBUF
    sockopt_vals[MVM_SO_RCVBUF]      = SO_RCVBUF;
#endif
#ifdef SO_SNDLOWAT
    sockopt_vals[MVM_SO_SNDLOWAT]    = SO_SNDLOWAT;
#endif
#ifdef SO_RCVLOWAT
    sockopt_vals[MVM_SO_RCVLOWAT]    = SO_RCVLOWAT;
#endif
#ifdef SO_SNDTIMEO
    sockopt_vals[MVM_SO_SNDTIMEO]    = SO_SNDTIMEO;
#endif
#ifdef SO_RCVTIMEO
    sockopt_vals[MVM_SO_RCVTIMEO]    = SO_RCVTIMEO;
#endif
#ifdef SO_ERROR
    sockopt_vals[MVM_SO_ERROR]       = SO_ERROR;
#endif
#ifdef SO_TYPE
    sockopt_vals[MVM_SO_TYPE]        = SO_TYPE;
#endif
#ifdef SO_NETPROC
    sockopt_vals[MVM_SO_NETPROC]     = SO_NETPROC;
#endif
#ifdef SO_RTABLE
    sockopt_vals[MVM_SO_RTABLE]      = SO_RTABLE;
#endif
#ifdef SO_PEERCRED
    sockopt_vals[MVM_SO_PEERCRED]    = SO_PEERCRED;
#endif
#ifdef SO_SPLICE
    sockopt_vals[MVM_SO_SPLICE]      = SO_SPLICE;
#endif
}

static void populate_instance_valid_sockopts(MVMThreadContext *tc, MVMint16 sockopt_vals[NUM_SOCKOPTS_WANTED]) {
    MVMuint64 valid_sockopts = 0;
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

    MVMint16 sockopt_wanted_vals[NUM_SOCKOPTS_WANTED];
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
