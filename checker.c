#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "checker.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

/* Definitions for globals declared in checker.h */
unsigned int S = 80;
unsigned int Z = 25;
occupiedPixel_T *occupiedPixels = NULL;
static unsigned int simNumberOfObjects = 0; /* internal simulation count */
unsigned int numberOfObjects = 0;           /* detection count (updated via setDetectedObjects) */
int numberOfOccupiedPixels = 0;

/* List of detected object centers (objectList_T nodes). */
objectList_T *objectPositions = NULL;
/* Mutex protecting objectPositions and numberOfObjects */
static pthread_mutex_t objectPositions_lock = PTHREAD_MUTEX_INITIALIZER;

/* Internal data structures */
typedef struct internalObject_T
{
    objectList_T node; /* contains position + next/prev */
    float vx;          /* velocity in columns (s) per second */
    float vy;          /* velocity in rows (z) per second */
} internalObject_T;

/* Head/tail of the object list */
static internalObject_T *obj_head = NULL;
static internalObject_T *obj_tail = NULL;

/* Synchronization primitives */
static pthread_rwlock_t obj_lock;

/* Timer thread that calls updateObjectPosition periodically */
static pthread_t timer_thread;
static int timer_running = 0;
static const unsigned int TIMER_MS = 100; /* 100 ms tick */

/* Helper: add and remove objects (internal) */
static void add_internal_object(internalObject_T *o)
{
    pthread_rwlock_wrlock(&obj_lock);
    o->node.prev = (objectList_T *)obj_tail;
    o->node.next = NULL;
    if (obj_tail)
        obj_tail->node.next = (objectList_T *)o;
    else
        obj_head = o;
    obj_tail = o;
    simNumberOfObjects++;
    pthread_rwlock_unlock(&obj_lock);
}

/* Add a simulated object */
int addObject(float sx, float zy, float vx, float vy)
{
    internalObject_T *o = malloc(sizeof(*o));
    if (!o)
        return -1;
    o->node.object.s = sx;
    o->node.object.z = zy;
    o->vx = vx;
    o->vy = vy;
    o->node.next = NULL;
    o->node.prev = NULL;
    add_internal_object(o);
    return 0;
}

/* Compute overlap area between two 1D intervals */
static float overlap1d(float a1, float a2, float b1, float b2)
{
    float lo = fmaxf(a1, b1);
    float hi = fminf(a2, b2);
    return fmaxf(0.0f, hi - lo);
}

/* Public API: check(s,z) -> coverage percent [0..100]
   For each object (square of side 1 centered at object position) compute
   its area overlap with pixel [s,s+1)x[z,z+1). Sum overlaps and return
   100 * overlap_area (clamped to 100). */
coverage_T check(unsigned int s, unsigned int z)
{
    float area = 0.0f;
    float px1 = (float)s;
    float px2 = (float)s + 1.0f;
    float py1 = (float)z;
    float py2 = (float)z + 1.0f;

    pthread_rwlock_rdlock(&obj_lock);
    for (internalObject_T *it = obj_head; it != NULL; it = (internalObject_T *)it->node.next)
    {
        float cx = it->node.object.s;
        float cy = it->node.object.z;
        float ox1 = cx - 0.5f;
        float ox2 = cx + 0.5f;
        float oy1 = cy - 0.5f;
        float oy2 = cy + 0.5f;
        float wx = overlap1d(px1, px2, ox1, ox2);
        float wy = overlap1d(py1, py2, oy1, oy2);
        area += wx * wy;
    }
    pthread_rwlock_unlock(&obj_lock);

    float cov = area * 100.0f; /* area (in pixels) -> percent */
    if (cov > 100.0f)
        cov = 100.0f;
    if (cov < 0.0f)
        cov = 0.0f;
    return (coverage_T)roundf(cov);
}

/* Update object positions according to velocities. Remove objects
   that have left the image (center far outside bounds). */
void updateObjectPosition(void)
{
    const float dt = ((float)TIMER_MS) / 1000.0f;
    pthread_rwlock_wrlock(&obj_lock);
    internalObject_T *it = obj_head;
    while (it)
    {
        internalObject_T *next = (internalObject_T *)it->node.next;
        it->node.object.s += it->vx * dt;
        it->node.object.z += it->vy * dt;
        /* if object center is beyond reasonable bounds, remove it */
        if (it->node.object.s < -2.0f || it->node.object.s > (float)S + 2.0f ||
            it->node.object.z < -2.0f || it->node.object.z > (float)Z + 2.0f)
        {
            /* remove safely: adjust links here since we hold write lock */
            if (it->node.prev)
                ((internalObject_T *)it->node.prev)->node.next = it->node.next;
            else
                obj_head = (internalObject_T *)it->node.next;

            if (it->node.next)
                ((internalObject_T *)it->node.next)->node.prev = it->node.prev;
            else
                obj_tail = (internalObject_T *)it->node.prev;

            simNumberOfObjects--;
            free(it);
        }
        it = next;
    }
    pthread_rwlock_unlock(&obj_lock);
}

