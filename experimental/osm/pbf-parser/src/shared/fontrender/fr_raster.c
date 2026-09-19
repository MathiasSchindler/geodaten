#include "fontrender/fr_raster.h"

#include <limits.h>
#include <stddef.h>

#include "fr_platform_internal.h"

#if !defined(FR_RASTER_DISABLE_SIMD)
#  if defined(__SSE2__)
#    include <emmintrin.h>
#    define FR_RASTER_HAVE_SSE2 1
#  elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#    include <arm_neon.h>
#    define FR_RASTER_HAVE_NEON 1
#  endif
#endif

enum {
    FR_OUTLINE_POINT_MASK = FR_OUTLINE_POINT_ON_CURVE,
    FR_RASTER_SUBSAMPLES = 8,
    FR_RASTER_TOTAL_SAMPLES = FR_RASTER_SUBSAMPLES * FR_RASTER_SUBSAMPLES,
    FR_RASTER_MAX_DIMENSION = 4096,
    FR_RASTER_MAX_PIXELS = 4096 * 4096,
    FR_QUAD_FLATNESS_LIMIT = FR_OUTLINE_ONE / 8,
    FR_QUAD_MAX_DEPTH = 16
};

typedef struct {
    int32_t x0;
    int32_t y0;
    int32_t dx;
    int32_t dy;
    int32_t y_min;
    int32_t y_max;
    int8_t winding_delta;
} FrEdge;

typedef struct {
    FrEdge *items;
    size_t count;
    size_t capacity;
} FrEdgeList;

typedef struct {
    size_t start;
    size_t count;
} FrRowEdgeSpan;

typedef struct {
    FrOutlinePoint p0;
    FrOutlinePoint p1;
    FrOutlinePoint p2;
    unsigned depth;
} FrQuadTask;

typedef struct {
    size_t edge_index;
    int64_t x;
    int64_t quotient;
    int64_t remainder;
    int64_t quotient_step;
    int64_t remainder_step;
    int32_t x0;
    int32_t dy;
    int8_t winding_delta;
} FrActiveEdge;

static int fr_reserve_array(void **items, size_t element_size, size_t *capacity, size_t required_count) {
    void *grown;
    size_t new_capacity;

    if (required_count <= *capacity) {
        return 0;
    }
    if (required_count > SIZE_MAX / element_size) {
        return -1;
    }

    new_capacity = *capacity != 0u ? *capacity : 8u;
    while (new_capacity < required_count) {
        if (new_capacity > SIZE_MAX / 2u) {
            new_capacity = required_count;
            break;
        }
        new_capacity *= 2u;
    }
    if (new_capacity > SIZE_MAX / element_size) {
        return -1;
    }

    grown = fr_platform_realloc(*items, new_capacity * element_size);
    if (grown == NULL) {
        return -1;
    }

    *items = grown;
    *capacity = new_capacity;
    return 0;
}

static int32_t fr_fixed_midpoint(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a + (int64_t)b) / 2ll);
}

static int fr_fixed_floor_to_int(int32_t value) {
    int whole = value / FR_OUTLINE_ONE;
    int frac = value % FR_OUTLINE_ONE;

    if (value < 0 && frac != 0) {
        whole -= 1;
    }
    return whole;
}

static int fr_fixed_ceil_to_int(int32_t value) {
    int whole = value / FR_OUTLINE_ONE;
    int frac = value % FR_OUTLINE_ONE;

    if (value > 0 && frac != 0) {
        whole += 1;
    }
    return whole;
}

static int32_t fr_double_floor_to_i32(double value) {
    int32_t whole = (int32_t)value;

    if ((double)whole > value) {
        whole -= 1;
    }
    return whole;
}

static int32_t fr_double_ceil_to_i32(double value) {
    int32_t whole = (int32_t)value;

    if ((double)whole < value) {
        whole += 1;
    }
    return whole;
}

static int64_t fr_div_floor_i64(int64_t numerator, int64_t denominator) {
    int64_t quotient = numerator / denominator;
    int64_t remainder = numerator % denominator;

    if (remainder != 0 && remainder < 0) {
        quotient -= 1;
    }
    return quotient;
}

static int64_t fr_div_ceil_i64(int64_t numerator, int64_t denominator) {
    return -fr_div_floor_i64(-numerator, denominator);
}

static FrOutlinePoint fr_outline_point_midpoint(FrOutlinePoint a, FrOutlinePoint b) {
    FrOutlinePoint mid;

    mid.x = fr_fixed_midpoint(a.x, b.x);
    mid.y = fr_fixed_midpoint(a.y, b.y);
    mid.flags = FR_OUTLINE_POINT_ON_CURVE;
    mid.reserved[0] = 0u;
    mid.reserved[1] = 0u;
    mid.reserved[2] = 0u;
    return mid;
}

