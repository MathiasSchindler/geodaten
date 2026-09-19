#include "platform.h"
#include "runtime.h"

#define WIN_STD_INPUT_HANDLE  ((unsigned long)-10L)
#define WIN_STD_OUTPUT_HANDLE ((unsigned long)-11L)
#define WIN_STD_ERROR_HANDLE  ((unsigned long)-12L)
#define WIN_INVALID_HANDLE_VALUE ((void *)(long long)-1)
#define WIN_GENERIC_READ 0x80000000UL
#define WIN_GENERIC_WRITE 0x40000000UL
#define WIN_FILE_SHARE_READ 0x00000001UL
#define WIN_FILE_SHARE_WRITE 0x00000002UL
#define WIN_CREATE_NEW 1UL
#define WIN_CREATE_ALWAYS 2UL
#define WIN_OPEN_ALWAYS 4UL
#define WIN_OPEN_EXISTING 3UL
#define WIN_FILE_ATTRIBUTE_NORMAL 0x00000080UL
#define WIN_FILE_ATTRIBUTE_DIRECTORY 0x00000010UL
#define WIN_INVALID_FILE_ATTRIBUTES 0xffffffffUL
#define WIN_MOVEFILE_REPLACE_EXISTING 0x00000001UL
#define WIN_FILE_TYPE_CHAR 0x0002UL
#define WIN_ENABLE_ECHO_INPUT 0x0004UL
#define WIN_ENABLE_LINE_INPUT 0x0002UL
#define WIN_ENABLE_PROCESSED_INPUT 0x0001UL
#define WIN_ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200UL
#define WIN_ENABLE_PROCESSED_OUTPUT 0x0001UL
#define WIN_ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004UL
#define WIN_FD_TABLE_CAPACITY 64
#define WIN_FD_KIND_NONE 0
#define WIN_FD_KIND_FILE 1
#define WIN_FD_KIND_SOCKET 2
#define WIN_AF_UNSPEC 0
#define WIN_SOCK_STREAM 1
#define WIN_IPPROTO_TCP 6
#define WIN_INVALID_SOCKET (~(unsigned long long)0ULL)
#define WIN_SOCKET_ERROR (-1)
#define WIN_FD_SETSIZE 64

typedef unsigned long long WinSocket;

typedef struct WinSockAddr WinSockAddr;

typedef struct WinAddrInfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    char *ai_canonname;
    WinSockAddr *ai_addr;
    struct WinAddrInfo *ai_next;
} WinAddrInfo;

typedef struct {
    unsigned int fd_count;
    WinSocket fd_array[WIN_FD_SETSIZE];
} WinFdSet;

typedef struct {
    long tv_sec;
    long tv_usec;
} WinTimeval;

typedef struct {
    unsigned long low;
    unsigned long high;
} WinFileTime;

typedef struct {
    unsigned short year;
    unsigned short month;
    unsigned short day_of_week;
    unsigned short day;
    unsigned short hour;
    unsigned short minute;
    unsigned short second;
    unsigned short milliseconds;
} WinSystemTime;

typedef struct {
    unsigned short x;
    unsigned short y;
} WinCoord;

typedef struct {
    short left;
    short top;
    short right;
    short bottom;
} WinSmallRect;

typedef struct {
    WinCoord size;
    WinCoord cursor_position;
    unsigned short attributes;
    WinSmallRect window;
    WinCoord maximum_window_size;
} WinConsoleScreenBufferInfo;

typedef struct {
    unsigned long length;
    unsigned long memory_load;
    unsigned long long total_physical;
    unsigned long long available_physical;
    unsigned long long total_page_file;
    unsigned long long available_page_file;
    unsigned long long total_virtual;
    unsigned long long available_virtual;
    unsigned long long available_extended_virtual;
} WinMemoryStatusEx;

__declspec(dllimport) void __stdcall ExitProcess(unsigned int status);
__declspec(dllimport) char *__stdcall GetCommandLineA(void);
__declspec(dllimport) void *__stdcall GetStdHandle(unsigned long handle_id);
__declspec(dllimport) unsigned long __stdcall GetCurrentDirectoryA(unsigned long buffer_length, char *buffer);
__declspec(dllimport) int __stdcall GetComputerNameA(char *buffer, unsigned long *size_io);
__declspec(dllimport) unsigned long __stdcall GetCurrentProcessId(void);
__declspec(dllimport) unsigned long __stdcall GetEnvironmentVariableA(const char *name, char *buffer, unsigned long size);
__declspec(dllimport) int __stdcall SetEnvironmentVariableA(const char *name, const char *value);
__declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);
__declspec(dllimport) unsigned long __stdcall GetFileAttributesA(const char *file_name);
__declspec(dllimport) int __stdcall GetFileSizeEx(void *handle, long long *size_out);
__declspec(dllimport) unsigned long __stdcall GetFileType(void *handle);
__declspec(dllimport) int __stdcall GetConsoleMode(void *handle, unsigned long *mode_out);
__declspec(dllimport) int __stdcall SetConsoleMode(void *handle, unsigned long mode);
__declspec(dllimport) int __stdcall GetConsoleScreenBufferInfo(void *handle, WinConsoleScreenBufferInfo *info_out);
__declspec(dllimport) void __stdcall GetSystemTimeAsFileTime(WinFileTime *file_time_out);
__declspec(dllimport) int __stdcall FileTimeToSystemTime(const WinFileTime *file_time, WinSystemTime *system_time_out);
__declspec(dllimport) int __stdcall FileTimeToLocalFileTime(const WinFileTime *file_time, WinFileTime *local_file_time_out);
__declspec(dllimport) int __stdcall GlobalMemoryStatusEx(WinMemoryStatusEx *status_out);
__declspec(dllimport) int __stdcall GetDiskFreeSpaceExA(const char *directory_name, unsigned long long *free_available_out, unsigned long long *total_out, unsigned long long *free_total_out);
__declspec(dllimport) int __stdcall SetCurrentDirectoryA(const char *path_name);
__declspec(dllimport) int __stdcall SetFilePointerEx(void *handle, long long distance_to_move,
                                                     long long *new_file_pointer, unsigned long move_method);
__declspec(dllimport) int __stdcall SetEndOfFile(void *handle);
__declspec(dllimport) void __stdcall Sleep(unsigned long milliseconds);
__declspec(dllimport) void *__stdcall CreateFileA(const char *file_name, unsigned long desired_access,
                                                   unsigned long share_mode, void *security_attributes,
                                                   unsigned long creation_disposition,
                                                   unsigned long flags_and_attributes, void *template_file);
