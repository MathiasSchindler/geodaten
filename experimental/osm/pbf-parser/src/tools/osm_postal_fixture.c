/*
 * osm-postal-fixture
 *
 * Writes a tiny synthetic .osm.pbf file that exercises the osm-postal
 * extractor. The blobs are written uncompressed (Blob.raw), which the shared
 * PBF reader accepts, so the generator needs no deflate encoder.
 *
 * The fixture contains eight boundary=postal_code relations:
 *
 *   100  10115  postal_code:DE, outer ring built from four fragments that are
 *               listed out of order and one of which is reversed, plus an
 *               inner ring built from two fragments (one reversed)
 *   200  04109  postal_code only, single already-closed way
 *   300  75001  a French postal code area outside the German acceptance box
 *   400  20095  ref only, no postal_code tag, single closed way
 *   500  50667  outer ring with a gap: the ring cannot be closed
 *   600  1234   invalid four-digit code
 *   700  67000  inside the German box but tagged ref:INSEE, so foreign
 *   800  90402  references a way that is not in the file, as happens at the
 *               clipped border of a national extract
 *
 * No libc, no external dependencies.
 */

#include "platform.h"
#include "runtime.h"

#define FIXTURE_CAPACITY (256U * 1024U)

typedef struct {
    unsigned char *data;
    unsigned long long used;
    unsigned long long capacity;
    int overflow;
} FixtureBuffer;

static void buffer_byte(FixtureBuffer *buffer, unsigned char value) {
    if (buffer->used >= buffer->capacity) {
        buffer->overflow = 1;
        return;
    }
    buffer->data[buffer->used] = value;
    buffer->used += 1ULL;
}

static void buffer_bytes(FixtureBuffer *buffer, const unsigned char *data, unsigned long long size) {
    unsigned long long index;

    for (index = 0ULL; index < size; ++index) buffer_byte(buffer, data[index]);
}

static void buffer_varint(FixtureBuffer *buffer, unsigned long long value) {
    do {
        unsigned char byte = (unsigned char)(value & 0x7FULL);
        value >>= 7U;
        if (value != 0ULL) byte |= 0x80U;
        buffer_byte(buffer, byte);
    } while (value != 0ULL);
}

static unsigned long long zigzag(long long value) {
    return ((unsigned long long)value << 1U) ^ (unsigned long long)(value >> 63);
}

static void buffer_key(FixtureBuffer *buffer, unsigned int field, unsigned int wire_type) {
    buffer_varint(buffer, ((unsigned long long)field << 3U) | (unsigned long long)wire_type);
}

static void buffer_varint_field(FixtureBuffer *buffer, unsigned int field, unsigned long long value) {
    buffer_key(buffer, field, 0U);
    buffer_varint(buffer, value);
}

static void buffer_bytes_field(FixtureBuffer *buffer, unsigned int field, const unsigned char *data, unsigned long long size) {
    buffer_key(buffer, field, 2U);
    buffer_varint(buffer, size);
    buffer_bytes(buffer, data, size);
}

static void buffer_string_field(FixtureBuffer *buffer, unsigned int field, const char *text) {
    buffer_bytes_field(buffer, field, (const unsigned char *)text, (unsigned long long)rt_strlen(text));
}

/* ------------------------------------------------------------------ */
/* string table                                                        */
/* ------------------------------------------------------------------ */

#define FIXTURE_MAX_STRINGS 64U

typedef struct {
    const char *items[FIXTURE_MAX_STRINGS];
    unsigned int count;
} FixtureStrings;

static unsigned int strings_intern(FixtureStrings *strings, const char *text) {
    unsigned int index;

    for (index = 0U; index < strings->count; ++index) {
        if (rt_strcmp(strings->items[index], text) == 0) return index;
    }
    strings->items[strings->count] = text;
    strings->count += 1U;
    return strings->count - 1U;
}

/* ------------------------------------------------------------------ */
/* fixture data                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    long long id;
    long long lat_e7;
    long long lon_e7;
} FixtureNode;

typedef struct {
    long long id;
    const long long *refs;
    unsigned int ref_count;
} FixtureWay;

typedef struct {
    long long id;
    const char *role;
} FixtureMember;

typedef struct {
    long long id;
    const char *keys[8];
    const char *values[8];
    unsigned int tag_count;
    const FixtureMember *members;
    unsigned int member_count;
} FixtureRelation;

/*
 * Relation 100: square outer ring around (10.00,52.00)-(10.03,52.03) split
 * into three fragments, with an inner square hole made from two fragments.
 */
