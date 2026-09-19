#include "platform.h"
#include "runtime.h"
#include "common.h"

#define LINUX_AF_INET 2
#define LINUX_AF_INET6 10
#define LINUX_SOCK_STREAM 1
#define LINUX_SOCK_DGRAM 2
#define LINUX_SOCK_RAW 3
#define LINUX_IPPROTO_IP 0
#define LINUX_IPPROTO_ICMP 1
#define LINUX_IPPROTO_TCP 6
#define LINUX_IPPROTO_UDP 17
#define LINUX_IPPROTO_IPV6 41
#define LINUX_IPPROTO_ICMPV6 58
#define LINUX_IP_TTL 2
#define LINUX_IP_RECVERR 11
#define LINUX_IPV6_UNICAST_HOPS 16
#define LINUX_SOL_SOCKET 1
#define LINUX_SO_REUSEADDR 2
#define LINUX_SO_BROADCAST 6
#define LINUX_SHUT_WR 1
#define LINUX_POLLIN 0x0001
#define LINUX_POLLOUT 0x0004
#define LINUX_POLLERR 0x0008
#define LINUX_POLLHUP 0x0010
#define LINUX_MSG_ERRQUEUE 0x2000
#define LINUX_EPERM 1
#define LINUX_EAGAIN 11
#define LINUX_EACCES 13
#define LINUX_EWOULDBLOCK 11
#define LINUX_EADDRNOTAVAIL 99
#define LINUX_ENETDOWN 100
#define LINUX_ENETUNREACH 101
#define LINUX_ETIMEDOUT 110
#define LINUX_ECONNREFUSED 111
#define LINUX_EHOSTDOWN 112
#define LINUX_EHOSTUNREACH 113
#define LINUX_EINPROGRESS 115
#define LINUX_SO_EE_ORIGIN_ICMP 2U
#define LINUX_IFNAMSIZ 16
#define LINUX_IFF_UP 0x0001U
#define LINUX_IFF_BROADCAST 0x0002U
#define LINUX_IFF_LOOPBACK 0x0008U
#define LINUX_IFF_RUNNING 0x0040U
#define LINUX_IFF_MULTICAST 0x1000U
#define LINUX_SIOCADDRT 0x890BU
#define LINUX_SIOCDELRT 0x890CU
#define LINUX_SIOCGIFFLAGS 0x8913U
#define LINUX_SIOCSIFFLAGS 0x8914U
#define LINUX_SIOCGIFADDR 0x8915U
#define LINUX_SIOCSIFADDR 0x8916U
#define LINUX_SIOCGIFBRDADDR 0x8919U
#define LINUX_SIOCGIFNETMASK 0x891BU
#define LINUX_SIOCSIFNETMASK 0x891CU
#define LINUX_SIOCGIFMTU 0x8921U
#define LINUX_SIOCSIFMTU 0x8922U
#define LINUX_SIOCGIFHWADDR 0x8927U
#define LINUX_SIOCGIFINDEX 0x8933U
#define LINUX_RTF_UP 0x0001U
#define LINUX_RTF_GATEWAY 0x0002U
#define LINUX_RTF_HOST 0x0004U
#define LINUX_ICMP_ECHO 8U
#define LINUX_ICMP_REPLY 0U
#define LINUX_ICMP_TIME_EXCEEDED 11U
#define LINUX_ICMPV6_ECHO_REQUEST 128U
#define LINUX_ICMPV6_ECHO_REPLY 129U
#define LINUX_ICMPV6_TIME_EXCEEDED 3U
#define LINUX_CLOCK_MONOTONIC 1

typedef struct {
    unsigned char bytes[4];
} LinuxInAddr;

typedef struct {
    unsigned char bytes[16];
} LinuxIn6Addr;

typedef struct {
    unsigned char type;
    unsigned char code;
    unsigned short checksum;
    unsigned short identifier;
    unsigned short sequence;
} LinuxIcmpPacket;

typedef struct {
    unsigned char type;
    unsigned char code;
    unsigned short checksum;
    unsigned short identifier;
    unsigned short sequence;
} LinuxIcmpv6Packet;

struct linux_sockaddr {
    unsigned short sa_family;
    unsigned char sa_data[14];
};

struct linux_sockaddr_in {
    unsigned short sin_family;
    unsigned short sin_port;
    LinuxInAddr sin_addr;
    unsigned char sin_zero[8];
};

struct linux_sockaddr_in6 {
    unsigned short sin6_family;
    unsigned short sin6_port;
    unsigned int sin6_flowinfo;
    LinuxIn6Addr sin6_addr;
    unsigned int sin6_scope_id;
};

struct linux_ifreq {
    char ifr_name[LINUX_IFNAMSIZ];
    union {
        struct linux_sockaddr addr;
        struct linux_sockaddr netmask;
        struct linux_sockaddr broadaddr;
        struct linux_sockaddr hwaddr;
        short flags;
        int mtu;
        int ifindex;
    } data;
};

struct linux_rtentry {
    unsigned long rt_pad1;
    struct linux_sockaddr rt_dst;
    struct linux_sockaddr rt_gateway;
    struct linux_sockaddr rt_genmask;
    unsigned short rt_flags;
    short rt_pad2;
    unsigned long rt_pad3;
    void *rt_pad4;
    short rt_metric;
    char *rt_dev;
    unsigned long rt_mtu;
    unsigned long rt_window;
    unsigned short rt_irtt;
};

struct linux_pollfd {
    int fd;
    short events;
    short revents;
};

struct linux_iovec {
    void *iov_base;
    unsigned long iov_len;
};

struct linux_msghdr {
    void *msg_name;
    unsigned int msg_namelen;
    struct linux_iovec *msg_iov;
    unsigned long msg_iovlen;
    void *msg_control;
    unsigned long msg_controllen;
    unsigned int msg_flags;
};

struct linux_cmsghdr {
    unsigned long cmsg_len;
    int cmsg_level;
    int cmsg_type;
};

struct linux_sock_extended_err {
    unsigned int ee_errno;
    unsigned char ee_origin;
    unsigned char ee_type;
    unsigned char ee_code;
    unsigned char ee_pad;
    unsigned int ee_info;
    unsigned int ee_data;
};

static unsigned short linux_byte_swap16(unsigned short value) {
    return (unsigned short)(((value & 0x00ffU) << 8) | ((value & 0xff00U) >> 8));
}

static unsigned short linux_host_to_net16(unsigned short value) {
    return linux_byte_swap16(value);
}

static unsigned short linux_net_to_host16(unsigned short value) {
    return linux_byte_swap16(value);
}


static int linux_is_space_char(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void linux_skip_spaces(const char **cursor) {
    while (**cursor != '\0' && linux_is_space_char(**cursor)) {
        *cursor += 1;
    }
}

static int linux_copy_token(const char **cursor, char *buffer, size_t buffer_size) {
    size_t used = 0;
    const char *scan;

    if (cursor == 0 || *cursor == 0 || buffer == 0 || buffer_size == 0U) {
        return -1;
    }

    linux_skip_spaces(cursor);
    scan = *cursor;
    while (*scan != '\0' && !linux_is_space_char(*scan)) {
        if (used + 1U < buffer_size) {
            buffer[used++] = *scan;
        }
        scan += 1;
    }
    buffer[used] = '\0';
    *cursor = scan;
    return used == 0U ? -1 : 0;
}

static int linux_parse_decimal_text(const char *text, unsigned long long *value_out) {
    unsigned long long value = 0;
    size_t i = 0;

    if (text == 0 || value_out == 0 || text[0] == '\0') {
        return -1;
    }

    while (text[i] != '\0') {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }
        value = value * 10ULL + (unsigned long long)(text[i] - '0');
        i += 1U;
    }

    *value_out = value;
    return 0;
}

static int linux_parse_hex_text(const char *text, unsigned long *value_out) {
    unsigned long value = 0;
    size_t i = 0;

    if (text == 0 || value_out == 0 || text[0] == '\0') {
        return -1;
    }

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        i = 2U;
    }

    for (; text[i] != '\0'; ++i) {
        unsigned int digit;

        if (text[i] >= '0' && text[i] <= '9') {
            digit = (unsigned int)(text[i] - '0');
        } else if (text[i] >= 'a' && text[i] <= 'f') {
            digit = (unsigned int)(text[i] - 'a' + 10);
        } else if (text[i] >= 'A' && text[i] <= 'F') {
            digit = (unsigned int)(text[i] - 'A' + 10);
        } else {
            return -1;
        }
        value = (value << 4) | (unsigned long)digit;
    }

    *value_out = value;
    return 0;
}

static int linux_parse_ipv4_text(const char *text, LinuxInAddr *address_out) {
    unsigned char bytes[4];
    int part = 0;
    unsigned int value = 0U;
    int saw_digit = 0;
    size_t i = 0;

    if (text == 0 || address_out == 0) {
        return -1;
    }

    while (1) {
        char ch = text[i];

        if (ch >= '0' && ch <= '9') {
            value = value * 10U + (unsigned int)(ch - '0');
            if (value > 255U) {
                return -1;
            }
            saw_digit = 1;
            i += 1U;
            continue;
        }

        if ((ch == '.' || ch == '\0') && saw_digit && part < 4) {
            bytes[part++] = (unsigned char)value;
            value = 0U;
            saw_digit = 0;
            if (ch == '\0') {
                break;
            }
            i += 1U;
            continue;
        }

        return -1;
    }

    if (part != 4) {
        return -1;
    }

    address_out->bytes[0] = bytes[0];
    address_out->bytes[1] = bytes[1];
    address_out->bytes[2] = bytes[2];
    address_out->bytes[3] = bytes[3];
    return 0;
}

static void linux_ipv4_to_text(const LinuxInAddr *address, char *buffer, size_t buffer_size) {
    char temp[4];
    size_t used = 0;
    size_t i;

    if (buffer == 0 || buffer_size == 0U || address == 0) {
        return;
    }

    buffer[0] = '\0';
    for (i = 0; i < 4U; ++i) {
        unsigned int value = address->bytes[i];
        size_t digits = 0U;
        size_t j;

        if (i > 0U && used + 1U < buffer_size) {
            buffer[used++] = '.';
        }

        if (value >= 100U) {
            temp[digits++] = (char)('0' + (value / 100U));
            value %= 100U;
            temp[digits++] = (char)('0' + (value / 10U));
            temp[digits++] = (char)('0' + (value % 10U));
        } else if (value >= 10U) {
            temp[digits++] = (char)('0' + (value / 10U));
            temp[digits++] = (char)('0' + (value % 10U));
        } else {
            temp[digits++] = (char)('0' + value);
        }

        for (j = 0; j < digits && used + 1U < buffer_size; ++j) {
            buffer[used++] = temp[j];
        }
    }
    buffer[used] = '\0';
}

static void linux_trim_line(char *text) {
    size_t length;

    if (text == 0) {
        return;
    }

    length = rt_strlen(text);
    while (length > 0U && (text[length - 1U] == '\n' || text[length - 1U] == '\r' || text[length - 1U] == ' ' || text[length - 1U] == '\t')) {
        text[length - 1U] = '\0';
        length -= 1U;
    }
}

static int linux_read_text_file(const char *path, char *buffer, size_t buffer_size) {
    int fd;
    long bytes;

    if (path == 0 || buffer == 0 || buffer_size == 0U) {
        return -1;
    }

    fd = platform_open_read(path);
    if (fd < 0) {
        return -1;
    }

    bytes = platform_read(fd, buffer, buffer_size - 1U);
    platform_close(fd);
    if (bytes < 0) {
        return -1;
    }

    buffer[bytes] = '\0';
    linux_trim_line(buffer);
    return 0;
}

static unsigned int linux_prefix_from_mask(const LinuxInAddr *mask) {
    unsigned int count = 0U;
    size_t i;

    if (mask == 0) {
        return 0U;
    }

    for (i = 0U; i < 4U; ++i) {
        unsigned char byte = mask->bytes[i];
        unsigned int bit;

        for (bit = 0U; bit < 8U; ++bit) {
            if ((byte & 0x80U) == 0U) {
                return count;
            }
            count += 1U;
            byte <<= 1U;
        }
    }

    return count;
}

static unsigned int linux_route_prefix_from_mask(unsigned long mask_value) {
    unsigned int count = 0U;

    while ((mask_value & 1UL) != 0UL) {
        count += 1U;
        mask_value >>= 1U;
    }
    return count;
}

static unsigned int linux_map_link_flags(unsigned int raw_flags) {
    unsigned int flags = 0U;

    if ((raw_flags & LINUX_IFF_UP) != 0U) {
        flags |= PLATFORM_NETWORK_FLAG_UP;
    }
    if ((raw_flags & LINUX_IFF_BROADCAST) != 0U) {
        flags |= PLATFORM_NETWORK_FLAG_BROADCAST;
    }
    if ((raw_flags & LINUX_IFF_LOOPBACK) != 0U) {
        flags |= PLATFORM_NETWORK_FLAG_LOOPBACK;
    }
    if ((raw_flags & LINUX_IFF_RUNNING) != 0U) {
        flags |= PLATFORM_NETWORK_FLAG_RUNNING;
    }
    if ((raw_flags & LINUX_IFF_MULTICAST) != 0U) {
        flags |= PLATFORM_NETWORK_FLAG_MULTICAST;
    }

    return flags;
}

static void linux_format_mac_address(const unsigned char *bytes, size_t byte_count, char *buffer, size_t buffer_size) {
    static const char hex[] = "0123456789abcdef";
    size_t used = 0U;
    size_t i;

    if (buffer == 0 || buffer_size == 0U) {
        return;
    }

    buffer[0] = '\0';
    for (i = 0U; i < byte_count && used + 3U < buffer_size; ++i) {
        if (i > 0U) {
            buffer[used++] = ':';
        }
        buffer[used++] = hex[(bytes[i] >> 4) & 0x0fU];
        buffer[used++] = hex[bytes[i] & 0x0fU];
    }
    buffer[used] = '\0';
}

