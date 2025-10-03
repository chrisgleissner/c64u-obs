#include <obs-module.h>
#include <string.h>
#ifdef __cplusplus
#include <memory>
#include <string>
#include <system_error>
extern "C" {
#endif
#include "c64u-logging.h"
#include "c64u-network.h"
#include "plugin-support.h"

// Network helper functions
bool c64u_init_networking(void)
{
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        obs_log(LOG_ERROR, "[C64U] WSAStartup failed: %d", result);
        return false;
    }
    obs_log(LOG_DEBUG, "[C64U] Windows networking initialized");
#endif
    return true;
}

// Function to detect local machine's IP address
bool c64u_detect_local_ip(char *ip_buffer, size_t buffer_size)
{
    if (!ip_buffer || buffer_size < 16) {
        return false;
    }

    // Initialize buffer
    strncpy(ip_buffer, "127.0.0.1", buffer_size - 1);
    ip_buffer[buffer_size - 1] = '\0';

#ifdef _WIN32
    // Windows implementation using GetAdaptersInfo
    PIP_ADAPTER_INFO adapter_info = NULL;
    ULONG out_buf_len = 0;
    DWORD ret_val = 0;

    // Get buffer size needed
    ret_val = GetAdaptersInfo(adapter_info, &out_buf_len);
    if (ret_val == ERROR_BUFFER_OVERFLOW) {
        adapter_info = (IP_ADAPTER_INFO *)malloc(out_buf_len);
        if (adapter_info != NULL) {
            ret_val = GetAdaptersInfo(adapter_info, &out_buf_len);
            if (ret_val == NO_ERROR) {
                PIP_ADAPTER_INFO adapter = adapter_info;
                while (adapter) {
                    // Skip loopback and inactive adapters
                    if (adapter->Type != MIB_IF_TYPE_LOOPBACK &&
                        strcmp(adapter->IpAddressList.IpAddress.String, "0.0.0.0") != 0) {
                        strncpy(ip_buffer, adapter->IpAddressList.IpAddress.String, buffer_size - 1);
                        ip_buffer[buffer_size - 1] = '\0';
                        obs_log(LOG_INFO, "[C64U] Detected Windows IP address: %s (adapter: %s)", ip_buffer,
                                adapter->AdapterName);
                        free(adapter_info);
                        return true;
                    }
                    adapter = adapter->Next;
                }
            }
            free(adapter_info);
        }
    }
    obs_log(LOG_WARNING, "[C64U] Failed to detect Windows IP address, using fallback");
    return false;

#elif defined(__APPLE__) || defined(__linux__)
    // Unix/Linux/macOS implementation using getifaddrs
    struct ifaddrs *ifaddrs_ptr, *ifa;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddrs_ptr) == -1) {
        obs_log(LOG_WARNING, "[C64U] getifaddrs failed: %s", strerror(errno));
        return false;
    }

    for (ifa = ifaddrs_ptr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        // Skip loopback interface
        if (strcmp(ifa->ifa_name, "lo") == 0 || strcmp(ifa->ifa_name, "lo0") == 0) {
            continue;
        }

        int result = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
        if (result != 0) {
            continue;
        }

        // Skip if it's a localhost address
        if (strcmp(host, "127.0.0.1") == 0) {
            continue;
        }

        // We found a valid IP address
        strncpy(ip_buffer, host, buffer_size - 1);
        ip_buffer[buffer_size - 1] = '\0';
        obs_log(LOG_INFO, "[C64U] Detected %s IP address: %s (interface: %s)",
#ifdef __APPLE__
                "macOS",
#else
                "Linux",
#endif
                ip_buffer, ifa->ifa_name);
        freeifaddrs(ifaddrs_ptr);
        return true;
    }

    freeifaddrs(ifaddrs_ptr);
    obs_log(LOG_WARNING, "[C64U] No suitable network interface found, using fallback");
    return false;

#else
    // Fallback for other platforms
    obs_log(LOG_WARNING, "[C64U] IP detection not implemented for this platform, using fallback");
    return false;
#endif
}

void c64u_cleanup_networking(void)
{
#ifdef _WIN32
    WSACleanup();
    obs_log(LOG_DEBUG, "[C64U] Windows networking cleaned up");
#endif
}

int c64u_get_socket_error(void)
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

const char *c64u_get_socket_error_string(int error)
{
#ifdef _WIN32
    static char buffer[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, sizeof(buffer), NULL);
    return buffer;
#else
    return strerror(error);
#endif
}

socket_t create_udp_socket(uint32_t port)
{
    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET_VALUE) {
        int error = c64u_get_socket_error();
        obs_log(LOG_ERROR, "[C64U] Failed to create UDP socket: %s", c64u_get_socket_error_string(error));
        return INVALID_SOCKET_VALUE;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int error = c64u_get_socket_error();
        obs_log(LOG_ERROR, "[C64U] Failed to bind UDP socket to port %u: %s", port,
                c64u_get_socket_error_string(error));
        close(sock);
        return INVALID_SOCKET_VALUE;
    }

    // Set socket to non-blocking
