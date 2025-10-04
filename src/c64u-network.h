#ifndef C64U_NETWORK_H
#define C64U_NETWORK_H

#include <stdbool.h>
#include <stdint.h>

// Platform-specific networking includes
#ifdef _WIN32
// Prevent winsock.h inclusion to avoid conflicts with winsock2.h
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_ // Prevent winsock.h from being included
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <windows.h>
#include <iphlpapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#define close(s) closesocket(s)
#define SHUT_RDWR SD_BOTH
typedef int socklen_t;
typedef SOCKET socket_t;
// Define ssize_t for MSVC (MinGW has it, but MSVC doesn't)
#ifndef __MINGW32__
typedef long long ssize_t;
#endif
#define INVALID_SOCKET_VALUE INVALID_SOCKET
// Format specifier for ssize_t on Windows (64-bit)
#define SSIZE_T_FORMAT "%lld"
#define SSIZE_T_CAST(x) ((long long)(x))
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#ifdef __APPLE__
#include <ifaddrs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#elif __linux__
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <netdb.h>
#endif
typedef int socket_t;
#define INVALID_SOCKET_VALUE -1
// Format specifier for ssize_t on POSIX (typically 32-bit or 64-bit depending on platform)
#define SSIZE_T_FORMAT "%zd"
#define SSIZE_T_CAST(x) (x)
#endif

// Network initialization and cleanup
bool c64u_init_networking(void);
void c64u_cleanup_networking(void);

// IP detection and hostname resolution
bool c64u_detect_local_ip(char *ip_buffer, size_t buffer_size);
bool c64u_resolve_hostname(const char *hostname, char *ip_buffer, size_t buffer_size);
bool c64u_resolve_hostname_with_dns(const char *hostname, const char *custom_dns_server, char *ip_buffer,
                                    size_t buffer_size);

// Platform-specific utilities
bool c64u_get_user_documents_path(char *path_buffer, size_t buffer_size);

// Socket operations
socket_t c64u_create_udp_socket(uint32_t port);
socket_t c64u_create_tcp_socket(const char *ip, uint32_t port);
bool c64u_test_connectivity(const char *ip, uint32_t port);

// Error handling
int c64u_get_socket_error(void);
const char *c64u_get_socket_error_string(int error);

#endif // C64U_NETWORK_H
