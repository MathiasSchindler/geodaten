#ifndef NEWOS_PLATFORM_H
#define NEWOS_PLATFORM_H

#include <stddef.h>

#define PLATFORM_NAME_CAPACITY 256
#define PLATFORM_OWNER_CAPACITY 32
#define PLATFORM_GROUP_CAPACITY 32
#define PLATFORM_TERMINAL_STATE_CAPACITY 128
#define PLATFORM_TLS_OPAQUE_WORDS 8
#define PLATFORM_PING_DEFAULT_COUNT 4U
#define PLATFORM_PING_DEFAULT_INTERVAL_SECONDS 1U
#define PLATFORM_PING_DEFAULT_TIMEOUT_SECONDS 1U
#define PLATFORM_PING_DEFAULT_PAYLOAD_SIZE 56U
#define PLATFORM_PING_MAX_PAYLOAD_SIZE 1400U
#define PLATFORM_PING_MAX_TTL 255U
#define PLATFORM_TRACEROUTE_MAX_QUERIES 10U
#define PLATFORM_NETWORK_TEXT_CAPACITY 64
#define PLATFORM_NETWORK_FAMILY_ANY 0
#define PLATFORM_NETWORK_FAMILY_IPV4 4
#define PLATFORM_NETWORK_FAMILY_IPV6 6
#define PLATFORM_CONNECT_STATUS_OPEN 0
#define PLATFORM_CONNECT_STATUS_CLOSED 1
#define PLATFORM_CONNECT_STATUS_FILTERED 2
#define PLATFORM_CONNECT_STATUS_UNREACHABLE 3
#define PLATFORM_CONNECT_STATUS_ERROR 4
#define PLATFORM_DNS_RECORD_A 1U
#define PLATFORM_DNS_RECORD_NS 2U
#define PLATFORM_DNS_RECORD_CNAME 5U
#define PLATFORM_DNS_RECORD_PTR 12U
#define PLATFORM_DNS_RECORD_MX 15U
#define PLATFORM_DNS_RECORD_TXT 16U
#define PLATFORM_DNS_RECORD_AAAA 28U
#define PLATFORM_NETWORK_FLAG_UP        (1U << 0)
#define PLATFORM_NETWORK_FLAG_BROADCAST (1U << 1)
#define PLATFORM_NETWORK_FLAG_LOOPBACK  (1U << 2)
#define PLATFORM_NETWORK_FLAG_RUNNING   (1U << 3)
#define PLATFORM_NETWORK_FLAG_MULTICAST (1U << 4)
#define PLATFORM_ACCESS_EXISTS 0
#define PLATFORM_ACCESS_EXECUTE 1
#define PLATFORM_ACCESS_WRITE 2
#define PLATFORM_ACCESS_READ 4
#define PLATFORM_SEEK_SET 0
#define PLATFORM_SEEK_CUR 1
#define PLATFORM_SEEK_END 2
#define PLATFORM_MOUNT_RDONLY     (1ULL << 0)
#define PLATFORM_MOUNT_NOSUID     (1ULL << 1)
#define PLATFORM_MOUNT_NODEV      (1ULL << 2)
#define PLATFORM_MOUNT_NOEXEC     (1ULL << 3)
#define PLATFORM_MOUNT_SYNC       (1ULL << 4)
#define PLATFORM_MOUNT_REMOUNT    (1ULL << 5)
#define PLATFORM_MOUNT_MANDLOCK   (1ULL << 6)
#define PLATFORM_MOUNT_DIRSYNC    (1ULL << 7)
#define PLATFORM_MOUNT_NOATIME    (1ULL << 8)
#define PLATFORM_MOUNT_NODIRATIME (1ULL << 9)
#define PLATFORM_MOUNT_BIND       (1ULL << 10)
#define PLATFORM_MOUNT_REC        (1ULL << 11)
#define PLATFORM_MOUNT_SILENT     (1ULL << 12)
#define PLATFORM_MOUNT_RELATIME   (1ULL << 13)
#define PLATFORM_MOUNT_STRICTATIME (1ULL << 14)
#define PLATFORM_MOUNT_LAZYTIME   (1ULL << 15)

#define PLATFORM_NODE_FIFO  1U
#define PLATFORM_NODE_CHAR  2U
#define PLATFORM_NODE_BLOCK 3U