static int linux_collect_interface_names(char names[][PLATFORM_NAME_CAPACITY], size_t capacity, size_t *count_out) {
    int fd;
    char buffer[4096];
    long bytes;
    size_t count = 0U;
    const char *cursor;
    int line_index = 0;

    if (names == 0 || count_out == 0) {
        return -1;
    }

    *count_out = 0U;
    fd = platform_open_read("/proc/net/dev");
    if (fd < 0) {
        return -1;
    }

    bytes = platform_read(fd, buffer, sizeof(buffer) - 1U);
    platform_close(fd);
    if (bytes <= 0) {
        return -1;
    }
    buffer[bytes] = '\0';

    cursor = buffer;
    while (*cursor != '\0' && count < capacity) {
        const char *line_start = cursor;
        const char *line_end = cursor;
        const char *colon;
        size_t length;
        size_t i;
        int duplicate = 0;

        while (*line_end != '\0' && *line_end != '\n') {
            line_end += 1;
        }
        cursor = (*line_end == '\n') ? (line_end + 1) : line_end;
        line_index += 1;
        if (line_index <= 2) {
            continue;
        }

        colon = line_start;
        while (colon < line_end && *colon != ':') {
            colon += 1;
        }
        if (colon >= line_end) {
            continue;
        }

        while (line_start < colon && linux_is_space_char(*line_start)) {
            line_start += 1;
        }
        while (colon > line_start && linux_is_space_char(colon[-1])) {
            colon -= 1;
        }

        length = (size_t)(colon - line_start);
        if (length == 0U || length >= PLATFORM_NAME_CAPACITY) {
            continue;
        }

        for (i = 0U; i < count; ++i) {
            if (rt_strncmp(names[i], line_start, length) == 0 && names[i][length] == '\0') {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        memcpy(names[count], line_start, length);
        names[count][length] = '\0';
        count += 1U;
    }

    *count_out = count;
    return 0;
}

static int linux_open_socket(int family, int type, int protocol) {
    long fd = linux_syscall3(LINUX_SYS_SOCKET, family, type | LINUX_SOCK_CLOEXEC, protocol);

    if (fd == -LINUX_EINVAL) {
        fd = linux_syscall3(LINUX_SYS_SOCKET, family, type, protocol);
        if (fd >= 0 && linux_mark_fd_cloexec((int)fd) != 0) {
            linux_syscall1(LINUX_SYS_CLOSE, fd);
            return -1;
        }
    }

    return fd < 0 ? -1 : (int)fd;
}

static int linux_open_inet_socket(int type, int protocol) {
    return linux_open_socket(LINUX_AF_INET, type, protocol);
}

static void linux_prepare_sockaddr(struct linux_sockaddr_in *address, const LinuxInAddr *ip, unsigned int port) {
    memset(address, 0, sizeof(*address));
    address->sin_family = LINUX_AF_INET;
    address->sin_port = linux_host_to_net16((unsigned short)port);
    if (ip != 0) {
        address->sin_addr = *ip;
    } else {
        memset(&address->sin_addr, 0, sizeof(address->sin_addr));
    }
}

static void linux_prepare_sockaddr6(struct linux_sockaddr_in6 *address, const LinuxIn6Addr *ip, unsigned int port) {
    memset(address, 0, sizeof(*address));
    address->sin6_family = LINUX_AF_INET6;
    address->sin6_port = linux_host_to_net16((unsigned short)port);
    if (ip != 0) {
        address->sin6_addr = *ip;
    } else {
        memset(&address->sin6_addr, 0, sizeof(address->sin6_addr));
    }
}

static int linux_connect_ipv4(int sock, const LinuxInAddr *ip, unsigned int port) {
    struct linux_sockaddr_in address;

    linux_prepare_sockaddr(&address, ip, port);
    return linux_syscall3(LINUX_SYS_CONNECT, sock, (long)&address, sizeof(address)) < 0 ? -1 : 0;
}

static int linux_connect_status_from_error(long result) {
    long error_code = result < 0 ? -result : result;

    switch (error_code) {
    case LINUX_ECONNREFUSED:
        return PLATFORM_CONNECT_STATUS_CLOSED;
    case LINUX_ETIMEDOUT:
    case LINUX_EAGAIN:
    case LINUX_EINPROGRESS:
    case LINUX_EACCES:
    case LINUX_EPERM:
        return PLATFORM_CONNECT_STATUS_FILTERED;
    case LINUX_EHOSTUNREACH:
    case LINUX_ENETUNREACH:
    case LINUX_EHOSTDOWN:
    case LINUX_ENETDOWN:
    case LINUX_EADDRNOTAVAIL:
        return PLATFORM_CONNECT_STATUS_UNREACHABLE;
    default:
        return PLATFORM_CONNECT_STATUS_ERROR;
    }
}

static int linux_connect_ipv4_status(int sock, const LinuxInAddr *ip, unsigned int port, int *status_out) {
    struct linux_sockaddr_in address;
    long result;

    if (status_out != 0) {
        *status_out = PLATFORM_CONNECT_STATUS_ERROR;
    }
    linux_prepare_sockaddr(&address, ip, port);
    result = linux_syscall3(LINUX_SYS_CONNECT, sock, (long)&address, sizeof(address));
    if (result < 0) {
        if (status_out != 0) {
            *status_out = linux_connect_status_from_error(result);
        }
        return -1;
    }
    if (status_out != 0) {
        *status_out = PLATFORM_CONNECT_STATUS_OPEN;
    }
    return 0;
}

static int linux_bind_ipv4(int sock, const LinuxInAddr *ip, unsigned int port) {
    struct linux_sockaddr_in address;

    linux_prepare_sockaddr(&address, ip, port);
    return linux_syscall3(LINUX_SYS_BIND, sock, (long)&address, sizeof(address)) < 0 ? -1 : 0;
}

static int linux_connect_ipv6(int sock, const LinuxIn6Addr *ip, unsigned int port) {
    struct linux_sockaddr_in6 address;

    linux_prepare_sockaddr6(&address, ip, port);
    return linux_syscall3(LINUX_SYS_CONNECT, sock, (long)&address, sizeof(address)) < 0 ? -1 : 0;
}

static long linux_sendto_ipv6(int sock, const void *buffer, size_t length, const LinuxIn6Addr *ip, unsigned int port) {
    struct linux_sockaddr_in6 address;

    linux_prepare_sockaddr6(&address, ip, port);
    return linux_syscall6(LINUX_SYS_SENDTO, sock, (long)buffer, (long)length, 0, (long)&address, sizeof(address));
}

static long linux_sendto_ipv4(int sock, const void *buffer, size_t length, const LinuxInAddr *ip, unsigned int port) {
    struct linux_sockaddr_in address;

    linux_prepare_sockaddr(&address, ip, port);
    return linux_syscall6(LINUX_SYS_SENDTO, sock, (long)buffer, (long)length, 0, (long)&address, sizeof(address));
}

static long linux_recvfrom_ipv4(int sock, void *buffer, size_t length, LinuxInAddr *peer_out) {
    struct linux_sockaddr_in address;
    unsigned int address_length = sizeof(address);
    long bytes;

    rt_memset(&address, 0, sizeof(address));
    bytes = linux_syscall6(LINUX_SYS_RECVFROM, sock, (long)buffer, (long)length, 0, (long)&address, (long)&address_length);
    if (bytes >= 0 && peer_out != 0 && address.sin_family == LINUX_AF_INET) {
        *peer_out = address.sin_addr;
    }
    return bytes;
}

static size_t linux_control_align(size_t value) {
    size_t alignment = sizeof(unsigned long);

    return (value + alignment - 1U) & ~(alignment - 1U);
}

static long linux_recvmsg_ipv4_error(int sock, void *buffer, size_t length, LinuxInAddr *peer_out, unsigned char *icmp_type_out) {
    unsigned char control[128];
    struct linux_iovec iov;
    struct linux_msghdr message;
    long bytes;
    size_t offset = 0U;

    if (icmp_type_out != 0) {
        *icmp_type_out = 0U;
    }
    if (peer_out != 0) {
        rt_memset(peer_out, 0, sizeof(*peer_out));
    }
    rt_memset(control, 0, sizeof(control));
    rt_memset(&message, 0, sizeof(message));
    iov.iov_base = buffer;
    iov.iov_len = length;
    message.msg_iov = &iov;
    message.msg_iovlen = 1U;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);

    bytes = linux_syscall3(LINUX_SYS_RECVMSG, sock, (long)&message, LINUX_MSG_ERRQUEUE);
    if (bytes < 0) {
        return bytes;
    }

    while (offset + sizeof(struct linux_cmsghdr) <= message.msg_controllen) {
        const struct linux_cmsghdr *control_header = (const struct linux_cmsghdr *)(control + offset);
        size_t data_offset;
        size_t offender_offset;

        if (control_header->cmsg_len < sizeof(struct linux_cmsghdr) || offset + control_header->cmsg_len > message.msg_controllen) {
            break;
        }
        data_offset = offset + linux_control_align(sizeof(struct linux_cmsghdr));
        if (control_header->cmsg_level == LINUX_IPPROTO_IP && control_header->cmsg_type == LINUX_IP_RECVERR &&
            data_offset + sizeof(struct linux_sock_extended_err) <= offset + control_header->cmsg_len) {
            const struct linux_sock_extended_err *error_info = (const struct linux_sock_extended_err *)(control + data_offset);

            if (error_info->ee_origin == LINUX_SO_EE_ORIGIN_ICMP && icmp_type_out != 0) {
                *icmp_type_out = error_info->ee_type;
            }
            offender_offset = data_offset + linux_control_align(sizeof(struct linux_sock_extended_err));
            if (peer_out != 0 && offender_offset + sizeof(struct linux_sockaddr_in) <= offset + control_header->cmsg_len) {
                const struct linux_sockaddr_in *offender = (const struct linux_sockaddr_in *)(control + offender_offset);

                if (offender->sin_family == LINUX_AF_INET) {
                    *peer_out = offender->sin_addr;
                }
            }
        }
        offset += linux_control_align((size_t)control_header->cmsg_len);
    }

    return bytes;
}

static long linux_recvfrom_ipv6(int sock, void *buffer, size_t length, LinuxIn6Addr *peer_out) {
    struct linux_sockaddr_in6 address;
    unsigned int address_length = sizeof(address);
    long bytes;

    rt_memset(&address, 0, sizeof(address));
    bytes = linux_syscall6(LINUX_SYS_RECVFROM, sock, (long)buffer, (long)length, 0, (long)&address, (long)&address_length);
    if (bytes >= 0 && peer_out != 0 && address.sin6_family == LINUX_AF_INET6) {
        *peer_out = address.sin6_addr;
    }
    return bytes;
}

static int linux_set_socket_int_option(int sock, int level, int option, int value) {
    return linux_syscall5(LINUX_SYS_SETSOCKOPT, sock, level, option, (long)&value, sizeof(value)) < 0 ? -1 : 0;
}

static int linux_stream_to_stdout(int sock) {
    char buffer[4096];

    for (;;) {
        long bytes = platform_read(sock, buffer, sizeof(buffer));
        if (bytes < 0) {
            return -1;
        }
        if (bytes == 0) {
            return 0;
        }
        if (rt_write_all(1, buffer, (size_t)bytes) != 0) {
            return -1;
        }
    }
}

static int linux_stream_stdin_to_socket(int sock) {
    char buffer[4096];

    for (;;) {
        long bytes = platform_read(0, buffer, sizeof(buffer));
        if (bytes < 0) {
            return -1;
        }
        if (bytes == 0) {
            return 0;
        }
        if (platform_write(sock, buffer, (size_t)bytes) != bytes) {
            return -1;
        }
    }
}

static int linux_contains_char(const char *text, char ch) {
    size_t i = 0U;

    if (text == 0) {
        return 0;
    }
    while (text[i] != '\0') {
        if (text[i] == ch) {
            return 1;
        }
        i += 1U;
    }
    return 0;
}

static int linux_hex_digit_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return (int)(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return (int)(ch - 'a' + 10);
    }
    if (ch >= 'A' && ch <= 'F') {
        return (int)(ch - 'A' + 10);
    }
    return -1;
}

static int linux_parse_ipv6_text(const char *text, LinuxIn6Addr *address_out) {
    unsigned short groups[8];
    int group_count = 0;
    int compress_index = -1;
    const char *cursor = text;
    int i;

    if (text == 0 || address_out == 0) {
        return -1;
    }

    for (i = 0; i < 8; ++i) {
        groups[i] = 0U;
    }

    if (*cursor == ':') {
        if (cursor[1] != ':') {
            return -1;
        }
        compress_index = 0;
        cursor += 2;
    }

    while (*cursor != '\0') {
        unsigned int value = 0U;
        int digits = 0;

        if (group_count >= 8) {
            return -1;
        }

        if (linux_contains_char(cursor, '.')) {
            LinuxInAddr ipv4;
            if (group_count > 6 || linux_parse_ipv4_text(cursor, &ipv4) != 0) {
                return -1;
            }
            groups[group_count++] = (unsigned short)(((unsigned int)ipv4.bytes[0] << 8) | (unsigned int)ipv4.bytes[1]);
            groups[group_count++] = (unsigned short)(((unsigned int)ipv4.bytes[2] << 8) | (unsigned int)ipv4.bytes[3]);
            cursor += rt_strlen(cursor);
            break;
        }

        while (*cursor != '\0' && *cursor != ':') {
            int digit = linux_hex_digit_value(*cursor);
            if (digit < 0) {
                return -1;
            }
            value = (value << 4) | (unsigned int)digit;
            if (value > 0xffffU) {
                return -1;
            }
            digits += 1;
            cursor += 1;
        }

        if (digits == 0) {
            return -1;
        }
        groups[group_count++] = (unsigned short)value;

        if (*cursor == '\0') {
            break;
        }
        cursor += 1;
        if (*cursor == ':') {
            if (compress_index >= 0) {
                return -1;
            }
            compress_index = group_count;
            cursor += 1;
            if (*cursor == '\0') {
                break;
            }
        }
    }

    if (compress_index >= 0) {
        int missing = 8 - group_count;
        int index;

        for (index = group_count - 1; index >= compress_index; --index) {
            groups[index + missing] = groups[index];
        }
        for (index = 0; index < missing; ++index) {
            groups[compress_index + index] = 0U;
        }
        group_count = 8;
    }

    if (group_count != 8) {
        return -1;
    }

    for (i = 0; i < 8; ++i) {
        address_out->bytes[i * 2] = (unsigned char)(groups[i] >> 8);
        address_out->bytes[i * 2 + 1] = (unsigned char)(groups[i] & 0xffU);
    }
    return 0;
}

static void linux_ipv6_to_text(const LinuxIn6Addr *address, char *buffer, size_t buffer_size) {
    static const char digits[] = "0123456789abcdef";
    size_t used = 0U;
    int i;

    if (buffer == 0 || buffer_size == 0U) {
        return;
    }
    buffer[0] = '\0';
    if (address == 0) {
        return;
    }

    for (i = 0; i < 8; ++i) {
        unsigned int value = ((unsigned int)address->bytes[i * 2] << 8) | (unsigned int)address->bytes[i * 2 + 1];
        unsigned int shift = 12U;
        int started = 0;

        if (i > 0 && used + 1U < buffer_size) {
            buffer[used++] = ':';
            buffer[used] = '\0';
        }

        while (shift <= 12U) {
            unsigned int nibble = (value >> shift) & 0x0fU;
            if (nibble != 0U || started || shift == 0U) {
                if (used + 1U < buffer_size) {
                    buffer[used++] = digits[nibble];
                    buffer[used] = '\0';
                }
                started = 1;
            }
            if (shift == 0U) {
                break;
            }
            shift -= 4U;
        }
    }
}

static int linux_add_dns_entry(
    PlatformDnsEntry *entries_out,
    size_t entry_capacity,
    size_t *count_io,
    const char *name,
    int family,
    unsigned short record_type,
    const char *data,
    unsigned int ttl,
    unsigned short preference
) {
    PlatformDnsEntry *entry;
    size_t i;

    if (entries_out == 0 || count_io == 0 || *count_io >= entry_capacity) {
        return -1;
    }
    for (i = 0U; i < *count_io; ++i) {
        if (entries_out[i].family == family &&
            entries_out[i].record_type == record_type &&
            entries_out[i].preference == preference &&
            rt_strcmp(entries_out[i].name, name) == 0 &&
            rt_strcmp(entries_out[i].data, data) == 0) {
            return 0;
        }
    }

    entry = &entries_out[*count_io];
    rt_memset(entry, 0, sizeof(*entry));
    entry->family = family;
    entry->record_type = record_type;
    entry->preference = preference;
    entry->ttl = ttl;
    rt_copy_string(entry->name, sizeof(entry->name), name);
    rt_copy_string(entry->address, sizeof(entry->address), data);
    rt_copy_string(entry->data, sizeof(entry->data), data);
    *count_io += 1U;
    return 0;
}

static int linux_lookup_hosts_file(
    const char *host,
    int family_filter,
    PlatformDnsEntry *entries_out,
    size_t entry_capacity,
    size_t *count_io
) {
    char buffer[4096];
    const char *cursor;

    if (host == 0 || entries_out == 0 || count_io == 0) {
        return -1;
    }
    if (linux_read_text_file("/etc/hosts", buffer, sizeof(buffer)) != 0) {
        return -1;
    }

    cursor = buffer;
    while (*cursor != '\0') {
        char ip_text[PLATFORM_NETWORK_TEXT_CAPACITY];
        char name_text[PLATFORM_NAME_CAPACITY];
        const char *line_end = cursor;
        const char *line_cursor = cursor;

        while (*line_end != '\0' && *line_end != '\n') {
            line_end += 1;
        }
        if (*line_cursor == '#') {
            cursor = (*line_end == '\n') ? (line_end + 1) : line_end;
            continue;
        }
        if (linux_copy_token(&line_cursor, ip_text, sizeof(ip_text)) == 0) {
            while (linux_copy_token(&line_cursor, name_text, sizeof(name_text)) == 0) {
                if (rt_strcmp(name_text, host) == 0) {
                    LinuxInAddr ipv4;
                    LinuxIn6Addr ipv6;

                    if ((family_filter == PLATFORM_NETWORK_FAMILY_ANY || family_filter == PLATFORM_NETWORK_FAMILY_IPV4) &&
                        linux_parse_ipv4_text(ip_text, &ipv4) == 0) {
                        (void)linux_add_dns_entry(
                            entries_out,
                            entry_capacity,
                            count_io,
                            host,
                            PLATFORM_NETWORK_FAMILY_IPV4,
                            PLATFORM_DNS_RECORD_A,
                            ip_text,
                            0U,
                            0U
                        );
                    }
                    if ((family_filter == PLATFORM_NETWORK_FAMILY_ANY || family_filter == PLATFORM_NETWORK_FAMILY_IPV6) &&
                        linux_parse_ipv6_text(ip_text, &ipv6) == 0) {
                        (void)linux_add_dns_entry(
                            entries_out,
                            entry_capacity,
                            count_io,
                            host,
                            PLATFORM_NETWORK_FAMILY_IPV6,
                            PLATFORM_DNS_RECORD_AAAA,
                            ip_text,
                            0U,
                            0U
                        );
                    }
                }
            }
        }
        cursor = (*line_end == '\n') ? (line_end + 1) : line_end;
    }

    return 0;
}

