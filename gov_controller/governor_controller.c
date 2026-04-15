// Build:
//   gcc -O2 -Wall -o governor_controller governor_controller.c
//
// Run:
//   sudo ./governor_controller 50 3 5 ./file_reader data.bin
//   or
//   sudo ./governor_controller 50 3 5 ./custom_reader data.bin 5000000
//
// Args:
//   argv[1] = sample interval in ms = 50 ms
//   argv[2] = disk hysteresis windows = 3
//   argv[3] = cache hysteresis windows = 5
//

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <time.h>

#define MAX_POLICIES 128
#define MAX_GOV_LEN  64

typedef enum {
    WIN_IDLE = 0,
    WIN_CACHE,
    WIN_DISK,
    WIN_MIXED
} win_state_t;

typedef struct {
    unsigned long long rchar;
    unsigned long long read_bytes;
    bool ok;
} io_stats_t;

typedef struct {
    char path[PATH_MAX];
    char original[MAX_GOV_LEN];
    bool changed_by_us;
} policy_t;

static policy_t policies[MAX_POLICIES];
static int policy_count = 0;
static volatile sig_atomic_t stop_requested = 0;

static void on_signal(int signo) {
    (void)signo;
    stop_requested = 1;
}

static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static int read_first_line_trim(const char *path, char *buf, size_t buflen) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, (int)buflen, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ' || buf[n - 1] == '\t')) {
        buf[--n] = '\0';
    }
    return 0;
}

static int write_string_to_file(const char *path, const char *value) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    ssize_t n = write(fd, value, strlen(value));
    close(fd);
    return (n == (ssize_t)strlen(value)) ? 0 : -1;
}

static int discover_policies(void) {
    DIR *dir = opendir("/sys/devices/system/cpu/cpufreq");
    if (!dir) {
        perror("opendir cpufreq");
        return -1;
    }

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, "policy", 6) != 0) continue;
        if (policy_count >= MAX_POLICIES) break;

        policy_t *p = &policies[policy_count];
        snprintf(p->path, sizeof(p->path),
                 "/sys/devices/system/cpu/cpufreq/%s/scaling_governor", de->d_name);

        if (access(p->path, R_OK | W_OK) != 0) continue;
        if (read_first_line_trim(p->path, p->original, sizeof(p->original)) != 0) continue;

        p->changed_by_us = false;
        policy_count++;
    }

    closedir(dir);

    if (policy_count == 0) {
        fprintf(stderr, "No writable cpufreq policy*/scaling_governor files found.\n");
        return -1;
    }
    return 0;
}

static int set_all_governors(const char *gov) {
    int ok = 0;
    for (int i = 0; i < policy_count; i++) {
        if (write_string_to_file(policies[i].path, gov) == 0) {
            policies[i].changed_by_us = true;
            ok++;
        } else {
            fprintf(stderr, "Failed to write %s to %s: %s\n",
                    gov, policies[i].path, strerror(errno));
        }
    }
    return ok > 0 ? 0 : -1;
}

static void restore_governors(void) {
    for (int i = 0; i < policy_count; i++) {
        if (!policies[i].changed_by_us) continue;
        if (write_string_to_file(policies[i].path, policies[i].original) != 0) {
            fprintf(stderr, "Failed to restore %s to %s: %s\n",
                    policies[i].original, policies[i].path, strerror(errno));
        }
    }
}

static io_stats_t read_proc_io(pid_t pid) {
    io_stats_t s = {0, 0, false};

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/proc/%d/io", pid);

    FILE *f = fopen(path, "r");
    if (!f) return s;

    char key[64];
    unsigned long long value;
    while (fscanf(f, "%63[^:]: %llu\n", key, &value) == 2) {
        if (strcmp(key, "rchar") == 0) s.rchar = value;
        else if (strcmp(key, "read_bytes") == 0) s.read_bytes = value;
    }

    fclose(f);
    s.ok = true;
    return s;
}

