/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright 2022 David De Grave <david@ledav.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <pthread.h>
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
//#define fprintf(...)

typedef struct {
    unsigned long print_max;
    int thread_max;
    int lines_min;
    int lines_max;
    int lines_total;
    int uwait;
    int chances;
    int opts;
} _thread_params;

/* Test thread */
static void *thread_func_write(const _thread_params *thp)
{
    char th[LOGGER_MAX_THREAD_NAME_SZ];

    pthread_getname_np(pthread_self(), th, sizeof(th));

    for (int seq = 0; seq < thp->print_max; seq++) {
        if (!(rand() % thp->chances)) {
            fprintf(stderr, "<%s> Bad luck, waiting for %d usec\n", th, thp->uwait);
            usleep(thp->uwait);
        }
        struct timespec before, after;
        int level = rand() % LOGGER_LEVEL_COUNT;

        int r = -2;

        clock_gettime(CLOCK_MONOTONIC, &before);

        switch ( level ) {
        case LOGGER_LEVEL_EMERG:
            r = LOG_EMERGENCY("<%s> %d", th, seq); break;
        case LOGGER_LEVEL_ALERT:
            r = LOG_ALERT("<%s> %d", th, seq); break;
        case LOGGER_LEVEL_CRITICAL:
            r = LOG_CRITICAL("<%s> %d", th, seq); break;
        case LOGGER_LEVEL_ERROR:
            r = LOG_ERROR("<%s> %d", th, seq); break;
        case LOGGER_LEVEL_WARNING:
            r = LOG_WARNING("<%s> %d", th, seq); break;
        case LOGGER_LEVEL_NOTICE:
            r = LOG_NOTICE("<%s> %d", th, seq); break;
        case LOGGER_LEVEL_INFO:
            r = LOG_INFO("<%s> %d", th, seq); break;
        case LOGGER_LEVEL_DEBUG:
            r = LOG_DEBUG("<%s> %d", th, seq); break;
        case LOGGER_LEVEL_OKAY:
            r = LOG_OKAY("<%s> %d", th, seq); break;
        case LOGGER_LEVEL_TRACE:
            r = LOG_TRACE("<%s> %d", th, seq); break;
        default:
            r = LOG_OOPS("<%s> %d", th, seq); break;
        }

        clock_gettime(CLOCK_MONOTONIC, &after);

        if ( r < 0 ) {
            fprintf(stderr, "<%s> %d **LOST** (%m)\n", th, seq);
        }

        fprintf(stderr, "<%s> %lu logger_printf took %lu ns\n",
                        th, timespec_to_ns(after), elapsed_ns(before, after));
    }

    return NULL;
}

int main(int argc, char **argv)
{
    int start_wait = 0;

    if (argc < 7) {
        printf("%s <threads> <min q lines> <max q lines> <total lines> <print max/thd> <us wait> <wait chances> [blocking (0)] [printlost (0)] [delay sec]\n", argv[0]);
        return 1;
    }
    _thread_params thp = {
        .thread_max  = atoi(argv[1]),
        .lines_min   = atoi(argv[2]),
        .lines_max   = atoi(argv[3]),
        .lines_total = atoi(argv[4]),
        .print_max   = atoi(argv[5]),
        .uwait       = atoi(argv[6]),
        .chances     = atoi(argv[7]),
        .opts        = LOGGER_OPT_NOQUEUE,
    };
    if (argc > 8) {
        if (atoi(argv[8]) ) {
            thp.opts |= LOGGER_OPT_NONBLOCK;
        }
    }
    if ( argc > 9) {
        if (atoi(argv[9]) && (thp.opts & LOGGER_OPT_NONBLOCK)) {
            thp.opts |= LOGGER_OPT_PRINTLOST;
        }
    }
    if (argc > 10) {
        start_wait = atoi(argv[10]);
    }
    srand(time(NULL));

    fprintf(stderr, "cmdline: "); for (int i=0; i<argc; i++) { fprintf(stderr, "%s ", argv[i]); }
    fprintf(stderr, "\nthreads[%d] q_min[%d] q_max[%d] lines_total[%d] max_lines/thr[%lu] (1/%d chances to wait %d us) %s%s\n"
                    , thp.thread_max, thp.lines_min, thp.lines_max, thp.lines_total, thp.print_max, thp.chances, thp.uwait
                    , thp.opts & LOGGER_OPT_NONBLOCK  ? "non-blocking" : ""
                    , thp.opts & LOGGER_OPT_PRINTLOST ? "+printlost"   : "");
    fprintf(stderr, "Waiting for %d seconds after the logger-reader thread is started\n\n", start_wait);

    struct timespec before, after;
    clock_gettime(CLOCK_MONOTONIC, &before);
    fprintf(stderr, "For reference, the call to fprintf(stderr,...) to print this line took: ");
    clock_gettime(CLOCK_MONOTONIC, &after);
    fprintf(stderr, "%lu ns\n\n", elapsed_ns(before, after));

    logger_init(thp.thread_max * 1.5, 50, LOGGER_LEVEL_DEFAULT, LOGGER_OPT_NONE);
    sleep(start_wait);

    /* Writer threads */
    pthread_t tid[thp.thread_max];
    char      tnm[thp.thread_max][LOGGER_MAX_THREAD_NAME_SZ];
    unsigned long printed_lines = 0;

    for (int i=0 ; i < thp.thread_max ; i++ ) {
        int queue_size = thp.lines_min + rand() % (thp.lines_max - thp.lines_min + 1);

        snprintf(tnm[i], LOGGER_MAX_THREAD_NAME_SZ, "writer-thd-%04d", (char)i);
        logger_pthread_create(tnm[i], queue_size, thp.opts,
                                    &tid[i], NULL, (void *)thread_func_write, (void *)&thp);

        printed_lines += thp.print_max;
    }
    while ( printed_lines < thp.lines_total ) {
        for (int i=0 ; i < thp.thread_max ; i++ ) {
            if ( tid[i] && pthread_tryjoin_np(tid[i], NULL) ) {
                // Not yet terminated
                continue;
            }
            if ( printed_lines < thp.lines_total ) {
                // Not the right amount... Restart the exited thread
                int queue_size = thp.lines_min + rand() % (thp.lines_max - thp.lines_min + 1);
                logger_pthread_create(tnm[i], queue_size, thp.opts,
                                        &tid[i], NULL, (void *)thread_func_write, (void *)&thp);
                printed_lines += thp.print_max;
                fprintf(stderr, "Restarting thread %02d ...\n", i);
                continue;
            }
            tid[i] = 0;
        }
        usleep(100);
    }
    for (int i=0 ; i < thp.thread_max ; i++ ) {
        if ( tid[i] ) {
            // If not yet terminated, waiting for him ...
            pthread_join(tid[i], NULL);
        }
    }
    logger_deinit();

    fprintf(stderr, "%lu total printed lines ...\n", printed_lines);
    return 0;
}