#ifdef _WIN32
    u_long mode = 1;
    if (ioctlsocket(sock, FIONBIO, &mode) != 0) {
        int error = c64u_get_socket_error();
        obs_log(LOG_WARNING, "[C64U] Failed to set socket non-blocking: %s", c64u_get_socket_error_string(error));
    }
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    }
#endif

    obs_log(LOG_DEBUG, "[C64U] Created UDP socket on port %u", port);
    return sock;
}

socket_t create_tcp_socket(const char *ip, uint32_t port)
{
    if (!ip || strlen(ip) == 0) {
        obs_log(LOG_ERROR, "[C64U] Invalid IP address provided");
        return INVALID_SOCKET_VALUE;
    }

    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET_VALUE) {
        int error = c64u_get_socket_error();
        obs_log(LOG_ERROR, "[C64U] Failed to create TCP socket: %s", c64u_get_socket_error_string(error));
        return INVALID_SOCKET_VALUE;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        obs_log(LOG_ERROR, "[C64U] Invalid IP address format: %s", ip);
        close(sock);
        return INVALID_SOCKET_VALUE;
    }

    // Set socket to non-blocking for timeout control
#ifdef _WIN32
    u_long non_blocking = 1;
    if (ioctlsocket(sock, FIONBIO, &non_blocking) != 0) {
        obs_log(LOG_ERROR, "[C64U] Failed to set socket non-blocking");
        close(sock);
        return INVALID_SOCKET_VALUE;
    }
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        obs_log(LOG_ERROR, "[C64U] Failed to set socket non-blocking");
        close(sock);
        return INVALID_SOCKET_VALUE;
    }
#endif

    // Attempt connection (will return immediately with EINPROGRESS/WSAEWOULDBLOCK)
    int connect_result = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (connect_result == 0) {
        // Connected immediately (rare case)
        // Restore blocking mode
#ifdef _WIN32
        u_long blocking = 0;
        ioctlsocket(sock, FIONBIO, &blocking);
#else
        fcntl(sock, F_SETFL, flags);
#endif
        obs_log(LOG_DEBUG, "[C64U] Connected to C64U at %s:%u", ip, port);
        return sock;
    }

    int error = c64u_get_socket_error();
#ifdef _WIN32
    if (error != WSAEWOULDBLOCK) {
#else
    if (error != EINPROGRESS) {
#endif
        obs_log(LOG_WARNING, "[C64U] Failed to connect to C64U at %s:%u: %s", ip, port,
                c64u_get_socket_error_string(error));
        close(sock);
        return INVALID_SOCKET_VALUE;
    }

    // Wait for connection with 1-second timeout
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(sock, &write_fds);

    struct timeval timeout;
    timeout.tv_sec = 1; // 1 second timeout
    timeout.tv_usec = 0;

#ifdef _WIN32
    int select_result = select(0, NULL, &write_fds, NULL, &timeout);
#else
    int select_result = select(sock + 1, NULL, &write_fds, NULL, &timeout);
#endif
    if (select_result == 0) {
        // Timeout
        obs_log(LOG_WARNING, "[C64U] Connection to C64U at %s:%u timed out after 1 second", ip, port);
        close(sock);
        return INVALID_SOCKET_VALUE;
    }

    if (select_result < 0) {
        int select_error = c64u_get_socket_error();
        obs_log(LOG_ERROR, "[C64U] Select failed during connection to %s:%u: %s", ip, port,
                c64u_get_socket_error_string(select_error));
        close(sock);
        return INVALID_SOCKET_VALUE;
    }

    // Check if connection succeeded or failed
    int sock_error;
    socklen_t len = sizeof(sock_error);
#ifdef _WIN32
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&sock_error, &len) != 0) {
#else
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &sock_error, &len) < 0) {
#endif
        obs_log(LOG_ERROR, "[C64U] Failed to get socket error for %s:%u", ip, port);
        close(sock);
        return INVALID_SOCKET_VALUE;
    }

    if (sock_error != 0) {
        obs_log(LOG_WARNING, "[C64U] Failed to connect to C64U at %s:%u: %s", ip, port,
                c64u_get_socket_error_string(sock_error));
        close(sock);
        return INVALID_SOCKET_VALUE;
    }

    // Restore blocking mode
#ifdef _WIN32
    u_long blocking = 0;
    ioctlsocket(sock, FIONBIO, &blocking);
#else
    fcntl(sock, F_SETFL, flags);
#endif

    obs_log(LOG_DEBUG, "[C64U] Connected to C64U at %s:%u", ip, port);
    return sock;
}

#ifdef __cplusplus
} // extern "C"

// C++ utilities for improved error handling and resource management
namespace c64u {

// RAII socket wrapper
class Socket {
    public:
    explicit Socket(socket_t sock = INVALID_SOCKET_VALUE) : sock_(sock) {}
    ~Socket()
    {
        if (sock_ != INVALID_SOCKET_VALUE)
            close(sock_);
    }

    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;

    Socket(Socket &&other) noexcept : sock_(other.sock_) { other.sock_ = INVALID_SOCKET_VALUE; }

    socket_t get() const { return sock_; }
    socket_t release()
    {
        socket_t temp = sock_;
        sock_ = INVALID_SOCKET_VALUE;
        return temp;
    }

    private:
    socket_t sock_;
};

} // namespace c64u
#endif
