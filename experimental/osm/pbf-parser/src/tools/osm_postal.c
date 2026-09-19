/*
 * osm-postal
 *
 * Extracts German postal code areas (boundary=postal_code relations) from an
 * .osm.pbf extract and emits a self-contained RFC 7946 GeoJSON
 * FeatureCollection with Polygon/MultiPolygon geometries suitable for direct
 * point-in-polygon PLZ assignment.
 *
 * The tool performs three streaming passes over the PBF file:
 *
 *   pass 1  relations  collect boundary=postal_code relations, their tags and
 *                      their way members including the outer/inner role
 *   pass 2  ways       resolve the referenced way members to node id lists
 *   pass 3  nodes      resolve the referenced node ids to coordinates
 *
 * Ring assembly joins fragmented ways into closed rings independently of
 * member order and member direction. Inner rings are nested into the smallest
 * containing outer ring so multipolygon semantics are preserved.
 *
 * All geometry predicates (signed ring area, point in polygon) use exact
 * 128-bit integer arithmetic on the 1e-7 degree integer grid, so the output is
 * fully deterministic and free of floating point rounding.
 *
 * Relations whose rings cannot be closed, whose members are missing from the
 * extract, or whose inner rings cannot be nested are never emitted with
 * partial geometry. They are excluded, listed on stderr and recorded in the
 * optional TSV audit report.
 *
 * No libc, no external dependencies.
 */

#include "pbf.h"
#include "platform.h"
#include "runtime.h"

#define OSM_POSTAL_BUFFER_SIZE 262144U
#define OSM_POSTAL_NAME_MAX 128U
#define OSM_POSTAL_ROLE_OUTER 0U
#define OSM_POSTAL_ROLE_INNER 1U

/*
 * Default acceptance window for "geographically within Germany", in 1e-7
 * degrees. It covers the German land and sea territory with a small margin:
 * lon 5.5 .. 15.5, lat 47.0 .. 55.2.
 */
#define OSM_POSTAL_DE_MIN_LON 55000000LL
#define OSM_POSTAL_DE_MIN_LAT 470000000LL
#define OSM_POSTAL_DE_MAX_LON 155000000LL
#define OSM_POSTAL_DE_MAX_LAT 552000000LL

#define OSM_POSTAL_SOURCE_NONE 0U
#define OSM_POSTAL_SOURCE_DE 1U
#define OSM_POSTAL_SOURCE_POSTAL_CODE 2U
#define OSM_POSTAL_SOURCE_REF 3U

/*
 * Relation classification, in three bands.
 *
 *   1. Policy exclusions. The relation is simply not a German postal code
 *      area. This is a normal outcome and never fails the run.
 *   2. Incomplete input. The relation references a way or node that is not
 *      present in this file. National extracts are clipped at the border, so
 *      postal areas of neighbouring countries that reach into the extract
 *      routinely land here. Nothing is emitted for them, but the run does not
 *      fail either, because the file simply does not contain the data.
 *   3. Geometry failures. Every referenced member was present, yet the rings
 *      could not be built. This indicates broken data or an unsupported
 *      construct and is what --strict reacts to.
 */
#define OSM_POSTAL_STATUS_OK 0U
#define OSM_POSTAL_STATUS_NO_CODE 1U
#define OSM_POSTAL_STATUS_INVALID_CODE 2U
#define OSM_POSTAL_STATUS_FOREIGN_TAG 3U
#define OSM_POSTAL_STATUS_OUTSIDE_DE 4U
#define OSM_POSTAL_STATUS_NOT_DE_TAGGED 5U

#define OSM_POSTAL_STATUS_MISSING_WAY 6U
#define OSM_POSTAL_STATUS_MISSING_NODE 7U

#define OSM_POSTAL_STATUS_NO_WAY_MEMBERS 8U
#define OSM_POSTAL_STATUS_SHORT_WAY 9U
#define OSM_POSTAL_STATUS_OPEN_RING 10U
#define OSM_POSTAL_STATUS_DEGENERATE_RING 11U
#define OSM_POSTAL_STATUS_NO_OUTER_RING 12U
#define OSM_POSTAL_STATUS_ORPHAN_INNER 13U
#define OSM_POSTAL_STATUS_RELATION_MEMBER 14U
#define OSM_POSTAL_STATUS_OUT_OF_MEMORY 15U

#define OSM_POSTAL_FIRST_INCOMPLETE_STATUS OSM_POSTAL_STATUS_MISSING_WAY
#define OSM_POSTAL_FIRST_GEOMETRY_STATUS OSM_POSTAL_STATUS_NO_WAY_MEMBERS

typedef struct {
    long long *items;
    unsigned long long count;
    unsigned long long capacity;
} OsmPostalIdList;

typedef struct {
    long long way_id;
    unsigned int role;
} OsmPostalMember;

typedef struct {
    OsmPostalMember *items;
    unsigned long long count;
    unsigned long long capacity;
} OsmPostalMemberList;

typedef struct {
    long long id;
    char postcode[8];
    unsigned int postcode_source;
    unsigned long long name_offset;
    unsigned int name_length;
    unsigned long long member_start;
    unsigned int member_way_count;
    unsigned int member_node_count;
    unsigned int member_relation_count;
    unsigned int outer_member_count;
    unsigned int inner_member_count;
    int has_de_tag;
    int has_foreign_tag;
    unsigned int status;
} OsmPostalRelation;

typedef struct {
    OsmPostalRelation *items;
    unsigned long long count;
    unsigned long long capacity;
} OsmPostalRelationList;

typedef struct {
    char *items;
    unsigned long long count;
    unsigned long long capacity;
} OsmPostalTextPool;

typedef struct {
    unsigned long long ref_start;
    unsigned int ref_count;
    int found;
} OsmPostalWayEntry;

typedef struct {
    int lon;
    int lat;
} OsmPostalPoint;

/* One assembled ring: an index range into the shared ring point buffer. */
typedef struct {
    unsigned long long point_start;
    unsigned int point_count;
    unsigned int role;
    int min_lon;
    int min_lat;
    int max_lon;
    int max_lat;
    __int128 area2;
    int outer_index;
} OsmPostalRing;

typedef struct {
    long long node_id;
    unsigned int segment;
    unsigned int end;
} OsmPostalEndpoint;

typedef struct {
    int fd;
    unsigned char *data;
    unsigned long long capacity;
    unsigned long long used;
    int failed;
} OsmPostalWriter;

typedef struct {
    /* pass 1 */
    OsmPostalRelationList relations;
    OsmPostalMemberList members;
    OsmPostalTextPool names;

    /* pass 2 */
    long long *way_ids;
    OsmPostalWayEntry *way_entries;
    unsigned long long way_count;
    OsmPostalIdList way_refs;
    unsigned long long way_cursor;
    long long way_last_id;
    int way_unsorted;

    /* pass 3 */
    long long *node_ids;
    int *node_lon;
    int *node_lat;
    unsigned char *node_found;
    unsigned long long node_count;
    unsigned long long node_cursor;
    long long node_last_id;
    int node_unsorted;

    /* configuration */
    long long min_lon;
    long long min_lat;
    long long max_lon;
    long long max_lat;
    int bbox_filter;
    int require_de_tag;

    /* stats */
    unsigned long long relations_seen;
    unsigned long long relations_kept;
    unsigned long long relations_excluded;
    unsigned long long relations_incomplete;
    unsigned long long relations_broken;
    unsigned long long features_written;
    unsigned long long outer_rings;
    unsigned long long inner_rings;

    int out_of_memory;
} OsmPostalContext;

typedef struct {
    OsmPostalPoint *points;
    unsigned long long point_count;
    unsigned long long point_capacity;
    OsmPostalRing *rings;
    unsigned long long ring_count;
    unsigned long long ring_capacity;
    /* per-relation scratch buffers, reused across relations */
    OsmPostalEndpoint *endpoints;
    unsigned long long endpoint_capacity;
    unsigned char *segment_used;
    unsigned long long *segment_way;
    unsigned long long segment_capacity;
    long long *ring_nodes;
    unsigned long long ring_node_capacity;
} OsmPostalGeometry;

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static int text_equals_cstr(PbfText text, const char *value) {
    size_t value_size = rt_strlen(value);

    return text.size == value_size && (value_size == 0U || memcmp(text.data, value, value_size) == 0);
}

static int text_starts_with(PbfText text, const char *prefix) {
    size_t prefix_size = rt_strlen(prefix);

    return text.size >= prefix_size && memcmp(text.data, prefix, prefix_size) == 0;
}

/* Parses a decimal degree literal into 1e-7 degree units. */
static int parse_degrees(const char **cursor, long long *value_out) {
    const char *text = *cursor;
    long long whole = 0;
    long long fraction = 0;
    long long scale = 10000000LL;
    int negative = 0;
    int digits = 0;

    if (*text == '+' || *text == '-') {
        negative = (*text == '-');
        text += 1;
    }
    while (*text >= '0' && *text <= '9') {
        whole = whole * 10LL + (long long)(*text - '0');
        if (whole > 1000LL) return -1;
        text += 1;
        digits += 1;
    }
    if (*text == '.') {
        text += 1;
        while (*text >= '0' && *text <= '9') {
            if (scale > 1LL) {
                scale /= 10LL;
                fraction += (long long)(*text - '0') * scale;
            }
            text += 1;
            digits += 1;
        }
    }
    if (digits == 0) return -1;
    *value_out = (whole * 10000000LL + fraction) * (negative ? -1LL : 1LL);
    *cursor = text;
    return 0;
}