static int fr_edge_list_reserve(FrEdgeList *edges, size_t required_count) {
    return fr_reserve_array((void **)&edges->items, sizeof(*edges->items), &edges->capacity, required_count);
}

static void fr_edge_list_free(FrEdgeList *edges) {
    fr_platform_free(edges->items);
    edges->items = NULL;
    edges->count = 0u;
    edges->capacity = 0u;
}

static int fr_edge_list_push(FrEdgeList *edges, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    FrEdge *edge;
    int64_t dx;
    int64_t dy;

    if (x0 == x1 && y0 == y1) {
        return 0;
    }
    if (y0 == y1) {
        return 0;
    }
    if (fr_edge_list_reserve(edges, edges->count + 1u) != 0) {
        return -1;
    }

    dx = (int64_t)x1 - (int64_t)x0;
    dy = (int64_t)y1 - (int64_t)y0;
    if (dx < INT32_MIN || dx > INT32_MAX || dy < INT32_MIN || dy > INT32_MAX) {
        return -1;
    }

    edge = &edges->items[edges->count++];
    if (dy > 0) {
        edge->x0 = x0;
        edge->y0 = y0;
        edge->dx = (int32_t)dx;
        edge->dy = (int32_t)dy;
        edge->y_min = y0;
        edge->y_max = y1;
        edge->winding_delta = 1;
    } else {
        edge->x0 = x1;
        edge->y0 = y1;
        edge->dx = (int32_t)(-dx);
        edge->dy = (int32_t)(-dy);
        edge->y_min = y1;
        edge->y_max = y0;
        edge->winding_delta = -1;
    }
    return 0;
}

static int fr_quad_is_flat(const FrOutlinePoint *p0, const FrOutlinePoint *p1, const FrOutlinePoint *p2) {
    int64_t dx = (int64_t)p0->x - 2ll * (int64_t)p1->x + (int64_t)p2->x;
    int64_t dy = (int64_t)p0->y - 2ll * (int64_t)p1->y + (int64_t)p2->y;

    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    return dx <= FR_QUAD_FLATNESS_LIMIT && dy <= FR_QUAD_FLATNESS_LIMIT;
}

static double fr_quad_eval(double a, double b, double c, double t) {
    double mt = 1.0 - t;

    return mt * mt * a + 2.0 * mt * t * b + t * t * c;
}

static void fr_bounds_add_value(double value, double *min_value, double *max_value) {
    if (value < *min_value) {
        *min_value = value;
    }
    if (value > *max_value) {
        *max_value = value;
    }
}

static void fr_bounds_add_xy(double x, double y, double *min_x, double *min_y, double *max_x, double *max_y, int *have_bounds) {
    if (!*have_bounds) {
        *min_x = x;
        *min_y = y;
        *max_x = x;
        *max_y = y;
        *have_bounds = 1;
        return;
    }
    fr_bounds_add_value(x, min_x, max_x);
    fr_bounds_add_value(y, min_y, max_y);
}

static void fr_bounds_add_point(FrOutlinePoint point, double *min_x, double *min_y, double *max_x, double *max_y, int *have_bounds) {
    fr_bounds_add_xy((double)point.x, (double)point.y, min_x, min_y, max_x, max_y, have_bounds);
}

static void fr_bounds_add_quad_axis(double p0, double p1, double p2, double *min_value, double *max_value) {
    double denom = p0 - 2.0 * p1 + p2;

    if (denom != 0.0) {
        double t = (p0 - p1) / denom;

        if (t > 0.0 && t < 1.0) {
            fr_bounds_add_value(fr_quad_eval(p0, p1, p2, t), min_value, max_value);
        }
    }
}

static void fr_bounds_add_quad(FrOutlinePoint p0, FrOutlinePoint p1, FrOutlinePoint p2, double *min_x, double *min_y, double *max_x, double *max_y, int *have_bounds) {
    fr_bounds_add_point(p0, min_x, min_y, max_x, max_y, have_bounds);
    fr_bounds_add_point(p2, min_x, min_y, max_x, max_y, have_bounds);
    fr_bounds_add_quad_axis((double)p0.x, (double)p1.x, (double)p2.x, min_x, max_x);
    fr_bounds_add_quad_axis((double)p0.y, (double)p1.y, (double)p2.y, min_y, max_y);
}

static void fr_bounds_add_line(FrOutlinePoint p0, FrOutlinePoint p1, double *min_x, double *min_y, double *max_x, double *max_y, int *have_bounds) {
    fr_bounds_add_point(p0, min_x, min_y, max_x, max_y, have_bounds);
    fr_bounds_add_point(p1, min_x, min_y, max_x, max_y, have_bounds);
}

