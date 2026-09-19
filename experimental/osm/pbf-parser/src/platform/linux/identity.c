#include "platform.h"
#include "common.h"

static int linux_identity_read_file(const char *path, char *buffer, size_t buffer_size) {
    long fd;
    long bytes;

    if (path == 0 || buffer == 0 || buffer_size == 0U) {
        return -1;
    }

    fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)path, LINUX_O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    bytes = linux_syscall3(LINUX_SYS_READ, fd, (long)buffer, (long)(buffer_size - 1U));
    linux_syscall1(LINUX_SYS_CLOSE, fd);
    if (bytes < 0) {
        return -1;
    }
    buffer[bytes] = '\0';
    return 0;
}

static int linux_lookup_passwd_by_name(const char *username, PlatformIdentity *identity_out) {
    char buffer[8192];
    const char *cursor;

    if (username == 0 || identity_out == 0) {
        return -1;
    }
    if (linux_identity_read_file("/etc/passwd", buffer, sizeof(buffer)) != 0) {
        return -1;
    }

    cursor = buffer;
    while (*cursor != '\0') {
        const char *line_end = cursor;
        const char *name_end = cursor;
        const char *field;
        char uid_text[32];
        char gid_text[32];
        unsigned long long uid = 0ULL;
        unsigned long long gid = 0ULL;
        size_t out = 0U;
        int colon_count = 0;

        while (*line_end != '\0' && *line_end != '\n') {
            line_end += 1;
        }
        while (name_end < line_end && *name_end != ':') {
            name_end += 1;
        }
        if ((size_t)(name_end - cursor) != rt_strlen(username) || rt_strncmp(cursor, username, (size_t)(name_end - cursor)) != 0) {
            cursor = (*line_end == '\n') ? (line_end + 1) : line_end;
            continue;
        }

        field = cursor;
        while (field < line_end) {
            if (*field == ':') {
                colon_count += 1;
                field += 1;
                if (colon_count == 2) {
                    out = 0U;
                    while (field < line_end && *field != ':' && out + 1U < sizeof(uid_text)) {
                        uid_text[out++] = *field++;
                    }
                    uid_text[out] = '\0';
                } else if (colon_count == 3) {
                    out = 0U;
                    while (field < line_end && *field != ':' && out + 1U < sizeof(gid_text)) {
                        gid_text[out++] = *field++;
                    }
                    gid_text[out] = '\0';
                    break;
                }
                continue;
            }
            field += 1;
        }

        if (rt_parse_uint(uid_text, &uid) != 0 || rt_parse_uint(gid_text, &gid) != 0) {
            return -1;
        }
        identity_out->uid = (unsigned int)uid;
        identity_out->gid = (unsigned int)gid;
        linux_copy_string(identity_out->username, sizeof(identity_out->username), username);
        linux_unsigned_to_string(gid, identity_out->groupname, sizeof(identity_out->groupname));
        return 0;
    }

    return -1;
}

static int linux_lookup_group_by_name(const char *groupname, unsigned int *gid_out) {
    char buffer[8192];
    const char *cursor;

    if (groupname == 0 || gid_out == 0) {
        return -1;
    }
    if (linux_identity_read_file("/etc/group", buffer, sizeof(buffer)) != 0) {
        return -1;
    }

    cursor = buffer;
    while (*cursor != '\0') {
        const char *line_end = cursor;
        const char *name_end = cursor;
        char gid_text[32];
        unsigned long long gid = 0ULL;
        size_t out = 0U;
        int colon_count = 0;

        while (*line_end != '\0' && *line_end != '\n') {
            line_end += 1;
        }
        while (name_end < line_end && *name_end != ':') {
            name_end += 1;
        }
        if ((size_t)(name_end - cursor) == rt_strlen(groupname) && rt_strncmp(cursor, groupname, (size_t)(name_end - cursor)) == 0) {
            const char *field = cursor;
            while (field < line_end) {
                if (*field == ':') {
                    colon_count += 1;
                    field += 1;
                    if (colon_count == 2) {
                        while (field < line_end && *field != ':' && out + 1U < sizeof(gid_text)) {
                            gid_text[out++] = *field++;
                        }
                        gid_text[out] = '\0';
                        break;
                    }
                    continue;
                }
                field += 1;
            }
            if (rt_parse_uint(gid_text, &gid) != 0) {
                return -1;
            }
            *gid_out = (unsigned int)gid;
            return 0;
        }
        cursor = (*line_end == '\n') ? (line_end + 1) : line_end;
    }

    return -1;
}

