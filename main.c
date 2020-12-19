/*
 * Copyright (C) 2020 David De Grave <david@ledav.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE

#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "logger.h"

#define NTOS(v) ((v)/1000000000) /* nSec -> Sec  */
#define STON(v) ((v)*1000000000) /*  Sec -> nSec */
#define MTON(v) ((v)*1000000)    /* mSec -> nSec */
#define MTOU(v) ((v)*1000)       /* mSec -> uSec */

// Uncomment to strip all the debug lines for this source.
//#define fprintf

typedef struct {
    unsigned long print_max;
    int thread_max;
    int lines_min;
    int lines_max;
    int uwait;
    int chances;
} _thread_params;

atomic_int thread_idx = 0;

/* Test thread */
static void *thread_func_write(const _thread_params *thp)
{
    char th_name[LOGGER_MAX_THREAD_NAME_SZ];
    int th = atomic_fetch_add(&thread_idx, 1);

    snprintf(th_name, sizeof(th_name), "logger-W%02d", th);
    pthread_setname_np(pthread_self(), th_name);

    logger_write_queue_t *wrq = logger_get_write_queue(thp->lines_min + rand() % (thp->lines_max - thp->lines_min + 1));
    if (!wrq) {
        fprintf(stderr, "W%02d! No queue available ! Exit !\n", th);
        return NULL;
    }
    wrq->thread_idx = th;

    int seq;
    for (seq = 0; seq < thp->print_max; seq++) {
        int index = wrq->wr_seq % wrq->lines_nr;

        if (!(rand() % thp->chances)) {
            fprintf(stderr, "W%02d! Bad luck, waiting for %d usec\n", th, thp->uwait);
            usleep(thp->uwait);
        }
        struct timespec before, after;
        int level = rand() % LOGGER_LEVEL_COUNT;

        clock_gettime(CLOCK_MONOTONIC, &before);

        if ( LOG_LEVEL(level, "W%02d %lu => %d", th, seq, index) < 0 ) {
            fprintf(stderr, "W%02d! %d => %d **LOST** (%m)\n", th, seq, index);
        }
        clock_gettime(CLOCK_MONOTONIC, &after);

        fprintf(stderr, "W%02d? %lu logger_printf took %lu ns (%d)\n",
                        th, timespec_to_ns(after), elapsed_ns(before, after), index);
    }
    logger_free_write_queue();

    fprintf(stderr, "W%02d! Exit (%d/%lu lines printed/dropped)\n", th, seq, wrq->lost_total);
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        printf("%s <threads> <min lines> <max lines> <print max/thd> <us wait> <wait chances> [delay sec]\n", argv[0]);
        return 1;
    }
    _thread_params thp = {
        .thread_max = atoi(argv[1]),
        .lines_min  = atoi(argv[2]),
        .lines_max  = atoi(argv[3]),
        .print_max  = atoi(argv[4]),
        .uwait      = atoi(argv[5]),
        .chances    = atoi(argv[6]),
    };
    int start_wait = 0;
    if ( argc > 7 ) {
        start_wait = atoi(argv[7]);
    }
    srand(time(NULL));

    fprintf(stderr, "cmdline: "); for (int i=0; i<argc; i++) { fprintf(stderr, "%s ", argv[i]); }
    fprintf(stderr, "\nthread_max[%d] lines_min[%d] lines_max[%d] print_max[%lu] (1/%d chances to wait %d us)\n"
                    , thp.thread_max, thp.lines_min, thp.lines_max, thp.print_max, thp.chances, thp.uwait);
    fprintf(stderr, "Waiting for %d seconds after the logger-reader thread is started\n\n", start_wait);

    logger_init(thp.thread_max * 1.5, LOGGER_OPT_NONBLOCK|LOGGER_OPT_PRINTLOST);
    sleep(start_wait);

    /* Writer threads */
    thp.print_max >>= 2; // /4
    pthread_t tid[thp.thread_max];
    for (int i=0; i<4; i++) {
        for (long i=0 ; i < thp.thread_max ; i++ ) {
            pthread_create(&tid[i], NULL, (void *)thread_func_write, (void *)&thp);
        }
        for (int i=0 ; i < thp.thread_max ; i++ ) {
            pthread_join(tid[i], NULL);
        }
    }

    logger_deinit();
    return 0;
}