__declspec(dllimport) int __stdcall CreateDirectoryA(const char *path_name, void *security_attributes);
__declspec(dllimport) int __stdcall DeleteFileA(const char *file_name);
__declspec(dllimport) int __stdcall MoveFileExA(const char *existing_file_name, const char *new_file_name, unsigned long flags);
__declspec(dllimport) int __stdcall CreateHardLinkA(const char *file_name, const char *existing_file_name, void *security_attributes);
__declspec(dllimport) unsigned long __stdcall GetTempPathA(unsigned long buffer_length, char *buffer);
__declspec(dllimport) unsigned int __stdcall GetTempFileNameA(const char *path_name, const char *prefix_string, unsigned int unique, char *temp_file_name);
__declspec(dllimport) int __stdcall FlushFileBuffers(void *handle);
__declspec(dllimport) int __stdcall RemoveDirectoryA(const char *path_name);
__declspec(dllimport) int __stdcall CreatePipe(void **read_pipe, void **write_pipe, void *pipe_attributes, unsigned long size);
__declspec(dllimport) void *__stdcall VirtualAlloc(void *address, size_t size, unsigned long allocation_type, unsigned long protect);
__declspec(dllimport) int __stdcall CloseHandle(void *handle);
__declspec(dllimport) int __stdcall ReadFile(void *handle, void *buffer, unsigned long count, unsigned long *read_out, void *overlapped);
__declspec(dllimport) int __stdcall WriteFile(void *handle, const void *buffer, unsigned long count, unsigned long *written_out, void *overlapped);
__declspec(dllimport) int __stdcall WSAStartup(unsigned short version_requested, void *wsa_data);
__declspec(dllimport) int __stdcall getaddrinfo(const char *node_name, const char *service_name, const WinAddrInfo *hints, WinAddrInfo **result);
__declspec(dllimport) void __stdcall freeaddrinfo(WinAddrInfo *ai);
__declspec(dllimport) WinSocket __stdcall socket(int address_family, int type, int protocol);
__declspec(dllimport) int __stdcall connect(WinSocket s, const WinSockAddr *name, int name_length);
__declspec(dllimport) int __stdcall closesocket(WinSocket s);
__declspec(dllimport) int __stdcall recv(WinSocket s, char *buffer, int length, int flags);
__declspec(dllimport) int __stdcall send(WinSocket s, const char *buffer, int length, int flags);
__declspec(dllimport) int __stdcall select(int nfds, WinFdSet *readfds, WinFdSet *writefds, WinFdSet *exceptfds, WinTimeval *timeout);

unsigned long __stack_chk_guard;

void __main(void) {
}

__attribute__((naked))
void ___chkstk_ms(void) {
    __asm__(
        "pushq %rcx\n"
        "pushq %r10\n"
        "movq %rax, %r10\n"
        "leaq 24(%rsp), %rcx\n"
        "cmpq $0x1000, %rax\n"
        "jb 2f\n"
        "1:\n"
        "subq $0x1000, %rcx\n"
        "testq %rax, (%rcx)\n"
        "subq $0x1000, %rax\n"
        "cmpq $0x1000, %rax\n"
        "jae 1b\n"
        "2:\n"
        "subq %rax, %rcx\n"
        "testq %rax, (%rcx)\n"
        "movq %r10, %rax\n"
        "popq %r10\n"
        "popq %rcx\n"
        "ret\n"
    );
}

static char windows_command_line[4096];
static char *windows_argv[64];
static void *windows_fd_table[WIN_FD_TABLE_CAPACITY];
static unsigned char windows_fd_kind[WIN_FD_TABLE_CAPACITY];
static int windows_winsock_started;
static char windows_env_buffer[4096];

static int windows_copy_string(char *buffer, size_t buffer_size, const char *text) {
    if (buffer == 0 || buffer_size == 0U || text == 0 || rt_strlen(text) + 1U > buffer_size) return -1;
    rt_copy_string(buffer, buffer_size, text);
    return 0;
}

static int windows_append_char(char *buffer, size_t buffer_size, size_t *used, char ch) {
    if (buffer == 0 || used == 0 || *used + 1U >= buffer_size) return -1;
    buffer[*used] = ch;
    *used += 1U;
    buffer[*used] = '\0';
    return 0;
}

static int windows_append_text(char *buffer, size_t buffer_size, size_t *used, const char *text) {
    size_t i = 0U;
    while (text != 0 && text[i] != '\0') {
        if (windows_append_char(buffer, buffer_size, used, text[i]) != 0) return -1;
        i += 1U;
    }
    return 0;
}

static int windows_append_padded_uint(char *buffer, size_t buffer_size, size_t *used, unsigned int value, unsigned int width) {
    char digits[16];
    size_t length;
    rt_unsigned_to_string(value, digits, sizeof(digits));
    length = rt_strlen(digits);
    while (length < width) {
        if (windows_append_char(buffer, buffer_size, used, '0') != 0) return -1;
        length += 1U;
    }
    return windows_append_text(buffer, buffer_size, used, digits);
}

static unsigned long long windows_file_time_to_ticks(const WinFileTime *file_time) {
    return ((unsigned long long)file_time->high << 32) | (unsigned long long)file_time->low;
}

static void windows_ticks_to_file_time(unsigned long long ticks, WinFileTime *file_time) {
    file_time->low = (unsigned long)(ticks & 0xffffffffULL);
    file_time->high = (unsigned long)(ticks >> 32);
}

static int windows_is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int windows_parse_command_line(char *text, char **argv, int argv_capacity) {
    int argc = 0;
    char *cursor = text;

    while (*cursor != '\0' && argc + 1 < argv_capacity) {
        char *out;
        int in_quotes = 0;

        while (windows_is_space(*cursor)) cursor += 1;
        if (*cursor == '\0') break;

        argv[argc] = cursor;
        out = cursor;

        while (*cursor != '\0') {
            if (*cursor == '"') {
                in_quotes = !in_quotes;
                cursor += 1;
                continue;
            }
            if (!in_quotes && windows_is_space(*cursor)) {
                cursor += 1;
                break;
            }
            *out = *cursor;
            out += 1;
            cursor += 1;
        }

        *out = '\0';
        argc += 1;
    }

    argv[argc] = 0;
    return argc;
}

static int windows_startup_args(char ***argv_out) {
    char *command_line = GetCommandLineA();
    size_t length = rt_strlen(command_line);

    if (length >= sizeof(windows_command_line)) {
        length = sizeof(windows_command_line) - 1U;
    }
    memcpy(windows_command_line, command_line, length);
    windows_command_line[length] = '\0';

    *argv_out = windows_argv;
    return windows_parse_command_line(windows_command_line, windows_argv, (int)(sizeof(windows_argv) / sizeof(windows_argv[0])));
}

static void *windows_handle_for_fd(int fd) {
    if (fd == 0) return GetStdHandle(WIN_STD_INPUT_HANDLE);
    if (fd == 1) return GetStdHandle(WIN_STD_OUTPUT_HANDLE);
    if (fd == 2) return GetStdHandle(WIN_STD_ERROR_HANDLE);
    if (fd >= 0 && fd < WIN_FD_TABLE_CAPACITY && windows_fd_kind[fd] == WIN_FD_KIND_FILE) return windows_fd_table[fd];
    return 0;
}

static int windows_fd_is_socket(int fd) {
    return fd >= 0 && fd < WIN_FD_TABLE_CAPACITY && windows_fd_kind[fd] == WIN_FD_KIND_SOCKET;
}

static WinSocket windows_socket_for_fd(int fd) {
    return windows_fd_is_socket(fd) ? (WinSocket)(size_t)windows_fd_table[fd] : WIN_INVALID_SOCKET;
}

static int windows_allocate_fd(void *value, int kind) {
    int fd;
    for (fd = 3; fd < WIN_FD_TABLE_CAPACITY; ++fd) {
        if (windows_fd_kind[fd] == WIN_FD_KIND_NONE) {
            windows_fd_table[fd] = value;
            windows_fd_kind[fd] = (unsigned char)kind;
            return fd;
        }
    }
    return -1;
}

static int windows_winsock_start(void) {
    unsigned char wsa_data[512];
    if (windows_winsock_started) return 0;
    if (WSAStartup(0x0202U, wsa_data) != 0) return -1;
    windows_winsock_started = 1;
    return 0;
}

void *platform_allocate_pages(size_t size) {
    return VirtualAlloc(0, size, 0x3000UL, 0x04UL);
}

