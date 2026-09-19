#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#if defined(__linux__)
#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "platform.h"
#include "runtime.h"
#include "proc_util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <stdint.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#endif

static void set_default_load_average(char *buffer, size_t buffer_size) {
    rt_copy_string(buffer, buffer_size, "0.00 0.00 0.00");
}

static int fill_uptime_seconds(unsigned long long *seconds_out) {
    char uptime_text[128];

    if (seconds_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (read_text_file("/proc/uptime", uptime_text, sizeof(uptime_text)) == 0 &&
        parse_unsigned_prefix(uptime_text, seconds_out) == 0) {
        return 0;
    }

#if defined(__APPLE__)
    {
        struct timeval boot_time;
        size_t size = sizeof(boot_time);
        int mib[2] = { CTL_KERN, KERN_BOOTTIME };
        time_t now = time(NULL);

        if (sysctl(mib, 2, &boot_time, &size, 0, 0) == 0 && now >= boot_time.tv_sec) {
            *seconds_out = (unsigned long long)(now - boot_time.tv_sec);
            return 0;
        }
    }
#endif

#if defined(CLOCK_BOOTTIME)
    {
        struct timespec ts;
        if (clock_gettime(CLOCK_BOOTTIME, &ts) == 0) {
            *seconds_out = (unsigned long long)ts.tv_sec;
            return 0;
        }
    }
#endif

    return -1;
}

static void fill_load_average(char *buffer, size_t buffer_size) {
    set_default_load_average(buffer, buffer_size);

    if (read_loadavg_text(buffer, buffer_size) == 0) {
        return;
    }

    {
        double values[3];
#if !defined(__MSYS__)
        if (getloadavg(values, 3) == 3) {
            (void)snprintf(buffer, buffer_size, "%.2f %.2f %.2f", values[0], values[1], values[2]);
        }
#else
        (void)values;
#endif
    }
}

static int posix_append_char(char *buffer, size_t buffer_size, size_t *length_io, char ch) {
    size_t length = *length_io;

    if (length + 1U >= buffer_size) {
        return -1;
    }

    buffer[length] = ch;
    buffer[length + 1U] = '\0';
    *length_io = length + 1U;
    return 0;
}

static int posix_append_padded(char *buffer, size_t buffer_size, size_t *length_io, unsigned int value, unsigned int width) {
    char digits[16];
    unsigned int count = 0;

    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value > 0U && count < sizeof(digits));

    while (count < width) {
        if (posix_append_char(buffer, buffer_size, length_io, '0') != 0) {
            return -1;
        }
        width -= 1U;
    }

    while (count > 0U) {
        count -= 1U;
        if (posix_append_char(buffer, buffer_size, length_io, digits[count]) != 0) {
            return -1;
        }
    }

    return 0;
}

static int posix_append_year(char *buffer, size_t buffer_size, size_t *length_io, int year) {
    unsigned int absolute_year;

    if (year < 0) {
        if (posix_append_char(buffer, buffer_size, length_io, '-') != 0) {
            return -1;
        }
        absolute_year = (unsigned int)(-(year + 1)) + 1U;
    } else {
        absolute_year = (unsigned int)year;
    }

    return posix_append_padded(buffer, buffer_size, length_io, absolute_year, 4U);
}

static int posix_format_time_fallback(const struct tm *tm_ptr, const char *format, char *buffer, size_t buffer_size) {
    size_t index = 0;
    size_t length = 0;
    int year = tm_ptr->tm_year + 1900;
    unsigned int month = (unsigned int)(tm_ptr->tm_mon + 1);
    unsigned int day = (unsigned int)tm_ptr->tm_mday;
    unsigned int hour = (unsigned int)tm_ptr->tm_hour;
    unsigned int minute = (unsigned int)tm_ptr->tm_min;
    unsigned int second = (unsigned int)tm_ptr->tm_sec;

    buffer[0] = '\0';
    while (format[index] != '\0') {
        if (format[index] != '%') {
            if (posix_append_char(buffer, buffer_size, &length, format[index]) != 0) {
                return -1;
            }
            index += 1U;
            continue;
        }

        index += 1U;
        if (format[index] == '\0') {
            return -1;
        }

        switch (format[index]) {
            case '%':
                if (posix_append_char(buffer, buffer_size, &length, '%') != 0) {
                    return -1;
                }
                break;
            case 'Y':
                if (posix_append_year(buffer, buffer_size, &length, year) != 0) {
                    return -1;
                }
                break;
            case 'm':
                if (posix_append_padded(buffer, buffer_size, &length, month, 2U) != 0) {
                    return -1;
                }
                break;
            case 'd':
                if (posix_append_padded(buffer, buffer_size, &length, day, 2U) != 0) {
                    return -1;
                }
                break;
            case 'H':
                if (posix_append_padded(buffer, buffer_size, &length, hour, 2U) != 0) {
                    return -1;
                }
                break;
            case 'M':
                if (posix_append_padded(buffer, buffer_size, &length, minute, 2U) != 0) {
                    return -1;
                }
                break;
            case 'S':
                if (posix_append_padded(buffer, buffer_size, &length, second, 2U) != 0) {
                    return -1;
                }
                break;
            case 'F':
                if (posix_append_year(buffer, buffer_size, &length, year) != 0 ||
                    posix_append_char(buffer, buffer_size, &length, '-') != 0 ||
                    posix_append_padded(buffer, buffer_size, &length, month, 2U) != 0 ||
                    posix_append_char(buffer, buffer_size, &length, '-') != 0 ||
                    posix_append_padded(buffer, buffer_size, &length, day, 2U) != 0) {
                    return -1;
                }
                break;
            case 'T':
                if (posix_append_padded(buffer, buffer_size, &length, hour, 2U) != 0 ||
                    posix_append_char(buffer, buffer_size, &length, ':') != 0 ||
                    posix_append_padded(buffer, buffer_size, &length, minute, 2U) != 0 ||
                    posix_append_char(buffer, buffer_size, &length, ':') != 0 ||
                    posix_append_padded(buffer, buffer_size, &length, second, 2U) != 0) {
                    return -1;
                }
                break;
            default:
                return -1;
        }

        index += 1U;
    }

    return 0;
}