static const long long r100_outer_a[] = {1, 2};          /* SW -> SE */
static const long long r100_outer_b[] = {4, 3};          /* NW <- NE, reversed */
static const long long r100_outer_c[] = {2, 3};          /* SE -> NE */
static const long long r100_outer_d[] = {4, 1};          /* NW -> SW */
static const long long r100_inner_a[] = {11, 12, 13};    /* three corners */
static const long long r100_inner_b[] = {11, 14, 13};    /* closing side, reversed direction */

static const long long r200_way[] = {21, 22, 23, 24, 21};
static const long long r300_way[] = {31, 32, 33, 34, 31};
static const long long r400_way[] = {41, 42, 43, 44, 41};
static const long long r500_way_a[] = {51, 52};
static const long long r500_way_b[] = {53, 54};
static const long long r600_way[] = {61, 62, 63, 64, 61};
static const long long r700_way[] = {71, 72, 73, 74, 71};

static const FixtureNode fixture_nodes[] = {
    {1, 520000000LL, 100000000LL},
    {2, 520000000LL, 100300000LL},
    {3, 520300000LL, 100300000LL},
    {4, 520300000LL, 100000000LL},
    {11, 520100000LL, 100100000LL},
    {12, 520100000LL, 100200000LL},
    {13, 520200000LL, 100200000LL},
    {14, 520200000LL, 100100000LL},
    {21, 512000000LL, 123000000LL},
    {22, 512000000LL, 123500000LL},
    {23, 512500000LL, 123500000LL},
    {24, 512500000LL, 123000000LL},
    {31, 488000000LL, 23000000LL},
    {32, 488000000LL, 23500000LL},
    {33, 488500000LL, 23500000LL},
    {34, 488500000LL, 23000000LL},
    {41, 535500000LL, 99900000LL},
    {42, 535500000LL, 100400000LL},
    {43, 536000000LL, 100400000LL},
    {44, 536000000LL, 99900000LL},
    {51, 508000000LL, 69000000LL},
    {52, 508000000LL, 69500000LL},
    {53, 508500000LL, 69500000LL},
    {54, 508500000LL, 69000000LL},
    {61, 480000000LL, 116000000LL},
    {62, 480000000LL, 116500000LL},
    {63, 480500000LL, 116500000LL},
    {64, 480500000LL, 116000000LL},
    {71, 490000000LL, 76000000LL},
    {72, 490000000LL, 76500000LL},
    {73, 490500000LL, 76500000LL},
    {74, 490500000LL, 76000000LL}
};

static const FixtureWay fixture_ways[] = {
    {1001, r100_outer_a, 2U},
    {1002, r100_outer_b, 2U},
    {1003, r100_outer_c, 2U},
    {1004, r100_outer_d, 2U},
    {1011, r100_inner_a, 3U},
    {1012, r100_inner_b, 3U},
    {2001, r200_way, 5U},
    {3001, r300_way, 5U},
    {4001, r400_way, 5U},
    {5001, r500_way_a, 2U},
    {5002, r500_way_b, 2U},
    {6001, r600_way, 5U},
    {7001, r700_way, 5U}
};

/* Members are deliberately listed out of order for relation 100. */
static const FixtureMember r100_members[] = {
    {1011, "inner"},
    {1003, "outer"},
    {1001, ""},
    {1012, "inner"},
    {1004, "outer"},
    {1002, "outer"}
};
static const FixtureMember r200_members[] = {{2001, "outer"}};
static const FixtureMember r300_members[] = {{3001, "outer"}};
static const FixtureMember r400_members[] = {{4001, "outer"}};
static const FixtureMember r500_members[] = {{5001, "outer"}, {5002, "outer"}};
static const FixtureMember r600_members[] = {{6001, "outer"}};
static const FixtureMember r700_members[] = {{7001, "outer"}};
/* way 8001 is deliberately absent from fixture_ways: extract-clipping case */
static const FixtureMember r800_members[] = {{8001, "outer"}};