struct linux_utsname_identity {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

int platform_get_hostname(char *buffer, size_t buffer_size) {
    struct linux_utsname_identity info;
    if (buffer == 0 || buffer_size == 0) {
        return -1;
    }
    if (linux_syscall1(LINUX_SYS_UNAME, (long)&info) < 0) {
        return -1;
    }
    linux_copy_string(buffer, (unsigned long)buffer_size, info.nodename);
    return 0;
}

int platform_set_hostname(const char *name) {
    if (name == 0) {
        return -1;
    }

    return linux_syscall2(LINUX_SYS_SETHOSTNAME, (long)name, (long)linux_string_length(name)) < 0 ? -1 : 0;
}

int platform_lookup_group(const char *groupname, unsigned int *gid_out) {
    PlatformIdentity identity;
    unsigned long long value = 0;

    if (groupname == 0 || groupname[0] == '\0' || gid_out == 0) {
        return -1;
    }

    if (rt_parse_uint(groupname, &value) == 0) {
        *gid_out = (unsigned int)value;
        return 0;
    }

    if (linux_lookup_group_by_name(groupname, gid_out) == 0) {
        return 0;
    }

    if (platform_get_identity(&identity) != 0) {
        return -1;
    }

    if (rt_strcmp(groupname, identity.groupname) != 0) {
        return -1;
    }

    *gid_out = identity.gid;
    return 0;
}

int platform_lookup_identity(const char *username, PlatformIdentity *identity_out) {
    unsigned long long uid;
    unsigned long long gid;

    if (identity_out == 0) {
        return -1;
    }

    uid = (unsigned long long)linux_syscall1(LINUX_SYS_GETUID, 0);
    gid = (unsigned long long)linux_syscall1(LINUX_SYS_GETGID, 0);
    identity_out->uid = (unsigned int)uid;
    identity_out->gid = (unsigned int)gid;
    linux_unsigned_to_string(uid, identity_out->username, sizeof(identity_out->username));
    linux_unsigned_to_string(gid, identity_out->groupname, sizeof(identity_out->groupname));

    if (username == 0 || username[0] == '\0') {
        return 0;
    }
    if (rt_strcmp(username, identity_out->username) == 0) {
        return 0;
    }
    if (rt_parse_uint(username, &uid) == 0) {
        identity_out->uid = (unsigned int)uid;
        linux_unsigned_to_string(uid, identity_out->username, sizeof(identity_out->username));
        return 0;
    }
    if (linux_lookup_passwd_by_name(username, identity_out) == 0) {
        return 0;
    }

    return -1;
}

int platform_get_identity(PlatformIdentity *identity_out) {
    return platform_lookup_identity(0, identity_out);
}

int platform_list_groups_for_identity(
    const PlatformIdentity *identity,
    PlatformGroupEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out
) {
    if (identity == 0 || count_out == 0 || (entry_capacity > 0 && entries_out == 0)) {
        return -1;
    }

    *count_out = 0;
    if (entry_capacity == 0) {
        return 0;
    }

    entries_out[0].gid = identity->gid;
    linux_copy_string(entries_out[0].name, sizeof(entries_out[0].name), identity->groupname);
    *count_out = 1;
    return 0;
}

int platform_list_sessions(PlatformSessionEntry *entries_out, size_t entry_capacity, size_t *count_out) {
    PlatformIdentity identity;

    if (count_out == 0 || (entry_capacity > 0 && entries_out == 0)) {
        return -1;
    }

    if (platform_get_identity(&identity) != 0) {
        return -1;
    }

    *count_out = 1;
    if (entry_capacity == 0) {
        return 0;
    }

    linux_copy_string(entries_out[0].username, sizeof(entries_out[0].username), identity.username);
    entries_out[0].terminal[0] = '\0';
    entries_out[0].host[0] = '\0';
    entries_out[0].login_time = 0;
    return 0;
}