static int linux_find_default_nameserver(LinuxInAddr *address_out) {
    char buffer[2048];
    const char *cursor;

    if (address_out == 0) {
        return -1;
    }
    if (linux_read_text_file("/etc/resolv.conf", buffer, sizeof(buffer)) == 0) {
        cursor = buffer;
        while (*cursor != '\0') {
            char keyword[32];
            char value[PLATFORM_NETWORK_TEXT_CAPACITY];
            const char *line_end = cursor;
            const char *line_cursor = cursor;

            while (*line_end != '\0' && *line_end != '\n') {
                line_end += 1;
            }
            if (*line_cursor != '#' && linux_copy_token(&line_cursor, keyword, sizeof(keyword)) == 0 &&
                rt_strcmp(keyword, "nameserver") == 0 &&
                linux_copy_token(&line_cursor, value, sizeof(value)) == 0 &&
                linux_parse_ipv4_text(value, address_out) == 0) {
                return 0;
            }
            cursor = (*line_end == '\n') ? (line_end + 1) : line_end;
        }
    }

    return linux_parse_ipv4_text("10.0.2.3", address_out);
}

static int linux_dns_encode_name(const char *name, unsigned char *buffer, size_t buffer_size, size_t *offset_io) {
    const char *label = name;

    if (name == 0 || buffer == 0 || offset_io == 0) {
        return -1;
    }

    while (*label != '\0') {
        const char *end = label;
        size_t length;

        while (*end != '\0' && *end != '.') {
            end += 1;
        }
        length = (size_t)(end - label);
        if (length == 0U || length > 63U || *offset_io + length + 2U > buffer_size) {
            return -1;
        }
        buffer[(*offset_io)++] = (unsigned char)length;
        memcpy(buffer + *offset_io, label, length);
        *offset_io += length;
        label = (*end == '.') ? (end + 1) : end;
    }

    if (*offset_io + 1U > buffer_size) {
        return -1;
    }
    buffer[(*offset_io)++] = 0U;
    return 0;
}

static int linux_dns_skip_name(const unsigned char *message, size_t message_length, size_t *offset_io) {
    size_t offset = *offset_io;

    if (message == 0 || offset_io == 0) {
        return -1;
    }

    while (offset < message_length) {
        unsigned char length = message[offset];
        if (length == 0U) {
            offset += 1U;
            *offset_io = offset;
            return 0;
        }
        if ((length & 0xc0U) == 0xc0U) {
            if (offset + 1U >= message_length) {
                return -1;
            }
            *offset_io = offset + 2U;
            return 0;
        }
        offset += 1U + (size_t)length;
    }

    return -1;
}

static int linux_dns_read_name(
    const unsigned char *message,
    size_t message_length,
    size_t start_offset,
    size_t *next_offset_out,
    char *buffer,
    size_t buffer_size
) {
    size_t offset = start_offset;
    size_t next_offset = start_offset;
    size_t used = 0U;
    unsigned int jumps = 0U;
    int jumped = 0;

    if (message == 0 || buffer == 0 || buffer_size == 0U) {
        return -1;
    }

    while (offset < message_length) {
        unsigned char length = message[offset];
        if (length == 0U) {
            if (!jumped) {
                next_offset = offset + 1U;
            }
            if (used == 0U) {
                if (buffer_size < 2U) {
                    return -1;
                }
                buffer[0] = '.';
                buffer[1] = '\0';
            } else {
                buffer[used] = '\0';
            }
            if (next_offset_out != 0) {
                *next_offset_out = next_offset;
            }
            return 0;
        }
        if ((length & 0xc0U) == 0xc0U) {
            unsigned short pointer;
            if (offset + 1U >= message_length) {
                return -1;
            }
            pointer = (unsigned short)((((unsigned short)length & 0x3fU) << 8) | (unsigned short)message[offset + 1U]);
            if (!jumped) {
                next_offset = offset + 2U;
                jumped = 1;
            }
            jumps += 1U;
            if (jumps > 16U || (size_t)pointer >= message_length) {
                return -1;
            }
            offset = (size_t)pointer;
            continue;
        }
        offset += 1U;
        if (offset + (size_t)length > message_length) {
            return -1;
        }
        if (used != 0U) {
            if (used + 1U >= buffer_size) {
                return -1;
            }
            buffer[used++] = '.';
        }
        if (used + (size_t)length >= buffer_size) {
            return -1;
        }
        memcpy(buffer + used, message + offset, (size_t)length);
        used += (size_t)length;
        offset += (size_t)length;
        if (!jumped) {
            next_offset = offset;
        }
    }

    return -1;
}

static int linux_dns_format_txt(const unsigned char *data, size_t length, char *buffer, size_t buffer_size) {
    size_t offset = 0U;
    size_t used = 0U;

    if (buffer == 0 || buffer_size == 0U) {
        return -1;
    }
    buffer[0] = '\0';
    while (offset < length) {
        unsigned char part_length = data[offset++];
        size_t copy_length;
        if (offset + (size_t)part_length > length) {
            return -1;
        }
        if (used != 0U) {
            if (used + 1U >= buffer_size) {
                return -1;
            }
            buffer[used++] = ' ';
        }
        copy_length = (size_t)part_length;
        if (used + copy_length >= buffer_size) {
            copy_length = buffer_size - used - 1U;
        }
        if (copy_length > 0U) {
            memcpy(buffer + used, data + offset, copy_length);
            used += copy_length;
        }
        offset += (size_t)part_length;
    }
    buffer[used] = '\0';
    return 0;
}

static int linux_dns_query(
    const char *server,
    unsigned int port,
    const char *name,
    unsigned short query_type,
    PlatformDnsEntry *entries_out,
    size_t entry_capacity,
    size_t *count_io
) {
    LinuxInAddr server_address;
    unsigned char packet[512];
    unsigned char reply[512];
    size_t used = 0U;
    unsigned short query_id;
    int sock;
    long reply_bytes;
    size_t offset;
    unsigned int answer_count;
    unsigned int i;

    if (name == 0 || entries_out == 0 || count_io == 0) {
        return -1;
    }
    if ((server != 0 && server[0] != '\0' && linux_parse_ipv4_text(server, &server_address) != 0 && rt_strcmp(server, "localhost") != 0) ||
        (server != 0 && rt_strcmp(server, "localhost") == 0 && linux_parse_ipv4_text("127.0.0.1", &server_address) != 0)) {
        return -1;
    }
    if ((server == 0 || server[0] == '\0') && linux_find_default_nameserver(&server_address) != 0) {
        return -1;
    }

    sock = linux_open_inet_socket(LINUX_SOCK_DGRAM, LINUX_IPPROTO_UDP);
    if (sock < 0 || linux_connect_ipv4(sock, &server_address, port == 0U ? 53U : port) != 0) {
        if (sock >= 0) {
            platform_close(sock);
        }
        return -1;
    }

    query_id = (unsigned short)(((unsigned int)platform_get_process_id() & 0xffffU) ^ 0x5a5aU);
    rt_memset(packet, 0, sizeof(packet));
    packet[0] = (unsigned char)(query_id >> 8);
    packet[1] = (unsigned char)(query_id & 0xffU);
    packet[2] = 0x01U;
    packet[3] = 0x00U;
    packet[4] = 0x00U;
    packet[5] = 0x01U;
    used = 12U;
    if (linux_dns_encode_name(name, packet, sizeof(packet), &used) != 0 || used + 4U > sizeof(packet)) {
        platform_close(sock);
        return -1;
    }
    packet[used++] = (unsigned char)(query_type >> 8);
    packet[used++] = (unsigned char)(query_type & 0xffU);
    packet[used++] = 0x00U;
    packet[used++] = 0x01U;

    if (platform_write(sock, packet, used) < (long)used) {
        platform_close(sock);
        return -1;
    }
    {
        int fds[1];
        size_t ready_index = 0U;
        fds[0] = sock;
        if (platform_poll_fds(fds, 1U, &ready_index, 2000) <= 0) {
            platform_close(sock);
            return -1;
        }
    }
    reply_bytes = platform_read(sock, reply, sizeof(reply));
    platform_close(sock);
    if (reply_bytes < 12) {
        return -1;
    }
    if (((unsigned short)reply[0] << 8 | (unsigned short)reply[1]) != query_id) {
        return -1;
    }

    if ((reply[3] & 0x0fU) != 0U) {
        return -1;
    }
    answer_count = ((unsigned int)reply[6] << 8) | (unsigned int)reply[7];
    offset = 12U;
    {
        unsigned int question_count = ((unsigned int)reply[4] << 8) | (unsigned int)reply[5];
        for (i = 0U; i < question_count; ++i) {
            if (linux_dns_skip_name(reply, (size_t)reply_bytes, &offset) != 0 || offset + 4U > (size_t)reply_bytes) {
                return -1;
            }
            offset += 4U;
        }
    }

    for (i = 0U; i < answer_count; ++i) {
        char owner_name[PLATFORM_NAME_CAPACITY];
        unsigned short type;
        unsigned short class_code;
        unsigned int ttl;
        unsigned short rdlength;
        int family = PLATFORM_NETWORK_FAMILY_ANY;
        char data_text[PLATFORM_NAME_CAPACITY];
        unsigned short preference = 0U;
        int keep_record = 0;

        if (linux_dns_read_name(reply, (size_t)reply_bytes, offset, &offset, owner_name, sizeof(owner_name)) != 0 ||
            offset + 10U > (size_t)reply_bytes) {
            break;
        }
        type = (unsigned short)(((unsigned int)reply[offset] << 8) | (unsigned int)reply[offset + 1U]);
        class_code = (unsigned short)(((unsigned int)reply[offset + 2U] << 8) | (unsigned int)reply[offset + 3U]);
        ttl = ((unsigned int)reply[offset + 4U] << 24) |
              ((unsigned int)reply[offset + 5U] << 16) |
              ((unsigned int)reply[offset + 6U] << 8) |
              (unsigned int)reply[offset + 7U];
        rdlength = (unsigned short)(((unsigned int)reply[offset + 8U] << 8) | (unsigned int)reply[offset + 9U]);
        offset += 10U;
        if (offset + rdlength > (size_t)reply_bytes) {
            break;
        }

        data_text[0] = '\0';
        if (class_code == 1U && type == PLATFORM_DNS_RECORD_A && rdlength == 4U) {
            char address_text[PLATFORM_NETWORK_TEXT_CAPACITY];
            LinuxInAddr address;
            address.bytes[0] = reply[offset];
            address.bytes[1] = reply[offset + 1U];
            address.bytes[2] = reply[offset + 2U];
            address.bytes[3] = reply[offset + 3U];
            linux_ipv4_to_text(&address, address_text, sizeof(address_text));
            family = PLATFORM_NETWORK_FAMILY_IPV4;
            rt_copy_string(data_text, sizeof(data_text), address_text);
            keep_record = 1;
        } else if (class_code == 1U && type == PLATFORM_DNS_RECORD_AAAA && rdlength == 16U) {
            char address_text[PLATFORM_NETWORK_TEXT_CAPACITY];
            LinuxIn6Addr address6;
            memcpy(address6.bytes, reply + offset, 16U);
            linux_ipv6_to_text(&address6, address_text, sizeof(address_text));
            family = PLATFORM_NETWORK_FAMILY_IPV6;
            rt_copy_string(data_text, sizeof(data_text), address_text);
            keep_record = 1;
        } else if (class_code == 1U && (type == PLATFORM_DNS_RECORD_NS || type == PLATFORM_DNS_RECORD_CNAME || type == PLATFORM_DNS_RECORD_PTR)) {
            if (linux_dns_read_name(reply, (size_t)reply_bytes, offset, 0, data_text, sizeof(data_text)) == 0) {
                keep_record = 1;
            }
        } else if (class_code == 1U && type == PLATFORM_DNS_RECORD_MX && rdlength >= 3U) {
            preference = (unsigned short)(((unsigned int)reply[offset] << 8) | (unsigned int)reply[offset + 1U]);
            if (linux_dns_read_name(reply, (size_t)reply_bytes, offset + 2U, 0, data_text, sizeof(data_text)) == 0) {
                keep_record = 1;
            }
        } else if (class_code == 1U && type == PLATFORM_DNS_RECORD_TXT) {
            if (linux_dns_format_txt(reply + offset, rdlength, data_text, sizeof(data_text)) == 0) {
                keep_record = 1;
            }
        }
        if (keep_record &&
            (type == query_type ||
             type == PLATFORM_DNS_RECORD_CNAME ||
             (query_type == PLATFORM_DNS_RECORD_A && type == PLATFORM_DNS_RECORD_A) ||
             (query_type == PLATFORM_DNS_RECORD_AAAA && type == PLATFORM_DNS_RECORD_AAAA))) {
            (void)linux_add_dns_entry(
                entries_out,
                entry_capacity,
                count_io,
                owner_name[0] == '\0' ? name : owner_name,
                family,
                type,
                data_text,
                ttl,
                preference
            );
        }
        offset += rdlength;
    }

    return *count_io > 0U ? 0 : -1;
}

static int linux_resolve_ipv4_host(const char *host, LinuxInAddr *address_out) {
    PlatformDnsEntry entries[4];
    size_t count = 0U;
    size_t i;

    if (host == 0 || address_out == 0) {
        return -1;
    }
    if (linux_parse_ipv4_text(host, address_out) == 0) {
        return 0;
    }
    if (rt_strcmp(host, "localhost") == 0) {
        address_out->bytes[0] = 127U;
        address_out->bytes[1] = 0U;
        address_out->bytes[2] = 0U;
        address_out->bytes[3] = 1U;
        return 0;
    }
    (void)linux_lookup_hosts_file(host, PLATFORM_NETWORK_FAMILY_IPV4, entries, sizeof(entries) / sizeof(entries[0]), &count);
    for (i = 0U; i < count; ++i) {
        if (entries[i].record_type == PLATFORM_DNS_RECORD_A && linux_parse_ipv4_text(entries[i].address, address_out) == 0) {
            return 0;
        }
    }
    count = 0U;
    if (linux_dns_query(0, 53U, host, 1U, entries, sizeof(entries) / sizeof(entries[0]), &count) == 0 && count > 0U) {
        for (i = 0U; i < count; ++i) {
            if (entries[i].record_type == PLATFORM_DNS_RECORD_A && linux_parse_ipv4_text(entries[i].address, address_out) == 0) {
                return 0;
            }
        }
    }
    return -1;
}

static int linux_resolve_ipv6_host(const char *host, LinuxIn6Addr *address_out) {
    PlatformDnsEntry entries[4];
    size_t count = 0U;
    size_t i;

    if (host == 0 || address_out == 0) {
        return -1;
    }
    if (linux_parse_ipv6_text(host, address_out) == 0) {
        return 0;
    }
    if (rt_strcmp(host, "localhost") == 0) {
        rt_memset(address_out, 0, sizeof(*address_out));
        address_out->bytes[15] = 1U;
        return 0;
    }
    (void)linux_lookup_hosts_file(host, PLATFORM_NETWORK_FAMILY_IPV6, entries, sizeof(entries) / sizeof(entries[0]), &count);
    for (i = 0U; i < count; ++i) {
        if (entries[i].record_type == PLATFORM_DNS_RECORD_AAAA && linux_parse_ipv6_text(entries[i].address, address_out) == 0) {
            return 0;
        }
    }
    count = 0U;
    if (linux_dns_query(0, 53U, host, 28U, entries, sizeof(entries) / sizeof(entries[0]), &count) == 0 && count > 0U) {
        for (i = 0U; i < count; ++i) {
            if (entries[i].record_type == PLATFORM_DNS_RECORD_AAAA && linux_parse_ipv6_text(entries[i].address, address_out) == 0) {
                return 0;
            }
        }
    }
    return -1;
}

static unsigned long long linux_monotonic_milliseconds(void) {
    struct linux_timespec time_value;

    if (linux_syscall2(LINUX_SYS_CLOCK_GETTIME, LINUX_CLOCK_MONOTONIC, (long)&time_value) >= 0) {
        return (unsigned long long)time_value.tv_sec * 1000ULL + (unsigned long long)(time_value.tv_nsec / 1000000L);
    }
    return (unsigned long long)platform_get_epoch_time() * 1000ULL;
}

static unsigned short linux_compute_icmp_checksum(const void *data, size_t length) {
    const unsigned short *words = (const unsigned short *)data;
    unsigned int sum = 0U;
    size_t remaining = length;

    while (remaining > 1U) {
        sum += (unsigned int)(*words++);
        remaining -= 2U;
    }
    if (remaining == 1U) {
        sum += (unsigned int)(*(const unsigned char *)words);
    }
    while ((sum >> 16) != 0U) {
        sum = (sum & 0xffffU) + (sum >> 16);
    }
    return (unsigned short)~sum;
}

static void linux_write_milliseconds(unsigned long long elapsed_ms, unsigned long long sub_ms) {
    rt_write_uint(1, elapsed_ms);
    rt_write_char(1, '.');
    if (sub_ms < 100ULL) {
        rt_write_char(1, '0');
    }
    if (sub_ms < 10ULL) {
        rt_write_char(1, '0');
    }
    rt_write_uint(1, sub_ms);
}