long platform_write(int fd, const void *buffer, size_t count) {
    void *handle = windows_handle_for_fd(fd);
    unsigned long chunk;
    unsigned long written = 0;

    if (windows_fd_is_socket(fd)) {
        int socket_chunk;
        int result;
        if (buffer == 0) return -1;
        socket_chunk = count > 0x7fffffffU ? 0x7fffffff : (int)count;
        result = send(windows_socket_for_fd(fd), (const char *)buffer, socket_chunk, 0);
        return result == WIN_SOCKET_ERROR ? -1 : (long)result;
    }

    if (handle == 0) return -1;
    chunk = count > 0xffffffffUL ? 0xffffffffUL : (unsigned long)count;
    if (!WriteFile(handle, buffer, chunk, &written, 0)) return -1;
    return (long)written;
}

long platform_read(int fd, void *buffer, size_t count) {
    void *handle = windows_handle_for_fd(fd);
    unsigned long chunk;
    unsigned long bytes_read = 0;

    if (windows_fd_is_socket(fd)) {
        int socket_chunk;
        int result;
        if (buffer == 0) return -1;
        socket_chunk = count > 0x7fffffffU ? 0x7fffffff : (int)count;
        result = recv(windows_socket_for_fd(fd), (char *)buffer, socket_chunk, 0);
        return result == WIN_SOCKET_ERROR ? -1 : (long)result;
    }

    if (handle == 0) return -1;
    chunk = count > 0xffffffffUL ? 0xffffffffUL : (unsigned long)count;
    if (!ReadFile(handle, buffer, chunk, &bytes_read, 0)) return -1;
    return (long)bytes_read;
}

int platform_open_read(const char *path) {
    void *handle;
    int fd;

    if (path == 0 || (path[0] == '-' && path[1] == '\0')) return 0;

    handle = CreateFileA(path, WIN_GENERIC_READ, WIN_FILE_SHARE_READ, 0,
                         WIN_OPEN_EXISTING, WIN_FILE_ATTRIBUTE_NORMAL, 0);
    if (handle == 0 || handle == WIN_INVALID_HANDLE_VALUE) return -1;

    fd = windows_allocate_fd(handle, WIN_FD_KIND_FILE);
    if (fd >= 0) return fd;

    (void)CloseHandle(handle);
    return -1;
}

int platform_open_write_mode(const char *path, unsigned int mode, int truncate_existing) {
    void *handle;
    unsigned long creation_disposition;
    int fd;

    (void)mode;
    if (path == 0 || (path[0] == '-' && path[1] == '\0')) return 1;

    creation_disposition = truncate_existing ? WIN_CREATE_ALWAYS : WIN_OPEN_ALWAYS;
    handle = CreateFileA(path, WIN_GENERIC_WRITE, WIN_FILE_SHARE_READ | WIN_FILE_SHARE_WRITE, 0,
                         creation_disposition, WIN_FILE_ATTRIBUTE_NORMAL, 0);
    if (handle == 0 || handle == WIN_INVALID_HANDLE_VALUE) return -1;

    fd = windows_allocate_fd(handle, WIN_FD_KIND_FILE);
    if (fd >= 0) return fd;

    (void)CloseHandle(handle);
    return -1;
}

int platform_open_write(const char *path, unsigned int mode) {
    return platform_open_write_mode(path, mode, 1);
}

int platform_open_create_exclusive(const char *path, unsigned int mode) {
    void *handle;
    int fd;

    (void)mode;
    if (path == 0 || path[0] == '\0') return -1;

    handle = CreateFileA(path, WIN_GENERIC_WRITE, WIN_FILE_SHARE_READ, 0,
                         WIN_CREATE_NEW, WIN_FILE_ATTRIBUTE_NORMAL, 0);
    if (handle == 0 || handle == WIN_INVALID_HANDLE_VALUE) return -1;

    fd = windows_allocate_fd(handle, WIN_FD_KIND_FILE);
    if (fd >= 0) return fd;

    (void)CloseHandle(handle);
    return -1;
}

int platform_create_temp_file(char *path_buffer, size_t buffer_size, const char *prefix, unsigned int mode) {
    char temp_dir[260];
    char temp_path[260];
    const char *actual_prefix = "nws";
    unsigned long dir_length;
    void *handle;
    int fd;

    (void)mode;
    if (path_buffer == 0 || buffer_size == 0U) return -1;
    if (prefix != 0 && prefix[0] != '\0') {
        size_t length = rt_strlen(prefix);
        while (length > 0U && (prefix[length - 1U] == '/' || prefix[length - 1U] == '\\')) length -= 1U;
        while (length > 0U && prefix[length - 1U] != '/' && prefix[length - 1U] != '\\') length -= 1U;
        if (prefix[length] != '\0') actual_prefix = prefix + length;
    }

    dir_length = GetTempPathA((unsigned long)sizeof(temp_dir), temp_dir);
    if (dir_length == 0UL || dir_length >= (unsigned long)sizeof(temp_dir)) return -1;
    if (GetTempFileNameA(temp_dir, actual_prefix, 0U, temp_path) == 0U) return -1;
    if (rt_strlen(temp_path) + 1U > buffer_size) {
        (void)DeleteFileA(temp_path);
        return -1;
    }

    handle = CreateFileA(temp_path, WIN_GENERIC_READ | WIN_GENERIC_WRITE, 0, 0,
                         WIN_OPEN_EXISTING, WIN_FILE_ATTRIBUTE_NORMAL, 0);
    if (handle == 0 || handle == WIN_INVALID_HANDLE_VALUE) {
        (void)DeleteFileA(temp_path);
        return -1;
    }

    fd = windows_allocate_fd(handle, WIN_FD_KIND_FILE);
    if (fd < 0) {
        (void)CloseHandle(handle);
        (void)DeleteFileA(temp_path);
        return -1;
    }
    rt_copy_string(path_buffer, buffer_size, temp_path);
    return fd;
}

int platform_open_append(const char *path, unsigned int mode) {
    int fd = platform_open_write_mode(path, mode, 0);
    if (fd >= 0 && platform_seek(fd, 0, PLATFORM_SEEK_END) < 0) {
        (void)platform_close(fd);
        return -1;
    }
    return fd;
}

int platform_open_append_existing(const char *path) {
    void *handle;
    int fd;

    if (path == 0 || (path[0] == '-' && path[1] == '\0')) return 1;

    handle = CreateFileA(path, WIN_GENERIC_WRITE, WIN_FILE_SHARE_READ | WIN_FILE_SHARE_WRITE, 0,
                         WIN_OPEN_EXISTING, WIN_FILE_ATTRIBUTE_NORMAL, 0);
    if (handle == 0 || handle == WIN_INVALID_HANDLE_VALUE) return -1;

    fd = windows_allocate_fd(handle, WIN_FD_KIND_FILE);
    if (fd < 0 || platform_seek(fd, 0, PLATFORM_SEEK_END) < 0) {
        if (fd >= 0) (void)platform_close(fd);
        else (void)CloseHandle(handle);
        return -1;
    }
    return fd;
}

int platform_close(int fd) {
    void *handle;

    if (fd >= 0 && fd <= 2) return 0;
    if (fd < 0 || fd >= WIN_FD_TABLE_CAPACITY || windows_fd_kind[fd] == WIN_FD_KIND_NONE) return -1;

    handle = windows_fd_table[fd];
    windows_fd_table[fd] = 0;
    if (windows_fd_kind[fd] == WIN_FD_KIND_SOCKET) {
        windows_fd_kind[fd] = WIN_FD_KIND_NONE;
        return closesocket((WinSocket)(size_t)handle) == 0 ? 0 : -1;
    }
    windows_fd_kind[fd] = WIN_FD_KIND_NONE;
    return CloseHandle(handle) != 0 ? 0 : -1;
}