static int fr_edge_list_push_quad(FrEdgeList *edges, FrOutlinePoint p0, FrOutlinePoint p1, FrOutlinePoint p2) {
    FrQuadTask stack[FR_QUAD_MAX_DEPTH + 1u];
    size_t depth = 0u;

    stack[depth].p0 = p0;
    stack[depth].p1 = p1;
    stack[depth].p2 = p2;
    stack[depth].depth = 0u;
    depth += 1u;

    while (depth != 0u) {
        FrQuadTask task = stack[depth - 1u];

        depth -= 1u;
        if (task.depth >= FR_QUAD_MAX_DEPTH || fr_quad_is_flat(&task.p0, &task.p1, &task.p2)) {
            if (fr_edge_list_push(edges, task.p0.x, task.p0.y, task.p2.x, task.p2.y) != 0) {
                return -1;
            }
            continue;
        }

        {
            FrOutlinePoint p01 = fr_outline_point_midpoint(task.p0, task.p1);
            FrOutlinePoint p12 = fr_outline_point_midpoint(task.p1, task.p2);
            FrOutlinePoint p012 = fr_outline_point_midpoint(p01, p12);

            if (depth + 2u > sizeof(stack) / sizeof(stack[0])) {
                return -1;
            }

            stack[depth].p0 = p012;
            stack[depth].p1 = p12;
            stack[depth].p2 = task.p2;
            stack[depth].depth = task.depth + 1u;
            depth += 1u;

            stack[depth].p0 = task.p0;
            stack[depth].p1 = p01;
            stack[depth].p2 = p012;
            stack[depth].depth = task.depth + 1u;
            depth += 1u;
        }
    }

    return 0;
}

static int fr_edge_list_add_contour(FrEdgeList *edges, const FrOutlinePoint *points, size_t point_count,
                                    double *min_x, double *min_y, double *max_x, double *max_y,
                                    int *have_bounds) {
    FrOutlinePoint start;
    FrOutlinePoint current;
    size_t i;

    if (point_count < 3u) {
        return 0;
    }

    if ((points[0].flags & FR_OUTLINE_POINT_ON_CURVE) != 0u) {
        start = points[0];
        i = 1u;
    } else if ((points[point_count - 1u].flags & FR_OUTLINE_POINT_ON_CURVE) != 0u) {
        start = points[point_count - 1u];
        i = 0u;
    } else {
        start = fr_outline_point_midpoint(points[point_count - 1u], points[0]);
        i = 0u;
    }

    current = start;
    while (i < point_count) {
        FrOutlinePoint point = points[i];

        if ((point.flags & FR_OUTLINE_POINT_ON_CURVE) != 0u) {
            fr_bounds_add_line(current, point, min_x, min_y, max_x, max_y, have_bounds);
            if (fr_edge_list_push(edges, current.x, current.y, point.x, point.y) != 0) {
                return -1;
            }
            current = point;
            i += 1u;
            continue;
        }

        {
            FrOutlinePoint next = points[(i + 1u) % point_count];

            if ((next.flags & FR_OUTLINE_POINT_ON_CURVE) != 0u) {
                fr_bounds_add_quad(current, point, next, min_x, min_y, max_x, max_y, have_bounds);
                if (fr_edge_list_push_quad(edges, current, point, next) != 0) {
                    return -1;
                }
                current = next;
                i += 2u;
                continue;
            }

            next = fr_outline_point_midpoint(point, next);
            fr_bounds_add_quad(current, point, next, min_x, min_y, max_x, max_y, have_bounds);
            if (fr_edge_list_push_quad(edges, current, point, next) != 0) {
                return -1;
            }
            current = next;
            i += 1u;
        }
    }

    if (current.x != start.x || current.y != start.y) {
        fr_bounds_add_line(current, start, min_x, min_y, max_x, max_y, have_bounds);
        if (fr_edge_list_push(edges, current.x, current.y, start.x, start.y) != 0) {
            return -1;
        }
    }

    return 0;
}

static int fr_build_edges(const FrOutline *outline, FrEdgeList *edges, int32_t *min_x, int32_t *min_y, int32_t *max_x, int32_t *max_y) {
    size_t i;
    double bounds_min_x = 0.0;
    double bounds_min_y = 0.0;
    double bounds_max_x = 0.0;
    double bounds_max_y = 0.0;
    int have_bounds = 0;

    if (fr_outline_validate(outline) != 0) {
        return -1;
    }

    edges->items = NULL;
    edges->count = 0u;
    edges->capacity = 0u;

    for (i = 0u; i < outline->contour_count; ++i) {
        FrOutlineContour contour = outline->contours[i];
        size_t base_count = edges->count;

        if (fr_edge_list_add_contour(edges, outline->points + contour.first_point, contour.point_count,
                                     &bounds_min_x, &bounds_min_y, &bounds_max_x, &bounds_max_y,
                                     &have_bounds) != 0) {
            fr_edge_list_free(edges);
            return -1;
        }
        if (edges->count == base_count) {
            continue;
        }
    }

    if (!have_bounds) {
        *min_x = 0;
        *min_y = 0;
        *max_x = 0;
        *max_y = 0;
    } else {
        *min_x = fr_double_floor_to_i32(bounds_min_x);
        *min_y = fr_double_floor_to_i32(bounds_min_y);
        *max_x = fr_double_ceil_to_i32(bounds_max_x);
        *max_y = fr_double_ceil_to_i32(bounds_max_y);
    }

    return 0;
}