static const FixtureRelation fixture_relations[] = {
    {100,
     {"type", "boundary", "postal_code:DE", "name"},
     {"boundary", "postal_code", "10115", "10115 Berlin Mitte"},
     4U, r100_members, 6U},
    {200,
     {"type", "boundary", "postal_code", "name"},
     {"boundary", "postal_code", "04109", "04109 Leipzig"},
     4U, r200_members, 1U},
    {300,
     {"type", "boundary", "postal_code", "name"},
     {"boundary", "postal_code", "75001", "75001 Paris"},
     4U, r300_members, 1U},
    {400,
     {"type", "boundary", "ref"},
     {"boundary", "postal_code", "20095"},
     3U, r400_members, 1U},
    {500,
     {"type", "boundary", "postal_code"},
     {"boundary", "postal_code", "50667"},
     3U, r500_members, 2U},
    {600,
     {"type", "boundary", "postal_code"},
     {"boundary", "postal_code", "1234"},
     3U, r600_members, 1U},
    {700,
     {"type", "boundary", "postal_code", "ref:INSEE"},
     {"boundary", "postal_code", "67000", "67482"},
     4U, r700_members, 1U},
    {800,
     {"type", "boundary", "postal_code"},
     {"boundary", "postal_code", "90402"},
     3U, r800_members, 1U}
};

/* ------------------------------------------------------------------ */
/* block construction                                                  */
/* ------------------------------------------------------------------ */

static void build_string_table(FixtureStrings *strings) {
    unsigned int index;

    strings->count = 0U;
    (void)strings_intern(strings, "");
    for (index = 0U; index < sizeof(fixture_relations) / sizeof(fixture_relations[0]); ++index) {
        unsigned int tag;
        unsigned int member;
        for (tag = 0U; tag < fixture_relations[index].tag_count; ++tag) {
            (void)strings_intern(strings, fixture_relations[index].keys[tag]);
            (void)strings_intern(strings, fixture_relations[index].values[tag]);
        }
        for (member = 0U; member < fixture_relations[index].member_count; ++member) {
            (void)strings_intern(strings, fixture_relations[index].members[member].role);
        }
    }
}

