//we specify the amount of bytes we want to read at the end of command, same file_reader logic
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
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <file> <bytes_to_read>\n", argv[0]);
        return 1;
    }

    const char *path = argv[1];
    uint64_t limit = strtoull(argv[2], NULL, 10);

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

    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        perror("clock_gettime");
        free(buf);
        close(fd);
        return 1;
    }

    while (total < limit) {
        size_t want = BUF_SIZE;
        if (limit - total < BUF_SIZE) {
            want = (size_t)(limit - total);
        }

        ssize_t n = read(fd, buf, want);
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

    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
        perror("clock_gettime");
        free(buf);
        close(fd);
        return 1;
    }

    double sec = sec_diff(start, end);
    double mb = total / (1024.0 * 1024.0);

    printf("Read %.2f MB in %.6f s (%.2f MB/s)\n", mb, sec, mb / sec);

    free(buf);
    close(fd);
    return 0;
}