#include "semaphore_utils.h"


void P(int semid, int idx) {
    struct sembuf sem;
    sem.sem_num = idx;
    sem.sem_op = -1;    // mutex - 1
    sem.sem_flg = 0;
    semop(
        /*semid=*/semid,
        /*sops=*/&sem,
        /*nsops=*/1
    );
    return;
}


void V(int semid, int idx) {
    struct sembuf sem;
    sem.sem_num = idx;
    sem.sem_op = +1;    // mutex + 1
    sem.sem_flg = 0;
    semop(
        /*semid=*/semid,
        /*sops=*/&sem,
        /*nops=*/1
    );
    return;
}

