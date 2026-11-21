/*
 * ReiPan for the PlayStation 5 - libc functions that no module a title loads
 * exports and that the SDK's platform layer (at PS5_Vulkan's pin) does not
 * provide. Their stubs let a title link, but the import is null at run time
 * and its first call jumps to address 0, so the title defines them itself:
 * ps5/tools/link-title.sh refuses a title that still imports one.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* A title's folders hold no symbolic or hard links, and it cannot make any
 * (RADV asks readlink for its executable's path; SDL and libc++ have the
 * rest). */
ssize_t readlink(const char *path, char *buffer, size_t size)
{
    (void)path;
    (void)buffer;
    (void)size;
    errno = EINVAL;
    return -1;
}

int symlink(const char *target, const char *path)
{
    (void)target;
    (void)path;
    errno = EPERM;
    return -1;
}

int link(const char *existing, const char *path)
{
    (void)existing;
    (void)path;
    errno = EPERM;
    return -1;
}

/* The trailing XXXXXX of the template replaced until a new file opens. */
int mkstemp(char *path_template)
{
    static const char letters[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static unsigned long long counter;
    const size_t length = path_template ? strlen(path_template) : 0;
    if (length < 6 || strcmp(path_template + length - 6, "XXXXXX") != 0)
    {
        errno = EINVAL;
        return -1;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    unsigned long long value = (unsigned long long)now.tv_nsec ^
                               ((unsigned long long)now.tv_sec << 20) ^ (counter++ * 7919);
    for (int attempt = 0; attempt < 1000; attempt++)
    {
        unsigned long long v = value + (unsigned long long)attempt * 104729;
        for (size_t i = length - 6; i < length; i++)
        {
            path_template[i] = letters[v % 62];
            v /= 62;
        }
        const int fd = open(path_template, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0 || errno != EEXIST)
        {
            return fd;
        }
    }
    errno = EEXIST;
    return -1;
}

/* timegm (ImPlot calls it): a broken-down UTC time to seconds since the
 * epoch, normalising out-of-range fields as timegm does, with days counted by
 * Howard Hinnant's days_from_civil algorithm. */

static long long days_from_civil(long long y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

time_t timegm(struct tm *tm)
{
    long long year = 1900LL + tm->tm_year;
    long long month = tm->tm_mon;
    year += month / 12;
    month %= 12;
    if (month < 0)
    {
        month += 12;
        year -= 1;
    }
    const long long days = days_from_civil(year, (unsigned)month + 1, 1) + tm->tm_mday - 1;
    const long long seconds =
        ((days * 24 + tm->tm_hour) * 60 + tm->tm_min) * 60LL + tm->tm_sec;

    /* Normalise the fields, as timegm does */
    long long day = seconds >= 0 ? seconds / 86400 : (seconds - 86399) / 86400;
    long long rest = seconds - day * 86400;
    tm->tm_hour = (int)(rest / 3600);
    tm->tm_min = (int)(rest % 3600 / 60);
    tm->tm_sec = (int)(rest % 60);
    tm->tm_wday = (int)((day % 7 + 11) % 7); /* 1970-01-01 was a Thursday */
    {
        /* civil_from_days */
        long long z = day + 719468;
        const long long era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = (unsigned)(z - era * 146097);
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned mp = (5 * doy + 2) / 153;
        const unsigned d = doy - (153 * mp + 2) / 5 + 1;
        const unsigned m = mp < 10 ? mp + 3 : mp - 9;
        const long long y = (long long)yoe + era * 400 + (m <= 2);
        tm->tm_year = (int)(y - 1900);
        tm->tm_mon = (int)m - 1;
        tm->tm_mday = (int)d;
        tm->tm_yday = (int)(day - days_from_civil(y, 1, 1));
    }
    tm->tm_isdst = 0;
    return (time_t)seconds;
}
