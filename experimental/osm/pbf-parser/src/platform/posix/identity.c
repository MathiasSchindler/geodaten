#if defined(__linux__)
#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "platform.h"
#include "common.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <utmpx.h>
#include <unistd.h>

#if defined(__APPLE__)
int sethostname(const char *name, int namelen);
int getgrouplist(const char *name, int basegid, gid_t *groups, int *ngroups);
#endif

int platform_get_hostname(char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        errno = EINVAL;
        return -1;
    }

    buffer[0] = '\0';
    if (gethostname(buffer, buffer_size) != 0) {
        return -1;
    }
    buffer[buffer_size - 1U] = '\0';
    return 0;
}

int platform_set_hostname(const char *name) {
    if (name == NULL) {
        errno = EINVAL;
        return -1;
    }

#if defined(__MSYS__)
    errno = ENOSYS;
    return -1;
#else
    return sethostname(name, (int)rt_strlen(name));
#endif
}

static void fill_group_name(char *buffer, size_t buffer_size, gid_t gid) {
    struct group *gr;

    gr = getgrgid(gid);
    if (gr != NULL && gr->gr_name != NULL && gr->gr_name[0] != '\0') {
        posix_copy_string(buffer, buffer_size, gr->gr_name);
    } else {
        snprintf(buffer, buffer_size, "%u", (unsigned int)gid);
    }
}

static void fill_identity_from_passwd(struct passwd *pw, PlatformIdentity *identity_out) {
    identity_out->uid = (unsigned int)pw->pw_uid;
    identity_out->gid = (unsigned int)pw->pw_gid;

    if (pw->pw_name != NULL && pw->pw_name[0] != '\0') {
        posix_copy_string(identity_out->username, sizeof(identity_out->username), pw->pw_name);
    } else {
        snprintf(identity_out->username, sizeof(identity_out->username), "%u", identity_out->uid);
    }

    fill_group_name(identity_out->groupname, sizeof(identity_out->groupname), pw->pw_gid);
}

static void copy_field(char *dst, size_t dst_size, const char *src, size_t src_size) {
    size_t i = 0;

    if (dst_size == 0) {
        return;
    }

    while (i + 1 < dst_size && i < src_size && src[i] != '\0') {
        dst[i] = src[i];
        i += 1;
    }
    dst[i] = '\0';
}

static int compare_group_ids(const void *lhs, const void *rhs) {
    gid_t left = *(const gid_t *)lhs;
    gid_t right = *(const gid_t *)rhs;

    if (left < right) {
        return -1;
    }
    if (left > right) {
        return 1;
    }
    return 0;
}

int platform_lookup_group(const char *groupname, unsigned int *gid_out) {
    struct group *gr = NULL;
    unsigned long long value = 0;

    if (groupname == NULL || groupname[0] == '\0' || gid_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (rt_parse_uint(groupname, &value) == 0) {
        *gid_out = (unsigned int)value;
        return 0;
    }

    gr = getgrnam(groupname);
    if (gr == NULL) {
        errno = ENOENT;
        return -1;
    }

    *gid_out = (unsigned int)gr->gr_gid;
    return 0;
}

int platform_lookup_identity(const char *username, PlatformIdentity *identity_out) {
    struct passwd *pw = NULL;

    if (identity_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (username != NULL && username[0] != '\0') {
        pw = getpwnam(username);
        if (pw == NULL) {
            errno = ENOENT;
            return -1;
        }
        fill_identity_from_passwd(pw, identity_out);
        return 0;
    }

    pw = getpwuid(getuid());
    if (pw != NULL) {
        fill_identity_from_passwd(pw, identity_out);
        return 0;
    }

    identity_out->uid = (unsigned int)getuid();
    identity_out->gid = (unsigned int)getgid();
    snprintf(identity_out->username, sizeof(identity_out->username), "%u", identity_out->uid);
    fill_group_name(identity_out->groupname, sizeof(identity_out->groupname), (gid_t)identity_out->gid);
    return 0;
}

int platform_get_identity(PlatformIdentity *identity_out) {
    return platform_lookup_identity(NULL, identity_out);
}

int platform_list_groups_for_identity(
    const PlatformIdentity *identity,
    PlatformGroupEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out
) {
    gid_t *group_ids = NULL;
    int group_count = 16;
    size_t stored = 0;
    int i;

    if (identity == NULL || count_out == NULL || (entry_capacity > 0 && entries_out == NULL)) {
        errno = EINVAL;
        return -1;
    }

    *count_out = 0;
    if (entry_capacity == 0) {
        return 0;
    }

    group_ids = (gid_t *)malloc((size_t)group_count * sizeof(gid_t));
    if (group_ids == NULL) {
        errno = ENOMEM;
        return -1;
    }

    if (getgrouplist(identity->username, (int)identity->gid, group_ids, &group_count) < 0) {
        gid_t *resized = (gid_t *)realloc(group_ids, (size_t)group_count * sizeof(gid_t));
        if (resized == NULL) {
            free(group_ids);
            errno = ENOMEM;
            return -1;
        }
        group_ids = resized;
        if (getgrouplist(identity->username, (int)identity->gid, group_ids, &group_count) < 0) {
            group_count = 1;
            group_ids[0] = (gid_t)identity->gid;
        }
    }

    qsort(group_ids, (size_t)group_count, sizeof(gid_t), compare_group_ids);

    for (i = 0; i < group_count && stored < entry_capacity; ++i) {
        if (i > 0 && group_ids[i] == group_ids[i - 1]) {
            continue;
        }
        entries_out[stored].gid = (unsigned int)group_ids[i];
        fill_group_name(entries_out[stored].name, sizeof(entries_out[stored].name), group_ids[i]);
        stored += 1U;
    }

    free(group_ids);
    *count_out = stored;
    return 0;
}

int platform_list_sessions(PlatformSessionEntry *entries_out, size_t entry_capacity, size_t *count_out) {
    struct utmpx *entry;
    size_t count = 0;

    if (count_out == NULL || (entry_capacity > 0 && entries_out == NULL)) {
        errno = EINVAL;
        return -1;
    }

    setutxent();
    while ((entry = getutxent()) != NULL) {
        if (entry->ut_type != USER_PROCESS || entry->ut_user[0] == '\0') {
            continue;
        }

        if (count < entry_capacity) {
            copy_field(entries_out[count].username, sizeof(entries_out[count].username), entry->ut_user, sizeof(entry->ut_user));
            copy_field(entries_out[count].terminal, sizeof(entries_out[count].terminal), entry->ut_line, sizeof(entry->ut_line));
            copy_field(entries_out[count].host, sizeof(entries_out[count].host), entry->ut_host, sizeof(entry->ut_host));
            entries_out[count].login_time = (long long)entry->ut_tv.tv_sec;
        }
        count += 1U;
    }
    endutxent();

    *count_out = count;
    return 0;
}