#define PLATFORM_TERMINAL_ECHO    (1U << 0)
#define PLATFORM_TERMINAL_ICANON  (1U << 1)
#define PLATFORM_TERMINAL_ISIG    (1U << 2)
#define PLATFORM_TERMINAL_IXON    (1U << 3)
#define PLATFORM_TERMINAL_OPOST   (1U << 4)
#define PLATFORM_TERMINAL_ROWS    (1U << 5)
#define PLATFORM_TERMINAL_COLUMNS (1U << 6)

#define PLATFORM_SHUTDOWN_HALT 0
#define PLATFORM_SHUTDOWN_POWEROFF 1
#define PLATFORM_SHUTDOWN_REBOOT 2

typedef struct {
    char name[PLATFORM_NAME_CAPACITY];
    unsigned long long device;
    unsigned int mode;
    unsigned int uid;
    unsigned int gid;
    unsigned long long size;
    unsigned long long inode;
    unsigned long nlink;
    long long atime;
    long long mtime;
    long long ctime;
    char owner[PLATFORM_OWNER_CAPACITY];
    char group[PLATFORM_GROUP_CAPACITY];
    int is_dir;
    int is_hidden;
} PlatformDirEntry;

typedef struct {
    int pid;
    int ppid;
    unsigned int uid;
    unsigned long long rss_kb;
    char state[16];
    char user[PLATFORM_NAME_CAPACITY];
    char name[PLATFORM_NAME_CAPACITY];
} PlatformProcessEntry;

typedef struct {
    unsigned int uid;
    unsigned int gid;
    char username[PLATFORM_NAME_CAPACITY];
    char groupname[PLATFORM_NAME_CAPACITY];
} PlatformIdentity;

typedef struct {
    unsigned long long total_bytes;
    unsigned long long free_bytes;
    unsigned long long available_bytes;
    unsigned long long shared_bytes;
    unsigned long long buffer_bytes;
    unsigned long long cache_bytes;
    unsigned long long swap_total_bytes;
    unsigned long long swap_free_bytes;
} PlatformMemoryInfo;

typedef struct {
    unsigned long long total_bytes;
    unsigned long long free_bytes;
    unsigned long long available_bytes;
    unsigned long long total_inodes;
    unsigned long long free_inodes;
    unsigned long long available_inodes;
    char type_name[PLATFORM_NAME_CAPACITY];
} PlatformFilesystemInfo;

typedef struct {
    unsigned long long uptime_seconds;
    char load_average[64];
} PlatformUptimeInfo;

typedef struct {
    unsigned int gid;
    char name[PLATFORM_NAME_CAPACITY];
} PlatformGroupEntry;

typedef struct {
    char username[PLATFORM_NAME_CAPACITY];
    char terminal[PLATFORM_NAME_CAPACITY];
    char host[PLATFORM_NAME_CAPACITY];
    long long login_time;
} PlatformSessionEntry;

typedef struct {
    unsigned int count;
    unsigned int interval_seconds;
    unsigned int timeout_seconds;
    unsigned int payload_size;
    unsigned int ttl;
    unsigned int deadline_seconds;
    int quiet_output;
    int family;
    int numeric_only;
} PlatformPingOptions;

typedef struct {
    unsigned int ttl;
    unsigned int probe_count;
    unsigned int reply_count;
    int reached_destination;
    char address[PLATFORM_NETWORK_TEXT_CAPACITY];
    char hostname[PLATFORM_NAME_CAPACITY];
    unsigned char probe_replied[PLATFORM_TRACEROUTE_MAX_QUERIES];
    unsigned int rtt_milliseconds[PLATFORM_TRACEROUTE_MAX_QUERIES];
} PlatformTracerouteHop;

typedef void (*PlatformTracerouteHopCallback)(const PlatformTracerouteHop *hop, void *user_data);

typedef struct {
    unsigned int max_ttl;
    unsigned int queries;
    unsigned int timeout_seconds;
    unsigned int payload_size;
    int family;
    int numeric_only;
    PlatformTracerouteHopCallback hop_callback;
    void *hop_callback_user_data;
} PlatformTracerouteOptions;

typedef struct {
    int listen_mode;
    int use_udp;
    int scan_mode;
    unsigned int timeout_milliseconds;
    int family;
    int numeric_only;
    unsigned int bind_port;
    char bind_host[PLATFORM_NETWORK_TEXT_CAPACITY];
    unsigned char *banner_buffer;
    unsigned int banner_capacity;
    unsigned int banner_read_timeout_milliseconds;
    unsigned int *banner_received_length;
    int *connect_status_out;
} PlatformNetcatOptions;