static int fr_edge_sample_range(const FrEdge *edge, int64_t first_sample_y, int64_t sample_step,
                                size_t sample_count, size_t *out_start, size_t *out_end) {
    int64_t start = fr_div_ceil_i64((int64_t)edge->y_min - first_sample_y, sample_step);
    int64_t end = fr_div_ceil_i64((int64_t)edge->y_max - first_sample_y, sample_step) - 1ll;

    if (end < 0 || start >= (int64_t)sample_count) {
        return 0;
    }
    if (start < 0) {
        start = 0;
    }
    if (end >= (int64_t)sample_count) {
        end = (int64_t)sample_count - 1ll;
    }
    if (start > end) {
        return 0;
    }

    *out_start = (size_t)start;
    *out_end = (size_t)end;
    return 1;
}

static int fr_build_sample_edge_events(const FrEdgeList *edges, int bottom, int height,
                                       FrRowEdgeSpan **out_add_spans, size_t **out_add_indices,
                                       FrRowEdgeSpan **out_remove_spans, size_t **out_remove_indices) {
    size_t sample_count;
    int64_t first_sample_y;
    const int64_t sample_step = (int64_t)FR_OUTLINE_ONE / (int64_t)FR_RASTER_SUBSAMPLES;
    FrRowEdgeSpan *add_spans = NULL;
    FrRowEdgeSpan *remove_spans = NULL;
    size_t *add_counts = NULL;
    size_t *remove_counts = NULL;
    size_t *add_indices = NULL;
    size_t *remove_indices = NULL;
    size_t total_adds = 0u;
    size_t total_removes = 0u;
    size_t i;

    if (height < 0) {
        return -1;
    }
    if ((size_t)height > SIZE_MAX / FR_RASTER_SUBSAMPLES) {
        return -1;
    }

    sample_count = (size_t)height * FR_RASTER_SUBSAMPLES;
    if (sample_count == 0u) {
        *out_add_spans = NULL;
        *out_add_indices = NULL;
        *out_remove_spans = NULL;
        *out_remove_indices = NULL;
        return 0;
    }

    add_spans = (FrRowEdgeSpan *)fr_platform_calloc(sample_count, sizeof(*add_spans));
    remove_spans = (FrRowEdgeSpan *)fr_platform_calloc(sample_count, sizeof(*remove_spans));
    add_counts = (size_t *)fr_platform_calloc(sample_count, sizeof(*add_counts));
    remove_counts = (size_t *)fr_platform_calloc(sample_count, sizeof(*remove_counts));
    if (add_spans == NULL || remove_spans == NULL || add_counts == NULL || remove_counts == NULL) {
        fr_platform_free(remove_counts);
        fr_platform_free(add_counts);
        fr_platform_free(remove_spans);
        fr_platform_free(add_spans);
        return -1;
    }

    first_sample_y = (int64_t)bottom * (int64_t)FR_OUTLINE_ONE + (int64_t)FR_OUTLINE_ONE / 16ll;
    for (i = 0u; i < edges->count; ++i) {
        size_t start;
        size_t end;

        if (!fr_edge_sample_range(&edges->items[i], first_sample_y, sample_step, sample_count, &start, &end)) {
            continue;
        }

        add_counts[start] += 1u;
        if (end + 1u < sample_count) {
            remove_counts[end + 1u] += 1u;
        }
    }

    for (i = 0u; i < sample_count; ++i) {
        add_spans[i].start = total_adds;
        add_spans[i].count = add_counts[i];
        total_adds += add_counts[i];

        remove_spans[i].start = total_removes;
        remove_spans[i].count = remove_counts[i];
        total_removes += remove_counts[i];
    }

    if (total_adds != 0u) {
        add_indices = (size_t *)fr_platform_alloc(total_adds * sizeof(*add_indices));
        if (add_indices == NULL) {
            fr_platform_free(remove_counts);
            fr_platform_free(add_counts);
            fr_platform_free(remove_spans);
            fr_platform_free(add_spans);
            return -1;
        }
    }
    if (total_removes != 0u) {
        remove_indices = (size_t *)fr_platform_alloc(total_removes * sizeof(*remove_indices));
        if (remove_indices == NULL) {
            fr_platform_free(add_indices);
            fr_platform_free(remove_counts);
            fr_platform_free(add_counts);
            fr_platform_free(remove_spans);
            fr_platform_free(add_spans);
            return -1;
        }
    }

    for (i = 0u; i < sample_count; ++i) {
        add_counts[i] = add_spans[i].start;
        remove_counts[i] = remove_spans[i].start;
    }

    for (i = 0u; i < edges->count; ++i) {
        size_t start;
        size_t end;

        if (!fr_edge_sample_range(&edges->items[i], first_sample_y, sample_step, sample_count, &start, &end)) {
            continue;
        }

        add_indices[add_counts[start]++] = i;
        if (end + 1u < sample_count) {
            remove_indices[remove_counts[end + 1u]++] = i;
        }
    }

    fr_platform_free(remove_counts);
    fr_platform_free(add_counts);
    *out_add_spans = add_spans;
    *out_add_indices = add_indices;
    *out_remove_spans = remove_spans;
    *out_remove_indices = remove_indices;
    return 0;
}