int platform_connect_tcp(const char *host, unsigned int port, int *socket_fd_out) {
    LinuxInAddr address;
    int sock;

    if (host == 0 || socket_fd_out == 0 || port == 0U || port > 65535U) {
        return -1;
    }
    if (linux_resolve_ipv4_host(host, &address) != 0) {
        return -1;
    }

    sock = linux_open_inet_socket(LINUX_SOCK_STREAM, LINUX_IPPROTO_TCP);
    if (sock < 0) {
        return -1;
    }
    if (linux_connect_ipv4(sock, &address, port) != 0) {
        platform_close(sock);
        return -1;
    }

    *socket_fd_out = sock;
    return 0;
}

int platform_open_tcp_listener(const char *host, unsigned int port, int *socket_fd_out) {
    LinuxInAddr address;
    LinuxInAddr *bind_address = 0;
    int sock;

    if (socket_fd_out == 0 || port == 0U || port > 65535U) {
        return -1;
    }

    if (host != 0 && host[0] != '\0' && rt_strcmp(host, "0.0.0.0") != 0 && rt_strcmp(host, "*") != 0) {
        if (linux_resolve_ipv4_host(host, &address) != 0) {
            return -1;
        }
        bind_address = &address;
    }

    sock = linux_open_inet_socket(LINUX_SOCK_STREAM, LINUX_IPPROTO_TCP);
    if (sock < 0) {
        return -1;
    }
    (void)linux_set_socket_int_option(sock, LINUX_SOL_SOCKET, LINUX_SO_REUSEADDR, 1);
    if (linux_bind_ipv4(sock, bind_address, port) != 0 || linux_syscall2(LINUX_SYS_LISTEN, sock, 16) < 0) {
        platform_close(sock);
        return -1;
    }

    *socket_fd_out = sock;
    return 0;
}

int platform_accept_tcp(int listener_fd, int *client_fd_out) {
    long accepted;

    if (listener_fd < 0 || client_fd_out == 0) {
        return -1;
    }

    accepted = linux_syscall4(LINUX_SYS_ACCEPT4, listener_fd, 0, 0, LINUX_SOCK_CLOEXEC);
    if (accepted == -LINUX_EINVAL || accepted == -LINUX_ENOSYS) {
        accepted = linux_syscall3(LINUX_SYS_ACCEPT, listener_fd, 0, 0);
        if (accepted >= 0 && linux_mark_fd_cloexec((int)accepted) != 0) {
            linux_syscall1(LINUX_SYS_CLOSE, accepted);
            return -1;
        }
    }
    if (accepted < 0) {
        return -1;
    }

    *client_fd_out = (int)accepted;
    return 0;
}

int platform_poll_fds(const int *fds, size_t fd_count, size_t *ready_index_out, int timeout_milliseconds) {
    struct linux_pollfd poll_fds[16];
    struct linux_timespec timeout;
    long result;
    size_t i;

    if (fds == 0 || fd_count == 0U || fd_count > sizeof(poll_fds) / sizeof(poll_fds[0]) || ready_index_out == 0) {
        return -1;
    }

    for (i = 0U; i < fd_count; ++i) {
        poll_fds[i].fd = fds[i];
        poll_fds[i].events = (short)(LINUX_POLLIN | LINUX_POLLERR | LINUX_POLLHUP);
        poll_fds[i].revents = 0;
    }

    if (timeout_milliseconds >= 0) {
        timeout.tv_sec = timeout_milliseconds / 1000;
        timeout.tv_nsec = (long)(timeout_milliseconds % 1000) * 1000000L;
        result = linux_syscall5(LINUX_SYS_PPOLL, (long)poll_fds, fd_count, (long)&timeout, 0, 0);
    } else {
        result = linux_syscall5(LINUX_SYS_PPOLL, (long)poll_fds, fd_count, 0, 0, 0);
    }

    if (result <= 0) {
        return (int)result;
    }

    for (i = 0U; i < fd_count; ++i) {
        if ((poll_fds[i].revents & (LINUX_POLLIN | LINUX_POLLERR | LINUX_POLLHUP)) != 0) {
            *ready_index_out = i;
            return 1;
        }
    }

    return 0;
}

int platform_netcat(const char *host, unsigned int port, const PlatformNetcatOptions *options) {
    PlatformNetcatOptions defaults;
    LinuxInAddr address;
    LinuxInAddr bind_address;
    const LinuxInAddr *bind_ptr = 0;
    int sock = -1;
    int connect_status = PLATFORM_CONNECT_STATUS_ERROR;

    if (options == 0) {
        rt_memset(&defaults, 0, sizeof(defaults));
        options = &defaults;
    }
    if (options->connect_status_out != 0) {
        *options->connect_status_out = PLATFORM_CONNECT_STATUS_ERROR;
    }

    if (options->use_udp || options->family == PLATFORM_NETWORK_FAMILY_IPV6) {
        return -1;
    }
    if (options->bind_host[0] != '\0') {
        if ((options->numeric_only && linux_parse_ipv4_text(options->bind_host, &bind_address) != 0) ||
            (!options->numeric_only && linux_resolve_ipv4_host(options->bind_host, &bind_address) != 0)) {
            return -1;
        }
        bind_ptr = &bind_address;
    }

    if (options->listen_mode) {
        long accepted;
        int reuse = 1;
        unsigned int listen_port = options->bind_port != 0U ? options->bind_port : port;
        size_t ready_index = 0U;

        sock = linux_open_inet_socket(LINUX_SOCK_STREAM, LINUX_IPPROTO_TCP);
        if (sock < 0) {
            return -1;
        }
        (void)linux_set_socket_int_option(sock, LINUX_SOL_SOCKET, LINUX_SO_REUSEADDR, reuse);
        if (linux_bind_ipv4(sock, bind_ptr, listen_port) != 0 || linux_syscall2(LINUX_SYS_LISTEN, sock, 1) < 0) {
            platform_close(sock);
            return -1;
        }

        if (options->timeout_milliseconds > 0U &&
            platform_poll_fds(&sock, 1U, &ready_index, (int)options->timeout_milliseconds) <= 0) {
            platform_close(sock);
            return -1;
        }

        accepted = linux_syscall4(LINUX_SYS_ACCEPT4, sock, 0, 0, LINUX_SOCK_CLOEXEC);
        if (accepted == -LINUX_EINVAL || accepted == -LINUX_ENOSYS) {
            accepted = linux_syscall3(LINUX_SYS_ACCEPT, sock, 0, 0);
            if (accepted >= 0 && linux_mark_fd_cloexec((int)accepted) != 0) {
                linux_syscall1(LINUX_SYS_CLOSE, accepted);
                accepted = -1;
            }
        }
        platform_close(sock);
        if (accepted < 0) {
            return -1;
        }
        sock = (int)accepted;
        if (options->scan_mode) {
            platform_close(sock);
            return 0;
        }
        if (linux_stream_to_stdout(sock) != 0) {
            platform_close(sock);
            return -1;
        }
        platform_close(sock);
        return 0;
    }

    if (host == 0) {
        return -1;
    }
    if ((options->numeric_only && linux_parse_ipv4_text(host, &address) != 0) ||
        (!options->numeric_only && linux_resolve_ipv4_host(host, &address) != 0)) {
        if (options->connect_status_out != 0) {
            *options->connect_status_out = PLATFORM_CONNECT_STATUS_UNREACHABLE;
        }
        return -1;
    }
    sock = linux_open_inet_socket(LINUX_SOCK_STREAM, LINUX_IPPROTO_TCP);
    if (sock < 0) {
        return -1;
    }
    if ((bind_ptr != 0 || options->bind_port != 0U) && linux_bind_ipv4(sock, bind_ptr, options->bind_port) != 0) {
        platform_close(sock);
        return -1;
    }
    if (linux_connect_ipv4_status(sock, &address, port, &connect_status) != 0) {
        if (options->connect_status_out != 0) {
            *options->connect_status_out = connect_status;
        }
        platform_close(sock);
        return -1;
    }
    if (options->connect_status_out != 0) {
        *options->connect_status_out = PLATFORM_CONNECT_STATUS_OPEN;
    }
    if (options->scan_mode) {
        if (options->banner_received_length != 0) {
            *options->banner_received_length = 0U;
        }
        if (options->banner_buffer != 0 && options->banner_capacity > 0U) {
            unsigned int read_timeout = options->banner_read_timeout_milliseconds;
            size_t ready_index = 0U;

            if (read_timeout == 0U) {
                read_timeout = 500U;
            }
            if (platform_poll_fds(&sock, 1U, &ready_index, (int)read_timeout) > 0) {
                long received = platform_read(sock, (char *)options->banner_buffer, (size_t)options->banner_capacity);
                if (received > 0 && options->banner_received_length != 0) {
                    *options->banner_received_length = (unsigned int)received;
                }
            }
        }
        platform_close(sock);
        return 0;
    }

    if (!platform_isatty(0)) {
        if (linux_stream_stdin_to_socket(sock) != 0) {
            platform_close(sock);
            return -1;
        }
        (void)linux_syscall2(LINUX_SYS_SHUTDOWN, sock, LINUX_SHUT_WR);
    }

    if (linux_stream_to_stdout(sock) != 0) {
        platform_close(sock);
        return -1;
    }

    platform_close(sock);
    return 0;
}

int platform_netcat_tcp(const char *host, unsigned int port, int listen_mode) {
    PlatformNetcatOptions options;

    rt_memset(&options, 0, sizeof(options));
    options.listen_mode = listen_mode;
    return platform_netcat(host, port, &options);
}

int platform_list_network_links(PlatformNetworkLink *entries_out, size_t entry_capacity, size_t *count_out) {
    char names[32][PLATFORM_NAME_CAPACITY];
    size_t name_count = 0U;
    int sock;
    size_t i;

    if (entries_out == 0 || count_out == 0) {
        return -1;
    }
    *count_out = 0U;

    if (linux_collect_interface_names(names, sizeof(names) / sizeof(names[0]), &name_count) != 0) {
        return -1;
    }

    sock = linux_open_inet_socket(LINUX_SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }

    for (i = 0U; i < name_count && i < entry_capacity; ++i) {
        struct linux_ifreq ifr;
        PlatformNetworkLink *entry = &entries_out[i];

        rt_memset(entry, 0, sizeof(*entry));
        rt_copy_string(entry->name, sizeof(entry->name), names[i]);
        entry->mtu = 1500U;

        rt_memset(&ifr, 0, sizeof(ifr));
        rt_copy_string(ifr.ifr_name, sizeof(ifr.ifr_name), names[i]);
        if (linux_syscall3(LINUX_SYS_IOCTL, sock, LINUX_SIOCGIFINDEX, (long)&ifr) >= 0) {
            entry->index = (unsigned int)ifr.data.ifindex;
        }
        rt_memset(&ifr, 0, sizeof(ifr));
        rt_copy_string(ifr.ifr_name, sizeof(ifr.ifr_name), names[i]);
        if (linux_syscall3(LINUX_SYS_IOCTL, sock, LINUX_SIOCGIFFLAGS, (long)&ifr) >= 0) {
            entry->flags = linux_map_link_flags((unsigned int)ifr.data.flags);
        }
        rt_memset(&ifr, 0, sizeof(ifr));
        rt_copy_string(ifr.ifr_name, sizeof(ifr.ifr_name), names[i]);
        if (linux_syscall3(LINUX_SYS_IOCTL, sock, LINUX_SIOCGIFMTU, (long)&ifr) >= 0 && ifr.data.mtu > 0) {
            entry->mtu = (unsigned int)ifr.data.mtu;
        }
        rt_memset(&ifr, 0, sizeof(ifr));
        rt_copy_string(ifr.ifr_name, sizeof(ifr.ifr_name), names[i]);
        if (linux_syscall3(LINUX_SYS_IOCTL, sock, LINUX_SIOCGIFHWADDR, (long)&ifr) >= 0) {
            linux_format_mac_address(((const unsigned char *)&ifr.data.hwaddr) + 2, 6U, entry->mac, sizeof(entry->mac));
            entry->has_mac = entry->mac[0] != '\0';
        }

        *count_out = i + 1U;
    }

    platform_close(sock);
    return 0;
}

int platform_list_network_addresses(
    PlatformNetworkAddress *entries_out,
    size_t entry_capacity,
    size_t *count_out,
    int family_filter,
    const char *ifname_filter
) {
    char names[32][PLATFORM_NAME_CAPACITY];
    size_t name_count = 0U;
    size_t count = 0U;
    int sock;
    size_t i;

    if (entries_out == 0 || count_out == 0) {
        return -1;
    }
    *count_out = 0U;

    if (family_filter == PLATFORM_NETWORK_FAMILY_IPV6) {
        return 0;
    }
    if (linux_collect_interface_names(names, sizeof(names) / sizeof(names[0]), &name_count) != 0) {
        return -1;
    }

    sock = linux_open_inet_socket(LINUX_SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }

    for (i = 0U; i < name_count && count < entry_capacity; ++i) {
        struct linux_ifreq ifr;
        struct linux_ifreq mask_ifr;
        struct linux_ifreq broad_ifr;
        struct linux_sockaddr_in *addr_in;
        struct linux_sockaddr_in *mask_in;
        struct linux_sockaddr_in *broad_in;
        PlatformNetworkAddress *entry;

        if (ifname_filter != 0 && rt_strcmp(ifname_filter, names[i]) != 0) {
            continue;
        }

        rt_memset(&ifr, 0, sizeof(ifr));
        rt_copy_string(ifr.ifr_name, sizeof(ifr.ifr_name), names[i]);
        if (linux_syscall3(LINUX_SYS_IOCTL, sock, LINUX_SIOCGIFADDR, (long)&ifr) < 0) {
            continue;
        }

        entry = &entries_out[count];
        rt_memset(entry, 0, sizeof(*entry));
        rt_copy_string(entry->ifname, sizeof(entry->ifname), names[i]);
        entry->family = PLATFORM_NETWORK_FAMILY_IPV4;

        addr_in = (struct linux_sockaddr_in *)&ifr.data.addr;
        linux_ipv4_to_text(&addr_in->sin_addr, entry->address, sizeof(entry->address));
        rt_copy_string(entry->scope, sizeof(entry->scope), rt_strcmp(names[i], "lo") == 0 ? "host" : "global");

        rt_memset(&mask_ifr, 0, sizeof(mask_ifr));
        rt_copy_string(mask_ifr.ifr_name, sizeof(mask_ifr.ifr_name), names[i]);
        if (linux_syscall3(LINUX_SYS_IOCTL, sock, LINUX_SIOCGIFNETMASK, (long)&mask_ifr) >= 0) {
            mask_in = (struct linux_sockaddr_in *)&mask_ifr.data.netmask;
            entry->prefix_length = linux_prefix_from_mask(&mask_in->sin_addr);
        }

        rt_memset(&broad_ifr, 0, sizeof(broad_ifr));
        rt_copy_string(broad_ifr.ifr_name, sizeof(broad_ifr.ifr_name), names[i]);
        if (linux_syscall3(LINUX_SYS_IOCTL, sock, LINUX_SIOCGIFBRDADDR, (long)&broad_ifr) >= 0) {
            broad_in = (struct linux_sockaddr_in *)&broad_ifr.data.broadaddr;
            linux_ipv4_to_text(&broad_in->sin_addr, entry->broadcast, sizeof(entry->broadcast));
            entry->has_broadcast = entry->broadcast[0] != '\0';
        }

        count += 1U;
    }

    platform_close(sock);
    *count_out = count;
    return 0;
}

