#include "platform.h"
#include "common.h"
#include "proc_util.h"

static int linux_append_char(char *buffer, size_t buffer_size, size_t *length_io, char ch) {
    size_t length = *length_io;

    if (length + 1 >= buffer_size) {
        return -1;
    }

    buffer[length] = ch;
    buffer[length + 1] = '\0';
    *length_io = length + 1;
    return 0;
}

static int linux_append_padded(char *buffer, size_t buffer_size, size_t *length_io, unsigned int value, unsigned int width) {
    char digits[16];
    unsigned int count = 0;

    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value > 0U && count < sizeof(digits));

    while (count < width) {
        if (linux_append_char(buffer, buffer_size, length_io, '0') != 0) {
            return -1;
        }
        width -= 1U;
    }

    while (count > 0U) {
        count -= 1U;
        if (linux_append_char(buffer, buffer_size, length_io, digits[count]) != 0) {
            return -1;
        }
    }

    return 0;
}

static void linux_civil_from_days(long long z, int *year_out, unsigned int *month_out, unsigned int *day_out) {
    long long era;
    unsigned long long doe;
    unsigned long long yoe;
    unsigned long long doy;
    unsigned long long mp;
    int year;
    unsigned int month;

    z += 719468LL;
    era = (z >= 0LL ? z : z - 146096LL) / 146097LL;
    doe = (unsigned long long)(z - era * 146097LL);
    yoe = (doe - doe / 1460ULL + doe / 36524ULL - doe / 146096ULL) / 365ULL;
    year = (int)yoe + (int)(era * 400LL);
    doy = doe - (365ULL * yoe + yoe / 4ULL - yoe / 100ULL);
    mp = (5ULL * doy + 2ULL) / 153ULL;

    *day_out = (unsigned int)(doy - (153ULL * mp + 2ULL) / 5ULL + 1ULL);
    month = (unsigned int)(mp < 10ULL ? mp + 3ULL : mp - 9ULL);
    *month_out = month;
    *year_out = year + (month <= 2U ? 1 : 0);
}

static int linux_append_year(char *buffer, size_t buffer_size, size_t *length_io, int year) {
    unsigned int absolute_year;

    if (year < 0) {
        if (linux_append_char(buffer, buffer_size, length_io, '-') != 0) {
            return -1;
        }
        absolute_year = (unsigned int)(-(year + 1)) + 1U;
    } else {
        absolute_year = (unsigned int)year;
    }

    return linux_append_padded(buffer, buffer_size, length_io, absolute_year, 4U);
}

int platform_sleep_milliseconds(unsigned long long milliseconds) {
    struct linux_timespec req;
    struct linux_timespec rem;
    long result;

    req.tv_sec = (long)(milliseconds / 1000ULL);
    req.tv_nsec = (long)((milliseconds % 1000ULL) * 1000000ULL);
    for (;;) {
        result = linux_syscall2(LINUX_SYS_NANOSLEEP, (long)&req, (long)&rem);
        if (result == 0) {
            return 0;
        }
        if (result != -LINUX_EINTR) {
            return -1;
        }
        req = rem;
    }
}

int platform_sleep_seconds(unsigned int seconds) {
    return platform_sleep_milliseconds((unsigned long long)seconds * 1000ULL);
}

long long platform_get_epoch_time(void) {
    struct linux_timespec now;

    if (linux_syscall2(LINUX_SYS_CLOCK_GETTIME, 0, (long)&now) < 0) {
        return 0;
    }

    return (long long)now.tv_sec;
}

unsigned long long platform_get_monotonic_time_ns(void) {
    struct linux_timespec now;

    if (linux_syscall2(LINUX_SYS_CLOCK_GETTIME, 1, (long)&now) < 0) {
        return 0ULL;
    }

    return ((unsigned long long)now.tv_sec * 1000000000ULL) + (unsigned long long)now.tv_nsec;
}

int platform_get_memory_info(PlatformMemoryInfo *info_out) {
    char meminfo[4096];
    unsigned long long reclaimable_bytes = 0;

    if (info_out == 0) {
        return -1;
    }

    clear_memory_info(info_out);

    if (read_text_file("/proc/meminfo", meminfo, sizeof(meminfo)) != 0 ||
        parse_meminfo_field(meminfo, "MemTotal", &info_out->total_bytes) != 0 ||
        parse_meminfo_field(meminfo, "MemFree", &info_out->free_bytes) != 0) {
        return -1;
    }

    if (parse_meminfo_field(meminfo, "MemAvailable", &info_out->available_bytes) != 0) {
        info_out->available_bytes = info_out->free_bytes;
    }
    (void)parse_meminfo_field(meminfo, "Shmem", &info_out->shared_bytes);
    (void)parse_meminfo_field(meminfo, "Buffers", &info_out->buffer_bytes);
    (void)parse_meminfo_field(meminfo, "Cached", &info_out->cache_bytes);
    if (parse_meminfo_field(meminfo, "SReclaimable", &reclaimable_bytes) == 0) {
        info_out->cache_bytes += reclaimable_bytes;
    }
    (void)parse_meminfo_field(meminfo, "SwapTotal", &info_out->swap_total_bytes);
    (void)parse_meminfo_field(meminfo, "SwapFree", &info_out->swap_free_bytes);

    return 0;
}

