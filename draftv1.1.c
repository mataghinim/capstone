#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

void* worker(void* arg) {
    int cpu = *(int*)arg;
    char path[256];

    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);

    while (1) {
        FILE *f = fopen(path, "r");
        long freq;

        if (f && fscanf(f, "%ld", &freq) == 1) {
            printf("CPU%d: %ld\n", cpu, freq);
        }

        if (f) fclose(f);
        sleep(5);
    }
    return NULL;
}

int main() {
    int first, last, n;
    FILE *f = fopen("/sys/devices/system/cpu/online", "r");

    if (!f || fscanf(f, "%d-%d", &first, &last) != 2){
        return 1;
    }
    fclose(f);

    n = last - first + 1;
    pthread_t cpu_threads[n];
    int cpu_id[n];

    for (int i = 0; i < n; i++) {
        cpu_id[i] = i;
        pthread_create(&cpu_threads[i], NULL, worker, &cpu_id[i]);
    }

    for (int i = 0; i < n; i++) {
        pthread_join(cpu_threads[i], NULL);
    }
    return 0;
}