static void fr_init_active_edge(FrActiveEdge *active, size_t edge_index, const FrEdge *edge,
                                int64_t sample_y, int64_t sample_step) {
    int64_t numerator = (sample_y - (int64_t)edge->y0) * (int64_t)edge->dx;
    int64_t step_numerator = sample_step * (int64_t)edge->dx;

    active->edge_index = edge_index;
    active->quotient = numerator / (int64_t)edge->dy;
    active->remainder = numerator % (int64_t)edge->dy;
    active->quotient_step = step_numerator / (int64_t)edge->dy;
    active->remainder_step = step_numerator % (int64_t)edge->dy;
    active->x0 = edge->x0;
    active->dy = edge->dy;
    active->winding_delta = edge->winding_delta;
    active->x = (int64_t)edge->x0 + active->quotient;
}

static void fr_advance_active_edge(FrActiveEdge *active) {
    int64_t dy = (int64_t)active->dy;

    active->quotient += active->quotient_step;
    active->remainder += active->remainder_step;
    if (active->remainder >= dy) {
        active->quotient += 1ll;
        active->remainder -= dy;
    } else if (active->remainder <= -dy) {
        active->quotient -= 1ll;
        active->remainder += dy;
    }
    active->x = (int64_t)active->x0 + active->quotient;
}

static void fr_sort_active_edges(FrActiveEdge *edges, size_t edge_count) {
    size_t i;

    for (i = 1u; i < edge_count; ++i) {
        FrActiveEdge value = edges[i];
        size_t j = i;

        while (j != 0u && edges[j - 1u].x > value.x) {
            edges[j] = edges[j - 1u];
            j -= 1u;
        }
        edges[j] = value;
    }
}

static void fr_accumulate_sample_row(uint16_t *row_coverage, int width, int left,
                                     const FrActiveEdge *active_edges, size_t active_count,
                                     int initial_winding, int64_t sample_step) {
    size_t crossing_index = 0u;
    int winding = initial_winding;
    int pixel_x;
    int64_t sample_x = (int64_t)left * (int64_t)FR_OUTLINE_ONE + (int64_t)FR_OUTLINE_ONE / 16ll;

    for (pixel_x = 0; pixel_x < width; ++pixel_x) {
        uint16_t *coverage = &row_coverage[pixel_x];
        int sample_hits = 0;
        int sx;

        for (sx = 0; sx < FR_RASTER_SUBSAMPLES; ++sx) {
            while (crossing_index < active_count && active_edges[crossing_index].x <= sample_x) {
                winding -= active_edges[crossing_index].winding_delta;
                crossing_index += 1u;
            }
            sample_hits += winding != 0;
            sample_x += sample_step;
        }

        *coverage = (uint16_t)(*coverage + (uint16_t)sample_hits);
    }
}

#if defined(FR_RASTER_HAVE_SSE2)
static void fr_store_row_coverage_simd_sse2(uint8_t *row, const uint16_t *coverage, int width) {
    const __m128i scale = _mm_set1_epi16(255);
    const __m128i rounding = _mm_set1_epi16(FR_RASTER_TOTAL_SAMPLES / 2);
    int x = 0;

    for (; x + 8 <= width; x += 8) {
        __m128i counts = _mm_loadu_si128((const __m128i *)(coverage + x));
        __m128i pixels = _mm_srli_epi16(_mm_add_epi16(_mm_mullo_epi16(counts, scale), rounding), 6);
        pixels = _mm_packus_epi16(pixels, pixels);
        _mm_storel_epi64((__m128i *)(row + x), pixels);
    }
    for (; x < width; ++x) {
        row[x] = (uint8_t)((coverage[x] * 255u + (FR_RASTER_TOTAL_SAMPLES / 2u)) / FR_RASTER_TOTAL_SAMPLES);
    }
}
#elif defined(FR_RASTER_HAVE_NEON)
static void fr_store_row_coverage_simd_neon(uint8_t *row, const uint16_t *coverage, int width) {
    const uint16x8_t scale = vdupq_n_u16(255u);
    const uint16x8_t rounding = vdupq_n_u16(FR_RASTER_TOTAL_SAMPLES / 2u);
    int x = 0;

    for (; x + 8 <= width; x += 8) {
        uint16x8_t counts = vld1q_u16(coverage + x);
        uint16x8_t pixels = vshrq_n_u16(vaddq_u16(vmulq_u16(counts, scale), rounding), 6);
        vst1_u8(row + x, vmovn_u16(pixels));
    }
    for (; x < width; ++x) {
        row[x] = (uint8_t)((coverage[x] * 255u + (FR_RASTER_TOTAL_SAMPLES / 2u)) / FR_RASTER_TOTAL_SAMPLES);
    }
}
#endif