long long platform_seek(int fd, long long offset, int whence) {
    void *handle = windows_handle_for_fd(fd);
    unsigned long method;
    long long new_position = 0;

    if (handle == 0) return -1;
    if (whence == PLATFORM_SEEK_SET) {
        method = 0UL;
    } else if (whence == PLATFORM_SEEK_CUR) {
        method = 1UL;
    } else if (whence == PLATFORM_SEEK_END) {
        method = 2UL;
    } else {
        return -1;
    }

    if (!SetFilePointerEx(handle, offset, &new_position, method)) return -1;
    return new_position;
}

const char *platform_getenv(const char *name) {
    unsigned long length;

    if (name == 0 || name[0] == '\0') return 0;
    length = GetEnvironmentVariableA(name, windows_env_buffer, (unsigned long)sizeof(windows_env_buffer));
    if (length == 0UL || length >= (unsigned long)sizeof(windows_env_buffer)) return 0;
    return windows_env_buffer;
}

const char *platform_getenv_entry(size_t index) {
    (void)index;
    return 0;
}

int platform_setenv(const char *name, const char *value, int overwrite) {
    char existing[2];

    if (name == 0 || name[0] == '\0' || value == 0) return -1;
    if (!overwrite && GetEnvironmentVariableA(name, existing, sizeof(existing)) > 0UL) return 0;
    return SetEnvironmentVariableA(name, value) != 0 ? 0 : -1;
}

int platform_unsetenv(const char *name) {
    if (name == 0 || name[0] == '\0') return -1;
    return SetEnvironmentVariableA(name, 0) != 0 ? 0 : -1;
}

int platform_clearenv(void) {
    return 0;
}

int platform_isatty(int fd) {
    void *handle = windows_handle_for_fd(fd);
    unsigned long mode;

    if (handle == 0) return 0;
    if (GetFileType(handle) != WIN_FILE_TYPE_CHAR) return 0;
    return GetConsoleMode(handle, &mode) != 0;
}

int platform_get_terminal_size(int fd, unsigned int *rows_out, unsigned int *columns_out) {
    void *handle = windows_handle_for_fd(fd);
    WinConsoleScreenBufferInfo info;

    if (handle == 0 || rows_out == 0 || columns_out == 0) return -1;
    if (!GetConsoleScreenBufferInfo(handle, &info)) return -1;
    *rows_out = (unsigned int)(info.window.bottom - info.window.top + 1);
    *columns_out = (unsigned int)(info.window.right - info.window.left + 1);
    return 0;
}

int platform_terminal_get_mode(int fd, PlatformTerminalMode *mode_out) {
    void *handle = windows_handle_for_fd(fd);
    unsigned long mode;

    if (handle == 0 || mode_out == 0 || !GetConsoleMode(handle, &mode)) return -1;
    rt_memset(mode_out, 0, sizeof(*mode_out));
    mode_out->echo = (mode & WIN_ENABLE_ECHO_INPUT) != 0UL;
    mode_out->icanon = (mode & WIN_ENABLE_LINE_INPUT) != 0UL;
    mode_out->isig = (mode & WIN_ENABLE_PROCESSED_INPUT) != 0UL;
    mode_out->ixon = 0;
    mode_out->opost = 1;
    (void)platform_get_terminal_size(fd, &mode_out->rows, &mode_out->columns);
    return 0;
}

int platform_terminal_set_mode(int fd, const PlatformTerminalMode *mode_in, unsigned int change_mask) {
    void *handle = windows_handle_for_fd(fd);
    unsigned long mode;

    if (handle == 0 || mode_in == 0 || !GetConsoleMode(handle, &mode)) return -1;
    if ((change_mask & PLATFORM_TERMINAL_ECHO) != 0U) {
        mode = mode_in->echo ? (mode | WIN_ENABLE_ECHO_INPUT) : (mode & ~WIN_ENABLE_ECHO_INPUT);
    }
    if ((change_mask & PLATFORM_TERMINAL_ICANON) != 0U) {
        mode = mode_in->icanon ? (mode | WIN_ENABLE_LINE_INPUT) : (mode & ~WIN_ENABLE_LINE_INPUT);
    }
    if ((change_mask & PLATFORM_TERMINAL_ISIG) != 0U) {
        mode = mode_in->isig ? (mode | WIN_ENABLE_PROCESSED_INPUT) : (mode & ~WIN_ENABLE_PROCESSED_INPUT);
    }
    return SetConsoleMode(handle, mode) != 0 ? 0 : -1;
}

int platform_terminal_enable_raw_mode(int fd, PlatformTerminalState *state_out) {
    void *input_handle = windows_handle_for_fd(fd);
    void *output_handle = windows_handle_for_fd(1);
    unsigned long input_mode;
    unsigned long output_mode;
    unsigned long raw_mode;

    if (input_handle == 0 || state_out == 0 || !GetConsoleMode(input_handle, &input_mode)) return -1;
    rt_memset(state_out, 0, sizeof(*state_out));
    memcpy(state_out->bytes, &input_mode, sizeof(input_mode));

    raw_mode = input_mode;
    raw_mode &= ~(WIN_ENABLE_ECHO_INPUT | WIN_ENABLE_LINE_INPUT);
    raw_mode |= WIN_ENABLE_PROCESSED_INPUT | WIN_ENABLE_VIRTUAL_TERMINAL_INPUT;
    if (!SetConsoleMode(input_handle, raw_mode)) return -1;

    if (output_handle != 0 && GetConsoleMode(output_handle, &output_mode)) {
        output_mode |= WIN_ENABLE_PROCESSED_OUTPUT | WIN_ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        (void)SetConsoleMode(output_handle, output_mode);
    }
    return 0;
}

int platform_terminal_restore_mode(int fd, const PlatformTerminalState *state) {
    void *handle = windows_handle_for_fd(fd);
    unsigned long mode;

    if (handle == 0 || state == 0) return -1;
    memcpy(&mode, state->bytes, sizeof(mode));
    return SetConsoleMode(handle, mode) != 0 ? 0 : -1;
}

int platform_get_process_id(void) {
    return (int)GetCurrentProcessId();
}

long platform_read_kernel_log(char *buffer, size_t buffer_size, int clear_after_read) {
    (void)clear_after_read;
    if (buffer != 0 && buffer_size > 0U) buffer[0] = '\0';
    return 0;
}

int platform_clear_kernel_log(void) {
    return 0;
}

int platform_set_console_log_level(int level) {
    (void)level;
    return -1;
}

long long platform_get_epoch_time(void) {
    WinFileTime file_time;
    unsigned long long ticks;

    GetSystemTimeAsFileTime(&file_time);
    ticks = windows_file_time_to_ticks(&file_time);
    if (ticks < 116444736000000000ULL) return 0;
    return (long long)((ticks - 116444736000000000ULL) / 10000000ULL);
}

unsigned long long platform_get_monotonic_time_ns(void) {
    return GetTickCount64() * 1000000ULL;
}

int platform_get_memory_info(PlatformMemoryInfo *info_out) {
    WinMemoryStatusEx status;

    if (info_out == 0) return -1;
    rt_memset(&status, 0, sizeof(status));
    status.length = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) return -1;
    rt_memset(info_out, 0, sizeof(*info_out));
    info_out->total_bytes = status.total_physical;
    info_out->free_bytes = status.available_physical;
    info_out->available_bytes = status.available_physical;
    info_out->swap_total_bytes = status.total_page_file > status.total_physical ? status.total_page_file - status.total_physical : 0ULL;
    info_out->swap_free_bytes = status.available_page_file > status.available_physical ? status.available_page_file - status.available_physical : 0ULL;
    return 0;
}

int platform_get_uptime_info(PlatformUptimeInfo *info_out) {
    if (info_out == 0) return -1;
    info_out->uptime_seconds = GetTickCount64() / 1000ULL;
    windows_copy_string(info_out->load_average, sizeof(info_out->load_average), "0.00, 0.00, 0.00");
    return 0;
}