/* Timer thread routine */
static void *timer_loop(void *arg)
{
    (void)arg;
    while (timer_running)
    {
        struct timespec req = {.tv_sec = 0, .tv_nsec = (long)TIMER_MS * 1000000L};
        nanosleep(&req, NULL);
        updateObjectPosition();
    }
    return NULL;
}

/* Initialize framework: allocate buffers and start timer thread */
int init(void)
{
    if (occupiedPixels)
        free(occupiedPixels);
    occupiedPixels = malloc(sizeof(occupiedPixel_T) * (size_t)S * (size_t)Z);
    if (!occupiedPixels)
        return -1;

    /* initialize synchronization */
    pthread_rwlock_init(&obj_lock, NULL);

    /* start timer thread */
    timer_running = 1;
    if (pthread_create(&timer_thread, NULL, timer_loop, NULL) != 0)
    {
        timer_running = 0;
        free(occupiedPixels);
        occupiedPixels = NULL;
        return -1;
    }
    pthread_detach(timer_thread);
    return 0;
}

/* Simple frame renderer (stdout fallback) */
void render_frame(void)
{
    /* If ncurses is initialized, use it; otherwise print to stdout line-based */
    /* We do not require ncurses here; rendering is intended to be called by the
       surv program which will handle ncurses context. For simplicity we provide
       a simple stdout fallback. */
    for (unsigned int z = 0; z < Z; ++z)
    {
        for (unsigned int s = 0; s < S; ++s)
        {
            coverage_T c = check(s, z);
            char ch = ' ';
            if (c == 0)
                ch = '.';
            else if (c < 25)
                ch = ':';
            else if (c < 50)
                ch = 'o';
            else if (c < 75)
                ch = 'O';
            else
                ch = '@';
            putchar(ch);
        }
        putchar('\n');
    }
    printf("Objects: %u\n", numberOfObjects);
}

/* Cleanup: stop timer and free resources */
void checker_shutdown(void)
{
    timer_running = 0;
    /* give thread a moment to exit (wait in milliseconds) */
    {
        struct timespec ts = {.tv_sec = (2 * TIMER_MS) / 1000, .tv_nsec = ((2 * TIMER_MS) % 1000) * 1000000L};
        nanosleep(&ts, NULL);
    }

    pthread_rwlock_wrlock(&obj_lock);
    internalObject_T *it = obj_head;
    while (it)
    {
        internalObject_T *next = (internalObject_T *)it->node.next;
        free(it);
        it = next;
    }
    obj_head = obj_tail = NULL;
    simNumberOfObjects = 0;
    pthread_rwlock_unlock(&obj_lock);

    /* Clear detected object list */
    pthread_mutex_lock(&objectPositions_lock);
    objectList_T *ot = objectPositions;
    while (ot)
    {
        objectList_T *n = ot->next;
        free(ot);
        ot = n;
    }
    objectPositions = NULL;
    numberOfObjects = 0;
    pthread_mutex_unlock(&objectPositions_lock);

    if (occupiedPixels)
    {
        free(occupiedPixels);
        occupiedPixels = NULL;
    }
}

/* Replace detected object list */
void setDetectedObjects(objectPosition_T *dets, int count)
{
    pthread_mutex_lock(&objectPositions_lock);

    /* free old list */
    objectList_T *it = objectPositions;
    while (it)
    {
        objectList_T *n = it->next;
        free(it);
        it = n;
    }
    objectPositions = NULL;

    /* build new list (doubly linked) */
    objectList_T *tail = NULL;
    for (int i = 0; i < count; ++i)
    {
        objectList_T *n = malloc(sizeof(objectList_T));
        if (!n)
            break;
        n->object = dets[i];
        n->next = NULL;
        n->prev = tail;
        if (tail)
            tail->next = n;
        else
            objectPositions = n;
        tail = n;
    }
    numberOfObjects = 0;
    for (objectList_T *p = objectPositions; p != NULL; p = p->next)
        numberOfObjects++;

    pthread_mutex_unlock(&objectPositions_lock);
}

/* Copy up to maxCount detected object centers into out, return copied count */
int getDetectedObjects(objectPosition_T *out, int maxCount)
{
    pthread_mutex_lock(&objectPositions_lock);
    int i = 0;
    for (objectList_T *p = objectPositions; p != NULL && i < maxCount; p = p->next)
    {
        out[i++] = p->object;
    }
    pthread_mutex_unlock(&objectPositions_lock);
    return i;
}
