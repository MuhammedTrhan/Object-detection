#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <ncurses.h>
#include <pthread.h>

#include "checker.h"
#include <string.h>
#include <math.h>

static volatile int keep_running = 1;

static void sigint_handler(int sig)
{
    (void)sig;
    keep_running = 0;
}

static void msleep(unsigned int ms)
{
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

/* Map coverage to a display character */
static int cov_char(coverage_T c)
{
    if (c == 0)
        return '.';
    if (c < 25)
        return ':';
    if (c < 50)
        return 'o';
    if (c < 75)
        return 'O';
    return '@';
}

/* Types and worker moved to file scope so we don't define nested functions */
typedef struct
{
    unsigned int s;
    unsigned int z;
} coord_t;
typedef struct
{
    coord_t *coords;
    int start, end;
} worker_arg_t;
typedef struct
{
    occupiedPixel_T *res;
    int count;
} worker_res_t;

static void *edge_worker(void *arg)
{
    worker_arg_t *wa = (worker_arg_t *)arg;
    int cap = wa->end - wa->start;
    if (cap <= 0)
    {
        worker_res_t *empty = malloc(sizeof(worker_res_t));
        empty->res = NULL;
        empty->count = 0;
        return empty;
    }

    occupiedPixel_T *local = malloc(sizeof(occupiedPixel_T) * cap);
    int cnt = 0;
    for (int i = wa->start; i < wa->end; ++i)
    {
        unsigned int cs = wa->coords[i].s;
        unsigned int cz = wa->coords[i].z;
        coverage_T c = check(cs, cz);
        if (c > 0)
        {
            local[cnt].s = cs;
            local[cnt].z = cz;
            local[cnt].coverage = c;
            cnt++;
        }
    }
    worker_res_t *wr = malloc(sizeof(worker_res_t));
    wr->res = local;
    wr->count = cnt;
    return wr;
}

int main(int argc, char **argv)
{
    int demo_mode = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--demo") == 0)
            demo_mode = 1;
    }

    signal(SIGINT, sigint_handler);

    if (init() != 0)
    {
        fprintf(stderr, "Failed to initialize checker framework\n");
        return 1;
    }

    /* If demo mode is enabled, spawn a few objects coming in from edges */
    if (demo_mode)
    {
        /* left -> right */
        addObject(-0.5f, (float)Z * 0.25f, 0.6f, 0.0f);
        /* right -> left */
        addObject((float)S + 0.5f, (float)Z * 0.55f, -0.5f, 0.0f);
        /* top -> down */
        addObject((float)S * 0.33f, -0.5f, 0.0f, 0.5f);
        /* bottom -> up */
        addObject((float)S * 0.66f, (float)Z + 0.5f, 0.0f, -0.45f);
    }

    /* Initialize ncurses */
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);

    /* Main loop: check all edge pixels in parallel (up to 4 threads) */
    while (keep_running)
    {
        /* Build list of edge coordinates */
        int R = 0;
        /* Upper bound allocation */
        int maxR = 2 * S + 2 * ((int)Z > 2 ? (int)Z - 2 : 0);
        coord_t *coords = malloc(sizeof(coord_t) * maxR);
        if (!coords)
            break;

        /* Top row */
        for (unsigned int s = 0; s < S; ++s)
            coords[R++] = (coord_t){s, 0};
        /* Bottom row */
        if (Z > 1)
            for (unsigned int s = 0; s < S; ++s)
                coords[R++] = (coord_t){s, Z - 1};
        /* Left/Right columns excluding corners */
        if (Z > 2)
        {
            for (unsigned int z = 1; z < Z - 1; ++z)
            {
                coords[R++] = (coord_t){0, z};
                if (S > 1)
                    coords[R++] = (coord_t){S - 1, z};
            }
        }

        /* Prepare for parallel checking */
        numberOfOccupiedPixels = 0; /* will be set after aggregation */
        int num_threads = 4;
        if (R < num_threads)
            num_threads = R > 0 ? R : 1;

        pthread_t threads[4];
        worker_arg_t args[4];

        int base = 0;
        int per = R / num_threads;
        int rem = R % num_threads;
        for (int t = 0; t < num_threads; ++t)
        {
            int st = base;
            int en = st + per + (t < rem ? 1 : 0);
            base = en;
            args[t].coords = (coord_t *)coords;
            args[t].start = st;
            args[t].end = en;
            pthread_create(&threads[t], NULL, edge_worker, &args[t]);
        }

        /* Aggregate results */
        int total = 0;
        for (int t = 0; t < num_threads; ++t)
        {
            worker_res_t *wr;
            pthread_join(threads[t], (void **)&wr);
            if (wr)
            {
                for (int k = 0; k < wr->count; ++k)
                {
                    if (total < (int)(S * Z))
                    {
                        occupiedPixels[total] = wr->res[k];
                        total++;
                    }
                }
                free(wr->res);
                free(wr);
            }
        }
        numberOfOccupiedPixels = total;

        /* Object detection: find connected components of occupied pixels and compute one centroid per component */
        int N = (int)S * (int)Z;
        float *cov = malloc(sizeof(float) * (size_t)N);
        unsigned char *vis = malloc((size_t)N);
        int *stack = malloc(sizeof(int) * (size_t)N);
        if (!cov || !vis || !stack)
        {
            free(cov);
            free(vis);
            free(stack);
            continue; /* skip this cycle if memory allocation fails */
        }
        /* Fill coverage grid */
        for (unsigned int z = 0; z < Z; ++z)
        {
            for (unsigned int s = 0; s < S; ++s)
            {
                int idx = (int)z * (int)S + (int)s;
                coverage_T cc = check(s, z);
                cov[idx] = cc / 100.0f;
                vis[idx] = 0;
            }
        }

        objectPosition_T *dets = malloc(sizeof(objectPosition_T) * (size_t)N);
        int det_count = 0;

        for (int idx0 = 0; idx0 < N; ++idx0)
        {
            if (vis[idx0] || cov[idx0] <= 0.0f)
                continue;
            /* BFS flood-fill */
            int sp = 0;
            stack[sp++] = idx0;
            vis[idx0] = 1;
            float sumA = 0.0f, sumX = 0.0f, sumY = 0.0f;
            while (sp > 0)
            {
                int idx = stack[--sp];
                int z = idx / (int)S;
                int s = idx % (int)S;
                float a = cov[idx];
                sumA += a;
                sumX += a * (s + 0.5f);
                sumY += a * (z + 0.5f);
                /* 4-neighbors */
                int nx, nz, nidx;
                nx = s - 1;
                nz = z;
                if (nx >= 0)
                {
                    nidx = nz * (int)S + nx;
                    if (!vis[nidx] && cov[nidx] > 0.0f)
                    {
                        vis[nidx] = 1;
                        stack[sp++] = nidx;
                    }
                }
                nx = s + 1;
                nz = z;
                if (nx < (int)S)
                {
                    nidx = nz * (int)S + nx;
                    if (!vis[nidx] && cov[nidx] > 0.0f)
                    {
                        vis[nidx] = 1;
                        stack[sp++] = nidx;
                    }
                }
                nx = s;
                nz = z - 1;
                if (nz >= 0)
                {
                    nidx = nz * (int)S + nx;
                    if (!vis[nidx] && cov[nidx] > 0.0f)
                    {
                        vis[nidx] = 1;
                        stack[sp++] = nidx;
                    }
                }
                nx = s;
                nz = z + 1;
                if (nz < (int)Z)
                {
                    nidx = nz * (int)S + nx;
                    if (!vis[nidx] && cov[nidx] > 0.0f)
                    {
                        vis[nidx] = 1;
                        stack[sp++] = nidx;
                    }
                }
            }
            if (sumA < 0.05f)
                continue; /* ignore tiny noise */
            dets[det_count].s = sumX / sumA;
            dets[det_count].z = sumY / sumA;
            det_count++;
        }

        free(cov);
        free(vis);
        free(stack);

        /* Publish detections to the canonical detected-list */
        setDetectedObjects(dets, det_count);
        free(dets);

        /* Note: setDetectedObjects atomically updates `objectPositions` and `numberOfObjects` */

        /* Visualization: draw full grid using check() results */
        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        if ((int)Z + 6 + (int)numberOfObjects > rows || (int)S + 1 > cols)
        {
            clear();
            mvprintw(0, 0, "Terminal too small: need at least %u cols x %u rows", S + 1, Z + 6 + numberOfObjects);
            mvprintw(1, 0, "Press 'q' or Ctrl-C to quit");
            refresh();
            msleep(200);
            int ch = getch();
            if (ch == 'q' || ch == 'Q')
                break;
            continue;
        }

        /* Draw pixels */
        for (unsigned int z = 0; z < Z; ++z)
        {
            for (unsigned int s = 0; s < S; ++s)
            {
                coverage_T c = check(s, z);
                int ch = cov_char(c);
                mvaddch(z, s, ch);
            }
        }

        /* Retrieve canonical detected objects and display them */
        int max_objs = (int)(S * Z);
        objectPosition_T *objs = malloc(sizeof(objectPosition_T) * (size_t)max_objs);
        int n_objs = 0;
        int row = Z + 2;
        if (objs)
        {
            n_objs = getDetectedObjects(objs, max_objs);
            for (int i = 0; i < n_objs; ++i)
            {
                int si = (int)roundf(objs[i].s);
                int zi = (int)roundf(objs[i].z);
                if (si >= 0 && si < (int)S && zi >= 0 && zi < (int)Z)
                    mvaddch(zi, si, 'O' | A_BOLD);
            }
            mvprintw(Z + 1, 0, "Detected objects: %u", numberOfObjects);
            for (int i = 0; i < n_objs && row < rows - 1; ++i)
                mvprintw(row++, 0, "#%2d: x=%6.2f y=%6.2f", i, objs[i].s, objs[i].z);
            free(objs);
        }
        else
        {
            mvprintw(Z + 1, 0, "Detected objects: %u", numberOfObjects);
        }

        mvprintw(row + 1, 0, "Press 'q' to quit.");
        refresh();

        /* Check for user input */
        int ch = getch();
        if (ch == 'q' || ch == 'Q')
            break;

        msleep(100); /* cycle delay */
    }

    endwin();

    /* Clean up checker framework */
    checker_shutdown();
    return 0;
}
