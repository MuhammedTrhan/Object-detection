#ifndef CHECKER_H
#define CHECKER_H

#include <ncurses.h>
#include <pthread.h>

/**
 * @brief Number of columns in the image.
 *
 */
extern unsigned int S;

/**
 * @brief Number of rows in the image.
 *
 */
extern unsigned int Z;

/**
 * @brief Coverage value of a pixel or subpixel.
 *
 * Represents the coverage percentage of a pixel.
 * The valid range is from 0 (not covered) to 100 (fully covered).
 */
typedef int coverage_T;

/**
 * @brief Occupancy of a single pixel.
 *
 * Describes the position of a pixel in the s/z coordinate system
 * and its coverage by an object.
 */
typedef struct occupiedPixel_T
{
  unsigned int s;      /**< Column index of the pixel (0 ≤ s < S) */
  unsigned int z;      /**< Row index of the pixel (0 ≤ z < Z) */
  coverage_T coverage; /**< Coverage of the pixel in percent */
} occupiedPixel_T;

/**
 * @brief Array with information about all occupied pixels.
 *
 * The array has a maximum size of S * Z and can contain both
 * border and interior pixels.
 *
 * The actual number of valid entries is managed externally.
 */
extern occupiedPixel_T *occupiedPixels;

/**
 * @brief Number of occupied pixels detected on the edge.
 *
 * Updated by the edge-check routine (surv.c). Maximum value is S * Z.
 */
extern int numberOfOccupiedPixels;

/**
 * @brief Continuous position of an object center.
 *
 * Describes the position of an object in the s/z coordinate system
 * with floating-point precision.
 */
typedef struct objectPosition_T
{
  float s; /**< Horizontal position (column direction) */
  float z; /**< Vertical position (row direction) */
} objectPosition_T;

/**
 * @brief Singly linked list of object positions.
 *
 * Used to dynamically manage all objects currently present in the image.
 */
typedef struct objectList_T
{
  objectPosition_T object;   /**< Position of the object center */
  struct objectList_T *next; /**< Pointer to next node */
  struct objectList_T *prev; /**< Pointer to previous node */
} objectList_T;

/**
 * @brief List of detected object centers.
 *
 * This list is maintained by the detection/tracking code and stores
 * object centers in `objectPosition_T` wrapped in `objectList_T` nodes.
 */
extern objectList_T *objectPositions;

/**
 * @brief Replace the detected object list with a new set of centers.
 *
 * Updates `objectPositions` and `numberOfObjects`.
 */
void setDetectedObjects(objectPosition_T *dets, int count);

/**
 * @brief Copy up to `maxCount` detected object centers into `out`.
 *
 * Returns the number of objects copied.
 */
int getDetectedObjects(objectPosition_T *out, int maxCount);

/**
 * @brief Number of objects in the field.
 */
extern unsigned int numberOfObjects;

/**
 * @brief Determines the coverage of a pixel.
 *
 * Returns the current coverage of the pixel specified by (s, z).
 *
 * The execution time of this function is constant and is
 * approximately 1 ms.
 *
 * @param s Column index of the pixel
 * @param z Row index of the pixel
 * @return Coverage value of the pixel (0–100)
 */
coverage_T check(unsigned int s, unsigned int z);

/**
 * @brief Initializes the system.
 *
 * Initializes the object buffer, internal data structures and
 * the background timer thread that periodically calls
 * updateObjectPosition().
 *
 * @return 0 on success, non-zero on error
 */
int init(void);

/* Utility to add a simulated object (for tests/demo). */
int addObject(float s, float z, float vx, float vy);

/**
 * @brief Updates the motion state of all objects.
 *
 * This function computes the next simulation step for all active
 * objects. The current velocity vectors of the objects are evaluated
 * and new positions are derived from them.
 *
 * The function only updates motion data. Actual rendering is done
 * elsewhere.
 *
 * Objects may, as a result of this update:
 * - change their position
 * - enter the visible area
 * - leave the visible area
 *
 * @note This function should only be called manually for testing.
 *       In normal operation, updateObjectPosition() is periodically
 *       executed in its own thread via a timer.
 *
 */
void updateObjectPosition(void);

/**
 * @brief Shutdown and cleanup the checker framework (for tests)
 *
 * Stops internal background threads and frees allocated resources.
 */
void checker_shutdown(void);

#endif /* CHECKER_H */
