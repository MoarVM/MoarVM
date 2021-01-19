#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <io.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
#endif

#ifdef _WIN32
#  if 0
/* TODO: Is Windows' UNIX socket support more complete nowadays...? SOCK_DGRAM
 * support was missing at one point. */
#  include <afunix.h>
#  endif
#else
#  include <sys/un.h>
#  define MVM_HAS_PF_UNIX
#  define MVM_SUN_PATH_SIZE sizeof(((struct sockaddr_un *)NULL)->sun_path)
#endif

#ifdef _WIN32
typedef ULONG  in_addr_t;
typedef USHORT in_port_t;
typedef USHORT sa_family_t;
typedef SOCKET MVMSocket;
#else
typedef int MVMSocket;
#endif

#ifdef _WIN32
#  define MVM_platform_close_socket closesocket
#  define MVM_platform_isatty _isatty
#else
#  define MVM_platform_close_socket close
#  define MVM_platform_isatty isatty
#endif