static void fr_store_row_coverage(uint8_t *row, const uint16_t *coverage, int width) {
#if defined(FR_RASTER_HAVE_SSE2)
    fr_store_row_coverage_simd_sse2(row, coverage, width);
#elif defined(FR_RASTER_HAVE_NEON)
    fr_store_row_coverage_simd_neon(row, coverage, width);
#else
    int x;

    for (x = 0; x < width; ++x) {
        row[x] = (uint8_t)((coverage[x] * 255u + (FR_RASTER_TOTAL_SAMPLES / 2u)) / FR_RASTER_TOTAL_SAMPLES);
    }
#endif
}

void fr_outline_init(FrOutline *outline) {
    if (outline == NULL) {
        return;
    }
    outline->points = NULL;
    outline->point_count = 0u;
    outline->point_capacity = 0u;
    outline->contours = NULL;
    outline->contour_count = 0u;
    outline->contour_capacity = 0u;
}

void fr_outline_reset(FrOutline *outline) {
    if (outline == NULL) {
        return;
    }
    outline->point_count = 0u;
    outline->contour_count = 0u;
}

void fr_outline_free(FrOutline *outline) {
    if (outline == NULL) {
        return;
    }
    fr_platform_free(outline->points);
    fr_platform_free(outline->contours);
    fr_outline_init(outline);
}

int fr_outline_reserve(FrOutline *outline, size_t point_capacity, size_t contour_capacity) {
    if (outline == NULL) {
        return -1;
    }
    if (point_capacity > outline->point_capacity &&
        fr_reserve_array((void **)&outline->points, sizeof(*outline->points), &outline->point_capacity, point_capacity) != 0) {
        return -1;
    }
    if (contour_capacity > outline->contour_capacity &&
        fr_reserve_array((void **)&outline->contours, sizeof(*outline->contours), &outline->contour_capacity, contour_capacity) != 0) {
        return -1;
    }
    return 0;
}

int fr_outline_add_point(FrOutline *outline, int32_t x, int32_t y, uint8_t flags) {
    FrOutlinePoint *point;

    if (outline == NULL || (flags & (uint8_t)(~FR_OUTLINE_POINT_MASK)) != 0u) {
        return -1;
    }
    if (fr_outline_reserve(outline, outline->point_count + 1u, outline->contour_count) != 0) {
        return -1;
    }

    point = &outline->points[outline->point_count++];
    point->x = x;
    point->y = y;
    point->flags = flags;
    point->reserved[0] = 0u;
    point->reserved[1] = 0u;
    point->reserved[2] = 0u;
    return 0;
}

int fr_outline_add_contour(FrOutline *outline, size_t first_point, size_t point_count) {
    FrOutlineContour *contour;

    if (outline == NULL || point_count == 0u || first_point > outline->point_count || point_count > outline->point_count - first_point) {
        return -1;
    }
    if (outline->contour_count != 0u) {
        FrOutlineContour last = outline->contours[outline->contour_count - 1u];
        if (first_point < last.first_point + last.point_count) {
            return -1;
        }
    }
    if (fr_outline_reserve(outline, outline->point_count, outline->contour_count + 1u) != 0) {
        return -1;
    }

    contour = &outline->contours[outline->contour_count++];
    contour->first_point = first_point;
    contour->point_count = point_count;
    return 0;
}

int fr_outline_validate(const FrOutline *outline) {
    size_t i;
    size_t next_allowed = 0u;

    if (outline == NULL) {
        return -1;
    }
    if ((outline->points == NULL) != (outline->point_capacity == 0u)) {
        return -1;
    }
    if ((outline->contours == NULL) != (outline->contour_capacity == 0u)) {
        return -1;
    }
    if (outline->point_count > outline->point_capacity || outline->contour_count > outline->contour_capacity) {
        return -1;
    }
    if (outline->contour_count == 0u) {
        return outline->point_count == 0u ? 0 : -1;
    }

    for (i = 0u; i < outline->contour_count; ++i) {
        FrOutlineContour contour = outline->contours[i];
        size_t j;

        if (contour.point_count == 0u || contour.first_point > outline->point_count || contour.point_count > outline->point_count - contour.first_point) {
            return -1;
        }
        if (contour.first_point < next_allowed) {
            return -1;
        }
        next_allowed = contour.first_point + contour.point_count;

        for (j = 0u; j < contour.point_count; ++j) {
            uint8_t flags = outline->points[contour.first_point + j].flags;

            if ((flags & (uint8_t)(~FR_OUTLINE_POINT_MASK)) != 0u) {
                return -1;
            }
        }
    }

    return 0;
}

