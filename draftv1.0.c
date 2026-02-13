#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>


int main(void){
    FILE *file;
    int first, last, n;

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

    while (1) {
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
        sleep(2);
    }

    return 0;
}