int platform_list_network_routes(
    PlatformRouteEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out,
    int family_filter,
    const char *ifname_filter
) {
    int fd;
    char buffer[4096];
    long bytes;
    const char *cursor;
    int line_index = 0;
    size_t count = 0U;

    if (entries_out == 0 || count_out == 0) {
        return -1;
    }
    *count_out = 0U;

    if (family_filter == PLATFORM_NETWORK_FAMILY_IPV6) {
        return 0;
    }

    fd = platform_open_read("/proc/net/route");
    if (fd < 0) {
        return -1;
    }
    bytes = platform_read(fd, buffer, sizeof(buffer) - 1U);
    platform_close(fd);
    if (bytes <= 0) {
        return -1;
    }
    buffer[bytes] = '\0';

    cursor = buffer;
    while (*cursor != '\0' && count < entry_capacity) {
        const char *line_end = cursor;
        char ifname[PLATFORM_NAME_CAPACITY];
        char destination_text[32];
        char gateway_text[32];
        char flags_text[32];
        char metric_text[32];
        char mask_text[32];
        unsigned long destination = 0UL;
        unsigned long gateway = 0UL;
        unsigned long flags = 0UL;
        unsigned long long metric_value = 0ULL;
        unsigned long mask = 0UL;
        PlatformRouteEntry *entry;

        while (*line_end != '\0' && *line_end != '\n') {
            line_end += 1;
        }
        line_index += 1;
        if (line_index <= 1) {
            cursor = (*line_end == '\n') ? (line_end + 1) : line_end;
            continue;
        }

        if (linux_copy_token(&cursor, ifname, sizeof(ifname)) != 0 ||
            linux_copy_token(&cursor, destination_text, sizeof(destination_text)) != 0 ||
            linux_copy_token(&cursor, gateway_text, sizeof(gateway_text)) != 0 ||
            linux_copy_token(&cursor, flags_text, sizeof(flags_text)) != 0) {
            cursor = (*line_end == '\n') ? (line_end + 1) : line_end;
            continue;
        }

        if (linux_copy_token(&cursor, metric_text, sizeof(metric_text)) != 0 ||
            linux_copy_token(&cursor, metric_text, sizeof(metric_text)) != 0 ||
            linux_copy_token(&cursor, metric_text, sizeof(metric_text)) != 0 ||
            linux_copy_token(&cursor, mask_text, sizeof(mask_text)) != 0) {
            cursor = (*line_end == '\n') ? (line_end + 1) : line_end;
            continue;
        }

        cursor = (*line_end == '\n') ? (line_end + 1) : line_end;

        if (ifname_filter != 0 && rt_strcmp(ifname_filter, ifname) != 0) {
            continue;
        }
        if (linux_parse_hex_text(destination_text, &destination) != 0 ||
            linux_parse_hex_text(gateway_text, &gateway) != 0 ||
            linux_parse_hex_text(flags_text, &flags) != 0 ||
            linux_parse_decimal_text(metric_text, &metric_value) != 0 ||
            linux_parse_hex_text(mask_text, &mask) != 0) {
            continue;
        }
        if ((flags & LINUX_RTF_UP) == 0U) {
            continue;
        }

        entry = &entries_out[count];
        rt_memset(entry, 0, sizeof(*entry));
        entry->family = PLATFORM_NETWORK_FAMILY_IPV4;
        entry->metric = (unsigned int)metric_value;
        rt_copy_string(entry->ifname, sizeof(entry->ifname), ifname);
        entry->prefix_length = linux_route_prefix_from_mask(mask);

        if (destination == 0UL && mask == 0UL) {
            entry->is_default = 1;
            rt_copy_string(entry->destination, sizeof(entry->destination), "default");
        } else {
            LinuxInAddr addr;
            addr.bytes[0] = (unsigned char)(destination & 0xffU);
            addr.bytes[1] = (unsigned char)((destination >> 8) & 0xffU);
            addr.bytes[2] = (unsigned char)((destination >> 16) & 0xffU);
            addr.bytes[3] = (unsigned char)((destination >> 24) & 0xffU);
            linux_ipv4_to_text(&addr, entry->destination, sizeof(entry->destination));
        }

        if (gateway != 0UL) {
            LinuxInAddr gw;
            gw.bytes[0] = (unsigned char)(gateway & 0xffU);
            gw.bytes[1] = (unsigned char)((gateway >> 8) & 0xffU);
            gw.bytes[2] = (unsigned char)((gateway >> 16) & 0xffU);
            gw.bytes[3] = (unsigned char)((gateway >> 24) & 0xffU);
            linux_ipv4_to_text(&gw, entry->gateway, sizeof(entry->gateway));
            entry->has_gateway = 1;
        }

        count += 1U;
    }

    *count_out = count;
    return 0;
}

static int linux_socket_next_token(const char **cursor_io, char *buffer, size_t buffer_size) {
    const char *cursor = *cursor_io;
    size_t length = 0U;

    while (*cursor == ' ' || *cursor == '\t') {
        cursor += 1;
    }
    while (*cursor != '\0' && *cursor != '\n' && *cursor != ' ' && *cursor != '\t') {
        if (length + 1U < buffer_size) {
            buffer[length++] = *cursor;
        }
        cursor += 1;
    }
    buffer[length] = '\0';
    *cursor_io = cursor;
    return length == 0U ? -1 : 0;
}

static const char *linux_socket_state_name(unsigned long state, int udp) {
    if (udp) return "UNCONN";
    switch (state) {
        case 1UL: return "ESTAB";
        case 2UL: return "SYN-SENT";
        case 3UL: return "SYN-RECV";
        case 4UL: return "FIN-WAIT-1";
        case 5UL: return "FIN-WAIT-2";
        case 6UL: return "TIME-WAIT";
        case 7UL: return "CLOSE";
        case 8UL: return "CLOSE-WAIT";
        case 9UL: return "LAST-ACK";
        case 10UL: return "LISTEN";
        case 11UL: return "CLOSING";
        default: return "UNKNOWN";
    }
}

static int linux_socket_parse_endpoint(const char *text, int ipv6, char *address_out, size_t address_size, unsigned int *port_out) {
    char address_text[40];
    char port_text[16];
    size_t i = 0U;
    size_t addr_len = 0U;
    unsigned long port = 0UL;

    while (text[i] != '\0' && text[i] != ':' && addr_len + 1U < sizeof(address_text)) {
        address_text[addr_len++] = text[i++];
    }
    address_text[addr_len] = '\0';
    if (text[i] != ':') return -1;
    i += 1U;
    rt_copy_string(port_text, sizeof(port_text), text + i);
    if (linux_parse_hex_text(port_text, &port) != 0) return -1;
    *port_out = (unsigned int)port;
    if (ipv6) {
        rt_copy_string(address_out, address_size, address_text);
    } else {
        LinuxInAddr address;
        unsigned long raw = 0UL;
        if (linux_parse_hex_text(address_text, &raw) != 0) return -1;
        address.bytes[0] = (unsigned char)(raw & 0xffUL);
        address.bytes[1] = (unsigned char)((raw >> 8U) & 0xffUL);
        address.bytes[2] = (unsigned char)((raw >> 16U) & 0xffUL);
        address.bytes[3] = (unsigned char)((raw >> 24U) & 0xffUL);
        linux_ipv4_to_text(&address, address_out, address_size);
    }
    return 0;
}

static int linux_socket_read_file(const char *path,
                                  const char *protocol,
                                  int ipv6,
                                  int udp,
                                  PlatformSocketEntry *entries_out,
                                  size_t entry_capacity,
                                  size_t *count_io,
                                  int listening_only) {
    int fd;
    char buffer[65536];
    long bytes;
    const char *cursor;
    int line_number = 0;

    fd = platform_open_read(path);
    if (fd < 0) {
        return 0;
    }
    bytes = platform_read(fd, buffer, sizeof(buffer) - 1U);
    platform_close(fd);
    if (bytes <= 0) return 0;
    buffer[bytes] = '\0';
    cursor = buffer;
    while (*cursor != '\0' && *count_io < entry_capacity) {
        const char *line = cursor;
        const char *line_end = cursor;
        char sl[16], local[80], remote[80], state_text[16], token[80];
        unsigned long state = 0UL;
        unsigned long long inode = 0ULL;
        int token_index;
        PlatformSocketEntry *entry;

        while (*line_end != '\0' && *line_end != '\n') line_end += 1;
        cursor = (*line_end == '\n') ? line_end + 1 : line_end;
        line_number += 1;
        if (line_number == 1) continue;
        if (linux_socket_next_token(&line, sl, sizeof(sl)) != 0 ||
            linux_socket_next_token(&line, local, sizeof(local)) != 0 ||
            linux_socket_next_token(&line, remote, sizeof(remote)) != 0 ||
            linux_socket_next_token(&line, state_text, sizeof(state_text)) != 0 ||
            linux_parse_hex_text(state_text, &state) != 0) {
            continue;
        }
        if (listening_only && !(udp || state == 10UL)) {
            continue;
        }
        for (token_index = 0; token_index < 5; ++token_index) {
            if (linux_socket_next_token(&line, token, sizeof(token)) != 0) {
                break;
            }
        }
        if (linux_socket_next_token(&line, token, sizeof(token)) == 0) {
            (void)linux_parse_decimal_text(token, &inode);
        }
        entry = &entries_out[*count_io];
        rt_memset(entry, 0, sizeof(*entry));
        rt_copy_string(entry->protocol, sizeof(entry->protocol), protocol);
        rt_copy_string(entry->state, sizeof(entry->state), linux_socket_state_name(state, udp));
        if (linux_socket_parse_endpoint(local, ipv6, entry->local_address, sizeof(entry->local_address), &entry->local_port) != 0 ||
            linux_socket_parse_endpoint(remote, ipv6, entry->remote_address, sizeof(entry->remote_address), &entry->remote_port) != 0) {
            continue;
        }
        entry->inode = inode;
        *count_io += 1U;
    }
    return 0;
}

int platform_list_sockets(PlatformSocketEntry *entries_out, size_t entry_capacity, size_t *count_out, int include_tcp, int include_udp, int listening_only) {
    size_t count = 0U;

    if (entries_out == 0 || count_out == 0) return -1;
    if (!include_tcp && !include_udp) {
        include_tcp = 1;
        include_udp = 1;
    }
    if (include_tcp) {
        (void)linux_socket_read_file("/proc/net/tcp", "tcp", 0, 0, entries_out, entry_capacity, &count, listening_only);
        (void)linux_socket_read_file("/proc/net/tcp6", "tcp6", 1, 0, entries_out, entry_capacity, &count, listening_only);
    }
    if (include_udp) {
        (void)linux_socket_read_file("/proc/net/udp", "udp", 0, 1, entries_out, entry_capacity, &count, listening_only);
        (void)linux_socket_read_file("/proc/net/udp6", "udp6", 1, 1, entries_out, entry_capacity, &count, listening_only);
    }
    *count_out = count;
    return 0;
}

int platform_network_link_set(const char *ifname, int want_up, unsigned int mtu_value, int set_mtu) {
    int sock;
    struct linux_ifreq ifr;

    if (ifname == 0) {
        return -1;
    }

    sock = linux_open_inet_socket(LINUX_SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }

    rt_memset(&ifr, 0, sizeof(ifr));
    rt_copy_string(ifr.ifr_name, sizeof(ifr.ifr_name), ifname);
    if (linux_syscall3(LINUX_SYS_IOCTL, sock, LINUX_SIOCGIFFLAGS, (long)&ifr) < 0) {
        platform_close(sock);
        return -1;
    }

    if (want_up >= 0) {
        if (want_up != 0) {
            ifr.data.flags = (short)(ifr.data.flags | (short)LINUX_IFF_UP);
        } else {
            ifr.data.flags = (short)(ifr.data.flags & (short)~LINUX_IFF_UP);
        }
        if (linux_syscall3(LINUX_SYS_IOCTL, sock, LINUX_SIOCSIFFLAGS, (long)&ifr) < 0) {
            platform_close(sock);
            return -1;
        }
    }

    if (set_mtu != 0) {
        rt_memset(&ifr, 0, sizeof(ifr));
        rt_copy_string(ifr.ifr_name, sizeof(ifr.ifr_name), ifname);
        ifr.data.mtu = (int)mtu_value;
        if (linux_syscall3(LINUX_SYS_IOCTL, sock, LINUX_SIOCSIFMTU, (long)&ifr) < 0) {
            platform_close(sock);
            return -1;
        }
    }

    platform_close(sock);
    return 0;
}

static int linux_parse_ipv4_cidr(const char *text, LinuxInAddr *address_out, LinuxInAddr *mask_out, unsigned int *prefix_out) {
    char copy[PLATFORM_NETWORK_TEXT_CAPACITY];
    char *slash = 0;
    unsigned long long prefix = 32ULL;
    unsigned int i;

    if (text == 0 || address_out == 0 || mask_out == 0 || prefix_out == 0 || rt_strlen(text) >= sizeof(copy)) {
        return -1;
    }

    rt_copy_string(copy, sizeof(copy), text);
    for (i = 0U; copy[i] != '\0'; ++i) {
        if (copy[i] == '/') {
            slash = &copy[i];
            break;
        }
    }

    if (slash != 0) {
        *slash = '\0';
        if (linux_parse_decimal_text(slash + 1, &prefix) != 0 || prefix > 32ULL) {
            return -1;
        }
    }
    if (linux_parse_ipv4_text(copy, address_out) != 0) {
        return -1;
    }

    if (prefix == 0ULL) {
        memset(mask_out->bytes, 0, sizeof(mask_out->bytes));
    } else {
        unsigned int remaining = (unsigned int)prefix;
        for (i = 0U; i < 4U; ++i) {
            if (remaining >= 8U) {
                mask_out->bytes[i] = 0xffU;
                remaining -= 8U;
            } else if (remaining > 0U) {
                mask_out->bytes[i] = (unsigned char)(0xffU << (8U - remaining));
                remaining = 0U;
            } else {
                mask_out->bytes[i] = 0U;
            }
        }
    }

    *prefix_out = (unsigned int)prefix;
    return 0;
}

int platform_network_address_change(const char *ifname, const char *cidr, int add) {
    int sock;
    struct linux_ifreq ifr;
    LinuxInAddr address;
    LinuxInAddr mask;
    unsigned int prefix = 0U;
    struct linux_sockaddr_in *sockaddr_in;

    if (ifname == 0 || cidr == 0 || linux_parse_ipv4_cidr(cidr, &address, &mask, &prefix) != 0) {
        return -1;
    }
    (void)prefix;

    sock = linux_open_inet_socket(LINUX_SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }

    rt_memset(&ifr, 0, sizeof(ifr));
    rt_copy_string(ifr.ifr_name, sizeof(ifr.ifr_name), ifname);
    sockaddr_in = (struct linux_sockaddr_in *)&ifr.data.addr;
    linux_prepare_sockaddr(sockaddr_in, add ? &address : 0, 0U);
    if (linux_syscall3(LINUX_SYS_IOCTL, sock, LINUX_SIOCSIFADDR, (long)&ifr) < 0) {
        platform_close(sock);
        return -1;
    }

    rt_memset(&ifr, 0, sizeof(ifr));
    rt_copy_string(ifr.ifr_name, sizeof(ifr.ifr_name), ifname);
    sockaddr_in = (struct linux_sockaddr_in *)&ifr.data.netmask;
    linux_prepare_sockaddr(sockaddr_in, add ? &mask : 0, 0U);
    if (linux_syscall3(LINUX_SYS_IOCTL, sock, LINUX_SIOCSIFNETMASK, (long)&ifr) < 0) {
        platform_close(sock);
        return -1;
    }

    platform_close(sock);
    return 0;
}

int platform_network_route_change(const char *destination, const char *gateway, const char *ifname, int add) {
    int sock;
    struct linux_rtentry route;
    LinuxInAddr dst;
    LinuxInAddr mask;
    LinuxInAddr gw;
    unsigned int prefix = 0U;
    struct linux_sockaddr_in *sockaddr_in;

    if (destination == 0) {
        return -1;
    }

    rt_memset(&route, 0, sizeof(route));
    if (rt_strcmp(destination, "default") == 0) {
        rt_memset(&dst, 0, sizeof(dst));
        rt_memset(&mask, 0, sizeof(mask));
        prefix = 0U;
    } else if (linux_parse_ipv4_cidr(destination, &dst, &mask, &prefix) != 0) {
        return -1;
    }

    if (gateway != 0 && gateway[0] != '\0') {
        if (linux_parse_ipv4_text(gateway, &gw) != 0) {
            return -1;
        }
        route.rt_flags = (unsigned short)(route.rt_flags | LINUX_RTF_GATEWAY);
    } else {
        rt_memset(&gw, 0, sizeof(gw));
    }

    route.rt_flags = (unsigned short)(route.rt_flags | LINUX_RTF_UP);
    if (prefix == 32U) {
        route.rt_flags = (unsigned short)(route.rt_flags | LINUX_RTF_HOST);
    }
    route.rt_dev = (char *)ifname;

    sockaddr_in = (struct linux_sockaddr_in *)&route.rt_dst;
    linux_prepare_sockaddr(sockaddr_in, &dst, 0U);
    sockaddr_in = (struct linux_sockaddr_in *)&route.rt_gateway;
    linux_prepare_sockaddr(sockaddr_in, &gw, 0U);
    sockaddr_in = (struct linux_sockaddr_in *)&route.rt_genmask;
    linux_prepare_sockaddr(sockaddr_in, &mask, 0U);

    sock = linux_open_inet_socket(LINUX_SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }

    if (linux_syscall3(LINUX_SYS_IOCTL, sock, add ? LINUX_SIOCADDRT : LINUX_SIOCDELRT, (long)&route) < 0) {
        platform_close(sock);
        return -1;
    }

    platform_close(sock);
    return 0;
}