void fr_bitmap_init(FrBitmap *bitmap) {
    if (bitmap == NULL) {
        return;
    }
    bitmap->pixels = NULL;
    bitmap->width = 0;
    bitmap->height = 0;
    bitmap->stride = 0;
    bitmap->left = 0;
    bitmap->top = 0;
}

void fr_bitmap_free(FrBitmap *bitmap) {
    if (bitmap == NULL) {
        return;
    }
    fr_platform_free(bitmap->pixels);
    fr_bitmap_init(bitmap);
}

int fr_bitmap_alloc(FrBitmap *bitmap, int width, int height) {
    FrBitmap fresh;
    size_t total_bytes;

    if (bitmap == NULL || width < 0 || height < 0 || width > FR_RASTER_MAX_DIMENSION || height > FR_RASTER_MAX_DIMENSION) {
        return -1;
    }
    if ((width == 0) != (height == 0)) {
        return -1;
    }
    if (width != 0 && height != 0) {
        if ((size_t)width > SIZE_MAX / (size_t)height) {
            return -1;
        }
        total_bytes = (size_t)width * (size_t)height;
        if (total_bytes > FR_RASTER_MAX_PIXELS) {
            return -1;
        }
    } else {
        total_bytes = 0u;
    }

    fr_bitmap_init(&fresh);
    if (total_bytes != 0u) {
        fresh.pixels = (uint8_t *)fr_platform_alloc(total_bytes);
        if (fresh.pixels == NULL) {
            return -1;
        }
    }
    fresh.width = width;
    fresh.height = height;
    fresh.stride = width;

    fr_bitmap_free(bitmap);
    *bitmap = fresh;
    return 0;
}

int fr_raster_outline_box(const FrOutline *outline, int *out_left, int *out_top, int *out_width, int *out_height) {
    FrEdgeList edges;
    int32_t min_x;
    int32_t min_y;
    int32_t max_x;
    int32_t max_y;
    int left;
    int right;
    int top;
    int bottom;

    if (out_left == NULL || out_top == NULL || out_width == NULL || out_height == NULL) {
        return -1;
    }
    if (fr_build_edges(outline, &edges, &min_x, &min_y, &max_x, &max_y) != 0) {
        return -1;
    }
    if (edges.count == 0u) {
        fr_edge_list_free(&edges);
        *out_left = 0;
        *out_top = 0;
        *out_width = 0;
        *out_height = 0;
        return 0;
    }

    left = fr_fixed_floor_to_int(min_x);
    right = fr_fixed_ceil_to_int(max_x);
    top = fr_fixed_ceil_to_int(max_y);
    bottom = fr_fixed_floor_to_int(min_y);
    fr_edge_list_free(&edges);

    if (right < left || top < bottom) {
        return -1;
    }
    if (right - left > FR_RASTER_MAX_DIMENSION || top - bottom > FR_RASTER_MAX_DIMENSION) {
        return -1;
    }

    *out_left = left;
    *out_top = top;
    *out_width = right - left;
    *out_height = top - bottom;
    return 0;
}