static win_state_t classify_window(unsigned long long drchar,
                                   unsigned long long dread_bytes) {
    // No meaningful read activity in this window.
    if (drchar < 4096ULL && dread_bytes < 4096ULL) {
        return WIN_IDLE;
    }

    // Reader made read-like progress, but storage-layer fetch did not move.
    if (drchar >= 4096ULL && dread_bytes < 4096ULL) {
        return WIN_CACHE;
    }

    // Storage-layer fetch roughly tracks requested bytes.
    // Use a ratio threshold so small noise does not count as disk.
    double ratio = 0.0;
    if (drchar > 0) ratio = (double)dread_bytes / (double)drchar;

    if (ratio >= 0.60) return WIN_DISK;
    if (ratio <= 0.10) return WIN_CACHE;
    return WIN_MIXED;
}

static const char *state_name(win_state_t s) {
    switch (s) {
        case WIN_IDLE:  return "IDLE";
        case WIN_CACHE: return "CACHE";
        case WIN_DISK:  return "DISK";
        case WIN_MIXED: return "MIXED";
        default:        return "?";
    }
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr,
            "Usage: %s <sample_ms> <disk_windows> <cache_windows> <reader_cmd> [args...]\n",
            argv[0]);
        return 1;
    }

    long sample_ms = atol(argv[1]);
    int disk_need = atoi(argv[2]);
    int cache_need = atoi(argv[3]);
    char **child_argv = &argv[4];

    if (sample_ms <= 0 || disk_need <= 0 || cache_need <= 0) {
        fprintf(stderr, "Invalid arguments.\n");
        return 1;
    }

    if (discover_policies() != 0) {
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        execvp(child_argv[0], child_argv);
        perror("execvp");
        _exit(127);
    }

    fprintf(stderr, "Started reader PID=%d\n", pid);

    io_stats_t prev = {0};
    bool have_prev = false;

    int disk_streak = 0;
    int cache_streak = 0;
    bool powersave_set = false;

    while (!stop_requested) {
        int status;
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            fprintf(stderr, "Reader exited.\n");
            break;
        } else if (w < 0 && errno != EINTR) {
            perror("waitpid");
            break;
        }

        io_stats_t cur = read_proc_io(pid);
        if (!cur.ok) {
            // /proc/<pid>/io may briefly fail near exit.
            sleep_ms(sample_ms);
            continue;
        }

        if (have_prev) {
            unsigned long long drchar = cur.rchar - prev.rchar;
            unsigned long long dread_bytes = cur.read_bytes - prev.read_bytes;

            win_state_t st = classify_window(drchar, dread_bytes);

            if (st == WIN_DISK) {
                disk_streak++;
                cache_streak = 0;
            } else if (st == WIN_CACHE) {
                cache_streak++;
                disk_streak = 0;
            } else if (st == WIN_MIXED) {
                // mixed breaks strict streaks
                disk_streak = 0;
                cache_streak = 0;
            } else {
                // idle: do not force a state change, but also do not strengthen streaks
            }

            if (!powersave_set && disk_streak >= disk_need) {
                if (set_all_governors("powersave") == 0) {
                    powersave_set = true;
                    fprintf(stderr,
                            "[ACTION] switched to powersave after %d DISK windows\n",
                            disk_streak);
                } else {
                    fprintf(stderr, "[WARN] failed to switch to powersave\n");
                }
            }

            if (powersave_set && cache_streak >= cache_need) {
                restore_governors();
                powersave_set = false;
                fprintf(stderr,
                        "[ACTION] restored original governors after %d CACHE windows\n",
                        cache_streak);
            }

            fprintf(stderr,
                    "window=%s drchar=%llu dread_bytes=%llu disk_streak=%d cache_streak=%d\n",
                    state_name(st), drchar, dread_bytes, disk_streak, cache_streak);
        }

        prev = cur;
        have_prev = true;
        sleep_ms(sample_ms);
    }

    if (stop_requested) {
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
    }

    restore_governors();
    return 0;
}