static int windows_format_time_component(char token, const WinSystemTime *time, char *buffer, size_t buffer_size, size_t *used) {
    static const char *short_months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    static const char *long_months[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    static const char *short_days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *long_days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

    switch (token) {
        case '%': return windows_append_char(buffer, buffer_size, used, '%');
        case 'n': return windows_append_char(buffer, buffer_size, used, '\n');
        case 't': return windows_append_char(buffer, buffer_size, used, '\t');
        case 'Y': return windows_append_padded_uint(buffer, buffer_size, used, time->year, 4U);
        case 'y': return windows_append_padded_uint(buffer, buffer_size, used, time->year % 100U, 2U);
        case 'm': return windows_append_padded_uint(buffer, buffer_size, used, time->month, 2U);
        case 'd': return windows_append_padded_uint(buffer, buffer_size, used, time->day, 2U);
        case 'e':
            if (time->day < 10U && windows_append_char(buffer, buffer_size, used, ' ') != 0) return -1;
            return windows_append_padded_uint(buffer, buffer_size, used, time->day, time->day < 10U ? 1U : 2U);
        case 'H': return windows_append_padded_uint(buffer, buffer_size, used, time->hour, 2U);
        case 'M': return windows_append_padded_uint(buffer, buffer_size, used, time->minute, 2U);
        case 'S': return windows_append_padded_uint(buffer, buffer_size, used, time->second, 2U);
        case 'F':
            return windows_format_time_component('Y', time, buffer, buffer_size, used) == 0 &&
                   windows_append_char(buffer, buffer_size, used, '-') == 0 &&
                   windows_format_time_component('m', time, buffer, buffer_size, used) == 0 &&
                   windows_append_char(buffer, buffer_size, used, '-') == 0 &&
                   windows_format_time_component('d', time, buffer, buffer_size, used) == 0 ? 0 : -1;
        case 'T':
            return windows_format_time_component('H', time, buffer, buffer_size, used) == 0 &&
                   windows_append_char(buffer, buffer_size, used, ':') == 0 &&
                   windows_format_time_component('M', time, buffer, buffer_size, used) == 0 &&
                   windows_append_char(buffer, buffer_size, used, ':') == 0 &&
                   windows_format_time_component('S', time, buffer, buffer_size, used) == 0 ? 0 : -1;
        case 'R':
            return windows_format_time_component('H', time, buffer, buffer_size, used) == 0 &&
                   windows_append_char(buffer, buffer_size, used, ':') == 0 &&
                   windows_format_time_component('M', time, buffer, buffer_size, used) == 0 ? 0 : -1;
        case 'b': return time->month >= 1U && time->month <= 12U ? windows_append_text(buffer, buffer_size, used, short_months[time->month - 1U]) : -1;
        case 'B': return time->month >= 1U && time->month <= 12U ? windows_append_text(buffer, buffer_size, used, long_months[time->month - 1U]) : -1;
        case 'a': return time->day_of_week < 7U ? windows_append_text(buffer, buffer_size, used, short_days[time->day_of_week]) : -1;
        case 'A': return time->day_of_week < 7U ? windows_append_text(buffer, buffer_size, used, long_days[time->day_of_week]) : -1;
        default:
            if (windows_append_char(buffer, buffer_size, used, '%') != 0) return -1;
            return windows_append_char(buffer, buffer_size, used, token);
    }
}

int platform_format_time(long long epoch_seconds, int use_local_time, const char *format, char *buffer, size_t buffer_size) {
    const char *actual_format = (format != 0 && format[0] != '\0') ? format : "%Y-%m-%d %H:%M:%S";
    unsigned long long ticks = (unsigned long long)epoch_seconds * 10000000ULL + 116444736000000000ULL;
    WinFileTime utc_file_time;
    WinFileTime local_file_time;
    WinSystemTime system_time;
    size_t used = 0U;
    size_t i;

    if (buffer == 0 || buffer_size == 0U || epoch_seconds < 0) return -1;
    buffer[0] = '\0';
    windows_ticks_to_file_time(ticks, &utc_file_time);
    if (use_local_time) {
        if (!FileTimeToLocalFileTime(&utc_file_time, &local_file_time)) return -1;
    } else {
        local_file_time = utc_file_time;
    }
    if (!FileTimeToSystemTime(&local_file_time, &system_time)) return -1;

    for (i = 0U; actual_format[i] != '\0'; ++i) {
        if (actual_format[i] == '%' && actual_format[i + 1U] != '\0') {
            i += 1U;
            if (windows_format_time_component(actual_format[i], &system_time, buffer, buffer_size, &used) != 0) return -1;
        } else if (windows_append_char(buffer, buffer_size, &used, actual_format[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

int platform_parse_signal_name(const char *text, int *signal_out) {
    (void)text;
    (void)signal_out;
    return -1;
}

const char *platform_signal_name(int signal_number) {
    (void)signal_number;
    return 0;
}

void platform_write_signal_list(int fd) {
    (void)fd;
}

int platform_ignore_signal(int signal_number) {
    (void)signal_number;
    return 0;
}

int platform_send_signal(int pid, int signal_number) {
    (void)pid;
    (void)signal_number;
    return -1;
}

int platform_shutdown_system(int action) {
    (void)action;
    return -1;
}

int platform_poll_fds(const int *fds, size_t fd_count, size_t *ready_index_out, int timeout_milliseconds) {
    WinFdSet readfds;
    WinTimeval timeout;
    WinTimeval *timeout_ptr = &timeout;
    size_t indexes[WIN_FD_SETSIZE];
    size_t i;
    int result;

    if (fds == 0 || fd_count == 0U || ready_index_out == 0) return -1;
    if (windows_winsock_start() != 0) return -1;

    readfds.fd_count = 0U;
    for (i = 0U; i < fd_count && readfds.fd_count < WIN_FD_SETSIZE; ++i) {
        if (windows_fd_is_socket(fds[i])) {
            indexes[readfds.fd_count] = i;
            readfds.fd_array[readfds.fd_count] = windows_socket_for_fd(fds[i]);
            readfds.fd_count += 1U;
        } else if (fds[i] >= 0) {
            *ready_index_out = i;
            return 1;
        }
    }
    if (readfds.fd_count == 0U) return -1;

    if (timeout_milliseconds < 0) {
        timeout_ptr = 0;
    } else {
        timeout.tv_sec = timeout_milliseconds / 1000;
        timeout.tv_usec = (long)(timeout_milliseconds % 1000) * 1000L;
    }

    result = select(0, &readfds, 0, 0, timeout_ptr);
    if (result <= 0) return result;
    if (readfds.fd_count > 0U) {
        WinSocket ready_socket = readfds.fd_array[0];
        for (i = 0U; i < fd_count; ++i) {
            if (windows_fd_is_socket(fds[i]) && windows_socket_for_fd(fds[i]) == ready_socket) {
                *ready_index_out = i;
                return 1;
            }
        }
        *ready_index_out = indexes[0];
        return 1;
    }
    return 0;
}

int platform_connect_tcp(const char *host, unsigned int port, int *socket_fd_out) {
    WinAddrInfo hints;
    WinAddrInfo *results = 0;
    WinAddrInfo *current;
    WinSocket sock = WIN_INVALID_SOCKET;
    char port_text[16];
    int fd;

    if (host == 0 || host[0] == '\0' || socket_fd_out == 0 || port == 0U || port > 65535U) return -1;
    if (windows_winsock_start() != 0) return -1;

    rt_memset(&hints, 0, sizeof(hints));
    hints.ai_family = WIN_AF_UNSPEC;
    hints.ai_socktype = WIN_SOCK_STREAM;
    hints.ai_protocol = WIN_IPPROTO_TCP;
    rt_unsigned_to_string(port, port_text, sizeof(port_text));

    if (getaddrinfo(host, port_text, &hints, &results) != 0) return -1;
    for (current = results; current != 0; current = current->ai_next) {
        sock = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (sock == WIN_INVALID_SOCKET) continue;
        if (connect(sock, current->ai_addr, (int)current->ai_addrlen) == 0) break;
        (void)closesocket(sock);
        sock = WIN_INVALID_SOCKET;
    }

    freeaddrinfo(results);
    if (sock == WIN_INVALID_SOCKET) return -1;
    fd = windows_allocate_fd((void *)(size_t)sock, WIN_FD_KIND_SOCKET);
    if (fd < 0) {
        (void)closesocket(sock);
        return -1;
    }

    *socket_fd_out = fd;
    return 0;
}

int platform_open_tcp_listener(const char *host, unsigned int port, int *socket_fd_out) {
    (void)host;
    (void)port;
    (void)socket_fd_out;
    return -1;
}

int platform_accept_tcp(int listener_fd, int *client_fd_out) {
    (void)listener_fd;
    (void)client_fd_out;
    return -1;
}

int platform_netcat(const char *host, unsigned int port, const PlatformNetcatOptions *options) {
    (void)host;
    (void)port;
    if (options != 0 && options->connect_status_out != 0) {
        *options->connect_status_out = PLATFORM_CONNECT_STATUS_ERROR;
    }
    return -1;
}

int platform_netcat_tcp(const char *host, unsigned int port, int listen_mode) {
    PlatformNetcatOptions options;

    rt_memset(&options, 0, sizeof(options));
    options.listen_mode = listen_mode;
    return platform_netcat(host, port, &options);
}

int platform_sleep_milliseconds(unsigned long long milliseconds) {
    while (milliseconds > 0xffffffffULL) {
        Sleep(0xffffffffUL);
        milliseconds -= 0xffffffffULL;
    }
    Sleep((unsigned long)milliseconds);
    return 0;
}

int platform_sleep_seconds(unsigned int seconds) {
    return platform_sleep_milliseconds((unsigned long long)seconds * 1000ULL);
}

int platform_get_current_directory(char *buffer, size_t buffer_size) {
    unsigned long length;

    if (buffer == 0 || buffer_size == 0U) return -1;
    if (buffer_size > 0xffffffffUL) buffer_size = 0xffffffffUL;

    length = GetCurrentDirectoryA((unsigned long)buffer_size, buffer);
    if (length == 0UL || length >= (unsigned long)buffer_size) return -1;
    return 0;
}

int platform_get_hostname(char *buffer, size_t buffer_size) {
    unsigned long length;

    if (buffer == 0 || buffer_size == 0U) return -1;
    if (buffer_size > 0xffffffffUL) buffer_size = 0xffffffffUL;

    length = (unsigned long)buffer_size;
    if (!GetComputerNameA(buffer, &length)) return -1;
    if (length >= (unsigned long)buffer_size) return -1;
    buffer[length] = '\0';
    return 0;
}

int platform_set_hostname(const char *name) {
    (void)name;
    return -1;
}

int platform_path_access(const char *path, int mode) {
    unsigned long attributes;

    (void)mode;
    if (path == 0 || path[0] == '\0') return -1;
    attributes = GetFileAttributesA(path);
    return attributes == WIN_INVALID_FILE_ATTRIBUTES ? -1 : 0;
}

int platform_change_directory(const char *path) {
    if (path == 0 || path[0] == '\0') return -1;
    return SetCurrentDirectoryA(path) != 0 ? 0 : -1;
}

int platform_make_directory(const char *path, unsigned int mode) {
    (void)mode;
    if (path == 0 || path[0] == '\0') return -1;
    return CreateDirectoryA(path, 0) != 0 ? 0 : -1;
}

int platform_mount_filesystem(
    const char *source,
    const char *target,
    const char *filesystem_type,
    unsigned long long flags,
    const char *data
) {
    (void)source;
    (void)target;
    (void)filesystem_type;
    (void)flags;
    (void)data;
    return -1;
}

int platform_unmount_filesystem(const char *target, int force, int lazy) {
    (void)target;
    (void)force;
    (void)lazy;
    return -1;
}

int platform_remove_file(const char *path) {
    if (path == 0 || path[0] == '\0') return -1;
    return DeleteFileA(path) != 0 ? 0 : -1;
}

int platform_remove_directory(const char *path) {
    if (path == 0 || path[0] == '\0') return -1;
    return RemoveDirectoryA(path) != 0 ? 0 : -1;
}

int platform_rename_path(const char *old_path, const char *new_path) {
    if (old_path == 0 || old_path[0] == '\0' || new_path == 0 || new_path[0] == '\0') return -1;
    return MoveFileExA(old_path, new_path, WIN_MOVEFILE_REPLACE_EXISTING) != 0 ? 0 : -1;
}

int platform_create_hard_link(const char *target_path, const char *link_path) {
    if (target_path == 0 || link_path == 0 || target_path[0] == '\0' || link_path[0] == '\0') return -1;
    return CreateHardLinkA(link_path, target_path, 0) != 0 ? 0 : -1;
}

int platform_create_symbolic_link(const char *target_path, const char *link_path) {
    (void)target_path;
    (void)link_path;
    return -1;
}

int platform_create_node(const char *path, unsigned int node_type, unsigned int mode, unsigned int major, unsigned int minor) {
    (void)path;
    (void)node_type;
    (void)mode;
    (void)major;
    (void)minor;
    return -1;
}

int platform_change_mode(const char *path, unsigned int mode) {
    (void)mode;
    return platform_path_access(path, 0) == 0 ? 0 : -1;
}

int platform_change_owner_ex(const char *path, unsigned int uid, unsigned int gid, int follow_symlinks) {
    (void)uid;
    (void)gid;
    (void)follow_symlinks;
    return platform_path_access(path, 0) == 0 ? 0 : -1;
}

int platform_change_owner(const char *path, unsigned int uid, unsigned int gid) {
    return platform_change_owner_ex(path, uid, gid, 1);
}

int platform_touch_path(const char *path) {
    int fd;

    if (path == 0 || path[0] == '\0') return -1;
    fd = platform_open_append(path, 0644U);
    if (fd < 0) return -1;
    return platform_close(fd);
}

int platform_set_path_times(
    const char *path,
    long long atime,
    long long mtime,
    int create_if_missing,
    int update_access,
    int update_modify
) {
    (void)atime;
    (void)mtime;
    (void)update_access;
    (void)update_modify;
    if (path == 0 || path[0] == '\0') return -1;
    if (platform_path_access(path, PLATFORM_ACCESS_EXISTS) == 0) return 0;
    return create_if_missing ? platform_touch_path(path) : -1;
}

int platform_truncate_path(const char *path, unsigned long long size) {
    int fd;
    void *handle;
    int ok;

    fd = platform_open_append_existing(path);
    if (fd < 0) return -1;
    handle = windows_handle_for_fd(fd);
    ok = handle != 0 && platform_seek(fd, (long long)size, PLATFORM_SEEK_SET) >= 0 && SetEndOfFile(handle) != 0;
    (void)platform_close(fd);
    return ok ? 0 : -1;
}

int platform_sync_all(void) {
    return 0;
}

int platform_sync_path(const char *path) {
    int fd;
    void *handle;
    int ok;

    fd = platform_open_append_existing(path);
    if (fd < 0) return -1;
    handle = windows_handle_for_fd(fd);
    ok = handle != 0 && FlushFileBuffers(handle) != 0;
    (void)platform_close(fd);
    return ok ? 0 : -1;
}

int platform_sync_path_data(const char *path) {
    return platform_sync_path(path);
}

int platform_get_path_info(const char *path, PlatformDirEntry *entry_out) {
    unsigned long attributes;
    void *handle;
    long long file_size = 0;
    int is_dir;

    if (path == 0 || path[0] == '\0' || entry_out == 0) return -1;
    attributes = GetFileAttributesA(path);
    if (attributes == WIN_INVALID_FILE_ATTRIBUTES) return -1;

    rt_memset(entry_out, 0, sizeof(*entry_out));
    is_dir = (attributes & WIN_FILE_ATTRIBUTE_DIRECTORY) != 0UL;
    entry_out->is_dir = is_dir;
    entry_out->mode = is_dir ? 0040755U : 0100644U;
    entry_out->nlink = 1UL;

    if (!is_dir) {
        handle = CreateFileA(path, WIN_GENERIC_READ, WIN_FILE_SHARE_READ | WIN_FILE_SHARE_WRITE, 0,
                             WIN_OPEN_EXISTING, WIN_FILE_ATTRIBUTE_NORMAL, 0);
        if (handle != 0 && handle != WIN_INVALID_HANDLE_VALUE) {
            if (GetFileSizeEx(handle, &file_size) != 0 && file_size >= 0) {
                entry_out->size = (unsigned long long)file_size;
            }
            (void)CloseHandle(handle);
        }
    }
    return 0;
}

int platform_get_path_info_follow(const char *path, PlatformDirEntry *entry_out) {
    return platform_get_path_info(path, entry_out);
}

int platform_open_read_secure(const char *path, PlatformDirEntry *entry_out) {
    if (platform_get_path_info(path, entry_out) != 0) return -1;
    return platform_open_read(path);
}

int platform_get_filesystem_info(const char *path, PlatformFilesystemInfo *info_out) {
    unsigned long long free_available = 0ULL;
    unsigned long long total = 0ULL;
    unsigned long long free_total = 0ULL;

    if (path == 0 || info_out == 0) return -1;
    if (!GetDiskFreeSpaceExA(path, &free_available, &total, &free_total)) return -1;
    rt_memset(info_out, 0, sizeof(*info_out));
    info_out->total_bytes = total;
    info_out->free_bytes = free_total;
    info_out->available_bytes = free_available;
    windows_copy_string(info_out->type_name, sizeof(info_out->type_name), "windows");
    return 0;
}

int platform_get_filesystem_usage(const char *path, unsigned long long *total_bytes_out, unsigned long long *free_bytes_out, unsigned long long *available_bytes_out) {
    PlatformFilesystemInfo info;

    if (total_bytes_out == 0 || free_bytes_out == 0 || available_bytes_out == 0) return -1;
    if (platform_get_filesystem_info(path, &info) != 0) return -1;
    *total_bytes_out = info.total_bytes;
    *free_bytes_out = info.free_bytes;
    *available_bytes_out = info.available_bytes;
    return 0;
}

int platform_path_is_directory(const char *path, int *is_directory_out) {
    unsigned long attributes;

    if (path == 0 || path[0] == '\0' || is_directory_out == 0) return -1;
    attributes = GetFileAttributesA(path);
    if (attributes == WIN_INVALID_FILE_ATTRIBUTES) return -1;
    *is_directory_out = (attributes & WIN_FILE_ATTRIBUTE_DIRECTORY) != 0UL;
    return 0;
}

int platform_collect_entries(
    const char *path,
    int include_hidden,
    PlatformDirEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out,
    int *path_is_directory
) {
    (void)path;
    (void)include_hidden;
    (void)entries_out;
    (void)entry_capacity;
    if (count_out != 0) *count_out = 0;
    if (path_is_directory != 0) *path_is_directory = 0;
    return -1;
}

void platform_free_entries(PlatformDirEntry *entries, size_t count) {
    (void)entries;
    (void)count;
}

void platform_format_mode(unsigned int mode, char out[11]) {
    out[0] = (mode & 0040000U) != 0U ? 'd' : '-';
    out[1] = (mode & 0400U) != 0U ? 'r' : '-';
    out[2] = (mode & 0200U) != 0U ? 'w' : '-';
    out[3] = (mode & 0100U) != 0U ? 'x' : '-';
    out[4] = (mode & 0040U) != 0U ? 'r' : '-';
    out[5] = (mode & 0020U) != 0U ? 'w' : '-';
    out[6] = (mode & 0010U) != 0U ? 'x' : '-';
    out[7] = (mode & 0004U) != 0U ? 'r' : '-';
    out[8] = (mode & 0002U) != 0U ? 'w' : '-';
    out[9] = (mode & 0001U) != 0U ? 'x' : '-';
    out[10] = '\0';
}

int platform_stream_file_to_stdout(const char *path) {
    char buffer[4096];
    int fd;
    long bytes_read;

    fd = platform_open_read(path);
    if (fd < 0) return -1;
    while ((bytes_read = platform_read(fd, buffer, sizeof(buffer))) > 0) {
        if (rt_write_all(1, buffer, (size_t)bytes_read) != 0) {
            (void)platform_close(fd);
            return -1;
        }
    }
    (void)platform_close(fd);
    return bytes_read < 0 ? -1 : 0;
}

int platform_get_identity(PlatformIdentity *identity_out) {
    const char *username;

    if (identity_out == 0) return -1;
    rt_memset(identity_out, 0, sizeof(*identity_out));
    username = platform_getenv("USERNAME");
    if (username == 0) username = platform_getenv("USER");
    if (username == 0 || username[0] == '\0') username = "user";
    identity_out->uid = 1000U;
    identity_out->gid = 1000U;
    if (windows_copy_string(identity_out->username, sizeof(identity_out->username), username) != 0) return -1;
    return windows_copy_string(identity_out->groupname, sizeof(identity_out->groupname), "Users");
}

int platform_lookup_identity(const char *username, PlatformIdentity *identity_out) {
    if (platform_get_identity(identity_out) != 0) return -1;
    if (username == 0 || username[0] == '\0') return 0;
    if (rt_strcmp(username, identity_out->username) != 0) {
        if (windows_copy_string(identity_out->username, sizeof(identity_out->username), username) != 0) return -1;
    }
    return 0;
}

int platform_lookup_group(const char *groupname, unsigned int *gid_out) {
    if (gid_out == 0) return -1;
    (void)groupname;
    *gid_out = 1000U;
    return 0;
}

int platform_list_groups_for_identity(const PlatformIdentity *identity, PlatformGroupEntry *entries_out, size_t entry_capacity, size_t *count_out) {
    (void)identity;
    if (count_out == 0) return -1;
    *count_out = 0U;
    if (entries_out == 0 || entry_capacity == 0U) return 0;
    entries_out[0].gid = 1000U;
    if (windows_copy_string(entries_out[0].name, sizeof(entries_out[0].name), "Users") != 0) return -1;
    *count_out = 1U;
    return 0;
}

int platform_list_sessions(PlatformSessionEntry *entries_out, size_t entry_capacity, size_t *count_out) {
    (void)entries_out;
    (void)entry_capacity;
    if (count_out == 0) return -1;
    *count_out = 0U;
    return 0;
}

int platform_spawn_process(
    char *const argv[],
    int stdin_fd,
    int stdout_fd,
    const char *input_path,
    const char *output_path,
    int output_append,
    int *pid_out
) {
    (void)argv;
    (void)stdin_fd;
    (void)stdout_fd;
    (void)input_path;
    (void)output_path;
    (void)output_append;
    (void)pid_out;
    return -1;
}

int platform_spawn_process_ex(
    char *const argv[],
    int stdin_fd,
    int stdout_fd,
    const char *input_path,
    const char *output_path,
    int output_append,
    const char *working_directory,
    const char *drop_user,
    const char *drop_group,
    int *pid_out
) {
    (void)working_directory;
    (void)drop_user;
    (void)drop_group;
    return platform_spawn_process(argv, stdin_fd, stdout_fd, input_path, output_path, output_append, pid_out);
}

int platform_trace_syscalls(char *const argv[], PlatformSyscallTraceCallback callback, void *user_data, int *exit_status_out) {
    (void)argv;
    (void)callback;
    (void)user_data;
    if (exit_status_out != 0) *exit_status_out = 1;
    return -1;
}

int platform_wait_process(int pid, int *exit_status_out) {
    (void)pid;
    (void)exit_status_out;
    return -1;
}

int platform_poll_process_exit(int pid, int *finished_out, int *exit_status_out) {
    (void)pid;
    if (finished_out != 0) *finished_out = 0;
    if (exit_status_out != 0) *exit_status_out = 0;
    return -1;
}

int platform_wait_process_timeout(
    int pid,
    unsigned long long timeout_milliseconds,
    unsigned long long kill_after_milliseconds,
    int signal_number,
    int preserve_status,
    int *exit_status_out
) {
    (void)timeout_milliseconds;
    (void)kill_after_milliseconds;
    (void)signal_number;
    (void)preserve_status;
    return platform_wait_process(pid, exit_status_out);
}

int platform_create_pipe(int pipe_fds[2]) {
    void *read_pipe = 0;
    void *write_pipe = 0;
    int read_fd;
    int write_fd;

    if (pipe_fds == 0) return -1;
    if (CreatePipe(&read_pipe, &write_pipe, 0, 0) == 0) return -1;
    read_fd = windows_allocate_fd(read_pipe, WIN_FD_KIND_FILE);
    write_fd = windows_allocate_fd(write_pipe, WIN_FD_KIND_FILE);
    if (read_fd < 0 || write_fd < 0) {
        if (read_fd >= 0) (void)platform_close(read_fd); else (void)CloseHandle(read_pipe);
        if (write_fd >= 0) (void)platform_close(write_fd); else (void)CloseHandle(write_pipe);
        return -1;
    }
    pipe_fds[0] = read_fd;
    pipe_fds[1] = write_fd;
    return 0;
}

int platform_drop_privileges(const char *username, const char *groupname) {
    (void)username;
    (void)groupname;
    return -1;
}

int platform_list_processes(PlatformProcessEntry *entries_out, size_t entry_capacity, size_t *count_out) {
    (void)entries_out;
    (void)entry_capacity;
    if (count_out == 0) return -1;
    *count_out = 0U;
    return 0;
}

int platform_list_network_links(PlatformNetworkLink *entries_out, size_t entry_capacity, size_t *count_out) {
    (void)entries_out;
    (void)entry_capacity;
    if (count_out == 0) return -1;
    *count_out = 0U;
    return 0;
}

int platform_list_network_addresses(
    PlatformNetworkAddress *entries_out,
    size_t entry_capacity,
    size_t *count_out,
    int family_filter,
    const char *ifname_filter
) {
    (void)entries_out;
    (void)entry_capacity;
    (void)family_filter;
    (void)ifname_filter;
    if (count_out == 0) return -1;
    *count_out = 0U;
    return 0;
}

int platform_list_network_routes(
    PlatformRouteEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out,
    int family_filter,
    const char *ifname_filter
) {
    (void)entries_out;
    (void)entry_capacity;
    (void)family_filter;
    (void)ifname_filter;
    if (count_out == 0) return -1;
    *count_out = 0U;
    return 0;
}

int platform_network_link_set(const char *ifname, int want_up, unsigned int mtu_value, int set_mtu) {
    (void)ifname;
    (void)want_up;
    (void)mtu_value;
    (void)set_mtu;
    return -1;
}

int platform_network_address_change(const char *ifname, const char *cidr, int add) {
    (void)ifname;
    (void)cidr;
    (void)add;
    return -1;
}

int platform_network_route_change(const char *destination, const char *gateway, const char *ifname, int add) {
    (void)destination;
    (void)gateway;
    (void)ifname;
    (void)add;
    return -1;
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
    (void)server;
    (void)port;
    (void)name;
    (void)family_filter;
    (void)entries_out;
    (void)entry_capacity;
    if (count_out != 0) *count_out = 0U;
    return -1;
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
    (void)record_type;
    return platform_dns_lookup(server, port, name, PLATFORM_NETWORK_FAMILY_ANY, entries_out, entry_capacity, count_out);
}

int platform_dhcp_request(
    const char *ifname,
    const char *server,
    unsigned int server_port,
    unsigned int client_port,
    unsigned int timeout_milliseconds,
    PlatformDhcpLease *lease_out
) {
    (void)ifname;
    (void)server;
    (void)server_port;
    (void)client_port;
    (void)timeout_milliseconds;
    if (lease_out != 0) rt_memset(lease_out, 0, sizeof(*lease_out));
    return -1;
}

int platform_list_sockets(PlatformSocketEntry *entries_out, size_t entry_capacity, size_t *count_out, int include_tcp, int include_udp, int listening_only) {
    (void)entries_out;
    (void)entry_capacity;
    (void)include_tcp;
    (void)include_udp;
    (void)listening_only;
    if (count_out != 0) *count_out = 0U;
    return -1;
}

int platform_ping_host(const char *host, const PlatformPingOptions *options) {
    (void)host;
    (void)options;
    return -1;
}

int platform_trace_route(
    const char *host,
    const PlatformTracerouteOptions *options,
    PlatformTracerouteHop *hops_out,
    size_t hop_capacity,
    size_t *hop_count_out
) {
    (void)host;
    (void)options;
    (void)hops_out;
    (void)hop_capacity;
    if (hop_count_out != 0) *hop_count_out = 0U;
    return -1;
}

int platform_read_symlink(const char *path, char *buffer, size_t buffer_size) {
    (void)path;
    (void)buffer;
    (void)buffer_size;
    return -1;
}

int platform_get_uname(
    char *sysname,
    size_t sysname_size,
    char *nodename,
    size_t nodename_size,
    char *release,
    size_t release_size,
    char *version,
    size_t version_size,
    char *machine,
    size_t machine_size
) {
    if (windows_copy_string(sysname, sysname_size, "Windows") != 0) return -1;
    if (platform_get_hostname(nodename, nodename_size) != 0) return -1;
    if (windows_copy_string(release, release_size, "NT") != 0) return -1;
    if (windows_copy_string(version, version_size, "freestanding") != 0) return -1;
    if (windows_copy_string(machine, machine_size, "x86_64") != 0) return -1;
    return 0;
}

__attribute__((noreturn, no_stack_protector))
void __stack_chk_fail(void) {
    static const char message[] = "newos: stack check failure\r\n";

    (void)platform_write(2, message, sizeof(message) - 1U);
    ExitProcess(127);
    for (;;) {
    }
}

__attribute__((no_stack_protector))
void __newos_stack_guard_init(long argc, char **argv) {
    unsigned long guard = 0xf00dfeedUL;

    guard ^= (unsigned long)(size_t)&guard;
    guard ^= (unsigned long)(size_t)argv;
    guard ^= (unsigned long)argc * 0x9e3779b97f4a7c15UL;
    guard &= ~0xffUL;
    __stack_chk_guard = guard == 0 ? 0xf00dfeedUL : guard;
}

int main(int argc, char **argv);

__attribute__((noreturn, no_stack_protector))
void mainCRTStartup(void) {
    char **argv;
    int argc = windows_startup_args(&argv);
    int status;

    __newos_stack_guard_init(argc, argv);
    status = main(argc, argv);
    ExitProcess((unsigned int)status);
    for (;;) {
    }
}