int fr_raster_render(FrBitmap *bitmap, const FrOutline *outline) {
    FrEdgeList edges;
    FrBitmap fresh;
    int32_t min_x;
    int32_t min_y;
    int32_t max_x;
    int32_t max_y;
    int left;
    int right;
    int top;
    int bottom;
    FrRowEdgeSpan *add_spans = NULL;
    FrRowEdgeSpan *remove_spans = NULL;
    size_t *add_indices = NULL;
    size_t *remove_indices = NULL;
    FrActiveEdge *active_edges = NULL;
    size_t *active_positions = NULL;
    uint16_t *row_coverage = NULL;
    size_t active_count = 0u;
    size_t sample_count;
    size_t sample_index;
    int active_winding = 0;
    int64_t sample_y;
    const int64_t sample_step = (int64_t)FR_OUTLINE_ONE / (int64_t)FR_RASTER_SUBSAMPLES;

    if (bitmap == NULL) {
        return -1;
    }
    if (fr_build_edges(outline, &edges, &min_x, &min_y, &max_x, &max_y) != 0) {
        return -1;
    }
    if (edges.count == 0u) {
        fr_edge_list_free(&edges);
        return fr_bitmap_alloc(bitmap, 0, 0);
    }

    left = fr_fixed_floor_to_int(min_x);
    right = fr_fixed_ceil_to_int(max_x);
    top = fr_fixed_ceil_to_int(max_y);
    bottom = fr_fixed_floor_to_int(min_y);
    if (right < left || top < bottom) {
        fr_edge_list_free(&edges);
        return -1;
    }

    fr_bitmap_init(&fresh);
    if (fr_bitmap_alloc(&fresh, right - left, top - bottom) != 0) {
        fr_edge_list_free(&edges);
        return -1;
    }
    if (fr_build_sample_edge_events(&edges, bottom, fresh.height, &add_spans, &add_indices, &remove_spans, &remove_indices) != 0) {
        fr_bitmap_free(&fresh);
        fr_edge_list_free(&edges);
        return -1;
    }
    if (edges.count != 0u) {
        active_edges = (FrActiveEdge *)fr_platform_alloc(edges.count * sizeof(*active_edges));
        active_positions = (size_t *)fr_platform_alloc(edges.count * sizeof(*active_positions));
        if (active_edges == NULL || active_positions == NULL) {
            fr_platform_free(active_positions);
            fr_platform_free(active_edges);
            fr_platform_free(remove_indices);
            fr_platform_free(remove_spans);
            fr_platform_free(add_indices);
            fr_platform_free(add_spans);
            fr_bitmap_free(&fresh);
            fr_edge_list_free(&edges);
            return -1;
        }
        fr_platform_memset(active_positions, 0xFF, edges.count * sizeof(*active_positions));
    }
    if (fresh.width != 0) {
        row_coverage = (uint16_t *)fr_platform_alloc((size_t)fresh.width * sizeof(*row_coverage));
        if (row_coverage == NULL) {
            fr_platform_free(active_positions);
            fr_platform_free(active_edges);
            fr_platform_free(remove_indices);
            fr_platform_free(remove_spans);
            fr_platform_free(add_indices);
            fr_platform_free(add_spans);
            fr_bitmap_free(&fresh);
            fr_edge_list_free(&edges);
            return -1;
        }
    }

    fresh.left = left;
    fresh.top = top;
    sample_count = (size_t)fresh.height * FR_RASTER_SUBSAMPLES;
    sample_y = (int64_t)bottom * (int64_t)FR_OUTLINE_ONE + (int64_t)FR_OUTLINE_ONE / 16ll;
    sample_index = 0u;
    for (int bitmap_y = fresh.height - 1; bitmap_y >= 0; --bitmap_y) {
        int subsample;

        if (fresh.width != 0) {
            fr_platform_memset(row_coverage, 0, (size_t)fresh.width * sizeof(*row_coverage));
        }

        for (subsample = 0; subsample < FR_RASTER_SUBSAMPLES; ++subsample, ++sample_index) {
            const FrRowEdgeSpan remove_span = remove_spans[sample_index];
            const FrRowEdgeSpan add_span = add_spans[sample_index];
            size_t i;

            for (i = 0u; i < remove_span.count; ++i) {
                size_t edge_index = remove_indices[remove_span.start + i];
                size_t active_pos = active_positions[edge_index];
                size_t last_pos;

                if (active_pos == SIZE_MAX) {
                    continue;
                }

                active_winding -= active_edges[active_pos].winding_delta;
                last_pos = active_count - 1u;
                if (active_pos != last_pos) {
                    active_edges[active_pos] = active_edges[last_pos];
                    active_positions[active_edges[active_pos].edge_index] = active_pos;
                }
                active_positions[edge_index] = SIZE_MAX;
                active_count = last_pos;
            }

            for (i = 0u; i < add_span.count; ++i) {
                size_t edge_index = add_indices[add_span.start + i];
                const FrEdge *edge = &edges.items[edge_index];

                fr_init_active_edge(&active_edges[active_count], edge_index, edge, sample_y, sample_step);
                active_positions[edge_index] = active_count;
                active_winding += edge->winding_delta;
                active_count += 1u;
            }

            if (fresh.width != 0 && active_count != 0u) {
                if (active_count > 1u) {
                    fr_sort_active_edges(active_edges, active_count);
                    for (i = 0u; i < active_count; ++i) {
                        active_positions[active_edges[i].edge_index] = i;
                    }
                }
                fr_accumulate_sample_row(row_coverage, fresh.width, left, active_edges, active_count,
                                         active_winding, sample_step);
            }

            if (sample_index + 1u < sample_count) {
                for (i = 0u; i < active_count; ++i) {
                    fr_advance_active_edge(&active_edges[i]);
                }
                sample_y += sample_step;
            }
        }

        if (fresh.width != 0) {
            uint8_t *row = fresh.pixels + (size_t)bitmap_y * (size_t)fresh.stride;

            fr_store_row_coverage(row, row_coverage, fresh.width);
        }
    }

    fr_platform_free(row_coverage);
    fr_platform_free(active_positions);
    fr_platform_free(active_edges);
    fr_platform_free(remove_indices);
    fr_platform_free(remove_spans);
    fr_platform_free(add_indices);
    fr_platform_free(add_spans);
    fr_edge_list_free(&edges);
    fr_bitmap_free(bitmap);
    *bitmap = fresh;
    return 0;
}