typedef struct {
    unsigned char bytes[PLATFORM_TERMINAL_STATE_CAPACITY];
} PlatformTerminalState;

typedef struct {
    int echo;
    int icanon;
    int isig;
    int ixon;
    int opost;
    unsigned int rows;
    unsigned int columns;
} PlatformTerminalMode;

typedef struct {
    void *opaque[PLATFORM_TLS_OPAQUE_WORDS];
    int socket_fd;
    int active;
} PlatformTlsClient;

typedef struct {
    char protocol[16];
    char cipher[64];
    char verification[160];
    char subject[256];
    char issuer[256];
    char dns_names[512];
    long long not_before;
    long long not_after;
} PlatformTlsPeerInfo;

typedef int (*PlatformThreadMain)(void *arg);

typedef struct {
    int tid;
    volatile int clear_tid;
    void *stack;
    size_t stack_size;
} PlatformThread;

typedef struct {
    volatile int state;
} PlatformMutex;

typedef struct {
    volatile int count;
} PlatformSemaphore;

typedef struct {
    unsigned int index;
    unsigned int flags;
    unsigned int mtu;
    int has_mac;
    char name[PLATFORM_NAME_CAPACITY];
    char mac[PLATFORM_NETWORK_TEXT_CAPACITY];
} PlatformNetworkLink;

typedef struct {
    int family;
    unsigned int prefix_length;
    int has_broadcast;
    char ifname[PLATFORM_NAME_CAPACITY];
    char address[PLATFORM_NETWORK_TEXT_CAPACITY];
    char broadcast[PLATFORM_NETWORK_TEXT_CAPACITY];
    char scope[16];
} PlatformNetworkAddress;

typedef struct {
    int family;
    unsigned int prefix_length;
    unsigned int metric;
    int is_default;
    int has_gateway;
    char ifname[PLATFORM_NAME_CAPACITY];
    char destination[PLATFORM_NETWORK_TEXT_CAPACITY];
    char gateway[PLATFORM_NETWORK_TEXT_CAPACITY];
} PlatformRouteEntry;

typedef struct {
    int family;
    unsigned short record_type;
    unsigned short preference;
    unsigned int ttl;
    char name[PLATFORM_NAME_CAPACITY];
    char address[PLATFORM_NETWORK_TEXT_CAPACITY];
    char data[PLATFORM_NAME_CAPACITY];
} PlatformDnsEntry;

typedef struct {
    unsigned int lease_seconds;
    unsigned int prefix_length;
    char address[PLATFORM_NETWORK_TEXT_CAPACITY];
    char server[PLATFORM_NETWORK_TEXT_CAPACITY];
    char router[PLATFORM_NETWORK_TEXT_CAPACITY];
    char dns1[PLATFORM_NETWORK_TEXT_CAPACITY];
    char dns2[PLATFORM_NETWORK_TEXT_CAPACITY];
} PlatformDhcpLease;

typedef struct {
    char protocol[8];
    char state[16];
    char local_address[PLATFORM_NETWORK_TEXT_CAPACITY];
    char remote_address[PLATFORM_NETWORK_TEXT_CAPACITY];
    unsigned int local_port;
    unsigned int remote_port;
    unsigned long long inode;
} PlatformSocketEntry;

typedef struct {
    int entering;
    long number;
    long args[6];
    long result;
} PlatformSyscallEvent;

typedef int (*PlatformSyscallTraceCallback)(const PlatformSyscallEvent *event, void *user_data);