int platform_dns_lookup(
    const char *server,
    unsigned int port,
    const char *name,
    int family_filter,
    PlatformDnsEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out
) {
    size_t count = 0U;
    LinuxInAddr ipv4;
    LinuxIn6Addr ipv6;

    if (entries_out == 0 || count_out == 0 || name == 0) {
        return -1;
    }
    *count_out = 0U;

    if ((family_filter == PLATFORM_NETWORK_FAMILY_ANY || family_filter == PLATFORM_NETWORK_FAMILY_IPV4) &&
        linux_parse_ipv4_text(name, &ipv4) == 0) {
        (void)linux_add_dns_entry(
            entries_out,
            entry_capacity,
            &count,
            name,
            PLATFORM_NETWORK_FAMILY_IPV4,
            PLATFORM_DNS_RECORD_A,
            name,
            0U,
            0U
        );
    }
    if ((family_filter == PLATFORM_NETWORK_FAMILY_ANY || family_filter == PLATFORM_NETWORK_FAMILY_IPV6) &&
        linux_parse_ipv6_text(name, &ipv6) == 0) {
        (void)linux_add_dns_entry(
            entries_out,
            entry_capacity,
            &count,
            name,
            PLATFORM_NETWORK_FAMILY_IPV6,
            PLATFORM_DNS_RECORD_AAAA,
            name,
            0U,
            0U
        );
    }

    (void)linux_lookup_hosts_file(name, family_filter, entries_out, entry_capacity, &count);
    if ((family_filter == PLATFORM_NETWORK_FAMILY_ANY || family_filter == PLATFORM_NETWORK_FAMILY_IPV4)) {
        (void)linux_dns_query(server, port, name, 1U, entries_out, entry_capacity, &count);
    }
    if ((family_filter == PLATFORM_NETWORK_FAMILY_ANY || family_filter == PLATFORM_NETWORK_FAMILY_IPV6)) {
        (void)linux_dns_query(server, port, name, 28U, entries_out, entry_capacity, &count);
    }

    *count_out = count;
    return count > 0U ? 0 : -1;
}

int platform_dns_query(
    const char *server,
    unsigned int port,
    const char *name,
    unsigned short record_type,
    PlatformDnsEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out
) {
    size_t count = 0U;
    LinuxInAddr ipv4;
    LinuxIn6Addr ipv6;

    if (entries_out == 0 || count_out == 0 || name == 0) {
        return -1;
    }
    *count_out = 0U;

    if (record_type == PLATFORM_DNS_RECORD_A && linux_parse_ipv4_text(name, &ipv4) == 0) {
        (void)linux_add_dns_entry(
            entries_out,
            entry_capacity,
            &count,
            name,
            PLATFORM_NETWORK_FAMILY_IPV4,
            PLATFORM_DNS_RECORD_A,
            name,
            0U,
            0U
        );
    } else if (record_type == PLATFORM_DNS_RECORD_AAAA && linux_parse_ipv6_text(name, &ipv6) == 0) {
        (void)linux_add_dns_entry(
            entries_out,
            entry_capacity,
            &count,
            name,
            PLATFORM_NETWORK_FAMILY_IPV6,
            PLATFORM_DNS_RECORD_AAAA,
            name,
            0U,
            0U
        );
    }

    if ((server == 0 || server[0] == '\0') && record_type == PLATFORM_DNS_RECORD_A) {
        (void)linux_lookup_hosts_file(name, PLATFORM_NETWORK_FAMILY_IPV4, entries_out, entry_capacity, &count);
    } else if ((server == 0 || server[0] == '\0') && record_type == PLATFORM_DNS_RECORD_AAAA) {
        (void)linux_lookup_hosts_file(name, PLATFORM_NETWORK_FAMILY_IPV6, entries_out, entry_capacity, &count);
    }

    (void)linux_dns_query(server, port, name, record_type, entries_out, entry_capacity, &count);
    *count_out = count;
    return count > 0U ? 0 : -1;
}

static int linux_select_mac_address(const char *ifname, unsigned char mac_out[6]) {
    char names[32][PLATFORM_NAME_CAPACITY];
    size_t name_count = 0U;
    int sock;
    size_t i;

    if (mac_out == 0) {
        return -1;
    }

    sock = linux_open_inet_socket(LINUX_SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }

    if (ifname != 0 && ifname[0] != '\0') {
        struct linux_ifreq ifr;
        rt_memset(&ifr, 0, sizeof(ifr));
        rt_copy_string(ifr.ifr_name, sizeof(ifr.ifr_name), ifname);
        if (linux_syscall3(LINUX_SYS_IOCTL, sock, LINUX_SIOCGIFHWADDR, (long)&ifr) >= 0) {
            memcpy(mac_out, ((const unsigned char *)&ifr.data.hwaddr) + 2, 6U);
            platform_close(sock);
            return 0;
        }
        platform_close(sock);
        return -1;
    }

    if (linux_collect_interface_names(names, sizeof(names) / sizeof(names[0]), &name_count) != 0) {
        platform_close(sock);
        return -1;
    }

    for (i = 0U; i < name_count; ++i) {
        struct linux_ifreq ifr;

        rt_memset(&ifr, 0, sizeof(ifr));
        rt_copy_string(ifr.ifr_name, sizeof(ifr.ifr_name), names[i]);
        if (linux_syscall3(LINUX_SYS_IOCTL, sock, LINUX_SIOCGIFFLAGS, (long)&ifr) >= 0 &&
            (ifr.data.flags & (short)LINUX_IFF_LOOPBACK) != 0) {
            continue;
        }
        rt_memset(&ifr, 0, sizeof(ifr));
        rt_copy_string(ifr.ifr_name, sizeof(ifr.ifr_name), names[i]);
        if (linux_syscall3(LINUX_SYS_IOCTL, sock, LINUX_SIOCGIFHWADDR, (long)&ifr) >= 0) {
            memcpy(mac_out, ((const unsigned char *)&ifr.data.hwaddr) + 2, 6U);
            platform_close(sock);
            return 0;
        }
    }

    platform_close(sock);
    return -1;
}

static void linux_store_be16(unsigned char *buffer, unsigned short value) {
    buffer[0] = (unsigned char)(value >> 8);
    buffer[1] = (unsigned char)(value & 0xffU);
}

static void linux_store_be32(unsigned char *buffer, unsigned int value) {
    buffer[0] = (unsigned char)(value >> 24);
    buffer[1] = (unsigned char)((value >> 16) & 0xffU);
    buffer[2] = (unsigned char)((value >> 8) & 0xffU);
    buffer[3] = (unsigned char)(value & 0xffU);
}

static unsigned int linux_load_be32(const unsigned char *buffer) {
    return ((unsigned int)buffer[0] << 24) |
           ((unsigned int)buffer[1] << 16) |
           ((unsigned int)buffer[2] << 8) |
           (unsigned int)buffer[3];
}

static int linux_build_dhcp_packet(
    unsigned char *packet,
    size_t packet_size,
    unsigned int xid,
    const unsigned char mac[6],
    unsigned char message_type,
    const LinuxInAddr *requested_ip,
    const LinuxInAddr *server_id
) {
    size_t offset = 240U;
    static const unsigned char param_request[] = { 1U, 3U, 6U, 15U, 51U, 54U };

    if (packet == 0 || packet_size < 300U || mac == 0) {
        return -1;
    }

    rt_memset(packet, 0, packet_size);
    packet[0] = 1U;
    packet[1] = 1U;
    packet[2] = 6U;
    linux_store_be32(packet + 4, xid);
    linux_store_be16(packet + 10, 0x8000U);
    memcpy(packet + 28, mac, 6U);
    linux_store_be32(packet + 236, 0x63825363U);

    packet[offset++] = 53U;
    packet[offset++] = 1U;
    packet[offset++] = message_type;

    packet[offset++] = 61U;
    packet[offset++] = 7U;
    packet[offset++] = 1U;
    memcpy(packet + offset, mac, 6U);
    offset += 6U;

    if (requested_ip != 0) {
        packet[offset++] = 50U;
        packet[offset++] = 4U;
        memcpy(packet + offset, requested_ip->bytes, 4U);
        offset += 4U;
    }
    if (server_id != 0) {
        packet[offset++] = 54U;
        packet[offset++] = 4U;
        memcpy(packet + offset, server_id->bytes, 4U);
        offset += 4U;
    }

    packet[offset++] = 55U;
    packet[offset++] = (unsigned char)sizeof(param_request);
    memcpy(packet + offset, param_request, sizeof(param_request));
    offset += sizeof(param_request);

    packet[offset++] = 255U;
    return (int)offset;
}

static int linux_parse_dhcp_reply(
    const unsigned char *packet,
    size_t packet_length,
    unsigned int xid,
    const unsigned char mac[6],
    unsigned char expected_message_type,
    PlatformDhcpLease *lease_out
) {
    size_t offset = 240U;
    unsigned char message_type = 0U;
    LinuxInAddr yiaddr;

    if (packet == 0 || lease_out == 0 || packet_length < 240U || mac == 0) {
        return -1;
    }
    {
        size_t mac_index;
        if (packet[0] != 2U || linux_load_be32(packet + 4) != xid) {
            return -1;
        }
        for (mac_index = 0U; mac_index < 6U; ++mac_index) {
            if (packet[28U + mac_index] != mac[mac_index]) {
                return -1;
            }
        }
    }
    if (linux_load_be32(packet + 236) != 0x63825363U) {
        return -1;
    }

    yiaddr.bytes[0] = packet[16];
    yiaddr.bytes[1] = packet[17];
    yiaddr.bytes[2] = packet[18];
    yiaddr.bytes[3] = packet[19];
    linux_ipv4_to_text(&yiaddr, lease_out->address, sizeof(lease_out->address));

    while (offset < packet_length) {
        unsigned char option = packet[offset++];
        unsigned char length;

        if (option == 0U) {
            continue;
        }
        if (option == 255U) {
            break;
        }
        if (offset >= packet_length) {
            break;
        }
        length = packet[offset++];
        if (offset + length > packet_length) {
            break;
        }

        if (option == 53U && length >= 1U) {
            message_type = packet[offset];
        } else if (option == 1U && length == 4U) {
            LinuxInAddr mask;
            mask.bytes[0] = packet[offset];
            mask.bytes[1] = packet[offset + 1U];
            mask.bytes[2] = packet[offset + 2U];
            mask.bytes[3] = packet[offset + 3U];
            lease_out->prefix_length = linux_prefix_from_mask(&mask);
        } else if (option == 3U && length >= 4U) {
            LinuxInAddr router;
            router.bytes[0] = packet[offset];
            router.bytes[1] = packet[offset + 1U];
            router.bytes[2] = packet[offset + 2U];
            router.bytes[3] = packet[offset + 3U];
            linux_ipv4_to_text(&router, lease_out->router, sizeof(lease_out->router));
        } else if (option == 6U && length >= 4U) {
            LinuxInAddr dns;
            dns.bytes[0] = packet[offset];
            dns.bytes[1] = packet[offset + 1U];
            dns.bytes[2] = packet[offset + 2U];
            dns.bytes[3] = packet[offset + 3U];
            linux_ipv4_to_text(&dns, lease_out->dns1, sizeof(lease_out->dns1));
            if (length >= 8U) {
                dns.bytes[0] = packet[offset + 4U];
                dns.bytes[1] = packet[offset + 5U];
                dns.bytes[2] = packet[offset + 6U];
                dns.bytes[3] = packet[offset + 7U];
                linux_ipv4_to_text(&dns, lease_out->dns2, sizeof(lease_out->dns2));
            }
        } else if (option == 51U && length == 4U) {
            lease_out->lease_seconds = linux_load_be32(packet + offset);
        } else if (option == 54U && length == 4U) {
            LinuxInAddr server_id;
            server_id.bytes[0] = packet[offset];
            server_id.bytes[1] = packet[offset + 1U];
            server_id.bytes[2] = packet[offset + 2U];
            server_id.bytes[3] = packet[offset + 3U];
            linux_ipv4_to_text(&server_id, lease_out->server, sizeof(lease_out->server));
        }

        offset += length;
    }

    if (message_type != expected_message_type) {
        return -1;
    }
    if (lease_out->prefix_length == 0U) {
        lease_out->prefix_length = 24U;
    }
    return 0;
}

int platform_dhcp_request(
    const char *ifname,
    const char *server,
    unsigned int server_port,
    unsigned int client_port,
    unsigned int timeout_milliseconds,
    PlatformDhcpLease *lease_out
) {
    unsigned char mac[6];
    unsigned char packet[512];
    unsigned char reply[512];
    LinuxInAddr server_address;
    LinuxInAddr requested_ip;
    LinuxInAddr server_id;
    int sock;
    int packet_length;
    unsigned int xid;
    int broadcast = 1;
    int fds[1];
    size_t ready_index = 0U;
    long reply_bytes;
    int have_server_id = 0;

    if (lease_out == 0) {
        return -1;
    }
    rt_memset(lease_out, 0, sizeof(*lease_out));

    if (linux_select_mac_address(ifname, mac) != 0) {
        return -1;
    }
    if (server == 0 || server[0] == '\0') {
        if (linux_parse_ipv4_text("10.0.2.2", &server_address) != 0) {
            return -1;
        }
    } else if (linux_resolve_ipv4_host(server, &server_address) != 0) {
        return -1;
    }

    xid = ((unsigned int)platform_get_process_id() & 0xffffU) ^ 0x44480000U;
    sock = linux_open_inet_socket(LINUX_SOCK_DGRAM, LINUX_IPPROTO_UDP);
    if (sock < 0) {
        return -1;
    }
    (void)linux_set_socket_int_option(sock, LINUX_SOL_SOCKET, LINUX_SO_REUSEADDR, 1);
    (void)linux_set_socket_int_option(sock, LINUX_SOL_SOCKET, LINUX_SO_BROADCAST, broadcast);
    if (linux_bind_ipv4(sock, 0, client_port == 0U ? 68U : client_port) != 0 ||
        linux_connect_ipv4(sock, &server_address, server_port == 0U ? 67U : server_port) != 0) {
        platform_close(sock);
        return -1;
    }

    packet_length = linux_build_dhcp_packet(packet, sizeof(packet), xid, mac, 1U, 0, 0);
    if (packet_length < 0 || platform_write(sock, packet, (size_t)packet_length) < packet_length) {
        platform_close(sock);
        return -1;
    }

    fds[0] = sock;
    if (platform_poll_fds(fds, 1U, &ready_index, (int)(timeout_milliseconds == 0U ? 3000U : timeout_milliseconds)) <= 0) {
        platform_close(sock);
        return -1;
    }
    reply_bytes = platform_read(sock, reply, sizeof(reply));
    if (reply_bytes <= 0 || linux_parse_dhcp_reply(reply, (size_t)reply_bytes, xid, mac, 2U, lease_out) != 0) {
        platform_close(sock);
        return -1;
    }

    if (linux_parse_ipv4_text(lease_out->address, &requested_ip) != 0) {
        platform_close(sock);
        return -1;
    }
    if (lease_out->server[0] != '\0' && linux_parse_ipv4_text(lease_out->server, &server_id) == 0) {
        have_server_id = 1;
    }

    packet_length = linux_build_dhcp_packet(packet, sizeof(packet), xid, mac, 3U, &requested_ip, have_server_id ? &server_id : 0);
    if (packet_length < 0 || platform_write(sock, packet, (size_t)packet_length) < packet_length) {
        platform_close(sock);
        return -1;
    }
    if (platform_poll_fds(fds, 1U, &ready_index, (int)(timeout_milliseconds == 0U ? 3000U : timeout_milliseconds)) <= 0) {
        platform_close(sock);
        return -1;
    }
    reply_bytes = platform_read(sock, reply, sizeof(reply));
    platform_close(sock);
    if (reply_bytes <= 0 || linux_parse_dhcp_reply(reply, (size_t)reply_bytes, xid, mac, 5U, lease_out) != 0) {
        return -1;
    }

    return 0;
}

