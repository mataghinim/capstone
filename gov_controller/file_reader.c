#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

#define BUF_SIZE (1024 * 1024)

static double sec_diff(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return 1;
    }

    const char *path = argv[1];
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open(%s) failed: %s\n", path, strerror(errno));
        return 1;
    }

    char *buf = malloc(BUF_SIZE);
    if (!buf) {
        perror("malloc");
        close(fd);
        return 1;
    }

    uint64_t total = 0;
    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);

    //while (1) {
    while (total < 1024ULL * 1024ULL * 2048ULL) {
        ssize_t n = read(fd, buf, BUF_SIZE);
        if (n < 0) {
            perror("read");
            free(buf);
            close(fd);
            return 1;
        }
        if (n == 0) {
            break;
        }
        total += (uint64_t)n;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double sec = sec_diff(start, end);
    double mb = total / (1024.0 * 1024.0);

    printf("Read %.2f MB in %.3f s (%.2f MB/s)\n", mb, sec, mb / sec);

    free(buf);
    close(fd);
    return 0;
}