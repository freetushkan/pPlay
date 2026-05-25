#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

/*
 * Some OpenOrbis SDK library combinations reference getentropy(), but
 * older/newlib-based libc variants do not export it. Provide a small
 * compatibility implementation for the PS4 packaging link step.
 */
int getentropy(void *buf, size_t buflen)
{
    if (!buf) {
        errno = EFAULT;
        return -1;
    }

    if (buflen > 256) {
        errno = EIO;
        return -1;
    }

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    uint8_t *ptr = (uint8_t *)buf;
    size_t done = 0;
    while (done < buflen) {
        ssize_t ret = read(fd, ptr + done, buflen - done);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return -1;
        }
        if (ret == 0) {
            errno = EIO;
            close(fd);
            return -1;
        }
        done += (size_t)ret;
    }

    close(fd);
    return 0;
}