int platform_ping_host(const char *host, const PlatformPingOptions *options) {
    PlatformPingOptions defaults;
    LinuxInAddr address;
    char address_text[PLATFORM_NETWORK_TEXT_CAPACITY];
    unsigned int transmitted = 0U;
    unsigned int received = 0U;
    unsigned int count;
    unsigned int payload_size;
    unsigned int timeout_ms;
    unsigned int interval_seconds;
    unsigned short identifier;
    int sock;
    unsigned int sequence;
    unsigned long long overall_start_ms;
    int deadline_exceeded = 0;

    if (host == 0) {
        return 1;
    }

    if (options == 0) {
        defaults.count = PLATFORM_PING_DEFAULT_COUNT;
        defaults.interval_seconds = PLATFORM_PING_DEFAULT_INTERVAL_SECONDS;
        defaults.timeout_seconds = PLATFORM_PING_DEFAULT_TIMEOUT_SECONDS;
        defaults.payload_size = PLATFORM_PING_DEFAULT_PAYLOAD_SIZE;
        defaults.ttl = 0U;
        defaults.deadline_seconds = 0U;
        defaults.quiet_output = 0;
        defaults.family = PLATFORM_NETWORK_FAMILY_ANY;
        defaults.numeric_only = 0;
        options = &defaults;
    }

    count = options->count == 0U ? PLATFORM_PING_DEFAULT_COUNT : options->count;
    payload_size = options->payload_size > PLATFORM_PING_MAX_PAYLOAD_SIZE ? PLATFORM_PING_MAX_PAYLOAD_SIZE : options->payload_size;
    timeout_ms = (options->timeout_seconds == 0U ? PLATFORM_PING_DEFAULT_TIMEOUT_SECONDS : options->timeout_seconds) * 1000U;
    interval_seconds = options->interval_seconds;

    if (options->family == PLATFORM_NETWORK_FAMILY_IPV6 ||
        (options->family != PLATFORM_NETWORK_FAMILY_IPV4 && linux_contains_char(host, ':'))) {
        LinuxIn6Addr address6;
        char address_text6[PLATFORM_NETWORK_TEXT_CAPACITY];

        if (linux_resolve_ipv6_host(host, &address6) != 0) {
            rt_write_cstr(2, "ping: cannot resolve ");
            rt_write_line(2, host);
            return 1;
        }

        sock = linux_open_socket(LINUX_AF_INET6, LINUX_SOCK_DGRAM, LINUX_IPPROTO_ICMPV6);
        if (sock < 0) {
            sock = linux_open_socket(LINUX_AF_INET6, LINUX_SOCK_RAW, LINUX_IPPROTO_ICMPV6);
        }
        if (sock < 0 || linux_connect_ipv6(sock, &address6, 0U) != 0) {
            if (sock >= 0) {
                platform_close(sock);
            }
            rt_write_line(2, "ping: ICMPv6 is not yet supported on this platform");
            return 1;
        }

        if (options->ttl != 0U) {
            int hops_value = (int)options->ttl;
            (void)linux_set_socket_int_option(sock, LINUX_IPPROTO_IPV6, LINUX_IPV6_UNICAST_HOPS, hops_value);
        }

        linux_ipv6_to_text(&address6, address_text6, sizeof(address_text6));
        rt_write_cstr(1, "PING ");
        rt_write_cstr(1, host);
        rt_write_cstr(1, " (");
        rt_write_cstr(1, address_text6);
        rt_write_cstr(1, "): ");
        rt_write_uint(1, (unsigned long long)payload_size);
        rt_write_line(1, " data bytes");

        overall_start_ms = linux_monotonic_milliseconds();
        identifier = (unsigned short)(platform_get_process_id() & 0xffff);
        for (sequence = 0U; sequence < count; ++sequence) {
            unsigned char packet[8 + PLATFORM_PING_MAX_PAYLOAD_SIZE];
            unsigned char reply[512];
            LinuxIcmpv6Packet *icmp6 = (LinuxIcmpv6Packet *)packet;
            unsigned long long start_ms;
            unsigned long long deadline;
            int matched = 0;
            size_t ready_index = 0U;
            unsigned int i;

            if (options->deadline_seconds > 0U &&
                linux_monotonic_milliseconds() - overall_start_ms >= (unsigned long long)options->deadline_seconds * 1000ULL) {
                deadline_exceeded = 1;
                break;
            }

            rt_memset(packet, 0, sizeof(packet));
            icmp6->type = 128U;
            icmp6->code = 0U;
            icmp6->identifier = linux_host_to_net16(identifier);
            icmp6->sequence = linux_host_to_net16((unsigned short)sequence);
            for (i = 0U; i < payload_size; ++i) {
                packet[sizeof(LinuxIcmpv6Packet) + i] = (unsigned char)('A' + (i % 26U));
            }
            icmp6->checksum = 0U;

            start_ms = linux_monotonic_milliseconds();
            if (platform_write(sock, packet, sizeof(LinuxIcmpv6Packet) + payload_size) < (long)(sizeof(LinuxIcmpv6Packet) + payload_size)) {
                rt_write_line(2, "ping: send failed");
                platform_close(sock);
                return 1;
            }
            transmitted += 1U;
            deadline = start_ms + timeout_ms;

            while (!matched) {
                int timeout_remaining = (int)(deadline > linux_monotonic_milliseconds() ? deadline - linux_monotonic_milliseconds() : 0ULL);
                long reply_bytes;
                size_t payload_offset = 0U;
                LinuxIcmpv6Packet *reply_icmp6;

                if (platform_poll_fds(&sock, 1U, &ready_index, timeout_remaining) <= 0) {
                    break;
                }

                reply_bytes = platform_read(sock, reply, sizeof(reply));
                if (reply_bytes <= 0) {
                    break;
                }

                if ((size_t)reply_bytes >= 40U + sizeof(LinuxIcmpv6Packet) && (reply[0] >> 4) == 6U) {
                    payload_offset = 40U;
                }
                if (payload_offset + sizeof(LinuxIcmpv6Packet) > (size_t)reply_bytes) {
                    payload_offset = 0U;
                }

                reply_icmp6 = (LinuxIcmpv6Packet *)(reply + payload_offset);
                if (reply_icmp6->type == 129U &&
                    linux_net_to_host16(reply_icmp6->identifier) == identifier &&
                    linux_net_to_host16(reply_icmp6->sequence) == (unsigned short)sequence) {
                    unsigned long long elapsed = linux_monotonic_milliseconds() - start_ms;
                    if (!options->quiet_output) {
                        rt_write_uint(1, (unsigned long long)(reply_bytes - (long)payload_offset));
                        rt_write_cstr(1, " bytes from ");
                        rt_write_cstr(1, address_text6);
                        rt_write_cstr(1, ": icmp_seq=");
                        rt_write_uint(1, (unsigned long long)(sequence + 1U));
                        rt_write_cstr(1, " time=");
                        linux_write_milliseconds(elapsed, 0ULL);
                        rt_write_line(1, " ms");
                    }
                    received += 1U;
                    matched = 1;
                }
            }

            if (!matched && !options->quiet_output) {
                rt_write_cstr(1, "Request timeout for icmp_seq ");
                rt_write_uint(1, (unsigned long long)(sequence + 1U));
                rt_write_char(1, '\n');
            }

            if (sequence + 1U < count && interval_seconds > 0U) {
                if (options->deadline_seconds > 0U &&
                    linux_monotonic_milliseconds() - overall_start_ms >= (unsigned long long)options->deadline_seconds * 1000ULL) {
                    deadline_exceeded = 1;
                    break;
                }
                (void)platform_sleep_seconds(interval_seconds);
            }
        }

        rt_write_cstr(1, "--- ");
        rt_write_cstr(1, host);
        rt_write_line(1, " ping statistics ---");
        rt_write_uint(1, (unsigned long long)transmitted);
        rt_write_cstr(1, " packets transmitted, ");
        rt_write_uint(1, (unsigned long long)received);
        rt_write_cstr(1, " packets received, ");
        rt_write_uint(1, transmitted == 0U ? 0ULL : (unsigned long long)(((transmitted - received) * 100U) / transmitted));
        rt_write_line(1, "% packet loss");

        if (deadline_exceeded && !options->quiet_output) {
            rt_write_line(1, "ping: deadline reached");
        }

        platform_close(sock);
        return received == 0U ? 1 : 0;
    }

    if (linux_resolve_ipv4_host(host, &address) != 0) {
        rt_write_cstr(2, "ping: cannot resolve ");
        rt_write_line(2, host);
        return 1;
    }

    sock = linux_open_inet_socket(LINUX_SOCK_RAW, LINUX_IPPROTO_ICMP);
    if (sock < 0 || linux_connect_ipv4(sock, &address, 0U) != 0) {
        if (sock >= 0) {
            platform_close(sock);
        }
        rt_write_line(2, "ping: ICMP is not yet supported on this platform");
        return 1;
    }

    if (options->ttl != 0U) {
        int ttl_value = (int)options->ttl;
        (void)linux_set_socket_int_option(sock, LINUX_IPPROTO_IP, LINUX_IP_TTL, ttl_value);
    }

    linux_ipv4_to_text(&address, address_text, sizeof(address_text));
    rt_write_cstr(1, "PING ");
    rt_write_cstr(1, host);
    rt_write_cstr(1, " (");
    rt_write_cstr(1, address_text);
    rt_write_cstr(1, "): ");
    rt_write_uint(1, (unsigned long long)payload_size);
    rt_write_line(1, " data bytes");

    overall_start_ms = linux_monotonic_milliseconds();
    identifier = (unsigned short)(platform_get_process_id() & 0xffff);
    for (sequence = 0U; sequence < count; ++sequence) {
        unsigned char packet[8 + PLATFORM_PING_MAX_PAYLOAD_SIZE];
        unsigned char reply[512];
        LinuxIcmpPacket *icmp = (LinuxIcmpPacket *)packet;
        unsigned long long start_ms;
        unsigned long long deadline;
        int matched = 0;
        size_t ready_index = 0U;
        unsigned int i;

        if (options->deadline_seconds > 0U &&
            linux_monotonic_milliseconds() - overall_start_ms >= (unsigned long long)options->deadline_seconds * 1000ULL) {
            deadline_exceeded = 1;
            break;
        }

        rt_memset(packet, 0, sizeof(packet));
        icmp->type = LINUX_ICMP_ECHO;
        icmp->code = 0U;
        icmp->identifier = linux_host_to_net16(identifier);
        icmp->sequence = linux_host_to_net16((unsigned short)sequence);
        for (i = 0U; i < payload_size; ++i) {
            packet[sizeof(LinuxIcmpPacket) + i] = (unsigned char)('A' + (i % 26U));
        }
        icmp->checksum = 0U;
        icmp->checksum = linux_compute_icmp_checksum(packet, sizeof(LinuxIcmpPacket) + payload_size);

        start_ms = linux_monotonic_milliseconds();
        if (platform_write(sock, packet, sizeof(LinuxIcmpPacket) + payload_size) < (long)(sizeof(LinuxIcmpPacket) + payload_size)) {
            rt_write_line(2, "ping: send failed");
            platform_close(sock);
            return 1;
        }
        transmitted += 1U;
        deadline = start_ms + timeout_ms;

        while (!matched) {
            int timeout_remaining = (int)(deadline > linux_monotonic_milliseconds() ? deadline - linux_monotonic_milliseconds() : 0ULL);
            long reply_bytes;
            size_t ip_header_length = 0U;
            LinuxIcmpPacket *reply_icmp;

            if (platform_poll_fds(&sock, 1U, &ready_index, timeout_remaining) <= 0) {
                break;
            }

            reply_bytes = platform_read(sock, reply, sizeof(reply));
            if (reply_bytes <= 0) {
                break;
            }

            if ((reply[0] >> 4) == 4U) {
                ip_header_length = (size_t)(reply[0] & 0x0fU) * 4U;
            }
            if (ip_header_length + sizeof(LinuxIcmpPacket) > (size_t)reply_bytes) {
                ip_header_length = 0U;
            }

            reply_icmp = (LinuxIcmpPacket *)(reply + ip_header_length);
            if (reply_icmp->type == LINUX_ICMP_REPLY &&
                linux_net_to_host16(reply_icmp->identifier) == identifier &&
                linux_net_to_host16(reply_icmp->sequence) == (unsigned short)sequence) {
                unsigned long long elapsed = linux_monotonic_milliseconds() - start_ms;
                unsigned int ttl = (ip_header_length >= 9U) ? reply[8] : 0U;
                if (!options->quiet_output) {
                    rt_write_uint(1, (unsigned long long)(reply_bytes - (long)ip_header_length));
                    rt_write_cstr(1, " bytes from ");
                    rt_write_cstr(1, address_text);
                    rt_write_cstr(1, ": icmp_seq=");
                    rt_write_uint(1, (unsigned long long)(sequence + 1U));
                    if (ttl != 0U) {
                        rt_write_cstr(1, " ttl=");
                        rt_write_uint(1, (unsigned long long)ttl);
                    }
                    rt_write_cstr(1, " time=");
                    linux_write_milliseconds(elapsed, 0ULL);
                    rt_write_line(1, " ms");
                }
                received += 1U;
                matched = 1;
            }
        }

        if (!matched && !options->quiet_output) {
            rt_write_cstr(1, "Request timeout for icmp_seq ");
            rt_write_uint(1, (unsigned long long)(sequence + 1U));
            rt_write_char(1, '\n');
        }

        if (sequence + 1U < count && interval_seconds > 0U) {
            if (options->deadline_seconds > 0U &&
                linux_monotonic_milliseconds() - overall_start_ms >= (unsigned long long)options->deadline_seconds * 1000ULL) {
                deadline_exceeded = 1;
                break;
            }
            (void)platform_sleep_seconds(interval_seconds);
        }
    }

    rt_write_cstr(1, "--- ");
    rt_write_cstr(1, host);
    rt_write_line(1, " ping statistics ---");
    rt_write_uint(1, (unsigned long long)transmitted);
    rt_write_cstr(1, " packets transmitted, ");
    rt_write_uint(1, (unsigned long long)received);
    rt_write_cstr(1, " packets received, ");
    rt_write_uint(1, transmitted == 0U ? 0ULL : (unsigned long long)(((transmitted - received) * 100U) / transmitted));
    rt_write_line(1, "% packet loss");

    if (deadline_exceeded && !options->quiet_output) {
        rt_write_line(1, "ping: deadline reached");
    }

    platform_close(sock);
    return received == 0U ? 1 : 0;
}

static int linux_trace_match_inner_ipv4(
    const unsigned char *reply,
    size_t reply_size,
    size_t outer_offset,
    unsigned short identifier,
    unsigned short sequence,
    int allow_identifier_rewrite
) {
    size_t inner_ip_offset = outer_offset + sizeof(LinuxIcmpPacket);
    size_t inner_ip_header_length;
    const LinuxIcmpPacket *inner_icmp;

    if (reply_size < inner_ip_offset + 20U || (reply[inner_ip_offset] >> 4) != 4U) {
        return 0;
    }
    inner_ip_header_length = (size_t)(reply[inner_ip_offset] & 0x0fU) * 4U;
    if (inner_ip_header_length < 20U || reply_size < inner_ip_offset + inner_ip_header_length + sizeof(LinuxIcmpPacket)) {
        return 0;
    }
    inner_icmp = (const LinuxIcmpPacket *)(reply + inner_ip_offset + inner_ip_header_length);
    return inner_icmp->type == LINUX_ICMP_ECHO &&
           linux_net_to_host16(inner_icmp->sequence) == sequence &&
           (allow_identifier_rewrite || linux_net_to_host16(inner_icmp->identifier) == identifier);
}

static int linux_trace_match_probe_ipv4(
    const unsigned char *packet,
    size_t packet_size,
    unsigned short identifier,
    unsigned short sequence,
    int allow_identifier_rewrite
) {
    size_t offset = 0U;
    const LinuxIcmpPacket *icmp;

    if (packet_size >= 20U && (packet[0] >> 4) == 4U) {
        offset = (size_t)(packet[0] & 0x0fU) * 4U;
        if (offset < 20U) {
            return 0;
        }
    }
    if (packet_size < offset + sizeof(LinuxIcmpPacket)) {
        return 0;
    }
    icmp = (const LinuxIcmpPacket *)(packet + offset);
    return icmp->type == LINUX_ICMP_ECHO &&
           linux_net_to_host16(icmp->sequence) == sequence &&
           (allow_identifier_rewrite || linux_net_to_host16(icmp->identifier) == identifier);
}

static int linux_trace_match_inner_ipv6(
    const unsigned char *reply,
    size_t reply_size,
    size_t outer_offset,
    unsigned short identifier,
    unsigned short sequence,
    int allow_identifier_rewrite
) {
    size_t inner_ip_offset = outer_offset + sizeof(LinuxIcmpv6Packet);
    const LinuxIcmpv6Packet *inner_icmp;

    if (reply_size < inner_ip_offset + 40U || (reply[inner_ip_offset] >> 4) != 6U) {
        return 0;
    }
    if (reply_size < inner_ip_offset + 40U + sizeof(LinuxIcmpv6Packet)) {
        return 0;
    }
    inner_icmp = (const LinuxIcmpv6Packet *)(reply + inner_ip_offset + 40U);
    return inner_icmp->type == LINUX_ICMPV6_ECHO_REQUEST &&
           linux_net_to_host16(inner_icmp->sequence) == sequence &&
           (allow_identifier_rewrite || linux_net_to_host16(inner_icmp->identifier) == identifier);
}

