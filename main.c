#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#define MAX_PATH_LENGTH     ( 512 )

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <sys/ipc.h>
#include <sys/shm.h>

#include <sys/sem.h>

union semun {
    int              val;    /* Value for SETVAL */
    struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
    unsigned short  *array;  /* Array for GETALL, SETALL */
    struct seminfo  *__buf;  /* Buffer for IPC_INFO
                                (Linux-specific) */
};

#include "semaphore_utils.h"

typedef char bool;


/* Configurations */

#define BUFFER_CELL_SIZE    ( 65536 )
#define BUFFER_CELL_NUM     ( 6 )


/* Runtime Variables */

char source_file[MAX_PATH_LENGTH] = { '\0' };
char target_file[MAX_PATH_LENGTH] = { '\0' };

typedef struct buffer_cell_t {
    char    data[BUFFER_CELL_SIZE];
    int     length;
} buffer_cell_t;

typedef struct buffer_state_t {
    int     num_cells;  // Number of buffer cells.
    int     left;       // Left edge of the first ready cell range. While
                        // `left` is equal to `oneoff`, if `is_full` is non-
                        // zero, the buffer is full; otherwise, the buffer is
                        // empty.
    int     oneoff;     // One-off the right edge of the ready cell range.
    /* bool    is_full;    // Whether the cyclic buffer is full. */
    bool    is_done;    // If non-zero, no more changes to the buffer will be
                        // done.
} buffer_state_t;

int buffer_shmid;
key_t reader_key;
buffer_cell_t *pbuffer;     // shared memory attach point

int state_shmid;
key_t state_shm_key;
buffer_state_t *pstate;     // shared memory attach point

/*
 * buffer state related semaphres
 * 0 - state variable operation semaphore
 * 1 - cbuf_getputloc() semaphore
 * 2 - cbuf_getgetloc() semaphore
 */
int state_semid;
key_t state_sem_key;


/* Helper Functions - Cyclic Buffer */

void cbuf_print(buffer_state_t state) {
    printf("left = %d, oneoff = %d\n", state.left, state.oneoff);
    return;
}


int cbuf_getputloc(buffer_state_t *pstate) {
    // cost one writable cell
    P(state_semid, 1);
    P(state_semid, 0);
    int retval = pstate->oneoff;
    pstate->oneoff = (pstate->oneoff + 1) % pstate->num_cells;
    V(state_semid, 0);
    return retval;
}


void cbuf_regput(void) {
    // open new buffer cell for read
    V(state_semid, 2);
    return;
}


int cbuf_getgetloc(buffer_state_t *pstate) {
    // cost one readable cell
    P(state_semid, 2);
    P(state_semid, 0);
    int retval = pstate->left;
    pstate->left = (pstate->left + 1) % pstate->num_cells;
    V(state_semid, 0);
    return retval;
}


void cbuf_regget(void) {
    V(state_semid, 1);
    return;
}


void cbuf_setdone(buffer_state_t *pstate) {
    P(state_semid, 0);
    pstate->is_done = 1;
    V(state_semid, 0);
    return;
}


/* Helper Functions - Jobs Common */

void worker_cleanup(void) {
    if (shmdt(pbuffer) == -1) {
        perror("shmdt");
        exit(EXIT_FAILURE);
    }
    if (shmdt(pstate) == -1) {
        perror("shmdt");
        exit(EXIT_FAILURE);
    }
    return;
}


/* Jobs */

void reader_fn(void) {
    if ((pbuffer = shmat(buffer_shmid, NULL, 0)) == (void *)-1) {
        perror("shmat:101");
        exit(EXIT_FAILURE);
    }
    if ((pstate = shmat(state_shmid, NULL, 0)) == (void *)-1) {
        perror("shmat:105");
        exit(EXIT_FAILURE);
    }
    /* printf("reader_fn: pbuffer = %p, pstate = %p\n", pbuffer, pstate); */
    // open source file
    FILE *psrc;
    if ((psrc = fopen(source_file, "r")) == NULL) {
        perror("fopen");
        worker_cleanup();
        exit(EXIT_FAILURE);
    }
    int putloc;
    // into the copy loop!
    while (!feof(psrc)) {
        /* printf("new round: %d\n", rand()); */
        // get next put location in cyclic buffer
        putloc = cbuf_getputloc(pstate);    // block if no available cell
        // read file, time-consuming
        /* printf("Reading file content to %d-th buffer cell...\n", putloc); */
        pbuffer[putloc].length = fread(pbuffer[putloc].data, sizeof(char), BUFFER_CELL_SIZE, psrc);
        /* printf("Registering last put action.\n"); */
        cbuf_regput();
        /* cbuf_print(*pstate); */
    }
    // notify the other process reading is done
    cbuf_setdone(pstate);
    // cleanups
    worker_cleanup();
    if (fclose(psrc) == EOF) {
        perror("fclose");
        exit(EXIT_FAILURE);
    }
    /* printf("Reader quit!\n"); */
    return;
}