static void build_primitive_block(FixtureBuffer *out, unsigned char *scratch, unsigned long long scratch_size) {
    FixtureStrings strings;
    FixtureBuffer inner;
    FixtureBuffer group;
    FixtureBuffer packed;
    unsigned int index;

    build_string_table(&strings);

    /* StringTable */
    rt_memset(&inner, 0, sizeof(inner));
    inner.data = scratch;
    inner.capacity = scratch_size / 4ULL;
    for (index = 0U; index < strings.count; ++index) {
        buffer_string_field(&inner, 1U, strings.items[index]);
    }
    buffer_bytes_field(out, 1U, inner.data, inner.used);

    /* PrimitiveGroup with plain nodes */
    rt_memset(&group, 0, sizeof(group));
    group.data = scratch + scratch_size / 4ULL;
    group.capacity = scratch_size / 4ULL;
    for (index = 0U; index < sizeof(fixture_nodes) / sizeof(fixture_nodes[0]); ++index) {
        FixtureBuffer node;
        rt_memset(&node, 0, sizeof(node));
        node.data = scratch + (scratch_size / 4ULL) * 2ULL;
        node.capacity = scratch_size / 8ULL;
        buffer_varint_field(&node, 1U, zigzag(fixture_nodes[index].id));
        /* granularity is 100 nanodegrees, so 1e-7 degrees map 1:1 */
        buffer_varint_field(&node, 8U, zigzag(fixture_nodes[index].lat_e7));
        buffer_varint_field(&node, 9U, zigzag(fixture_nodes[index].lon_e7));
        buffer_bytes_field(&group, 1U, node.data, node.used);
        if (node.overflow) out->overflow = 1;
    }
    buffer_bytes_field(out, 2U, group.data, group.used);

    /* PrimitiveGroup with ways */
    rt_memset(&group, 0, sizeof(group));
    group.data = scratch + scratch_size / 4ULL;
    group.capacity = scratch_size / 4ULL;
    for (index = 0U; index < sizeof(fixture_ways) / sizeof(fixture_ways[0]); ++index) {
        FixtureBuffer way;
        unsigned int ref;
        long long previous = 0;

        rt_memset(&way, 0, sizeof(way));
        way.data = scratch + (scratch_size / 4ULL) * 2ULL;
        way.capacity = scratch_size / 8ULL;
        buffer_varint_field(&way, 1U, (unsigned long long)fixture_ways[index].id);
        rt_memset(&packed, 0, sizeof(packed));
        packed.data = scratch + (scratch_size / 4ULL) * 3ULL;
        packed.capacity = scratch_size / 8ULL;
        for (ref = 0U; ref < fixture_ways[index].ref_count; ++ref) {
            long long value = fixture_ways[index].refs[ref];
            buffer_varint(&packed, zigzag(value - previous));
            previous = value;
        }
        buffer_bytes_field(&way, 8U, packed.data, packed.used);
        buffer_bytes_field(&group, 3U, way.data, way.used);
        if (way.overflow || packed.overflow) out->overflow = 1;
    }
    buffer_bytes_field(out, 2U, group.data, group.used);

    /* PrimitiveGroup with relations */
    rt_memset(&group, 0, sizeof(group));
    group.data = scratch + scratch_size / 4ULL;
    group.capacity = scratch_size / 4ULL;
    for (index = 0U; index < sizeof(fixture_relations) / sizeof(fixture_relations[0]); ++index) {
        const FixtureRelation *relation = &fixture_relations[index];
        FixtureBuffer record;
        unsigned int tag;
        unsigned int member;
        long long previous = 0;

        rt_memset(&record, 0, sizeof(record));
        record.data = scratch + (scratch_size / 4ULL) * 2ULL;
        record.capacity = scratch_size / 8ULL;
        buffer_varint_field(&record, 1U, (unsigned long long)relation->id);

        rt_memset(&packed, 0, sizeof(packed));
        packed.data = scratch + (scratch_size / 4ULL) * 3ULL;
        packed.capacity = scratch_size / 32ULL;
        for (tag = 0U; tag < relation->tag_count; ++tag) {
            buffer_varint(&packed, strings_intern(&strings, relation->keys[tag]));
        }
        buffer_bytes_field(&record, 2U, packed.data, packed.used);

        rt_memset(&packed, 0, sizeof(packed));
        packed.data = scratch + (scratch_size / 4ULL) * 3ULL + scratch_size / 32ULL;
        packed.capacity = scratch_size / 32ULL;
        for (tag = 0U; tag < relation->tag_count; ++tag) {
            buffer_varint(&packed, strings_intern(&strings, relation->values[tag]));
        }
        buffer_bytes_field(&record, 3U, packed.data, packed.used);

        rt_memset(&packed, 0, sizeof(packed));
        packed.data = scratch + (scratch_size / 4ULL) * 3ULL + (scratch_size / 32ULL) * 2ULL;
        packed.capacity = scratch_size / 32ULL;
        for (member = 0U; member < relation->member_count; ++member) {
            buffer_varint(&packed, strings_intern(&strings, relation->members[member].role));
        }
        buffer_bytes_field(&record, 8U, packed.data, packed.used);

        rt_memset(&packed, 0, sizeof(packed));
        packed.data = scratch + (scratch_size / 4ULL) * 3ULL + (scratch_size / 32ULL) * 3ULL;
        packed.capacity = scratch_size / 32ULL;
        for (member = 0U; member < relation->member_count; ++member) {
            long long value = relation->members[member].id;
            buffer_varint(&packed, zigzag(value - previous));
            previous = value;
        }
        buffer_bytes_field(&record, 9U, packed.data, packed.used);

        rt_memset(&packed, 0, sizeof(packed));
        packed.data = scratch + (scratch_size / 4ULL) * 3ULL + (scratch_size / 32ULL) * 4ULL;
        packed.capacity = scratch_size / 32ULL;
        for (member = 0U; member < relation->member_count; ++member) {
            buffer_varint(&packed, 1ULL); /* WAY */
        }
        buffer_bytes_field(&record, 10U, packed.data, packed.used);

        buffer_bytes_field(&group, 4U, record.data, record.used);
        if (record.overflow || packed.overflow) out->overflow = 1;
    }
    buffer_bytes_field(out, 2U, group.data, group.used);

    buffer_varint_field(out, 17U, 100ULL); /* granularity */
    buffer_varint_field(out, 19U, 0ULL);   /* lat_offset */
    buffer_varint_field(out, 20U, 0ULL);   /* lon_offset */
}

