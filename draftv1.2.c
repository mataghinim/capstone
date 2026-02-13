#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>


int main(int argc, char *argv[]) {
    FILE *file;
    FILE *out = stdout;
    int first, last, n;

    int duration = -1;
    int interval = -1;
    char *logfile = NULL;


    //parse arguments
    int opt;
    while ((opt = getopt(argc, argv, "d:i:f:")) != -1) {
        switch (opt) {
            case 'd': duration = atoi(optarg); break;
            case 'i': interval = atoi(optarg); break;
            case 'f': logfile = optarg; break;
            default:
                fprintf(stderr,
                        "Usage: %s [-d duration] [-i interval] [-f logfile]\n",
                        argv[0]);
                return 1;
        }
    }


    if (duration <= 0 || interval <= 0) {
        fprintf(stderr, "You must specify -d and -i\n");
        exit(1);
    }

    if (logfile) {
        out = fopen(logfile, "w");
        if (!out) {
            perror("fopen");
            return 1;
        }
    }


    file = fopen("/sys/devices/system/cpu/online", "r");
    if (!file) {
        perror("Failed to open /sys/devices/system/cpu/online");
        return 1;
    }
    if(fscanf(file, "%d-%d", &first, &last)!=2){
        fprintf(stderr, "Unexpected format\n");
        fclose(file);
        return 1;
    }
    fclose(file);

    n = last - first + 1;
    printf("Number of online CPUs: %d\n", n);

    int samples =  duration/ interval;
    for (int s = 0; s < samples; s++) {
        for (int i = 0; i < n; i++) {
            char path[256];
            FILE *f;
            long freq;

            snprintf(path, sizeof(path),
                     "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq",
                     i);

            f = fopen(path, "r");
            if (!f) {
                perror(path);
                continue;
            }

            if (fscanf(f, "%ld", &freq) == 1) {
                printf("CPU%d: %ld  ", i, freq);
            }

            fclose(f);
        }

        printf("\n");
        sleep(interval);
    }

    return 0;
}

