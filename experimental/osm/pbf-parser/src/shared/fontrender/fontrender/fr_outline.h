#ifndef FONTRENDER_FR_OUTLINE_H
#define FONTRENDER_FR_OUTLINE_H

#include <stddef.h>
#include <stdint.h>

enum {
    FR_OUTLINE_FRAC_BITS = 16,
    FR_OUTLINE_ONE = 1 << FR_OUTLINE_FRAC_BITS,
    FR_OUTLINE_POINT_ON_CURVE = 0x01u
};

typedef struct {
    int32_t x;
    int32_t y;
    uint8_t flags;
    uint8_t reserved[3];
} FrOutlinePoint;

typedef struct {
    size_t first_point;
    size_t point_count;
} FrOutlineContour;

typedef struct {
    FrOutlinePoint *points;
    size_t point_count;
    size_t point_capacity;
    FrOutlineContour *contours;
    size_t contour_count;
    size_t contour_capacity;
} FrOutline;

void fr_outline_init(FrOutline *outline);
void fr_outline_reset(FrOutline *outline);
void fr_outline_free(FrOutline *outline);
int fr_outline_reserve(FrOutline *outline, size_t point_capacity, size_t contour_capacity);
int fr_outline_add_point(FrOutline *outline, int32_t x, int32_t y, uint8_t flags);
int fr_outline_add_contour(FrOutline *outline, size_t first_point, size_t point_count);
int fr_outline_validate(const FrOutline *outline);

#endif