void writer_fn(void) {
    if ((pbuffer = shmat(buffer_shmid, NULL, 0)) == (void *)-1) {
        perror("shmat:124");
        exit(EXIT_FAILURE);
    }
    if ((pstate = shmat(state_shmid, NULL, 0)) == (void *)-1) {
        perror("shmat:128");
        exit(EXIT_FAILURE);
    }
    // create / truncate target file
    FILE *pdst;
    if ((pdst = fopen(target_file, "w")) == NULL) {
        perror("fopen");
        worker_cleanup();
        exit(EXIT_FAILURE);
    }
    int getloc;
    // into the write loop!
    while (1) {
        // if `is_done` is set, and no more readable cell, the copy is done
        if (pstate->is_done && semctl(state_semid, 2, GETVAL) == 0) {
            break;
        }
        // get latest get location
        getloc = cbuf_getgetloc(pstate);
        // write file, also time-consuming
        /* printf("Writing %d-th buffer cell's content...\n", getloc); */
        /* puts(pbuffer[getloc]); */
        fwrite(pbuffer[getloc].data, sizeof(char), pbuffer[getloc].length, pdst);
        cbuf_regget();
        /* printf("Registered last get action.\n"); */
    }
    // cleanups
    worker_cleanup();
    if (fclose(pdst) == EOF) {
        perror("fclose");
        exit(EXIT_FAILURE);
    }
    /* printf("Writer quit!\n"); */
    return;
}



/* Main */

int main(int argc, const char **argv) {

    // program configuration setup
    if ((argc != 2 && argc != 3) || (strncmp("-h", argv[1], 3) == 0)) {
        printf("Usage: PROG_NAME SOURCE_PATH [TARGET_PATH]\n");
        exit(EXIT_FAILURE);
    }

    strncpy(source_file, argv[1], MAX_PATH_LENGTH);
    // if no target path provided, use current working dir
    if (argc == 3) {
        strncpy(target_file, argv[2], MAX_PATH_LENGTH);
    } else {
        if (getcwd(target_file, MAX_PATH_LENGTH) == NULL) {
            perror("getcwd");
            exit(EXIT_FAILURE);
        }
        if (strnlen(target_file, MAX_PATH_LENGTH) > MAX_PATH_LENGTH - 10) {
            printf("Target path is too long!\n");
            exit(EXIT_FAILURE);
        }
        strncat(target_file, "/out", 5);
    }

    /* printf("Copying from %s\n   to %s\n", source_file, target_file); */

    // build shared buffer space
    if ((reader_key = ftok(source_file, 'r')) == -1) {
        perror("ftok");
        exit(EXIT_FAILURE);
    }
    if ((buffer_shmid = shmget(reader_key, BUFFER_CELL_NUM * sizeof(buffer_cell_t), IPC_CREAT | 0666)) == -1) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    // shared cyclic buffer state
    state_shm_key = rand();
    if ((state_shmid = shmget(state_shm_key, sizeof(buffer_state_t), IPC_CREAT | 0666)) == -1) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }
    if ((pstate = shmat(state_shmid, NULL, 0)) == (void *)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
    pstate->num_cells = BUFFER_CELL_NUM;
    pstate->left = 0;
    pstate->oneoff = 0;
    /* pstate->is_full = 0; */
    pstate->is_done = 0;

    // semaphore for buffer state
    state_sem_key = rand();
    if ((state_semid = semget(state_sem_key, 3, IPC_CREAT | 0666)) == -1) {
        perror("semget");
        exit(EXIT_FAILURE);
    }
    union semun semarg;
    // buffer state operation semaphore
    semarg.val = 1;
    if (semctl(state_semid, 0, SETVAL, semarg) == -1) {
        perror("semctl");
        exit(EXIT_FAILURE);
    }
    // cbuf_getputloc() semaphore
    semarg.val = BUFFER_CELL_NUM;
    if (semctl(state_semid, 1, SETVAL, semarg) == -1) {
        perror("semctl");
        exit(EXIT_FAILURE);
    }
    // cbuf_getgetloc() semaphore
    semarg.val = 0;
    if (semctl(state_semid, 2, SETVAL, semarg) == -1) {
        perror("semctl");
        exit(EXIT_FAILURE);
    }

    // fork worker processes
    pid_t reader_pid, writer_pid;
    if ((reader_pid = fork()) == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (reader_pid == 0) {
        reader_fn();
        exit(EXIT_SUCCESS);
    }
    if ((writer_pid = fork()) == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (writer_pid == 0) {
        writer_fn();
        exit(EXIT_SUCCESS);
    }

    // wait for all workers to join
    while (wait(NULL) > 0) ;
    /* printf("Workers all quit!\n"); */

    // release resources, mark shared memory segments and semaphores for destruction
    if (shmctl(buffer_shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl");
        exit(EXIT_FAILURE);
    }
    if (shmdt(pstate) == -1) {
        perror("shmdt");
        exit(EXIT_FAILURE);
    }
    if (shmctl(state_shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl");
        exit(EXIT_FAILURE);
    }
    if (semctl(state_semid, 0, IPC_RMID, 0) == -1) {
        perror("semctl");
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}