long platform_write(int fd, const void *buffer, size_t count);
long platform_read(int fd, void *buffer, size_t count);
void *platform_allocate_pages(size_t size);
int platform_thread_start(PlatformThread *thread, PlatformThreadMain entry, void *arg, size_t stack_size);
int platform_thread_join(PlatformThread *thread, int *result_out);
void platform_mutex_init(PlatformMutex *mutex);
void platform_mutex_lock(PlatformMutex *mutex);
void platform_mutex_unlock(PlatformMutex *mutex);
void platform_semaphore_init(PlatformSemaphore *semaphore, int value);
void platform_semaphore_wait(PlatformSemaphore *semaphore);
void platform_semaphore_post(PlatformSemaphore *semaphore);
int platform_open_read(const char *path);
int platform_open_read_secure(const char *path, PlatformDirEntry *entry_out);
int platform_open_write(const char *path, unsigned int mode);
int platform_open_write_mode(const char *path, unsigned int mode, int truncate_existing);
int platform_open_create_exclusive(const char *path, unsigned int mode);
int platform_open_append(const char *path, unsigned int mode);
int platform_open_append_existing(const char *path);
long long platform_seek(int fd, long long offset, int whence);
int platform_create_temp_file(char *path_buffer, size_t buffer_size, const char *prefix, unsigned int mode);
int platform_close(int fd);
int platform_make_directory(const char *path, unsigned int mode);
int platform_remove_file(const char *path);
int platform_remove_directory(const char *path);
int platform_mount_filesystem(
    const char *source,
    const char *target,
    const char *filesystem_type,
    unsigned long long flags,
    const char *data
);
int platform_unmount_filesystem(const char *target, int force, int lazy);
int platform_rename_path(const char *old_path, const char *new_path);
int platform_create_hard_link(const char *target_path, const char *link_path);
int platform_create_symbolic_link(const char *target_path, const char *link_path);
int platform_create_node(const char *path, unsigned int node_type, unsigned int mode, unsigned int major, unsigned int minor);
int platform_change_mode(const char *path, unsigned int mode);
int platform_change_owner_ex(const char *path, unsigned int uid, unsigned int gid, int follow_symlinks);
int platform_change_owner(const char *path, unsigned int uid, unsigned int gid);
int platform_touch_path(const char *path);
int platform_set_path_times(
    const char *path,
    long long atime,
    long long mtime,
    int create_if_missing,
    int update_access,
    int update_modify
);
int platform_path_access(const char *path, int mode);
int platform_change_directory(const char *path);
const char *platform_getenv(const char *name);
const char *platform_getenv_entry(size_t index);
int platform_setenv(const char *name, const char *value, int overwrite);
int platform_unsetenv(const char *name);
int platform_clearenv(void);
int platform_isatty(int fd);
int platform_get_terminal_size(int fd, unsigned int *rows_out, unsigned int *columns_out);
int platform_get_process_id(void);
long platform_read_kernel_log(char *buffer, size_t buffer_size, int clear_after_read);
int platform_clear_kernel_log(void);
int platform_set_console_log_level(int level);
int platform_random_bytes(unsigned char *buffer, size_t count);
int platform_terminal_get_mode(int fd, PlatformTerminalMode *mode_out);
int platform_terminal_set_mode(int fd, const PlatformTerminalMode *mode, unsigned int change_mask);
int platform_terminal_enable_raw_mode(int fd, PlatformTerminalState *state_out);
int platform_terminal_restore_mode(int fd, const PlatformTerminalState *state);
int platform_sleep_milliseconds(unsigned long long milliseconds);
int platform_sleep_seconds(unsigned int seconds);
long long platform_get_epoch_time(void);
unsigned long long platform_get_monotonic_time_ns(void);
int platform_format_time(long long epoch_seconds, int use_local_time, const char *format, char *buffer, size_t buffer_size);
int platform_send_signal(int pid, int signal_number);
int platform_ignore_signal(int signal_number);
int platform_shutdown_system(int action);
int platform_parse_signal_name(const char *text, int *signal_out);
const char *platform_signal_name(int signal_number);
void platform_write_signal_list(int fd);
int platform_get_hostname(char *buffer, size_t buffer_size);
int platform_set_hostname(const char *name);
int platform_netcat(const char *host, unsigned int port, const PlatformNetcatOptions *options);
int platform_netcat_tcp(const char *host, unsigned int port, int listen_mode);
int platform_connect_tcp(const char *host, unsigned int port, int *socket_fd_out);
int platform_open_tcp_listener(const char *host, unsigned int port, int *socket_fd_out);
int platform_accept_tcp(int listener_fd, int *client_fd_out);
int platform_tls_connect(PlatformTlsClient *client, const char *host, unsigned int port);
const char *platform_tls_last_error(void);
const char *platform_tls_peer_verification_status(void);
int platform_tls_peer_info(PlatformTlsClient *client, PlatformTlsPeerInfo *info_out);
long platform_tls_read(PlatformTlsClient *client, void *buffer, size_t count);
long platform_tls_write(PlatformTlsClient *client, const void *buffer, size_t count);
void platform_tls_close(PlatformTlsClient *client);
int platform_list_network_links(PlatformNetworkLink *entries_out, size_t entry_capacity, size_t *count_out);
int platform_list_network_addresses(
    PlatformNetworkAddress *entries_out,
    size_t entry_capacity,
    size_t *count_out,
    int family_filter,
    const char *ifname_filter
);
int platform_list_network_routes(
    PlatformRouteEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out,
    int family_filter,
    const char *ifname_filter
);
int platform_network_link_set(const char *ifname, int want_up, unsigned int mtu_value, int set_mtu);
int platform_network_address_change(const char *ifname, const char *cidr, int add);
int platform_network_route_change(const char *destination, const char *gateway, const char *ifname, int add);
int platform_dns_lookup(
    const char *server,
    unsigned int port,
    const char *name,
    int family_filter,
    PlatformDnsEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out
);
int platform_dns_query(
    const char *server,
    unsigned int port,
    const char *name,
    unsigned short record_type,
    PlatformDnsEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out
);
int platform_dhcp_request(
    const char *ifname,
    const char *server,
    unsigned int server_port,
    unsigned int client_port,
    unsigned int timeout_milliseconds,
    PlatformDhcpLease *lease_out
);
int platform_list_sockets(PlatformSocketEntry *entries_out, size_t entry_capacity, size_t *count_out, int include_tcp, int include_udp, int listening_only);
int platform_poll_fds(const int *fds, size_t fd_count, size_t *ready_index_out, int timeout_milliseconds);
int platform_create_pipe(int pipe_fds[2]);
int platform_drop_privileges(const char *username, const char *groupname);
int platform_spawn_process(
    char *const argv[],
    int stdin_fd,
    int stdout_fd,
    const char *input_path,
    const char *output_path,
    int output_append,
    int *pid_out
);
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
);
int platform_trace_syscalls(char *const argv[], PlatformSyscallTraceCallback callback, void *user_data, int *exit_status_out);
int platform_wait_process(int pid, int *exit_status_out);
int platform_poll_process_exit(int pid, int *finished_out, int *exit_status_out);
int platform_wait_process_timeout(
    int pid,
    unsigned long long timeout_milliseconds,
    unsigned long long kill_after_milliseconds,
    int signal_number,
    int preserve_status,
    int *exit_status_out
);
int platform_list_processes(PlatformProcessEntry *entries_out, size_t entry_capacity, size_t *count_out);
int platform_get_identity(PlatformIdentity *identity_out);
int platform_lookup_identity(const char *username, PlatformIdentity *identity_out);
int platform_lookup_group(const char *groupname, unsigned int *gid_out);
int platform_list_groups_for_identity(
    const PlatformIdentity *identity,
    PlatformGroupEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out
);
int platform_list_sessions(PlatformSessionEntry *entries_out, size_t entry_capacity, size_t *count_out);
int platform_get_memory_info(PlatformMemoryInfo *info_out);
int platform_get_uptime_info(PlatformUptimeInfo *info_out);
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
);
int platform_ping_host(const char *host, const PlatformPingOptions *options);
int platform_trace_route(
    const char *host,
    const PlatformTracerouteOptions *options,
    PlatformTracerouteHop *hops_out,
    size_t hop_capacity,
    size_t *hop_count_out
);

int platform_collect_entries(
    const char *path,
    int include_hidden,
    PlatformDirEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out,
    int *path_is_directory
);
int platform_path_is_directory(const char *path, int *is_directory_out);

int platform_stream_file_to_stdout(const char *path);
int platform_get_current_directory(char *buffer, size_t buffer_size);
int platform_get_path_info(const char *path, PlatformDirEntry *entry_out);
int platform_get_path_info_follow(const char *path, PlatformDirEntry *entry_out);
int platform_read_symlink(const char *path, char *buffer, size_t buffer_size);
int platform_get_filesystem_info(const char *path, PlatformFilesystemInfo *info_out);
int platform_get_filesystem_usage(const char *path, unsigned long long *total_bytes_out, unsigned long long *free_bytes_out, unsigned long long *available_bytes_out);
int platform_truncate_path(const char *path, unsigned long long size);
int platform_sync_all(void);
int platform_sync_path(const char *path);
int platform_sync_path_data(const char *path);

void platform_free_entries(PlatformDirEntry *entries, size_t count);
void platform_format_mode(unsigned int mode, char out[11]);

#endif
