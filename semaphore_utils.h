#ifndef SEMAPHORE_UTILS_H
#define SEMAPHORE_UTILS_H

#include <stdlib.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

/**
 * P Operation
 * :param semid: id of semaphore
 * :param idx: index of the target semaphore in the group
 */
void P(int semid, int idx);

/**
 * V Operation
 * :param semid: id of the semaphore
 * :param idx: index of the target semaphore in the group
 */
void V(int semid, int idx);

#endif