static void build_header_block(FixtureBuffer *out) {
    buffer_string_field(out, 4U, "OsmSchema-V0.6");
    buffer_string_field(out, 16U, "osm-postal-fixture");
    buffer_string_field(out, 17U, "pbf-parser synthetic fixture");
}

static int write_fileblock(int fd, const char *type, const unsigned char *payload, unsigned long long payload_size,
                           unsigned char *scratch, unsigned long long scratch_size) {
    FixtureBuffer blob;
    FixtureBuffer header;
    unsigned char length[4];

    rt_memset(&blob, 0, sizeof(blob));
    blob.data = scratch;
    blob.capacity = scratch_size / 2ULL;
    buffer_bytes_field(&blob, 1U, payload, payload_size); /* Blob.raw */
    if (blob.overflow) return -1;

    rt_memset(&header, 0, sizeof(header));
    header.data = scratch + scratch_size / 2ULL;
    header.capacity = scratch_size / 2ULL;
    buffer_string_field(&header, 1U, type);
    buffer_varint_field(&header, 3U, blob.used);
    if (header.overflow) return -1;

    length[0] = (unsigned char)((header.used >> 24U) & 0xFFULL);
    length[1] = (unsigned char)((header.used >> 16U) & 0xFFULL);
    length[2] = (unsigned char)((header.used >> 8U) & 0xFFULL);
    length[3] = (unsigned char)(header.used & 0xFFULL);
    if (rt_write_all(fd, length, sizeof(length)) != 0) return -1;
    if (rt_write_all(fd, header.data, (size_t)header.used) != 0) return -1;
    if (rt_write_all(fd, blob.data, (size_t)blob.used) != 0) return -1;
    return 0;
}

int main(int argc, char **argv) {
    FixtureBuffer block;
    unsigned char *payload;
    unsigned char *scratch;
    int fd;

    if (argc != 2) {
        rt_write_cstr(2, "Usage: ");
        rt_write_cstr(2, argc > 0 ? argv[0] : "osm-postal-fixture");
        rt_write_cstr(2, " OUT.osm.pbf\n");
        return 1;
    }
    payload = (unsigned char *)rt_malloc(FIXTURE_CAPACITY);
    scratch = (unsigned char *)rt_malloc(FIXTURE_CAPACITY);
    if (payload == 0 || scratch == 0) {
        rt_write_cstr(2, "osm-postal-fixture: out of memory\n");
        return 1;
    }
    fd = platform_open_write(argv[1], 0644U);
    if (fd < 0) {
        rt_write_cstr(2, "osm-postal-fixture: could not open output file\n");
        return 1;
    }

    rt_memset(&block, 0, sizeof(block));
    block.data = payload;
    block.capacity = FIXTURE_CAPACITY;
    build_header_block(&block);
    if (block.overflow || write_fileblock(fd, "OSMHeader", block.data, block.used, scratch, FIXTURE_CAPACITY) != 0) {
        rt_write_cstr(2, "osm-postal-fixture: could not write OSMHeader\n");
        return 1;
    }

    rt_memset(&block, 0, sizeof(block));
    block.data = payload;
    block.capacity = FIXTURE_CAPACITY;
    build_primitive_block(&block, scratch, FIXTURE_CAPACITY);
    if (block.overflow || write_fileblock(fd, "OSMData", block.data, block.used, scratch, FIXTURE_CAPACITY) != 0) {
        rt_write_cstr(2, "osm-postal-fixture: could not write OSMData\n");
        return 1;
    }

    if (platform_close(fd) != 0) {
        rt_write_cstr(2, "osm-postal-fixture: could not close output file\n");
        return 1;
    }
    rt_write_cstr(1, "wrote ");
    rt_write_cstr(1, argv[1]);
    rt_write_char(1, '\n');
    return 0;
}