int platform_sleep_milliseconds(unsigned long long milliseconds) {
    struct timespec request;
    struct timespec remaining;

    request.tv_sec = (time_t)(milliseconds / 1000ULL);
    request.tv_nsec = (long)((milliseconds % 1000ULL) * 1000000ULL);
    while (nanosleep(&request, &remaining) != 0) {
        if (errno != EINTR) {
            return -1;
        }
        request = remaining;
    }
    return 0;
}

int platform_sleep_seconds(unsigned int seconds) {
    return platform_sleep_milliseconds((unsigned long long)seconds * 1000ULL);
}

long long platform_get_epoch_time(void) {
    time_t now = time(NULL);
    return (now == (time_t)-1) ? 0 : (long long)now;
}

unsigned long long platform_get_monotonic_time_ns(void) {
    struct timespec now;

#if defined(CLOCK_MONOTONIC)
    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
        return ((unsigned long long)now.tv_sec * 1000000000ULL) + (unsigned long long)now.tv_nsec;
    }
#endif
    if (clock_gettime(CLOCK_REALTIME, &now) == 0) {
        return ((unsigned long long)now.tv_sec * 1000000000ULL) + (unsigned long long)now.tv_nsec;
    }
    return (unsigned long long)platform_get_epoch_time() * 1000000000ULL;
}

int platform_get_memory_info(PlatformMemoryInfo *info_out) {
    char meminfo[4096];

    if (info_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    clear_memory_info(info_out);

    if (read_text_file("/proc/meminfo", meminfo, sizeof(meminfo)) == 0 &&
        parse_meminfo_field(meminfo, "MemTotal", &info_out->total_bytes) == 0 &&
        parse_meminfo_field(meminfo, "MemFree", &info_out->free_bytes) == 0) {
        unsigned long long reclaimable_bytes = 0;
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

#if defined(__APPLE__)
    {
        uint64_t total_memory = 0;
        size_t total_size = sizeof(total_memory);
        mach_port_t host = mach_host_self();
        vm_statistics64_data_t vm_stats;
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        vm_size_t page_size = 0;

        if (sysctlbyname("hw.memsize", &total_memory, &total_size, 0, 0) != 0) {
            return -1;
        }

        if (host_page_size(host, &page_size) != KERN_SUCCESS) {
            return -1;
        }

        if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm_stats, &count) != KERN_SUCCESS) {
            return -1;
        }

        info_out->total_bytes = (unsigned long long)total_memory;
        info_out->free_bytes = (unsigned long long)(vm_stats.free_count + vm_stats.speculative_count) *
                               (unsigned long long)page_size;
        info_out->cache_bytes = (unsigned long long)vm_stats.inactive_count * (unsigned long long)page_size;
        info_out->buffer_bytes = 0;
        info_out->available_bytes = info_out->free_bytes + info_out->cache_bytes;

        {
            struct xsw_usage swap_usage;
            size_t swap_size = sizeof(swap_usage);
            if (sysctlbyname("vm.swapusage", &swap_usage, &swap_size, 0, 0) == 0) {
                info_out->swap_total_bytes = (unsigned long long)swap_usage.xsu_total;
                info_out->swap_free_bytes = (unsigned long long)swap_usage.xsu_avail;
            }
        }
        return 0;
    }
#elif defined(_SC_PHYS_PAGES)
    {
        long page_size = sysconf(_SC_PAGESIZE);
        long phys_pages = sysconf(_SC_PHYS_PAGES);
        long avail_pages = -1;

#if defined(_SC_AVPHYS_PAGES)
        avail_pages = sysconf(_SC_AVPHYS_PAGES);
#endif

        if (page_size <= 0 || phys_pages <= 0) {
            return -1;
        }

        if (avail_pages < 0) {
            avail_pages = phys_pages / 2;
        }

        info_out->total_bytes = (unsigned long long)page_size * (unsigned long long)phys_pages;
        info_out->free_bytes = (unsigned long long)page_size * (unsigned long long)avail_pages;
        info_out->available_bytes = info_out->free_bytes;
        return 0;
    }
#else
    return -1;
#endif
}

int platform_get_uptime_info(PlatformUptimeInfo *info_out) {
    if (info_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    info_out->uptime_seconds = 0;
    fill_load_average(info_out->load_average, sizeof(info_out->load_average));

    return fill_uptime_seconds(&info_out->uptime_seconds);
}

int platform_format_time(long long epoch_seconds, int use_local_time, const char *format, char *buffer, size_t buffer_size) {
    time_t when;
    struct tm tm_value;
    struct tm *tm_ptr;
    const char *actual_format;

    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }

    actual_format = (format != NULL && format[0] != '\0') ? format : "%Y-%m-%d %H:%M:%S";
    when = (time_t)epoch_seconds;
    tm_ptr = use_local_time ? localtime_r(&when, &tm_value) : gmtime_r(&when, &tm_value);
    if (tm_ptr == NULL) {
        buffer[0] = '\0';
        return -1;
    }

    if (strftime(buffer, buffer_size, actual_format, tm_ptr) == 0) {
        return posix_format_time_fallback(tm_ptr, actual_format, buffer, buffer_size);
    }

    return 0;
}