static int linux_trace_append_text(char *buffer, size_t buffer_size, const char *text) {
    size_t used;
    size_t index = 0U;

    if (buffer == 0 || buffer_size == 0U || text == 0) {
        return -1;
    }
    used = rt_strlen(buffer);
    while (text[index] != '\0') {
        if (used + 1U >= buffer_size) {
            return -1;
        }
        buffer[used++] = text[index++];
        buffer[used] = '\0';
    }
    return 0;
}

static int linux_trace_append_uint(char *buffer, size_t buffer_size, unsigned int value) {
    char digits[16];

    rt_unsigned_to_string((unsigned long long)value, digits, sizeof(digits));
    return linux_trace_append_text(buffer, buffer_size, digits);
}

static int linux_trace_ipv4_ptr_name(const LinuxInAddr *address, char *buffer, size_t buffer_size) {
    int index;

    if (address == 0 || buffer == 0 || buffer_size == 0U) {
        return -1;
    }
    buffer[0] = '\0';
    for (index = 3; index >= 0; --index) {
        if (linux_trace_append_uint(buffer, buffer_size, (unsigned int)address->bytes[index]) != 0 ||
            linux_trace_append_text(buffer, buffer_size, ".") != 0) {
            return -1;
        }
    }
    return linux_trace_append_text(buffer, buffer_size, "in-addr.arpa");
}

static int linux_trace_ipv6_ptr_name(const LinuxIn6Addr *address, char *buffer, size_t buffer_size) {
    static const char hex[] = "0123456789abcdef";
    int index;

    if (address == 0 || buffer == 0 || buffer_size == 0U) {
        return -1;
    }
    buffer[0] = '\0';
    for (index = 15; index >= 0; --index) {
        char nibble[3];
        nibble[0] = hex[address->bytes[index] & 0x0fU];
        nibble[1] = '.';
        nibble[2] = '\0';
        if (linux_trace_append_text(buffer, buffer_size, nibble) != 0) {
            return -1;
        }
        nibble[0] = hex[(address->bytes[index] >> 4) & 0x0fU];
        if (linux_trace_append_text(buffer, buffer_size, nibble) != 0) {
            return -1;
        }
    }
    return linux_trace_append_text(buffer, buffer_size, "ip6.arpa");
}

static void linux_trace_lookup_ptr(const char *ptr_name, char *name_out, size_t name_size) {
    PlatformDnsEntry entries[2];
    size_t count = 0U;
    size_t index;

    if (ptr_name == 0 || name_out == 0 || name_size == 0U) {
        return;
    }
    name_out[0] = '\0';
    if (linux_dns_query(0, 53U, ptr_name, PLATFORM_DNS_RECORD_PTR, entries, sizeof(entries) / sizeof(entries[0]), &count) != 0) {
        return;
    }
    for (index = 0U; index < count; ++index) {
        if (entries[index].record_type == PLATFORM_DNS_RECORD_PTR && entries[index].data[0] != '\0') {
            rt_copy_string(name_out, name_size, entries[index].data);
            return;
        }
    }
}

static void linux_trace_lookup_ipv4_name(const LinuxInAddr *address, char *name_out, size_t name_size) {
    char ptr_name[PLATFORM_NAME_CAPACITY];

    if (name_out == 0 || name_size == 0U) {
        return;
    }
    name_out[0] = '\0';
    if (linux_trace_ipv4_ptr_name(address, ptr_name, sizeof(ptr_name)) == 0) {
        linux_trace_lookup_ptr(ptr_name, name_out, name_size);
    }
}

static void linux_trace_lookup_ipv6_name(const LinuxIn6Addr *address, char *name_out, size_t name_size) {
    char ptr_name[PLATFORM_NAME_CAPACITY];

    if (name_out == 0 || name_size == 0U) {
        return;
    }
    name_out[0] = '\0';
    if (linux_trace_ipv6_ptr_name(address, ptr_name, sizeof(ptr_name)) == 0) {
        linux_trace_lookup_ptr(ptr_name, name_out, name_size);
    }
}

int platform_trace_route(
    const char *host,
    const PlatformTracerouteOptions *options,
    PlatformTracerouteHop *hops_out,
    size_t hop_capacity,
    size_t *hop_count_out
) {
    PlatformTracerouteOptions defaults;
    LinuxInAddr address;
    LinuxIn6Addr address6;
    unsigned int max_ttl;
    unsigned int queries;
    unsigned int timeout_ms;
    unsigned int payload_size;
    unsigned short identifier;
    unsigned int ttl;
    int sock;
    int allow_identifier_rewrite = 0;
    size_t hop_count = 0U;

    if (hop_count_out != 0) {
        *hop_count_out = 0U;
    }
    if (host == 0 || hops_out == 0 || hop_count_out == 0 || hop_capacity == 0U) {
        return -1;
    }
    if (options == 0) {
        defaults.max_ttl = 30U;
        defaults.queries = 3U;
        defaults.timeout_seconds = PLATFORM_PING_DEFAULT_TIMEOUT_SECONDS;
        defaults.payload_size = PLATFORM_PING_DEFAULT_PAYLOAD_SIZE;
        defaults.family = PLATFORM_NETWORK_FAMILY_IPV4;
        defaults.numeric_only = 1;
        defaults.hop_callback = 0;
        defaults.hop_callback_user_data = 0;
        options = &defaults;
    }
    max_ttl = options->max_ttl == 0U ? 30U : options->max_ttl;
    queries = options->queries == 0U ? 1U : options->queries;
    timeout_ms = (options->timeout_seconds == 0U ? PLATFORM_PING_DEFAULT_TIMEOUT_SECONDS : options->timeout_seconds) * 1000U;
    payload_size = options->payload_size > PLATFORM_PING_MAX_PAYLOAD_SIZE ? PLATFORM_PING_MAX_PAYLOAD_SIZE : options->payload_size;
    if (max_ttl > PLATFORM_PING_MAX_TTL || queries > PLATFORM_TRACEROUTE_MAX_QUERIES) {
        return -1;
    }

    if (options->family == PLATFORM_NETWORK_FAMILY_IPV6 ||
        (options->family != PLATFORM_NETWORK_FAMILY_IPV4 && linux_contains_char(host, ':'))) {
        int allow_identifier_rewrite6 = 1;

        if (linux_resolve_ipv6_host(host, &address6) != 0) {
            return -1;
        }
        sock = linux_open_socket(LINUX_AF_INET6, LINUX_SOCK_DGRAM, LINUX_IPPROTO_ICMPV6);
        if (sock < 0) {
            sock = linux_open_socket(LINUX_AF_INET6, LINUX_SOCK_RAW, LINUX_IPPROTO_ICMPV6);
            allow_identifier_rewrite6 = 0;
        }
        if (sock < 0) {
            return -1;
        }
        identifier = (unsigned short)(platform_get_process_id() & 0xffff);

        for (ttl = 1U; ttl <= max_ttl && hop_count < hop_capacity; ++ttl) {
            PlatformTracerouteHop *hop = hops_out + hop_count;
            unsigned int probe;
            int hops_value = (int)ttl;

            rt_memset(hop, 0, sizeof(*hop));
            hop->ttl = ttl;
            hop->probe_count = queries;
            (void)linux_set_socket_int_option(sock, LINUX_IPPROTO_IPV6, LINUX_IPV6_UNICAST_HOPS, hops_value);

            for (probe = 0U; probe < queries; ++probe) {
                unsigned char packet[sizeof(LinuxIcmpv6Packet) + PLATFORM_PING_MAX_PAYLOAD_SIZE];
                unsigned char reply[768];
                LinuxIcmpv6Packet *header = (LinuxIcmpv6Packet *)packet;
                size_t packet_size = sizeof(LinuxIcmpv6Packet) + payload_size;
                unsigned short sequence = (unsigned short)(ttl * 32U + probe);
                unsigned long long start_ms;
                unsigned long long deadline;
                size_t ready_index = 0U;
                unsigned int i;

                rt_memset(packet, 0, packet_size);
                header->type = LINUX_ICMPV6_ECHO_REQUEST;
                header->code = 0U;
                header->identifier = linux_host_to_net16(identifier);
                header->sequence = linux_host_to_net16(sequence);
                for (i = 0U; i < payload_size; ++i) {
                    packet[sizeof(LinuxIcmpv6Packet) + i] = (unsigned char)('A' + (i % 26U));
                }
                header->checksum = 0U;

                start_ms = linux_monotonic_milliseconds();
                if (linux_sendto_ipv6(sock, packet, packet_size, &address6, 0U) < (long)packet_size) {
                    continue;
                }
                deadline = start_ms + timeout_ms;

                for (;;) {
                    int timeout_remaining = (int)(deadline > linux_monotonic_milliseconds() ? deadline - linux_monotonic_milliseconds() : 0ULL);
                    long reply_bytes;
                    size_t offset = 0U;
                    LinuxIcmpv6Packet *reply_icmp6;
                    LinuxIn6Addr peer;
                    int matched = 0;

                    if (platform_poll_fds(&sock, 1U, &ready_index, timeout_remaining) <= 0) {
                        break;
                    }
                    rt_memset(&peer, 0, sizeof(peer));
                    reply_bytes = linux_recvfrom_ipv6(sock, reply, sizeof(reply), &peer);
                    if (reply_bytes <= 0) {
                        break;
                    }
                    if ((size_t)reply_bytes >= 40U && (reply[0] >> 4) == 6U) {
                        offset = 40U;
                    }
                    if ((size_t)reply_bytes < offset + sizeof(LinuxIcmpv6Packet)) {
                        continue;
                    }
                    reply_icmp6 = (LinuxIcmpv6Packet *)(reply + offset);
                    if (reply_icmp6->type == LINUX_ICMPV6_ECHO_REPLY &&
                        linux_net_to_host16(reply_icmp6->sequence) == sequence &&
                        (allow_identifier_rewrite6 || linux_net_to_host16(reply_icmp6->identifier) == identifier)) {
                        matched = 1;
                        hop->reached_destination = 1;
                    } else if (reply_icmp6->type == LINUX_ICMPV6_TIME_EXCEEDED &&
                               linux_trace_match_inner_ipv6(reply, (size_t)reply_bytes, offset, identifier, sequence, allow_identifier_rewrite6)) {
                        matched = 1;
                    }
                    if (!matched) {
                        continue;
                    }

                    hop->probe_replied[probe] = 1U;
                    hop->rtt_milliseconds[probe] = (unsigned int)(linux_monotonic_milliseconds() - start_ms);
                    hop->reply_count += 1U;
                    linux_ipv6_to_text(&peer, hop->address, sizeof(hop->address));
                    if (!options->numeric_only) {
                        linux_trace_lookup_ipv6_name(&peer, hop->hostname, sizeof(hop->hostname));
                    }
                    break;
                }
            }

            if (options->hop_callback != 0) {
                options->hop_callback(hop, options->hop_callback_user_data);
            }
            hop_count += 1U;
            if (hop->reached_destination) {
                break;
            }
        }

        platform_close(sock);
        *hop_count_out = hop_count;
        return 0;
    }

    if (linux_resolve_ipv4_host(host, &address) != 0) {
        return -1;
    }
    sock = linux_open_inet_socket(LINUX_SOCK_RAW, LINUX_IPPROTO_ICMP);
    if (sock < 0) {
        sock = linux_open_inet_socket(LINUX_SOCK_DGRAM, LINUX_IPPROTO_ICMP);
        allow_identifier_rewrite = 1;
    }
    if (sock < 0) {
        return -1;
    }
    (void)linux_set_socket_int_option(sock, LINUX_IPPROTO_IP, LINUX_IP_RECVERR, 1);
    identifier = (unsigned short)(platform_get_process_id() & 0xffff);

    for (ttl = 1U; ttl <= max_ttl && hop_count < hop_capacity; ++ttl) {
        PlatformTracerouteHop *hop = hops_out + hop_count;
        unsigned int probe;
        int ttl_value = (int)ttl;

        rt_memset(hop, 0, sizeof(*hop));
        hop->ttl = ttl;
        hop->probe_count = queries;
        (void)linux_set_socket_int_option(sock, LINUX_IPPROTO_IP, LINUX_IP_TTL, ttl_value);

        for (probe = 0U; probe < queries; ++probe) {
            unsigned char packet[sizeof(LinuxIcmpPacket) + PLATFORM_PING_MAX_PAYLOAD_SIZE];
            unsigned char reply[512];
            LinuxIcmpPacket *header = (LinuxIcmpPacket *)packet;
            size_t packet_size = sizeof(LinuxIcmpPacket) + payload_size;
            unsigned short sequence = (unsigned short)(ttl * 32U + probe);
            unsigned long long start_ms;
            unsigned long long deadline;
            size_t ready_index = 0U;
            unsigned int i;

            rt_memset(packet, 0, packet_size);
            header->type = LINUX_ICMP_ECHO;
            header->code = 0U;
            header->identifier = linux_host_to_net16(identifier);
            header->sequence = linux_host_to_net16(sequence);
            for (i = 0U; i < payload_size; ++i) {
                packet[sizeof(LinuxIcmpPacket) + i] = (unsigned char)('A' + (i % 26U));
            }
            header->checksum = 0U;
            header->checksum = linux_compute_icmp_checksum(packet, packet_size);

            start_ms = linux_monotonic_milliseconds();
            if (linux_sendto_ipv4(sock, packet, packet_size, &address, 0U) < (long)packet_size) {
                continue;
            }
            deadline = start_ms + timeout_ms;

            for (;;) {
                int timeout_remaining = (int)(deadline > linux_monotonic_milliseconds() ? deadline - linux_monotonic_milliseconds() : 0ULL);
                long reply_bytes;
                size_t offset = 0U;
                LinuxIcmpPacket *reply_icmp;
                LinuxInAddr peer;
                int matched = 0;

                if (platform_poll_fds(&sock, 1U, &ready_index, timeout_remaining) <= 0) {
                    break;
                }
                rt_memset(&peer, 0, sizeof(peer));
                reply_bytes = linux_recvfrom_ipv4(sock, reply, sizeof(reply), &peer);
                if (reply_bytes <= 0) {
                    unsigned char error_type = 0U;

                    reply_bytes = linux_recvmsg_ipv4_error(sock, reply, sizeof(reply), &peer, &error_type);
                    if (reply_bytes <= 0 || error_type != LINUX_ICMP_TIME_EXCEEDED ||
                        !linux_trace_match_probe_ipv4(reply, (size_t)reply_bytes, identifier, sequence, allow_identifier_rewrite)) {
                        continue;
                    }
                    matched = 1;
                } else {
                    if ((size_t)reply_bytes >= 20U && (reply[0] >> 4) == 4U) {
                        offset = (size_t)(reply[0] & 0x0fU) * 4U;
                    }
                    if ((size_t)reply_bytes < offset + sizeof(LinuxIcmpPacket)) {
                        continue;
                    }
                    reply_icmp = (LinuxIcmpPacket *)(reply + offset);
                    if (reply_icmp->type == LINUX_ICMP_REPLY &&
                        linux_net_to_host16(reply_icmp->sequence) == sequence &&
                        (allow_identifier_rewrite || linux_net_to_host16(reply_icmp->identifier) == identifier)) {
                        matched = 1;
                        hop->reached_destination = 1;
                    } else if (reply_icmp->type == LINUX_ICMP_TIME_EXCEEDED &&
                               linux_trace_match_inner_ipv4(reply, (size_t)reply_bytes, offset, identifier, sequence, allow_identifier_rewrite)) {
                        matched = 1;
                    }
                }
                if (!matched) {
                    continue;
                }

                hop->probe_replied[probe] = 1U;
                hop->rtt_milliseconds[probe] = (unsigned int)(linux_monotonic_milliseconds() - start_ms);
                hop->reply_count += 1U;
                if (peer.bytes[0] == 0U && peer.bytes[1] == 0U && peer.bytes[2] == 0U && peer.bytes[3] == 0U && offset >= 20U) {
                    peer.bytes[0] = reply[12];
                    peer.bytes[1] = reply[13];
                    peer.bytes[2] = reply[14];
                    peer.bytes[3] = reply[15];
                }
                if (peer.bytes[0] != 0U || peer.bytes[1] != 0U || peer.bytes[2] != 0U || peer.bytes[3] != 0U) {
                    linux_ipv4_to_text(&peer, hop->address, sizeof(hop->address));
                    if (!options->numeric_only) {
                        linux_trace_lookup_ipv4_name(&peer, hop->hostname, sizeof(hop->hostname));
                    }
                }
                break;
            }
        }

        if (options->hop_callback != 0) {
            options->hop_callback(hop, options->hop_callback_user_data);
        }
        hop_count += 1U;
        if (hop->reached_destination) {
            break;
        }
    }

    platform_close(sock);
    *hop_count_out = hop_count;
    return 0;
}