static int parse_bbox_arg(const char *text, long long *min_lon, long long *min_lat, long long *max_lon, long long *max_lat) {
    const char *cursor = text;

    if (parse_degrees(&cursor, min_lon) != 0 || *cursor != ',') return -1;
    cursor += 1;
    if (parse_degrees(&cursor, min_lat) != 0 || *cursor != ',') return -1;
    cursor += 1;
    if (parse_degrees(&cursor, max_lon) != 0 || *cursor != ',') return -1;
    cursor += 1;
    if (parse_degrees(&cursor, max_lat) != 0 || *cursor != '\0') return -1;
    if (*min_lon > *max_lon || *min_lat > *max_lat) return -1;
    return 0;
}

/*
 * German postal codes are exactly five digits. The leading two digits form the
 * Leitregion, which is numbered 01..99; "00" is not assigned.
 */
static int postcode_is_valid_de(const char *code) {
    unsigned int index;

    for (index = 0U; index < 5U; ++index) {
        if (code[index] < '0' || code[index] > '9') return 0;
    }
    if (code[5] != '\0') return 0;
    return !(code[0] == '0' && code[1] == '0');
}

static int text_to_postcode(PbfText text, char *out) {
    unsigned int index;

    if (text.size != 5U) return -1;
    for (index = 0U; index < 5U; ++index) out[index] = text.data[index];
    out[5] = '\0';
    return postcode_is_valid_de(out) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* growable buffers                                                    */
/* ------------------------------------------------------------------ */

static int id_list_reserve(OsmPostalIdList *list, unsigned long long extra) {
    unsigned long long needed = list->count + extra;
    unsigned long long capacity;
    long long *next;

    if (needed <= list->capacity) return 0;
    capacity = list->capacity == 0ULL ? 65536ULL : list->capacity;
    while (capacity < needed) capacity *= 2ULL;
    next = (long long *)rt_realloc(list->items, (size_t)capacity * sizeof(long long));
    if (next == 0) return -1;
    list->items = next;
    list->capacity = capacity;
    return 0;
}

static int member_list_append(OsmPostalMemberList *list, long long way_id, unsigned int role) {
    if (list->count == list->capacity) {
        unsigned long long capacity = list->capacity == 0ULL ? 4096ULL : list->capacity * 2ULL;
        OsmPostalMember *next = (OsmPostalMember *)rt_realloc(list->items, (size_t)capacity * sizeof(OsmPostalMember));
        if (next == 0) return -1;
        list->items = next;
        list->capacity = capacity;
    }
    list->items[list->count].way_id = way_id;
    list->items[list->count].role = role;
    list->count += 1ULL;
    return 0;
}

static int relation_list_append(OsmPostalRelationList *list, const OsmPostalRelation *value) {
    if (list->count == list->capacity) {
        unsigned long long capacity = list->capacity == 0ULL ? 256ULL : list->capacity * 2ULL;
        OsmPostalRelation *next = (OsmPostalRelation *)rt_realloc(list->items, (size_t)capacity * sizeof(OsmPostalRelation));
        if (next == 0) return -1;
        list->items = next;
        list->capacity = capacity;
    }
    list->items[list->count] = *value;
    list->count += 1ULL;
    return 0;
}

static int text_pool_append(OsmPostalTextPool *pool, const char *data, unsigned int length, unsigned long long *offset_out) {
    unsigned long long needed = pool->count + (unsigned long long)length;
    unsigned int index;

    if (needed > pool->capacity) {
        unsigned long long capacity = pool->capacity == 0ULL ? 4096ULL : pool->capacity;
        char *next;
        while (capacity < needed) capacity *= 2ULL;
        next = (char *)rt_realloc(pool->items, (size_t)capacity);
        if (next == 0) return -1;
        pool->items = next;
        pool->capacity = capacity;
    }
    *offset_out = pool->count;
    for (index = 0U; index < length; ++index) pool->items[pool->count + index] = data[index];
    pool->count = needed;
    return 0;
}

/* ------------------------------------------------------------------ */
/* radix sort + dedupe for id arrays                                   */
/* ------------------------------------------------------------------ */

static void sort_ids(long long *ids, unsigned long long count, long long *scratch) {
    unsigned long long counts[256];
    unsigned long long index;
    unsigned int byte;
    long long *src = ids;
    long long *dst = scratch;

    if (count < 2ULL) return;
    for (byte = 0U; byte < 8U; ++byte) {
        unsigned int shift = byte * 8U;
        unsigned long long total = 0ULL;
        unsigned int slot;
        int uniform = 0;

        for (slot = 0U; slot < 256U; ++slot) counts[slot] = 0ULL;
        for (index = 0ULL; index < count; ++index) {
            unsigned long long key = (unsigned long long)src[index] ^ 0x8000000000000000ULL;
            counts[(key >> shift) & 0xFFULL] += 1ULL;
        }
        for (slot = 0U; slot < 256U; ++slot) {
            if (counts[slot] == count) {
                uniform = 1;
                break;
            }
        }
        if (uniform) continue;
        for (slot = 0U; slot < 256U; ++slot) {
            unsigned long long value = counts[slot];
            counts[slot] = total;
            total += value;
        }
        for (index = 0ULL; index < count; ++index) {
            unsigned long long key = (unsigned long long)src[index] ^ 0x8000000000000000ULL;
            unsigned long long slot_index = (key >> shift) & 0xFFULL;
            dst[counts[slot_index]] = src[index];
            counts[slot_index] += 1ULL;
        }
        {
            long long *swap = src;
            src = dst;
            dst = swap;
        }
    }
    if (src != ids) {
        for (index = 0ULL; index < count; ++index) ids[index] = src[index];
    }
}

static unsigned long long dedupe_sorted_ids(long long *ids, unsigned long long count) {
    unsigned long long write = 0ULL;
    unsigned long long index;

    for (index = 0ULL; index < count; ++index) {
        if (write == 0ULL || ids[write - 1ULL] != ids[index]) {
            ids[write] = ids[index];
            write += 1ULL;
        }
    }
    return write;
}

static long long find_id(const long long *ids, unsigned long long count, long long id) {
    unsigned long long low = 0ULL;
    unsigned long long high = count;

    while (low < high) {
        unsigned long long mid = low + (high - low) / 2ULL;
        if (ids[mid] < id) low = mid + 1ULL;
        else high = mid;
    }
    if (low < count && ids[low] == id) return (long long)low;
    return -1;
}

/* ------------------------------------------------------------------ */
/* pass 1: relations                                                   */
/* ------------------------------------------------------------------ */

/*
 * Foreign-country detection. The rule is tag driven only: a relation is
 * treated as foreign when it carries a country-scoped tag naming a country
 * other than DE, a postal_code:XX tag for another country, or a national
 * reference key that only exists outside Germany.
 */
static int tag_marks_foreign(PbfText key, PbfText value) {
    if (text_equals_cstr(key, "addr:country") || text_equals_cstr(key, "country") ||
        text_equals_cstr(key, "is_in:country_code") || text_equals_cstr(key, "ISO3166-1") ||
        text_equals_cstr(key, "ISO3166-1:alpha2")) {
        return !text_equals_cstr(value, "DE");
    }
    if (text_equals_cstr(key, "ISO3166-2")) return !text_starts_with(value, "DE-");
    if (text_starts_with(key, "postal_code:") && !text_equals_cstr(key, "postal_code:DE")) return 1;
    if (text_equals_cstr(key, "ref:INSEE") || text_equals_cstr(key, "ref:ISTAT") ||
        text_equals_cstr(key, "ref:BAG") || text_equals_cstr(key, "ref:NUTS")) {
        return 1;
    }
    return 0;
}

static int on_relation(void *user, const PbfRelation *relation) {
    OsmPostalContext *context = (OsmPostalContext *)user;
    OsmPostalRelation record;
    unsigned int index;
    int is_postal_boundary = 0;
    PbfText code_de;
    PbfText code_plain;
    PbfText code_ref;
    PbfText name;
    int has_code_de = 0;
    int has_code_plain = 0;
    int has_code_ref = 0;
    int has_name = 0;
    int collect_members;
    unsigned int blocking_relation_members = 0U;

    rt_memset(&record, 0, sizeof(record));
    rt_memset(&code_de, 0, sizeof(code_de));
    rt_memset(&code_plain, 0, sizeof(code_plain));
    rt_memset(&code_ref, 0, sizeof(code_ref));
    rt_memset(&name, 0, sizeof(name));

    for (index = 0U; index < relation->tag_count; ++index) {
        PbfText key = relation->tags[index].key;
        PbfText value = relation->tags[index].value;

        if (text_equals_cstr(key, "boundary")) {
            if (text_equals_cstr(value, "postal_code")) is_postal_boundary = 1;
        } else if (!has_code_de && text_equals_cstr(key, "postal_code:DE")) {
            code_de = value;
            has_code_de = 1;
        } else if (!has_code_plain && text_equals_cstr(key, "postal_code")) {
            code_plain = value;
            has_code_plain = 1;
        } else if (!has_code_ref && text_equals_cstr(key, "ref")) {
            code_ref = value;
            has_code_ref = 1;
        } else if (!has_name && text_equals_cstr(key, "name")) {
            name = value;
            has_name = 1;
        }
        if (!record.has_foreign_tag && tag_marks_foreign(key, value)) record.has_foreign_tag = 1;
    }
    if (!is_postal_boundary) return 0;
    context->relations_seen += 1ULL;

    record.id = relation->id;
    record.has_de_tag = has_code_de;
    /*
     * Postcode source precedence: postal_code:DE wins, then postal_code, and
     * ref is only consulted when no postal_code tag exists at all.
     */
    if (has_code_de && text_to_postcode(code_de, record.postcode) == 0) {
        record.postcode_source = OSM_POSTAL_SOURCE_DE;
    } else if (has_code_plain && text_to_postcode(code_plain, record.postcode) == 0) {
        record.postcode_source = OSM_POSTAL_SOURCE_POSTAL_CODE;
    } else if (!has_code_plain && has_code_ref && text_to_postcode(code_ref, record.postcode) == 0) {
        record.postcode_source = OSM_POSTAL_SOURCE_REF;
    } else {
        record.postcode_source = OSM_POSTAL_SOURCE_NONE;
        record.postcode[0] = '\0';
        record.status = (has_code_de || has_code_plain || has_code_ref) ? OSM_POSTAL_STATUS_INVALID_CODE
                                                                       : OSM_POSTAL_STATUS_NO_CODE;
    }
    if (record.status == OSM_POSTAL_STATUS_OK) {
        if (context->require_de_tag && record.postcode_source != OSM_POSTAL_SOURCE_DE) {
            record.status = OSM_POSTAL_STATUS_NOT_DE_TAGGED;
        } else if (record.has_foreign_tag && record.postcode_source != OSM_POSTAL_SOURCE_DE) {
            record.status = OSM_POSTAL_STATUS_FOREIGN_TAG;
        }
    }
    collect_members = (record.status == OSM_POSTAL_STATUS_OK);

    record.member_start = context->members.count;
    for (index = 0U; index < relation->member_count; ++index) {
        const PbfRelationMember *member = &relation->members[index];
        unsigned int role;

        if (member->type == PBF_RELATION_MEMBER_NODE) {
            record.member_node_count += 1U;
            continue;
        }
        if (member->type == PBF_RELATION_MEMBER_RELATION) {
            record.member_relation_count += 1U;
            if (text_equals_cstr(member->role, "outer") || text_equals_cstr(member->role, "inner")) {
                blocking_relation_members += 1U;
            }
            continue;
        }
        if (text_equals_cstr(member->role, "inner")) {
            role = OSM_POSTAL_ROLE_INNER;
            record.inner_member_count += 1U;
        } else {
            /* "outer" and the empty role are both treated as outer. */
            role = OSM_POSTAL_ROLE_OUTER;
            record.outer_member_count += 1U;
        }
        record.member_way_count += 1U;
        if (collect_members && member_list_append(&context->members, member->id, role) != 0) {
            context->out_of_memory = 1;
            return 1;
        }
    }
    if (record.status == OSM_POSTAL_STATUS_OK) {
        if (blocking_relation_members != 0U) record.status = OSM_POSTAL_STATUS_RELATION_MEMBER;
        else if (record.member_way_count == 0U) record.status = OSM_POSTAL_STATUS_NO_WAY_MEMBERS;
    }
    if (has_name) {
        unsigned int length = name.size > OSM_POSTAL_NAME_MAX ? OSM_POSTAL_NAME_MAX : (unsigned int)name.size;
        if (text_pool_append(&context->names, name.data, length, &record.name_offset) != 0) {
            context->out_of_memory = 1;
            return 1;
        }
        record.name_length = length;
    }
    if (relation_list_append(&context->relations, &record) != 0) {
        context->out_of_memory = 1;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* pass 2: ways                                                        */
/* ------------------------------------------------------------------ */

static long long context_lookup_way(OsmPostalContext *context, long long id) {
    if (context->way_count == 0ULL) return -1;
    if (id < context->way_ids[0] || id > context->way_ids[context->way_count - 1ULL]) return -1;
    if (!context->way_unsorted && id >= context->way_last_id) {
        context->way_last_id = id;
        while (context->way_cursor < context->way_count && context->way_ids[context->way_cursor] < id) {
            context->way_cursor += 1ULL;
        }
        if (context->way_cursor < context->way_count && context->way_ids[context->way_cursor] == id) {
            return (long long)context->way_cursor;
        }
        return -1;
    }
    context->way_unsorted = 1;
    return find_id(context->way_ids, context->way_count, id);
}

static int on_way_id(void *user, long long id) {
    OsmPostalContext *context = (OsmPostalContext *)user;

    return context_lookup_way(context, id) >= 0 ? 1 : 0;
}

static int on_way(void *user, const PbfWay *way) {
    OsmPostalContext *context = (OsmPostalContext *)user;
    long long slot = find_id(context->way_ids, context->way_count, way->id);
    OsmPostalWayEntry *entry;
    unsigned int index;

    if (slot < 0) return 0;
    entry = &context->way_entries[slot];
    if (entry->found) return 0;
    if (id_list_reserve(&context->way_refs, (unsigned long long)way->ref_count) != 0) {
        context->out_of_memory = 1;
        return 1;
    }
    entry->ref_start = context->way_refs.count;
    entry->ref_count = way->ref_count;
    entry->found = 1;
    for (index = 0U; index < way->ref_count; ++index) {
        context->way_refs.items[context->way_refs.count] = way->refs[index];
        context->way_refs.count += 1ULL;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* pass 3: nodes                                                       */
/* ------------------------------------------------------------------ */

static int nano_to_e7(long long nano) {
    long long value;

    if (nano >= 0) value = (nano + 50LL) / 100LL;
    else value = -((-nano + 50LL) / 100LL);
    return (int)value;
}

static int on_node(void *user, const PbfNode *node) {
    OsmPostalContext *context = (OsmPostalContext *)user;
    long long slot;

    if (context->node_count == 0ULL) return 0;
    if (node->id < context->node_ids[0] || node->id > context->node_ids[context->node_count - 1ULL]) return 0;
    if (!context->node_unsorted && node->id >= context->node_last_id) {
        context->node_last_id = node->id;
        while (context->node_cursor < context->node_count && context->node_ids[context->node_cursor] < node->id) {
            context->node_cursor += 1ULL;
        }
        if (context->node_cursor >= context->node_count || context->node_ids[context->node_cursor] != node->id) return 0;
        slot = (long long)context->node_cursor;
    } else {
        context->node_unsorted = 1;
        slot = find_id(context->node_ids, context->node_count, node->id);
        if (slot < 0) return 0;
    }
    context->node_lon[slot] = nano_to_e7(node->lon_nano);
    context->node_lat[slot] = nano_to_e7(node->lat_nano);
    context->node_found[slot] = 1U;
    return 0;
}

/* ------------------------------------------------------------------ */
/* geometry                                                            */
/* ------------------------------------------------------------------ */

static __int128 ring_signed_area2(const OsmPostalPoint *points, unsigned int count) {
    __int128 sum = 0;
    unsigned int index;

    for (index = 0U; index + 1U < count; ++index) {
        __int128 x1 = (__int128)points[index].lon;
        __int128 y1 = (__int128)points[index].lat;
        __int128 x2 = (__int128)points[index + 1U].lon;
        __int128 y2 = (__int128)points[index + 1U].lat;
        sum += x1 * y2 - x2 * y1;
    }
    return sum;
}

/* Exact integer crossing-number point-in-polygon test. */
static int point_in_ring(const OsmPostalPoint *points, unsigned int count, int px, int py) {
    int inside = 0;
    unsigned int index;

    for (index = 0U; index + 1U < count; ++index) {
        long long x1 = points[index].lon;
        long long y1 = points[index].lat;
        long long x2 = points[index + 1U].lon;
        long long y2 = points[index + 1U].lat;
        __int128 lhs;
        __int128 rhs;

        if ((y1 > (long long)py) == (y2 > (long long)py)) continue;
        lhs = (__int128)((long long)px - x1) * (__int128)(y2 - y1);
        rhs = (__int128)((long long)py - y1) * (__int128)(x2 - x1);
        if (y2 > y1) {
            if (lhs < rhs) inside = !inside;
        } else {
            if (lhs > rhs) inside = !inside;
        }
    }
    return inside;
}

static int endpoint_compare(const void *left, const void *right) {
    const OsmPostalEndpoint *a = (const OsmPostalEndpoint *)left;
    const OsmPostalEndpoint *b = (const OsmPostalEndpoint *)right;

    if (a->node_id < b->node_id) return -1;
    if (a->node_id > b->node_id) return 1;
    if (a->segment < b->segment) return -1;
    if (a->segment > b->segment) return 1;
    if (a->end < b->end) return -1;
    if (a->end > b->end) return 1;
    return 0;
}

static int geometry_reserve_points(OsmPostalGeometry *geometry, unsigned long long extra) {
    unsigned long long needed = geometry->point_count + extra;
    unsigned long long capacity;
    OsmPostalPoint *next;

    if (needed <= geometry->point_capacity) return 0;
    capacity = geometry->point_capacity == 0ULL ? 65536ULL : geometry->point_capacity;
    while (capacity < needed) capacity *= 2ULL;
    next = (OsmPostalPoint *)rt_realloc(geometry->points, (size_t)capacity * sizeof(OsmPostalPoint));
    if (next == 0) return -1;
    geometry->points = next;
    geometry->point_capacity = capacity;
    return 0;
}

static int geometry_append_ring(OsmPostalGeometry *geometry, const OsmPostalRing *ring) {
    if (geometry->ring_count == geometry->ring_capacity) {
        unsigned long long capacity = geometry->ring_capacity == 0ULL ? 256ULL : geometry->ring_capacity * 2ULL;
        OsmPostalRing *next = (OsmPostalRing *)rt_realloc(geometry->rings, (size_t)capacity * sizeof(OsmPostalRing));
        if (next == 0) return -1;
        geometry->rings = next;
        geometry->ring_capacity = capacity;
    }
    geometry->rings[geometry->ring_count] = *ring;
    geometry->ring_count += 1ULL;
    return 0;
}

static int geometry_reserve_scratch(OsmPostalGeometry *geometry, unsigned long long segments, unsigned long long ring_nodes) {
    if (segments * 2ULL > geometry->endpoint_capacity) {
        unsigned long long capacity = segments * 2ULL;
        OsmPostalEndpoint *next = (OsmPostalEndpoint *)rt_realloc(geometry->endpoints, (size_t)capacity * sizeof(OsmPostalEndpoint));
        if (next == 0) return -1;
        geometry->endpoints = next;
        geometry->endpoint_capacity = capacity;
    }
    if (segments > geometry->segment_capacity) {
        unsigned char *used = (unsigned char *)rt_realloc(geometry->segment_used, (size_t)segments);
        unsigned long long *ways;
        if (used == 0) return -1;
        geometry->segment_used = used;
        ways = (unsigned long long *)rt_realloc(geometry->segment_way, (size_t)segments * sizeof(unsigned long long));
        if (ways == 0) return -1;
        geometry->segment_way = ways;
        geometry->segment_capacity = segments;
    }
    if (ring_nodes > geometry->ring_node_capacity) {
        long long *next = (long long *)rt_realloc(geometry->ring_nodes, (size_t)ring_nodes * sizeof(long long));
        if (next == 0) return -1;
        geometry->ring_nodes = next;
        geometry->ring_node_capacity = ring_nodes;
    }
    return 0;
}

/*
 * Joins the way members of one role into closed rings. Segments may appear in
 * any order and in any direction; the joiner walks endpoints until the ring
 * closes. Returns OSM_POSTAL_STATUS_OK or the failing status.
 */
static unsigned int assemble_rings(OsmPostalContext *context, OsmPostalGeometry *geometry,
                                   const OsmPostalRelation *relation, unsigned int role) {
    unsigned long long member_index;
    unsigned long long segment_count = 0ULL;
    unsigned long long total_nodes = 0ULL;
    unsigned long long endpoint_count = 0ULL;
    unsigned long long index;

    for (member_index = 0ULL; member_index < relation->member_way_count; ++member_index) {
        const OsmPostalMember *member = &context->members.items[relation->member_start + member_index];
        long long slot;
        if (member->role != role) continue;
        slot = find_id(context->way_ids, context->way_count, member->way_id);
        if (slot < 0 || !context->way_entries[slot].found) return OSM_POSTAL_STATUS_MISSING_WAY;
        if (context->way_entries[slot].ref_count < 2U) return OSM_POSTAL_STATUS_SHORT_WAY;
        total_nodes += context->way_entries[slot].ref_count;
        segment_count += 1ULL;
    }
    if (segment_count == 0ULL) return OSM_POSTAL_STATUS_OK;
    if (geometry_reserve_scratch(geometry, segment_count, total_nodes + 1ULL) != 0) {
        return OSM_POSTAL_STATUS_OUT_OF_MEMORY;
    }

    segment_count = 0ULL;
    for (member_index = 0ULL; member_index < relation->member_way_count; ++member_index) {
        const OsmPostalMember *member = &context->members.items[relation->member_start + member_index];
        long long slot;
        if (member->role != role) continue;
        slot = find_id(context->way_ids, context->way_count, member->way_id);
        geometry->segment_way[segment_count] = (unsigned long long)slot;
        geometry->segment_used[segment_count] = 0U;
        segment_count += 1ULL;
    }

    for (index = 0ULL; index < segment_count; ++index) {
        const OsmPostalWayEntry *entry = &context->way_entries[geometry->segment_way[index]];
        geometry->endpoints[endpoint_count].node_id = context->way_refs.items[entry->ref_start];
        geometry->endpoints[endpoint_count].segment = (unsigned int)index;
        geometry->endpoints[endpoint_count].end = 0U;
        endpoint_count += 1ULL;
        geometry->endpoints[endpoint_count].node_id = context->way_refs.items[entry->ref_start + entry->ref_count - 1U];
        geometry->endpoints[endpoint_count].segment = (unsigned int)index;
        geometry->endpoints[endpoint_count].end = 1U;
        endpoint_count += 1ULL;
    }
    rt_sort(geometry->endpoints, (size_t)endpoint_count, sizeof(OsmPostalEndpoint), endpoint_compare);

    for (index = 0ULL; index < segment_count; ++index) {
        unsigned long long ring_node_count = 0ULL;
        long long ring_start_node;
        long long cursor_node;
        const OsmPostalWayEntry *entry;
        unsigned long long guard = 0ULL;

        if (geometry->segment_used[index]) continue;
        entry = &context->way_entries[geometry->segment_way[index]];
        geometry->segment_used[index] = 1U;
        {
            unsigned int ref;
            for (ref = 0U; ref < entry->ref_count; ++ref) {
                geometry->ring_nodes[ring_node_count] = context->way_refs.items[entry->ref_start + ref];
                ring_node_count += 1ULL;
            }
        }
        ring_start_node = geometry->ring_nodes[0];
        cursor_node = geometry->ring_nodes[ring_node_count - 1ULL];

        while (cursor_node != ring_start_node) {
            unsigned long long low = 0ULL;
            unsigned long long high = endpoint_count;
            unsigned long long probe;
            long long chosen = -1;
            unsigned int chosen_end = 0U;
            const OsmPostalWayEntry *next_entry;

            if (guard++ > segment_count) return OSM_POSTAL_STATUS_OPEN_RING;
            while (low < high) {
                unsigned long long mid = low + (high - low) / 2ULL;
                if (geometry->endpoints[mid].node_id < cursor_node) low = mid + 1ULL;
                else high = mid;
            }
            for (probe = low; probe < endpoint_count && geometry->endpoints[probe].node_id == cursor_node; ++probe) {
                unsigned int segment = geometry->endpoints[probe].segment;
                if (geometry->segment_used[segment]) continue;
                chosen = (long long)segment;
                chosen_end = geometry->endpoints[probe].end;
                break;
            }
            if (chosen < 0) return OSM_POSTAL_STATUS_OPEN_RING;
            geometry->segment_used[chosen] = 1U;
            next_entry = &context->way_entries[geometry->segment_way[chosen]];
            if (chosen_end == 0U) {
                unsigned int ref;
                for (ref = 1U; ref < next_entry->ref_count; ++ref) {
                    geometry->ring_nodes[ring_node_count] = context->way_refs.items[next_entry->ref_start + ref];
                    ring_node_count += 1ULL;
                }
            } else {
                unsigned int ref = next_entry->ref_count - 1U;
                while (ref > 0U) {
                    ref -= 1U;
                    geometry->ring_nodes[ring_node_count] = context->way_refs.items[next_entry->ref_start + ref];
                    ring_node_count += 1ULL;
                }
            }
            cursor_node = geometry->ring_nodes[ring_node_count - 1ULL];
        }

        if (ring_node_count < 4ULL) return OSM_POSTAL_STATUS_DEGENERATE_RING;
        {
            OsmPostalRing ring;
            unsigned long long point_index;
            OsmPostalPoint *points;

            if (geometry_reserve_points(geometry, ring_node_count) != 0) return OSM_POSTAL_STATUS_OUT_OF_MEMORY;
            rt_memset(&ring, 0, sizeof(ring));
            ring.point_start = geometry->point_count;
            ring.point_count = (unsigned int)ring_node_count;
            ring.role = role;
            ring.outer_index = -1;
            points = geometry->points + ring.point_start;
            for (point_index = 0ULL; point_index < ring_node_count; ++point_index) {
                long long node_slot = find_id(context->node_ids, context->node_count, geometry->ring_nodes[point_index]);
                if (node_slot < 0 || !context->node_found[node_slot]) return OSM_POSTAL_STATUS_MISSING_NODE;
                points[point_index].lon = context->node_lon[node_slot];
                points[point_index].lat = context->node_lat[node_slot];
                if (point_index == 0ULL) {
                    ring.min_lon = points[0].lon;
                    ring.max_lon = points[0].lon;
                    ring.min_lat = points[0].lat;
                    ring.max_lat = points[0].lat;
                } else {
                    if (points[point_index].lon < ring.min_lon) ring.min_lon = points[point_index].lon;
                    if (points[point_index].lon > ring.max_lon) ring.max_lon = points[point_index].lon;
                    if (points[point_index].lat < ring.min_lat) ring.min_lat = points[point_index].lat;
                    if (points[point_index].lat > ring.max_lat) ring.max_lat = points[point_index].lat;
                }
            }
            ring.area2 = ring_signed_area2(points, ring.point_count);
            if (ring.area2 == 0) return OSM_POSTAL_STATUS_DEGENERATE_RING;
            /*
             * RFC 7946 requires counter-clockwise exterior rings and clockwise
             * interior rings.
             */
            {
                int want_ccw = (role == OSM_POSTAL_ROLE_OUTER);
                int is_ccw = ring.area2 > 0;
                if (is_ccw != want_ccw) {
                    unsigned int left = 0U;
                    unsigned int right = ring.point_count - 1U;
                    while (left < right) {
                        OsmPostalPoint temp = points[left];
                        points[left] = points[right];
                        points[right] = temp;
                        left += 1U;
                        right -= 1U;
                    }
                    ring.area2 = -ring.area2;
                }
            }
            geometry->point_count += ring_node_count;
            if (geometry_append_ring(geometry, &ring) != 0) return OSM_POSTAL_STATUS_OUT_OF_MEMORY;
        }
    }
    return OSM_POSTAL_STATUS_OK;
}

/* ------------------------------------------------------------------ */
/* output writer                                                       */
/* ------------------------------------------------------------------ */

static int writer_flush(OsmPostalWriter *writer) {
    if (writer->used == 0ULL) return 0;
    if (rt_write_all(writer->fd, writer->data, (size_t)writer->used) != 0) {
        writer->failed = 1;
        return -1;
    }
    writer->used = 0ULL;
    return 0;
}

static int writer_write(OsmPostalWriter *writer, const void *data, size_t size) {
    if (size == 0U) return 0;
    if ((unsigned long long)size > writer->capacity) {
        if (writer_flush(writer) != 0) return -1;
        if (rt_write_all(writer->fd, data, size) != 0) {
            writer->failed = 1;
            return -1;
        }
        return 0;
    }
    if (writer->used + (unsigned long long)size > writer->capacity && writer_flush(writer) != 0) return -1;
    memcpy(writer->data + writer->used, data, size);
    writer->used += (unsigned long long)size;
    return 0;
}

static int writer_char(OsmPostalWriter *writer, char ch) {
    return writer_write(writer, &ch, 1U);
}

static int writer_cstr(OsmPostalWriter *writer, const char *text) {
    return writer_write(writer, text, rt_strlen(text));
}

static int writer_uint(OsmPostalWriter *writer, unsigned long long value) {
    char buffer[32];

    rt_unsigned_to_string(value, buffer, sizeof(buffer));
    return writer_cstr(writer, buffer);
}

static int writer_int(OsmPostalWriter *writer, long long value) {
    if (value < 0) {
        if (writer_char(writer, '-') != 0) return -1;
        return writer_uint(writer, (unsigned long long)(-value));
    }
    return writer_uint(writer, (unsigned long long)value);
}

/* Writes a 1e-7 degree integer as a plain JSON number. */
static int writer_degrees(OsmPostalWriter *writer, long long value) {
    unsigned long long magnitude;
    unsigned long long whole;
    unsigned long long fraction;
    char digits[8];
    int index;
    int last;

    if (value < 0) {
        if (writer_char(writer, '-') != 0) return -1;
        magnitude = (unsigned long long)(-value);
    } else {
        magnitude = (unsigned long long)value;
    }
    whole = magnitude / 10000000ULL;
    fraction = magnitude % 10000000ULL;
    if (writer_uint(writer, whole) != 0) return -1;
    if (fraction == 0ULL) return 0;
    for (index = 6; index >= 0; --index) {
        digits[index] = (char)('0' + (int)(fraction % 10ULL));
        fraction /= 10ULL;
    }
    last = 6;
    while (last > 0 && digits[last] == '0') last -= 1;
    if (writer_char(writer, '.') != 0) return -1;
    return writer_write(writer, digits, (size_t)(last + 1));
}

static int writer_json_string(OsmPostalWriter *writer, const char *data, unsigned int length) {
    unsigned int start = 0U;
    unsigned int index;

    if (writer_char(writer, '"') != 0) return -1;
    for (index = 0U; index < length; ++index) {
        unsigned char ch = (unsigned char)data[index];
        const char *escape = 0;
        char buffer[7];

        if (ch == '"') escape = "\\\"";
        else if (ch == '\\') escape = "\\\\";
        else if (ch == '\n') escape = "\\n";
        else if (ch == '\r') escape = "\\r";
        else if (ch == '\t') escape = "\\t";
        else if (ch < 0x20U) {
            const char *hex = "0123456789abcdef";
            buffer[0] = '\\';
            buffer[1] = 'u';
            buffer[2] = '0';
            buffer[3] = '0';
            buffer[4] = hex[(ch >> 4) & 0x0FU];
            buffer[5] = hex[ch & 0x0FU];
            buffer[6] = '\0';
            escape = buffer;
        }
        if (escape == 0) continue;
        if (writer_write(writer, data + start, index - start) != 0) return -1;
        if (writer_cstr(writer, escape) != 0) return -1;
        start = index + 1U;
    }
    if (writer_write(writer, data + start, length - start) != 0) return -1;
    return writer_char(writer, '"');
}

static const char *source_name(unsigned int source) {
    switch (source) {
        case OSM_POSTAL_SOURCE_DE: return "postal_code:DE";
        case OSM_POSTAL_SOURCE_POSTAL_CODE: return "postal_code";
        case OSM_POSTAL_SOURCE_REF: return "ref";
        default: return "none";
    }
}

static const char *status_name(unsigned int status) {
    switch (status) {
        case OSM_POSTAL_STATUS_NO_CODE: return "no_postal_code_tag";
        case OSM_POSTAL_STATUS_INVALID_CODE: return "invalid_postal_code";
        case OSM_POSTAL_STATUS_FOREIGN_TAG: return "foreign_country_tag";
        case OSM_POSTAL_STATUS_OUTSIDE_DE: return "outside_germany";
        case OSM_POSTAL_STATUS_NOT_DE_TAGGED: return "not_postal_code_de_tagged";
        case OSM_POSTAL_STATUS_NO_WAY_MEMBERS: return "no_way_members";
        case OSM_POSTAL_STATUS_MISSING_WAY: return "missing_way_member";
        case OSM_POSTAL_STATUS_MISSING_NODE: return "missing_node";
        case OSM_POSTAL_STATUS_SHORT_WAY: return "way_with_less_than_two_nodes";
        case OSM_POSTAL_STATUS_OPEN_RING: return "unclosed_ring";
        case OSM_POSTAL_STATUS_DEGENERATE_RING: return "degenerate_ring";
        case OSM_POSTAL_STATUS_NO_OUTER_RING: return "no_outer_ring";
        case OSM_POSTAL_STATUS_ORPHAN_INNER: return "inner_ring_without_outer_ring";
        case OSM_POSTAL_STATUS_RELATION_MEMBER: return "unsupported_relation_member";
        case OSM_POSTAL_STATUS_OUT_OF_MEMORY: return "out_of_memory";
        default: return "ok";
    }
}

static int status_is_geometry_failure(unsigned int status) {
    return status >= OSM_POSTAL_FIRST_GEOMETRY_STATUS;
}

static int status_is_incomplete(unsigned int status) {
    return status >= OSM_POSTAL_FIRST_INCOMPLETE_STATUS && status < OSM_POSTAL_FIRST_GEOMETRY_STATUS;
}

/* ------------------------------------------------------------------ */
/* deterministic emission order                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned long long relation_index;
    char postcode[8];
    long long relation_id;
} OsmPostalOrder;

static int order_compare(const void *left, const void *right) {
    const OsmPostalOrder *a = (const OsmPostalOrder *)left;
    const OsmPostalOrder *b = (const OsmPostalOrder *)right;
    int cmp = rt_strcmp(a->postcode, b->postcode);

    if (cmp != 0) return cmp;
    if (a->relation_id < b->relation_id) return -1;
    if (a->relation_id > b->relation_id) return 1;
    return 0;
}

static int write_ring(OsmPostalWriter *writer, const OsmPostalGeometry *geometry, const OsmPostalRing *ring) {
    unsigned int index;

    if (writer_char(writer, '[') != 0) return -1;
    for (index = 0U; index < ring->point_count; ++index) {
        const OsmPostalPoint *point = &geometry->points[ring->point_start + index];
        if (index != 0U && writer_char(writer, ',') != 0) return -1;
        if (writer_char(writer, '[') != 0) return -1;
        if (writer_degrees(writer, point->lon) != 0) return -1;
        if (writer_char(writer, ',') != 0) return -1;
        if (writer_degrees(writer, point->lat) != 0) return -1;
        if (writer_char(writer, ']') != 0) return -1;
    }
    return writer_char(writer, ']');
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static void write_usage(const char *program) {
    rt_write_cstr(2, "Usage: ");
    rt_write_cstr(2, program);
    rt_write_cstr(2, " FILE.osm.pbf OUT.geojson [options]\n");
    rt_write_cstr(2,
                  "Options:\n"
                  "  --report FILE       write a TSV audit row for every candidate relation\n"
                  "  --strict            exit non-zero when a relation has broken geometry\n"
                  "  --strict-incomplete  also fail when a member way or node is missing\n"
                  "  --bbox MINLON,MINLAT,MAXLON,MAXLAT   override the Germany acceptance box\n"
                  "  --no-bbox-filter    accept relations regardless of their location\n"
                  "  --require-de-tag    only accept relations tagged postal_code:DE\n"
                  "  --quiet             do not list rejected relations on stderr\n");
}

static int output_is_stdout(const char *path) {
    return path[0] == '-' && path[1] == '\0';
}

static int write_report(const char *path, OsmPostalContext *context, const OsmPostalGeometry *geometry,
                        const unsigned long long *ring_start, const unsigned long long *ring_count,
                        const OsmPostalOrder *order, unsigned long long order_count) {
    OsmPostalWriter report;
    unsigned long long index;

    rt_memset(&report, 0, sizeof(report));
    report.fd = platform_open_write(path, 0644U);
    if (report.fd < 0) {
        rt_write_cstr(2, "osm-postal: could not open report file\n");
        return -1;
    }
    report.capacity = OSM_POSTAL_BUFFER_SIZE;
    report.data = (unsigned char *)rt_malloc((size_t)report.capacity);
    if (report.data == 0) {
        rt_write_cstr(2, "osm-postal: out of memory\n");
        return -1;
    }
    (void)writer_cstr(&report, "relation_id\tpostcode\tpostcode_source\tstatus\tdetail\tmember_ways\touter_rings\tinner_rings\n");
    for (index = 0ULL; index < order_count; ++index) {
        unsigned long long relation_index = order[index].relation_index;
        const OsmPostalRelation *relation = &context->relations.items[relation_index];
        unsigned long long ring_index;
        unsigned long long outer_count = 0ULL;
        unsigned long long inner_count = 0ULL;
        const char *status;

        for (ring_index = ring_start[relation_index]; ring_index < ring_start[relation_index] + ring_count[relation_index]; ++ring_index) {
            if (geometry->rings[ring_index].role == OSM_POSTAL_ROLE_OUTER) outer_count += 1ULL;
            else inner_count += 1ULL;
        }
        if (relation->status == OSM_POSTAL_STATUS_OK) status = "kept";
        else if (status_is_geometry_failure(relation->status)) status = "broken";
        else if (status_is_incomplete(relation->status)) status = "incomplete";
        else status = "excluded";
        (void)writer_int(&report, relation->id);
        (void)writer_char(&report, '\t');
        (void)writer_cstr(&report, relation->postcode);
        (void)writer_char(&report, '\t');
        (void)writer_cstr(&report, source_name(relation->postcode_source));
        (void)writer_char(&report, '\t');
        (void)writer_cstr(&report, status);
        (void)writer_char(&report, '\t');
        (void)writer_cstr(&report, status_name(relation->status));
        (void)writer_char(&report, '\t');
        (void)writer_uint(&report, relation->member_way_count);
        (void)writer_char(&report, '\t');
        (void)writer_uint(&report, outer_count);
        (void)writer_char(&report, '\t');
        (void)writer_uint(&report, inner_count);
        (void)writer_char(&report, '\n');
    }
    if (writer_flush(&report) != 0 || report.failed) {
        rt_write_cstr(2, "osm-postal: could not write report\n");
        return -1;
    }
    if (platform_close(report.fd) != 0) {
        rt_write_cstr(2, "osm-postal: could not close report file\n");
        return -1;
    }
    rt_free(report.data);
    return 0;
}

int main(int argc, char **argv) {
    const char *program = argc > 0 ? argv[0] : "osm-postal";
    const char *pbf_path;
    const char *out_path;
    const char *report_path = 0;
    OsmPostalContext context;
    OsmPostalGeometry geometry;
    OsmPostalWriter writer;
    PbfStreamCallbacks callbacks;
    char error[PBF_ERROR_CAPACITY];
    OsmPostalOrder *order = 0;
    OsmPostalOrder *report_order = 0;
    unsigned long long order_count = 0ULL;
    unsigned long long report_count = 0ULL;
    unsigned long long index;
    unsigned long long *ring_start = 0;
    unsigned long long *ring_count = 0;
    long long *scratch = 0;
    int argi;
    int output_stdout;
    int stats_fd;
    int quiet = 0;
    int strict = 0;
    int strict_incomplete = 0;
    int feature_written = 0;
    int has_total_bbox = 0;
    long long total_min_lon = 0;
    long long total_min_lat = 0;
    long long total_max_lon = 0;
    long long total_max_lat = 0;

    if (argc == 2 && (rt_strcmp(argv[1], "-h") == 0 || rt_strcmp(argv[1], "--help") == 0)) {
        write_usage(program);
        return 0;
    }
    if (argc < 3) {
        write_usage(program);
        return 1;
    }
    pbf_path = argv[1];
    out_path = argv[2];
    rt_memset(&context, 0, sizeof(context));
    rt_memset(&geometry, 0, sizeof(geometry));
    rt_memset(&writer, 0, sizeof(writer));
    context.min_lon = OSM_POSTAL_DE_MIN_LON;
    context.min_lat = OSM_POSTAL_DE_MIN_LAT;
    context.max_lon = OSM_POSTAL_DE_MAX_LON;
    context.max_lat = OSM_POSTAL_DE_MAX_LAT;
    context.bbox_filter = 1;

    argi = 3;
    while (argi < argc) {
        if (rt_strcmp(argv[argi], "--report") == 0) {
            argi += 1;
            if (argi >= argc) {
                write_usage(program);
                return 1;
            }
            report_path = argv[argi];
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--strict") == 0) {
            strict = 1;
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--strict-incomplete") == 0) {
            strict = 1;
            strict_incomplete = 1;
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--quiet") == 0) {
            quiet = 1;
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--no-bbox-filter") == 0) {
            context.bbox_filter = 0;
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--require-de-tag") == 0) {
            context.require_de_tag = 1;
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--bbox") == 0) {
            argi += 1;
            if (argi >= argc || parse_bbox_arg(argv[argi], &context.min_lon, &context.min_lat, &context.max_lon, &context.max_lat) != 0) {
                write_usage(program);
                return 1;
            }
            context.bbox_filter = 1;
            argi += 1;
        } else {
            write_usage(program);
            return 1;
        }
    }

    /* pass 1: relations */
    rt_memset(&callbacks, 0, sizeof(callbacks));
    callbacks.relation = on_relation;
    error[0] = '\0';
    if (pbf_stream_entities(pbf_path, &callbacks, &context, error, sizeof(error)) != 0 || context.out_of_memory) {
        rt_write_cstr(2, "osm-postal: relation pass failed: ");
        rt_write_cstr(2, context.out_of_memory ? "out of memory" : (error[0] == '\0' ? "failed to parse PBF" : error));
        rt_write_char(2, '\n');
        return 1;
    }

    /* build the sorted way lookup */
    if (context.members.count > 0ULL) {
        context.way_ids = (long long *)rt_malloc((size_t)context.members.count * sizeof(long long));
        scratch = (long long *)rt_malloc((size_t)context.members.count * sizeof(long long));
        if (context.way_ids == 0 || scratch == 0) {
            rt_write_cstr(2, "osm-postal: out of memory building the way index\n");
            return 1;
        }
        for (index = 0ULL; index < context.members.count; ++index) {
            context.way_ids[index] = context.members.items[index].way_id;
        }
        sort_ids(context.way_ids, context.members.count, scratch);
        context.way_count = dedupe_sorted_ids(context.way_ids, context.members.count);
        rt_free(scratch);
        scratch = 0;
        context.way_entries = (OsmPostalWayEntry *)rt_malloc((size_t)context.way_count * sizeof(OsmPostalWayEntry));
        if (context.way_entries == 0) {
            rt_write_cstr(2, "osm-postal: out of memory building the way index\n");
            return 1;
        }
        rt_memset(context.way_entries, 0, (size_t)context.way_count * sizeof(OsmPostalWayEntry));
    }

    /* pass 2: ways */
    if (context.way_count > 0ULL) {
        rt_memset(&callbacks, 0, sizeof(callbacks));
        callbacks.flags = PBF_STREAM_SKIP_WAY_TAGS;
        callbacks.way_id = on_way_id;
        callbacks.way = on_way;
        context.way_last_id = context.way_ids[0];
        error[0] = '\0';
        if (pbf_stream_entities(pbf_path, &callbacks, &context, error, sizeof(error)) != 0 || context.out_of_memory) {
            rt_write_cstr(2, "osm-postal: way pass failed: ");
            rt_write_cstr(2, context.out_of_memory ? "out of memory" : (error[0] == '\0' ? "failed to parse PBF" : error));
            rt_write_char(2, '\n');
            return 1;
        }
    }

    /* build the sorted node lookup */
    if (context.way_refs.count > 0ULL) {
        context.node_ids = (long long *)rt_malloc((size_t)context.way_refs.count * sizeof(long long));
        scratch = (long long *)rt_malloc((size_t)context.way_refs.count * sizeof(long long));
        if (context.node_ids == 0 || scratch == 0) {
            rt_write_cstr(2, "osm-postal: out of memory building the node index\n");
            return 1;
        }
        for (index = 0ULL; index < context.way_refs.count; ++index) {
            context.node_ids[index] = context.way_refs.items[index];
        }
        sort_ids(context.node_ids, context.way_refs.count, scratch);
        context.node_count = dedupe_sorted_ids(context.node_ids, context.way_refs.count);
        rt_free(scratch);
        scratch = 0;
        context.node_lon = (int *)rt_malloc((size_t)context.node_count * sizeof(int));
        context.node_lat = (int *)rt_malloc((size_t)context.node_count * sizeof(int));
        context.node_found = (unsigned char *)rt_malloc((size_t)context.node_count);
        if (context.node_lon == 0 || context.node_lat == 0 || context.node_found == 0) {
            rt_write_cstr(2, "osm-postal: out of memory building the node index\n");
            return 1;
        }
        rt_memset(context.node_found, 0, (size_t)context.node_count);
    }

    /* pass 3: nodes */
    if (context.node_count > 0ULL) {
        rt_memset(&callbacks, 0, sizeof(callbacks));
        callbacks.flags = PBF_STREAM_SKIP_NODE_TAGS;
        callbacks.node = on_node;
        context.node_last_id = context.node_ids[0];
        error[0] = '\0';
        if (pbf_stream_entities(pbf_path, &callbacks, &context, error, sizeof(error)) != 0) {
            rt_write_cstr(2, "osm-postal: node pass failed: ");
            rt_write_cstr(2, error[0] == '\0' ? "failed to parse PBF" : error);
            rt_write_char(2, '\n');
            return 1;
        }
    }

    if (context.relations.count > 0ULL) {
        ring_start = (unsigned long long *)rt_malloc((size_t)context.relations.count * sizeof(unsigned long long));
        ring_count = (unsigned long long *)rt_malloc((size_t)context.relations.count * sizeof(unsigned long long));
        order = (OsmPostalOrder *)rt_malloc((size_t)context.relations.count * sizeof(OsmPostalOrder));
        report_order = (OsmPostalOrder *)rt_malloc((size_t)context.relations.count * sizeof(OsmPostalOrder));
        if (ring_start == 0 || ring_count == 0 || order == 0 || report_order == 0) {
            rt_write_cstr(2, "osm-postal: out of memory assembling geometry\n");
            return 1;
        }
    }

    /* assemble geometry per relation */
    for (index = 0ULL; index < context.relations.count; ++index) {
        OsmPostalRelation *relation = &context.relations.items[index];
        unsigned long long base = geometry.ring_count;
        unsigned int status;
        unsigned long long ring_index;
        unsigned long long outer_count = 0ULL;
        unsigned long long inner_count = 0ULL;
        int rel_min_lon = 0;
        int rel_min_lat = 0;
        int rel_max_lon = 0;
        int rel_max_lat = 0;

        ring_start[index] = base;
        ring_count[index] = 0ULL;
        if (relation->status != OSM_POSTAL_STATUS_OK) continue;

        status = assemble_rings(&context, &geometry, relation, OSM_POSTAL_ROLE_OUTER);
        if (status == OSM_POSTAL_STATUS_OK) {
            status = assemble_rings(&context, &geometry, relation, OSM_POSTAL_ROLE_INNER);
        }
        if (status != OSM_POSTAL_STATUS_OK) {
            relation->status = status;
            geometry.ring_count = base;
            continue;
        }
        for (ring_index = base; ring_index < geometry.ring_count; ++ring_index) {
            const OsmPostalRing *ring = &geometry.rings[ring_index];
            if (ring->role == OSM_POSTAL_ROLE_OUTER) outer_count += 1ULL;
            else inner_count += 1ULL;
            if (ring_index == base) {
                rel_min_lon = ring->min_lon;
                rel_max_lon = ring->max_lon;
                rel_min_lat = ring->min_lat;
                rel_max_lat = ring->max_lat;
            } else {
                if (ring->min_lon < rel_min_lon) rel_min_lon = ring->min_lon;
                if (ring->max_lon > rel_max_lon) rel_max_lon = ring->max_lon;
                if (ring->min_lat < rel_min_lat) rel_min_lat = ring->min_lat;
                if (ring->max_lat > rel_max_lat) rel_max_lat = ring->max_lat;
            }
        }
        if (outer_count == 0ULL) {
            relation->status = OSM_POSTAL_STATUS_NO_OUTER_RING;
            geometry.ring_count = base;
            continue;
        }
        /*
         * Germany gate. A relation that is explicitly tagged postal_code:DE is
         * accepted on the tag alone. Every other relation must lie completely
         * inside the German acceptance box, which keeps neighbouring-country
         * postal areas out of extracts that cross the border.
         */
        if (context.bbox_filter && relation->postcode_source != OSM_POSTAL_SOURCE_DE) {
            if ((long long)rel_min_lon < context.min_lon || (long long)rel_max_lon > context.max_lon ||
                (long long)rel_min_lat < context.min_lat || (long long)rel_max_lat > context.max_lat) {
                relation->status = OSM_POSTAL_STATUS_OUTSIDE_DE;
                geometry.ring_count = base;
                continue;
            }
        }
        /* nest inner rings into the smallest containing outer ring */
        for (ring_index = base; ring_index < geometry.ring_count; ++ring_index) {
            OsmPostalRing *inner = &geometry.rings[ring_index];
            unsigned long long candidate;
            long long best = -1;
            __int128 best_area = 0;

            if (inner->role != OSM_POSTAL_ROLE_INNER) continue;
            for (candidate = base; candidate < geometry.ring_count; ++candidate) {
                const OsmPostalRing *outer = &geometry.rings[candidate];
                const OsmPostalPoint *outer_points;
                unsigned int votes = 0U;
                unsigned int samples = 0U;
                unsigned int sample;

                if (outer->role != OSM_POSTAL_ROLE_OUTER) continue;
                if (inner->min_lon < outer->min_lon || inner->max_lon > outer->max_lon) continue;
                if (inner->min_lat < outer->min_lat || inner->max_lat > outer->max_lat) continue;
                outer_points = geometry.points + outer->point_start;
                for (sample = 0U; sample < 3U; ++sample) {
                    unsigned int span = inner->point_count - 1U;
                    unsigned int offset = (unsigned int)(((unsigned long long)sample * (unsigned long long)span) / 3ULL);
                    const OsmPostalPoint *probe = geometry.points + inner->point_start + offset;
                    samples += 1U;
                    if (point_in_ring(outer_points, outer->point_count, probe->lon, probe->lat)) votes += 1U;
                }
                if (votes * 2U <= samples) continue;
                if (best < 0 || outer->area2 < best_area) {
                    best = (long long)candidate;
                    best_area = outer->area2;
                }
            }
            if (best < 0) {
                relation->status = OSM_POSTAL_STATUS_ORPHAN_INNER;
                break;
            }
            inner->outer_index = (int)(best - (long long)base);
        }
        if (relation->status != OSM_POSTAL_STATUS_OK) {
            geometry.ring_count = base;
            continue;
        }
        ring_count[index] = geometry.ring_count - base;
        context.outer_rings += outer_count;
        context.inner_rings += inner_count;
        if (!has_total_bbox) {
            total_min_lon = rel_min_lon;
            total_max_lon = rel_max_lon;
            total_min_lat = rel_min_lat;
            total_max_lat = rel_max_lat;
            has_total_bbox = 1;
        } else {
            if (rel_min_lon < total_min_lon) total_min_lon = rel_min_lon;
            if (rel_max_lon > total_max_lon) total_max_lon = rel_max_lon;
            if (rel_min_lat < total_min_lat) total_min_lat = rel_min_lat;
            if (rel_max_lat > total_max_lat) total_max_lat = rel_max_lat;
        }
    }

    for (index = 0ULL; index < context.relations.count; ++index) {
        const OsmPostalRelation *relation = &context.relations.items[index];
        if (relation->status == OSM_POSTAL_STATUS_OK) context.relations_kept += 1ULL;
        else if (status_is_geometry_failure(relation->status)) context.relations_broken += 1ULL;
        else if (status_is_incomplete(relation->status)) context.relations_incomplete += 1ULL;
        else context.relations_excluded += 1ULL;
        report_order[report_count].relation_index = index;
        report_order[report_count].relation_id = relation->id;
        rt_copy_string(report_order[report_count].postcode, sizeof(report_order[report_count].postcode), relation->postcode);
        report_count += 1ULL;
        if (relation->status != OSM_POSTAL_STATUS_OK) continue;
        order[order_count].relation_index = index;
        order[order_count].relation_id = relation->id;
        rt_copy_string(order[order_count].postcode, sizeof(order[order_count].postcode), relation->postcode);
        order_count += 1ULL;
    }
    if (order_count > 1ULL) rt_sort(order, (size_t)order_count, sizeof(OsmPostalOrder), order_compare);
    if (report_count > 1ULL) rt_sort(report_order, (size_t)report_count, sizeof(OsmPostalOrder), order_compare);

    output_stdout = output_is_stdout(out_path);
    writer.fd = output_stdout ? 1 : platform_open_write(out_path, 0644U);
    if (writer.fd < 0) {
        rt_write_cstr(2, "osm-postal: could not open output file\n");
        return 1;
    }
    writer.capacity = OSM_POSTAL_BUFFER_SIZE;
    writer.data = (unsigned char *)rt_malloc((size_t)writer.capacity);
    if (writer.data == 0) {
        rt_write_cstr(2, "osm-postal: out of memory\n");
        return 1;
    }

    (void)writer_cstr(&writer, "{\"type\":\"FeatureCollection\"");
    if (has_total_bbox) {
        (void)writer_cstr(&writer, ",\"bbox\":[");
        (void)writer_degrees(&writer, total_min_lon);
        (void)writer_char(&writer, ',');
        (void)writer_degrees(&writer, total_min_lat);
        (void)writer_char(&writer, ',');
        (void)writer_degrees(&writer, total_max_lon);
        (void)writer_char(&writer, ',');
        (void)writer_degrees(&writer, total_max_lat);
        (void)writer_char(&writer, ']');
    }
    (void)writer_cstr(&writer, ",\"features\":[");

    for (index = 0ULL; index < order_count; ++index) {
        unsigned long long relation_index = order[index].relation_index;
        const OsmPostalRelation *relation = &context.relations.items[relation_index];
        unsigned long long base = ring_start[relation_index];
        unsigned long long total = ring_count[relation_index];
        unsigned long long ring_index;
        unsigned long long outer_count = 0ULL;
        unsigned long long inner_count = 0ULL;
        unsigned long long coordinate_count = 0ULL;
        int rel_min_lon = 0;
        int rel_min_lat = 0;
        int rel_max_lon = 0;
        int rel_max_lat = 0;

        for (ring_index = base; ring_index < base + total; ++ring_index) {
            const OsmPostalRing *ring = &geometry.rings[ring_index];
            if (ring->role == OSM_POSTAL_ROLE_OUTER) outer_count += 1ULL;
            else inner_count += 1ULL;
            coordinate_count += ring->point_count;
            if (ring_index == base) {
                rel_min_lon = ring->min_lon;
                rel_max_lon = ring->max_lon;
                rel_min_lat = ring->min_lat;
                rel_max_lat = ring->max_lat;
            } else {
                if (ring->min_lon < rel_min_lon) rel_min_lon = ring->min_lon;
                if (ring->max_lon > rel_max_lon) rel_max_lon = ring->max_lon;
                if (ring->min_lat < rel_min_lat) rel_min_lat = ring->min_lat;
                if (ring->max_lat > rel_max_lat) rel_max_lat = ring->max_lat;
            }
        }

        if (feature_written) (void)writer_char(&writer, ',');
        feature_written = 1;
        (void)writer_cstr(&writer, "\n{\"type\":\"Feature\",\"id\":");
        (void)writer_int(&writer, relation->id);
        (void)writer_cstr(&writer, ",\"bbox\":[");
        (void)writer_degrees(&writer, rel_min_lon);
        (void)writer_char(&writer, ',');
        (void)writer_degrees(&writer, rel_min_lat);
        (void)writer_char(&writer, ',');
        (void)writer_degrees(&writer, rel_max_lon);
        (void)writer_char(&writer, ',');
        (void)writer_degrees(&writer, rel_max_lat);
        (void)writer_cstr(&writer, "],\"properties\":{\"postcode\":");
        (void)writer_json_string(&writer, relation->postcode, (unsigned int)rt_strlen(relation->postcode));
        (void)writer_cstr(&writer, ",\"postcode_source\":\"");
        (void)writer_cstr(&writer, source_name(relation->postcode_source));
        (void)writer_cstr(&writer, "\",\"relation_id\":");
        (void)writer_int(&writer, relation->id);
        (void)writer_cstr(&writer, ",\"name\":");
        if (relation->name_length != 0U) {
            (void)writer_json_string(&writer, context.names.items + relation->name_offset, relation->name_length);
        } else {
            (void)writer_cstr(&writer, "null");
        }
        (void)writer_cstr(&writer, ",\"outer_rings\":");
        (void)writer_uint(&writer, outer_count);
        (void)writer_cstr(&writer, ",\"inner_rings\":");
        (void)writer_uint(&writer, inner_count);
        (void)writer_cstr(&writer, ",\"coordinate_count\":");
        (void)writer_uint(&writer, coordinate_count);
        (void)writer_cstr(&writer, ",\"member_ways\":");
        (void)writer_uint(&writer, relation->member_way_count);
        (void)writer_cstr(&writer, ",\"member_nodes\":");
        (void)writer_uint(&writer, relation->member_node_count);
        (void)writer_cstr(&writer, ",\"member_relations\":");
        (void)writer_uint(&writer, relation->member_relation_count);
        (void)writer_cstr(&writer, ",\"has_postal_code_de_tag\":");
        (void)writer_cstr(&writer, relation->has_de_tag ? "true" : "false");
        (void)writer_cstr(&writer, "},\"geometry\":{\"type\":");

        if (outer_count == 1ULL) {
            (void)writer_cstr(&writer, "\"Polygon\",\"coordinates\":[");
            for (ring_index = base; ring_index < base + total; ++ring_index) {
                const OsmPostalRing *ring = &geometry.rings[ring_index];
                if (ring->role != OSM_POSTAL_ROLE_OUTER) continue;
                (void)write_ring(&writer, &geometry, ring);
            }
            for (ring_index = base; ring_index < base + total; ++ring_index) {
                const OsmPostalRing *ring = &geometry.rings[ring_index];
                if (ring->role != OSM_POSTAL_ROLE_INNER) continue;
                (void)writer_char(&writer, ',');
                (void)write_ring(&writer, &geometry, ring);
            }
            (void)writer_char(&writer, ']');
        } else {
            unsigned long long outer_index;
            int first_polygon = 1;
            (void)writer_cstr(&writer, "\"MultiPolygon\",\"coordinates\":[");
            for (outer_index = base; outer_index < base + total; ++outer_index) {
                const OsmPostalRing *outer = &geometry.rings[outer_index];
                if (outer->role != OSM_POSTAL_ROLE_OUTER) continue;
                if (!first_polygon) (void)writer_char(&writer, ',');
                first_polygon = 0;
                (void)writer_char(&writer, '[');
                (void)write_ring(&writer, &geometry, outer);
                for (ring_index = base; ring_index < base + total; ++ring_index) {
                    const OsmPostalRing *inner = &geometry.rings[ring_index];
                    if (inner->role != OSM_POSTAL_ROLE_INNER) continue;
                    if (inner->outer_index != (int)(outer_index - base)) continue;
                    (void)writer_char(&writer, ',');
                    (void)write_ring(&writer, &geometry, inner);
                }
                (void)writer_char(&writer, ']');
            }
            (void)writer_char(&writer, ']');
        }
        (void)writer_cstr(&writer, "}}");
        context.features_written += 1ULL;
        if (writer.failed) break;
    }
    (void)writer_cstr(&writer, "\n]}\n");
    if (writer_flush(&writer) != 0 || writer.failed) {
        rt_write_cstr(2, "osm-postal: could not write output\n");
        return 1;
    }
    if (!output_stdout && platform_close(writer.fd) != 0) {
        rt_write_cstr(2, "osm-postal: could not close output file\n");
        return 1;
    }
    rt_free(writer.data);

    if (report_path != 0 && write_report(report_path, &context, &geometry, ring_start, ring_count, report_order, report_count) != 0) {
        return 1;
    }

    stats_fd = output_stdout ? 2 : 1;
    rt_write_cstr(stats_fd, "candidate_relations: ");
    rt_write_uint(stats_fd, context.relations_seen);
    rt_write_char(stats_fd, '\n');
    rt_write_cstr(stats_fd, "relations_kept: ");
    rt_write_uint(stats_fd, context.relations_kept);
    rt_write_char(stats_fd, '\n');
    rt_write_cstr(stats_fd, "relations_excluded: ");
    rt_write_uint(stats_fd, context.relations_excluded);
    rt_write_char(stats_fd, '\n');
    rt_write_cstr(stats_fd, "relations_incomplete: ");
    rt_write_uint(stats_fd, context.relations_incomplete);
    rt_write_char(stats_fd, '\n');
    rt_write_cstr(stats_fd, "relations_broken: ");
    rt_write_uint(stats_fd, context.relations_broken);
    rt_write_char(stats_fd, '\n');
    rt_write_cstr(stats_fd, "features_written: ");
    rt_write_uint(stats_fd, context.features_written);
    rt_write_char(stats_fd, '\n');
    rt_write_cstr(stats_fd, "outer_rings: ");
    rt_write_uint(stats_fd, context.outer_rings);
    rt_write_char(stats_fd, '\n');
    rt_write_cstr(stats_fd, "inner_rings: ");
    rt_write_uint(stats_fd, context.inner_rings);
    rt_write_char(stats_fd, '\n');
    rt_write_cstr(stats_fd, "member_ways_resolved: ");
    rt_write_uint(stats_fd, context.way_count);
    rt_write_char(stats_fd, '\n');
    rt_write_cstr(stats_fd, "member_nodes_resolved: ");
    rt_write_uint(stats_fd, context.node_count);
    rt_write_char(stats_fd, '\n');

    if (!quiet) {
        for (index = 0ULL; index < report_count; ++index) {
            const OsmPostalRelation *relation = &context.relations.items[report_order[index].relation_index];
            if (relation->status == OSM_POSTAL_STATUS_OK) continue;
            if (status_is_geometry_failure(relation->status)) rt_write_cstr(2, "osm-postal: broken relation ");
            else if (status_is_incomplete(relation->status)) rt_write_cstr(2, "osm-postal: incomplete relation ");
            else rt_write_cstr(2, "osm-postal: excluded relation ");
            rt_write_int(2, relation->id);
            rt_write_cstr(2, " (");
            rt_write_cstr(2, relation->postcode[0] != '\0' ? relation->postcode : "-");
            rt_write_cstr(2, "): ");
            rt_write_cstr(2, status_name(relation->status));
            rt_write_char(2, '\n');
        }
    }
    if (strict && context.relations_broken != 0ULL) return 2;
    if (strict_incomplete && context.relations_incomplete != 0ULL) return 2;
    return 0;
}