int platform_get_uptime_info(PlatformUptimeInfo *info_out) {
    char uptime_text[128];

    if (info_out == 0) {
        return -1;
    }

    info_out->uptime_seconds = 0;
    linux_copy_string(info_out->load_average, sizeof(info_out->load_average), "0.00 0.00 0.00");

    if (read_text_file("/proc/uptime", uptime_text, sizeof(uptime_text)) != 0 ||
        parse_unsigned_prefix(uptime_text, &info_out->uptime_seconds) != 0) {
        return -1;
    }

    (void)read_loadavg_text(info_out->load_average, sizeof(info_out->load_average));
    return 0;
}

int platform_format_time(long long epoch_seconds, int use_local_time, const char *format, char *buffer, size_t buffer_size) {
    long long days;
    unsigned long long secs_of_day;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
    unsigned int month;
    unsigned int day;
    int year;
    size_t format_index = 0;
    size_t length = 0;
    const char *actual_format;

    /* Freestanding builds do not expose timezone data yet, so local output stays UTC here. */
    (void)use_local_time;

    if (buffer == 0 || buffer_size == 0) {
        return -1;
    }

    buffer[0] = '\0';
    actual_format = (format != 0 && format[0] != '\0') ? format : "%Y-%m-%d %H:%M:%S";
    days = epoch_seconds / 86400LL;
    secs_of_day = (unsigned long long)(epoch_seconds % 86400LL);

    if (epoch_seconds < 0 && secs_of_day != 0ULL) {
        days -= 1LL;
        secs_of_day += 86400ULL;
    }

    linux_civil_from_days(days, &year, &month, &day);
    hour = (unsigned int)(secs_of_day / 3600ULL);
    minute = (unsigned int)((secs_of_day % 3600ULL) / 60ULL);
    second = (unsigned int)(secs_of_day % 60ULL);

    while (actual_format[format_index] != '\0') {
        if (actual_format[format_index] != '%') {
            if (linux_append_char(buffer, buffer_size, &length, actual_format[format_index]) != 0) {
                return -1;
            }
            format_index += 1;
            continue;
        }

        format_index += 1;
        if (actual_format[format_index] == '\0') {
            return -1;
        }

        switch (actual_format[format_index]) {
            case '%':
                if (linux_append_char(buffer, buffer_size, &length, '%') != 0) {
                    return -1;
                }
                break;
            case 'Y':
                if (linux_append_year(buffer, buffer_size, &length, year) != 0) {
                    return -1;
                }
                break;
            case 'm':
                if (linux_append_padded(buffer, buffer_size, &length, month, 2U) != 0) {
                    return -1;
                }
                break;
            case 'd':
                if (linux_append_padded(buffer, buffer_size, &length, day, 2U) != 0) {
                    return -1;
                }
                break;
            case 'H':
                if (linux_append_padded(buffer, buffer_size, &length, hour, 2U) != 0) {
                    return -1;
                }
                break;
            case 'M':
                if (linux_append_padded(buffer, buffer_size, &length, minute, 2U) != 0) {
                    return -1;
                }
                break;
            case 'S':
                if (linux_append_padded(buffer, buffer_size, &length, second, 2U) != 0) {
                    return -1;
                }
                break;
            case 'F':
                if (linux_append_year(buffer, buffer_size, &length, year) != 0 ||
                    linux_append_char(buffer, buffer_size, &length, '-') != 0 ||
                    linux_append_padded(buffer, buffer_size, &length, month, 2U) != 0 ||
                    linux_append_char(buffer, buffer_size, &length, '-') != 0 ||
                    linux_append_padded(buffer, buffer_size, &length, day, 2U) != 0) {
                    return -1;
                }
                break;
            case 'T':
                if (linux_append_padded(buffer, buffer_size, &length, hour, 2U) != 0 ||
                    linux_append_char(buffer, buffer_size, &length, ':') != 0 ||
                    linux_append_padded(buffer, buffer_size, &length, minute, 2U) != 0 ||
                    linux_append_char(buffer, buffer_size, &length, ':') != 0 ||
                    linux_append_padded(buffer, buffer_size, &length, second, 2U) != 0) {
                    return -1;
                }
                break;
            default:
                return -1;
        }

        format_index += 1;
    }

    return 0;
}
