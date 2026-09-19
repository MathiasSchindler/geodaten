#include "osm_index.h"
#include "pbf.h"
#include "platform.h"
#include "runtime.h"

#define OSM_BUILDINGS_BUFFER_SIZE 131072U

typedef struct {
    PbfText country;
    PbfText state;
    PbfText city;
    PbfText suburb;
    PbfText street;
    PbfText housenumber;
    PbfText postcode;
    PbfText place;
    PbfText unit;
    PbfText floor;
    PbfText door;
    PbfText entrance;
    int has_country;
    int has_state;
    int has_city;
    int has_suburb;
    int has_street;
    int has_housenumber;
    int has_postcode;
    int has_place;
    int has_unit;
    int has_floor;
    int has_door;
    int has_entrance;
} OsmBuildingAddressFields;

typedef struct {
    PbfText building;
    PbfText building_part;
    PbfText levels;
    PbfText roof_levels;
    PbfText min_level;
    PbfText flats;
    PbfText apartments;
    PbfText use;
    PbfText start_date;
    PbfText name;
    PbfText amenity;
    PbfText shop;
    PbfText office;
    PbfText tourism;
    PbfText leisure;
    PbfText craft;
    PbfText healthcare;
    PbfText public_transport;
    int has_building;
    int has_building_part;
    int has_levels;
    int has_roof_levels;
    int has_min_level;
    int has_flats;
    int has_apartments;
    int has_use;
    int has_start_date;
    int has_name;
    int has_amenity;
    int has_shop;
    int has_office;
    int has_tourism;
    int has_leisure;
    int has_craft;
    int has_healthcare;
    int has_public_transport;
} OsmBuildingFields;

typedef struct {
    int has_coord;
    long long lat_nano;
    long long lon_nano;
    long long min_lat_nano;
    long long min_lon_nano;
    long long max_lat_nano;
    long long max_lon_nano;
    unsigned long long ref_count;
    unsigned long long found_ref_count;
    unsigned long long missing_ref_count;
} OsmBuildingGeometry;

typedef struct {
    int fd;
    unsigned char *data;
    size_t capacity;
    size_t used;
} OsmBuildingWriter;

typedef struct {
    OsmBuildingWriter writer;
    int write_header;
    int has_bbox;
    long long min_lon_nano;
    long long min_lat_nano;
    long long max_lon_nano;
    long long max_lat_nano;
    int residential_only;
    const char *node_index_path;
    const char *way_index_path;
    const char *spatial_index_path;
    OsmNodeIndex node_index;
    OsmWayIndex way_index;
    OsmSpatialIndex spatial_index;
    int node_index_open;
    int way_index_open;
    int spatial_index_open;
    unsigned long long limit;
    unsigned long long written;
    unsigned long long node_rows;
    unsigned long long way_rows;
    unsigned long long relation_rows;
    unsigned long long partial_geometry_rows;
    int failed;
    char error[OSM_INDEX_ERROR_CAPACITY];
} OsmBuildingsContext;

static void write_usage(const char *program) {
    rt_write_cstr(2, "Usage: ");
    rt_write_cstr(2, program);
    rt_write_cstr(2, " FILE.osm.pbf OUT.tsv [--bbox MINLON,MINLAT,MAXLON,MAXLAT] [--node-index FILE] [--way-index FILE] [--spatial-index FILE] [--residential-only] [--no-header] [--limit N]\n");
}

static int text_equals_cstr(PbfText text, const char *value) {
    size_t value_size = rt_strlen(value);
    return text.size == value_size && memcmp(text.data, value, value_size) == 0;
}

static int text_is_empty(PbfText text) {
    return text.size == 0U;
}

static int parse_uint_arg(const char *text, unsigned long long *value_out) {
    unsigned long long value;
    if (rt_parse_uint(text, &value) != 0) return -1;
    *value_out = value;
    return 0;
}

static int parse_coord_part(const char *text, size_t size, long long *value_out) {
    size_t index = 0U;
    unsigned long long whole = 0ULL;
    unsigned long long fraction = 0ULL;
    unsigned int fraction_digits = 0U;
    int negative = 0;

    if (size == 0U) return -1;
    if (text[index] == '-') {
        negative = 1;
        index += 1U;
    }
    if (index >= size || text[index] < '0' || text[index] > '9') return -1;
    while (index < size && text[index] >= '0' && text[index] <= '9') {
        whole = whole * 10ULL + (unsigned long long)(text[index] - '0');
        if (whole > 180ULL) return -1;
        index += 1U;
    }
    if (index < size && text[index] == '.') {
        index += 1U;
        while (index < size && text[index] >= '0' && text[index] <= '9') {
            if (fraction_digits >= 9U) return -1;
            fraction = fraction * 10ULL + (unsigned long long)(text[index] - '0');
            fraction_digits += 1U;
            index += 1U;
        }
    }
    if (index != size) return -1;
    while (fraction_digits < 9U) {
        fraction *= 10ULL;
        fraction_digits += 1U;
    }
    *value_out = (long long)(whole * 1000000000ULL + fraction);
    if (negative) *value_out = -*value_out;
    return 0;
}

static int parse_bbox_arg(const char *text, OsmBuildingsContext *context) {
    const char *parts[4];
    size_t sizes[4];
    size_t start = 0U;
    size_t index = 0U;
    unsigned int part = 0U;
    long long min_lon;
    long long min_lat;
    long long max_lon;
    long long max_lat;

    for (;;) {
        if (text[index] == ',' || text[index] == '\0') {
            if (part >= 4U) return -1;
            parts[part] = text + start;
            sizes[part] = index - start;
            part += 1U;
            if (text[index] == '\0') break;
            start = index + 1U;
        }
        index += 1U;
    }
    if (part != 4U) return -1;
    if (parse_coord_part(parts[0], sizes[0], &min_lon) != 0 ||
        parse_coord_part(parts[1], sizes[1], &min_lat) != 0 ||
        parse_coord_part(parts[2], sizes[2], &max_lon) != 0 ||
        parse_coord_part(parts[3], sizes[3], &max_lat) != 0) {
        return -1;
    }
    if (min_lon > max_lon || min_lat > max_lat) return -1;
    context->has_bbox = 1;
    context->min_lon_nano = min_lon;
    context->min_lat_nano = min_lat;
    context->max_lon_nano = max_lon;
    context->max_lat_nano = max_lat;
    return 0;
}

static void find_address_fields(const PbfTag *tags, unsigned int tag_count, OsmBuildingAddressFields *fields) {
    unsigned int index;

    rt_memset(fields, 0, sizeof(*fields));
    for (index = 0U; index < tag_count; ++index) {
        if (!fields->has_country && text_equals_cstr(tags[index].key, "addr:country")) { fields->country = tags[index].value; fields->has_country = 1; }
        else if (!fields->has_state && text_equals_cstr(tags[index].key, "addr:state")) { fields->state = tags[index].value; fields->has_state = 1; }
        else if (!fields->has_city && text_equals_cstr(tags[index].key, "addr:city")) { fields->city = tags[index].value; fields->has_city = 1; }
        else if (!fields->has_suburb && text_equals_cstr(tags[index].key, "addr:suburb")) { fields->suburb = tags[index].value; fields->has_suburb = 1; }
        else if (!fields->has_street && text_equals_cstr(tags[index].key, "addr:street")) { fields->street = tags[index].value; fields->has_street = 1; }
        else if (!fields->has_housenumber && text_equals_cstr(tags[index].key, "addr:housenumber")) { fields->housenumber = tags[index].value; fields->has_housenumber = 1; }
        else if (!fields->has_postcode && text_equals_cstr(tags[index].key, "addr:postcode")) { fields->postcode = tags[index].value; fields->has_postcode = 1; }
        else if (!fields->has_place && text_equals_cstr(tags[index].key, "addr:place")) { fields->place = tags[index].value; fields->has_place = 1; }
        else if (!fields->has_unit && text_equals_cstr(tags[index].key, "addr:unit")) { fields->unit = tags[index].value; fields->has_unit = 1; }
        else if (!fields->has_floor && text_equals_cstr(tags[index].key, "addr:floor")) { fields->floor = tags[index].value; fields->has_floor = 1; }
        else if (!fields->has_door && text_equals_cstr(tags[index].key, "addr:door")) { fields->door = tags[index].value; fields->has_door = 1; }
        else if (!fields->has_entrance && text_equals_cstr(tags[index].key, "entrance")) { fields->entrance = tags[index].value; fields->has_entrance = 1; }
    }
}

static void find_building_fields(const PbfTag *tags, unsigned int tag_count, OsmBuildingFields *fields) {
    unsigned int index;

    rt_memset(fields, 0, sizeof(*fields));
    for (index = 0U; index < tag_count; ++index) {
        if (!fields->has_building && text_equals_cstr(tags[index].key, "building")) { fields->building = tags[index].value; fields->has_building = 1; }
        else if (!fields->has_building_part && text_equals_cstr(tags[index].key, "building:part")) { fields->building_part = tags[index].value; fields->has_building_part = 1; }
        else if (!fields->has_levels && text_equals_cstr(tags[index].key, "building:levels")) { fields->levels = tags[index].value; fields->has_levels = 1; }
        else if (!fields->has_roof_levels && text_equals_cstr(tags[index].key, "roof:levels")) { fields->roof_levels = tags[index].value; fields->has_roof_levels = 1; }
        else if (!fields->has_min_level && text_equals_cstr(tags[index].key, "building:min_level")) { fields->min_level = tags[index].value; fields->has_min_level = 1; }
        else if (!fields->has_flats && text_equals_cstr(tags[index].key, "building:flats")) { fields->flats = tags[index].value; fields->has_flats = 1; }
        else if (!fields->has_apartments && text_equals_cstr(tags[index].key, "building:apartments")) { fields->apartments = tags[index].value; fields->has_apartments = 1; }
        else if (!fields->has_use && text_equals_cstr(tags[index].key, "building:use")) { fields->use = tags[index].value; fields->has_use = 1; }
        else if (!fields->has_start_date && (text_equals_cstr(tags[index].key, "start_date") || text_equals_cstr(tags[index].key, "building:year"))) { fields->start_date = tags[index].value; fields->has_start_date = 1; }
        else if (!fields->has_name && text_equals_cstr(tags[index].key, "name")) { fields->name = tags[index].value; fields->has_name = 1; }
        else if (!fields->has_amenity && text_equals_cstr(tags[index].key, "amenity")) { fields->amenity = tags[index].value; fields->has_amenity = 1; }
        else if (!fields->has_shop && text_equals_cstr(tags[index].key, "shop")) { fields->shop = tags[index].value; fields->has_shop = 1; }
        else if (!fields->has_office && text_equals_cstr(tags[index].key, "office")) { fields->office = tags[index].value; fields->has_office = 1; }
        else if (!fields->has_tourism && text_equals_cstr(tags[index].key, "tourism")) { fields->tourism = tags[index].value; fields->has_tourism = 1; }
        else if (!fields->has_leisure && text_equals_cstr(tags[index].key, "leisure")) { fields->leisure = tags[index].value; fields->has_leisure = 1; }
        else if (!fields->has_craft && text_equals_cstr(tags[index].key, "craft")) { fields->craft = tags[index].value; fields->has_craft = 1; }
        else if (!fields->has_healthcare && text_equals_cstr(tags[index].key, "healthcare")) { fields->healthcare = tags[index].value; fields->has_healthcare = 1; }
        else if (!fields->has_public_transport && text_equals_cstr(tags[index].key, "public_transport")) { fields->public_transport = tags[index].value; fields->has_public_transport = 1; }
    }
}

static int has_address(const OsmBuildingAddressFields *address) {
    return address->has_country || address->has_state || address->has_city || address->has_suburb ||
           address->has_street || address->has_housenumber || address->has_postcode || address->has_place ||
           address->has_unit || address->has_floor || address->has_door || address->has_entrance;
}

static int has_building(const OsmBuildingFields *building) {
    if (building->has_building && !text_equals_cstr(building->building, "no")) return 1;
    if (building->has_building_part && !text_equals_cstr(building->building_part, "no")) return 1;
    return 0;
}

static int is_residential_building_value(PbfText value) {
    if (text_is_empty(value)) return 0;
    return text_equals_cstr(value, "yes") ||
           text_equals_cstr(value, "residential") ||
           text_equals_cstr(value, "apartments") ||
           text_equals_cstr(value, "house") ||
           text_equals_cstr(value, "detached") ||
           text_equals_cstr(value, "semidetached_house") ||
           text_equals_cstr(value, "terrace") ||
           text_equals_cstr(value, "dormitory") ||
           text_equals_cstr(value, "farm") ||
           text_equals_cstr(value, "bungalow") ||
           text_equals_cstr(value, "static_caravan") ||
           text_equals_cstr(value, "houseboat");
}

static int is_residential_candidate(const OsmBuildingAddressFields *address, const OsmBuildingFields *building) {
    int non_residential_poi = building->has_amenity || building->has_shop || building->has_office ||
                              building->has_tourism || building->has_leisure || building->has_craft ||
                              building->has_healthcare || building->has_public_transport;
    int explicit_residential = building->has_flats || building->has_apartments ||
                               (building->has_building && !text_equals_cstr(building->building, "yes") && is_residential_building_value(building->building)) ||
                               (building->has_building_part && !text_equals_cstr(building->building_part, "yes") && is_residential_building_value(building->building_part));

    if (non_residential_poi && !explicit_residential) return 0;
    if ((address->has_street || address->has_place) && address->has_housenumber) return 1;
    if (explicit_residential) return 1;
    if (building->has_building && is_residential_building_value(building->building)) return 1;
    if (building->has_building_part && is_residential_building_value(building->building_part)) return 1;
    return 0;
}

static int writer_flush(OsmBuildingWriter *writer) {
    if (writer->used == 0U) return 0;
    if (rt_write_all(writer->fd, writer->data, writer->used) != 0) return -1;
    writer->used = 0U;
    return 0;
}

static int writer_write(OsmBuildingWriter *writer, const void *data, size_t size) {
    if (size == 0U) return 0;
    if (size > writer->capacity) {
        if (writer_flush(writer) != 0) return -1;
        return rt_write_all(writer->fd, data, size);
    }
    if (writer->used + size > writer->capacity && writer_flush(writer) != 0) return -1;
    memcpy(writer->data + writer->used, data, size);
    writer->used += size;
    return 0;
}

static int writer_char(OsmBuildingWriter *writer, char ch) {
    return writer_write(writer, &ch, 1U);
}

static int writer_cstr(OsmBuildingWriter *writer, const char *text) {
    return writer_write(writer, text, rt_strlen(text));
}

static int writer_uint(OsmBuildingWriter *writer, unsigned long long value) {
    char buffer[32];
    rt_unsigned_to_string(value, buffer, sizeof(buffer));
    return writer_cstr(writer, buffer);
}

static int writer_entity_id(OsmBuildingWriter *writer, long long id) {
    if (id < 0) {
        return writer_char(writer, '-') != 0 || writer_uint(writer, (unsigned long long)(-id)) != 0 ? -1 : 0;
    }
    return writer_uint(writer, (unsigned long long)id);
}

static int writer_coord(OsmBuildingWriter *writer, long long nano) {
    unsigned long long value;
    unsigned long long whole;
    unsigned long long fraction;
    unsigned long long divisor = 100000000ULL;

    if (nano < 0) {
        if (writer_char(writer, '-') != 0) return -1;
        value = (unsigned long long)(-nano);
    } else {
        value = (unsigned long long)nano;
    }
    whole = value / 1000000000ULL;
    fraction = value % 1000000000ULL;
    if (writer_uint(writer, whole) != 0 || writer_char(writer, '.') != 0) return -1;
    while (divisor != 0ULL) {
        if (writer_char(writer, (char)('0' + (fraction / divisor) % 10ULL)) != 0) return -1;
        divisor /= 10ULL;
    }
    return 0;
}

static int writer_tsv_text(OsmBuildingWriter *writer, PbfText text) {
    size_t start = 0U;
    size_t index;

    for (index = 0U; index < text.size; ++index) {
        char ch = text.data[index];
        if (ch == '\t' || ch == '\n' || ch == '\r') {
            if (writer_write(writer, text.data + start, index - start) != 0 || writer_char(writer, ' ') != 0) return -1;
            start = index + 1U;
        }
    }
    return writer_write(writer, text.data + start, text.size - start);
}

static int write_header(OsmBuildingWriter *writer) {
    return writer_cstr(writer,
        "entity_type\tosm_id\tlat\tlon\tmin_lat\tmin_lon\tmax_lat\tmax_lon\tref_count\tmissing_ref_count\t"
        "has_address\thas_building\tresidential_candidate\tbuilding\tbuilding_part\tbuilding_levels\troof_levels\t"
        "building_min_level\tbuilding_flats\tbuilding_apartments\tbuilding_use\tstart_date\taddr_country\taddr_state\t"
        "addr_city\taddr_suburb\taddr_street\taddr_housenumber\taddr_postcode\taddr_place\taddr_unit\taddr_floor\t"
        "addr_door\tentrance\tname\tamenity\tshop\toffice\ttourism\tleisure\tcraft\thealthcare\tpublic_transport\n");
}

static int bbox_intersects(const OsmBuildingsContext *context, const OsmBuildingGeometry *geometry) {
    if (!context->has_bbox) return 1;
    if (!geometry->has_coord) return 0;
    if (geometry->max_lon_nano < context->min_lon_nano || geometry->min_lon_nano > context->max_lon_nano) return 0;
    if (geometry->max_lat_nano < context->min_lat_nano || geometry->min_lat_nano > context->max_lat_nano) return 0;
    return 1;
}

static int add_node_to_geometry(OsmBuildingGeometry *geometry, const OsmNodeIndexRecord *record) {
    if (!geometry->has_coord) {
        geometry->min_lat_nano = record->lat_nano;
        geometry->max_lat_nano = record->lat_nano;
        geometry->min_lon_nano = record->lon_nano;
        geometry->max_lon_nano = record->lon_nano;
        geometry->has_coord = 1;
    } else {
        if (record->lat_nano < geometry->min_lat_nano) geometry->min_lat_nano = record->lat_nano;
        if (record->lat_nano > geometry->max_lat_nano) geometry->max_lat_nano = record->lat_nano;
        if (record->lon_nano < geometry->min_lon_nano) geometry->min_lon_nano = record->lon_nano;
        if (record->lon_nano > geometry->max_lon_nano) geometry->max_lon_nano = record->lon_nano;
    }
    geometry->found_ref_count += 1ULL;
    return 0;
}

static int lookup_node_record(OsmBuildingsContext *context, long long id, OsmNodeIndexRecord *record_out) {
    int result = osm_node_index_find(&context->node_index, id, record_out, context->error, sizeof(context->error));
    if (result < 0) context->failed = 1;
    return result;
}

static int add_refs_to_geometry(OsmBuildingsContext *context, const long long *refs, unsigned int ref_count, OsmBuildingGeometry *geometry) {
    unsigned int index;

    geometry->ref_count += (unsigned long long)ref_count;
    for (index = 0U; index < ref_count; ++index) {
        OsmNodeIndexRecord record;
        int result = lookup_node_record(context, refs[index], &record);
        if (result < 0) return -1;
        if (result == 0) {
            geometry->missing_ref_count += 1ULL;
        } else {
            (void)add_node_to_geometry(geometry, &record);
        }
    }
    return 0;
}

static void finish_geometry(OsmBuildingGeometry *geometry) {
    if (!geometry->has_coord) return;
    geometry->lat_nano = geometry->min_lat_nano + (geometry->max_lat_nano - geometry->min_lat_nano) / 2LL;
    geometry->lon_nano = geometry->min_lon_nano + (geometry->max_lon_nano - geometry->min_lon_nano) / 2LL;
}

static int compute_way_geometry(OsmBuildingsContext *context, const PbfWay *way, OsmBuildingGeometry *geometry) {
    rt_memset(geometry, 0, sizeof(*geometry));
    if (!context->node_index_open) {
        geometry->ref_count = (unsigned long long)way->ref_count;
        return 0;
    }
    if (add_refs_to_geometry(context, way->refs, way->ref_count, geometry) != 0) return -1;
    finish_geometry(geometry);
    return 0;
}

static int way_intersects_bbox(OsmBuildingsContext *context, long long way_id) {
    if (!context->has_bbox) return 1;
    if (!context->spatial_index_open) return 1;
    return osm_spatial_index_way_intersects(&context->spatial_index, way_id, context->min_lon_nano, context->min_lat_nano, context->max_lon_nano, context->max_lat_nano);
}

static int relation_intersects_bbox(OsmBuildingsContext *context, const PbfRelation *relation) {
    unsigned int index;

    if (!context->has_bbox) return 1;
    if (!context->spatial_index_open) return 1;
    for (index = 0U; index < relation->member_count; ++index) {
        if (relation->members[index].type != PBF_RELATION_MEMBER_WAY) continue;
        if (osm_spatial_index_way_intersects(&context->spatial_index, relation->members[index].id, context->min_lon_nano, context->min_lat_nano, context->max_lon_nano, context->max_lat_nano)) {
            return 1;
        }
    }
    return 0;
}

static int compute_relation_geometry(OsmBuildingsContext *context, const PbfRelation *relation, OsmBuildingGeometry *geometry) {
    unsigned int index;

    rt_memset(geometry, 0, sizeof(*geometry));
    if (!context->node_index_open || !context->way_index_open) return 0;
    for (index = 0U; index < relation->member_count; ++index) {
        OsmWayIndexRecord way_record;
        long long *refs = 0;
        int result;

        if (relation->members[index].type != PBF_RELATION_MEMBER_WAY) continue;
        result = osm_way_index_find(&context->way_index, relation->members[index].id, &way_record, context->error, sizeof(context->error));
        if (result < 0) {
            context->failed = 1;
            return -1;
        }
        if (result == 0) {
            geometry->missing_ref_count += 1ULL;
            continue;
        }
        if (osm_way_index_read_refs(&context->way_index, &way_record, &refs, context->error, sizeof(context->error)) != 0) {
            context->failed = 1;
            return -1;
        }
        if (refs != 0) {
            if (add_refs_to_geometry(context, refs, way_record.ref_count, geometry) != 0) {
                rt_free(refs);
                return -1;
            }
            rt_free(refs);
        }
    }
    finish_geometry(geometry);
    return 0;
}

static int emit_row(OsmBuildingsContext *context, const char *type, long long id, const OsmBuildingAddressFields *address, const OsmBuildingFields *building, const OsmBuildingGeometry *geometry) {
    OsmBuildingWriter *writer = &context->writer;
    int address_present = has_address(address);
    int building_present = has_building(building);
    int residential = is_residential_candidate(address, building);

    if (!address_present && !building_present) return 0;
    if (context->residential_only && !residential) return 0;
    if (!bbox_intersects(context, geometry)) return 0;
    if (context->limit != 0ULL && context->written >= context->limit) return 1;

    if (writer_cstr(writer, type) != 0 || writer_char(writer, '\t') != 0 || writer_entity_id(writer, id) != 0 || writer_char(writer, '\t') != 0) goto fail;
    if (geometry->has_coord && writer_coord(writer, geometry->lat_nano) != 0) goto fail;
    if (writer_char(writer, '\t') != 0) goto fail;
    if (geometry->has_coord && writer_coord(writer, geometry->lon_nano) != 0) goto fail;
    if (writer_char(writer, '\t') != 0) goto fail;
    if (geometry->has_coord && writer_coord(writer, geometry->min_lat_nano) != 0) goto fail;
    if (writer_char(writer, '\t') != 0) goto fail;
    if (geometry->has_coord && writer_coord(writer, geometry->min_lon_nano) != 0) goto fail;
    if (writer_char(writer, '\t') != 0) goto fail;
    if (geometry->has_coord && writer_coord(writer, geometry->max_lat_nano) != 0) goto fail;
    if (writer_char(writer, '\t') != 0) goto fail;
    if (geometry->has_coord && writer_coord(writer, geometry->max_lon_nano) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_uint(writer, geometry->ref_count) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_uint(writer, geometry->missing_ref_count) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_char(writer, address_present ? '1' : '0') != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_char(writer, building_present ? '1' : '0') != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_char(writer, residential ? '1' : '0') != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->building) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->building_part) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->levels) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->roof_levels) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->min_level) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->flats) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->apartments) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->use) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->start_date) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, address->country) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, address->state) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, address->city) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, address->suburb) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, address->street) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, address->housenumber) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, address->postcode) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, address->place) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, address->unit) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, address->floor) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, address->door) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, address->entrance) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->name) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->amenity) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->shop) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->office) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->tourism) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->leisure) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->craft) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->healthcare) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, building->public_transport) != 0 || writer_char(writer, '\n') != 0) goto fail;

    context->written += 1ULL;
    if (geometry->missing_ref_count != 0ULL) context->partial_geometry_rows += 1ULL;
    return context->limit != 0ULL && context->written >= context->limit;

fail:
    context->failed = 1;
    return 1;
}

static int on_node(void *user, const PbfNode *node) {
    OsmBuildingsContext *context = (OsmBuildingsContext *)user;
    OsmBuildingAddressFields address;
    OsmBuildingFields building;
    OsmBuildingGeometry geometry;
    unsigned long long previous;
    OsmNodeIndexRecord record;
    int stop;

    find_address_fields(node->tags, node->tag_count, &address);
    find_building_fields(node->tags, node->tag_count, &building);
    rt_memset(&geometry, 0, sizeof(geometry));
    record.id = node->id;
    record.lat_nano = node->lat_nano;
    record.lon_nano = node->lon_nano;
    (void)add_node_to_geometry(&geometry, &record);
    geometry.ref_count = 1ULL;
    finish_geometry(&geometry);
    previous = context->written;
    stop = emit_row(context, "node", node->id, &address, &building, &geometry);
    if (context->written != previous) context->node_rows += 1ULL;
    return stop;
}

static int on_way(void *user, const PbfWay *way) {
    OsmBuildingsContext *context = (OsmBuildingsContext *)user;
    OsmBuildingAddressFields address;
    OsmBuildingFields building;
    OsmBuildingGeometry geometry;
    unsigned long long previous;
    int stop;

    find_address_fields(way->tags, way->tag_count, &address);
    find_building_fields(way->tags, way->tag_count, &building);
    if (!has_address(&address) && !has_building(&building)) return 0;
    if (!way_intersects_bbox(context, way->id)) return 0;
    if (compute_way_geometry(context, way, &geometry) != 0) return 1;
    previous = context->written;
    stop = emit_row(context, "way", way->id, &address, &building, &geometry);
    if (context->written != previous) context->way_rows += 1ULL;
    return stop;
}

static int on_relation(void *user, const PbfRelation *relation) {
    OsmBuildingsContext *context = (OsmBuildingsContext *)user;
    OsmBuildingAddressFields address;
    OsmBuildingFields building;
    OsmBuildingGeometry geometry;
    unsigned long long previous;
    int stop;

    find_address_fields(relation->tags, relation->tag_count, &address);
    find_building_fields(relation->tags, relation->tag_count, &building);
    if (!has_address(&address) && !has_building(&building)) return 0;
    if (context->has_bbox && !context->way_index_open) return 0;
    if (!relation_intersects_bbox(context, relation)) return 0;
    if (compute_relation_geometry(context, relation, &geometry) != 0) return 1;
    previous = context->written;
    stop = emit_row(context, "relation", relation->id, &address, &building, &geometry);
    if (context->written != previous) context->relation_rows += 1ULL;
    return stop;
}

static int output_is_stdout(const char *path) {
    return path[0] == '-' && path[1] == '\0';
}

static char *make_temp_path(const char *path) {
    size_t path_size = rt_strlen(path);
    char *temp_path = (char *)rt_malloc(path_size + 5U);
    if (temp_path == 0) return 0;
    memcpy(temp_path, path, path_size);
    memcpy(temp_path + path_size, ".tmp", 5U);
    return temp_path;
}

int main(int argc, char **argv) {
    const char *program = argc > 0 ? argv[0] : "osm-buildings";
    const char *pbf_path;
    const char *out_path;
    char *temp_path = 0;
    const char *write_path;
    OsmBuildingsContext context;
    PbfStreamCallbacks callbacks;
    char parse_error[PBF_ERROR_CAPACITY];
    int output_stdout;
    int stats_fd;
    int argi;

    if (argc < 3 || rt_strcmp(argv[1], "-h") == 0 || rt_strcmp(argv[1], "--help") == 0) {
        write_usage(program);
        return argc == 2 ? 0 : 1;
    }
    pbf_path = argv[1];
    out_path = argv[2];
    rt_memset(&context, 0, sizeof(context));
    context.write_header = 1;
    argi = 3;
    while (argi < argc) {
        if (rt_strcmp(argv[argi], "--bbox") == 0) {
            argi += 1;
            if (argi >= argc || parse_bbox_arg(argv[argi], &context) != 0) {
                write_usage(program);
                return 1;
            }
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--node-index") == 0) {
            argi += 1;
            if (argi >= argc) {
                write_usage(program);
                return 1;
            }
            context.node_index_path = argv[argi];
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--way-index") == 0) {
            argi += 1;
            if (argi >= argc) {
                write_usage(program);
                return 1;
            }
            context.way_index_path = argv[argi];
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--spatial-index") == 0) {
            argi += 1;
            if (argi >= argc) {
                write_usage(program);
                return 1;
            }
            context.spatial_index_path = argv[argi];
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--residential-only") == 0) {
            context.residential_only = 1;
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--no-header") == 0) {
            context.write_header = 0;
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--limit") == 0) {
            argi += 1;
            if (argi >= argc || parse_uint_arg(argv[argi], &context.limit) != 0) {
                write_usage(program);
                return 1;
            }
            argi += 1;
        } else {
            write_usage(program);
            return 1;
        }
    }
    if (context.has_bbox && context.node_index_path == 0) {
        rt_write_cstr(2, "osm-buildings: --bbox requires --node-index for way geometry\n");
        return 1;
    }
    if (context.way_index_path != 0 && context.node_index_path == 0) {
        rt_write_cstr(2, "osm-buildings: --way-index requires --node-index\n");
        return 1;
    }
    if (context.spatial_index_path != 0 && context.node_index_path == 0) {
        rt_write_cstr(2, "osm-buildings: --spatial-index requires --node-index\n");
        return 1;
    }
    if (context.node_index_path != 0) {
        if (osm_node_index_open(&context.node_index, context.node_index_path, context.error, sizeof(context.error)) != 0) {
            rt_write_cstr(2, "osm-buildings: ");
            rt_write_cstr(2, context.error[0] == '\0' ? "could not open node index" : context.error);
            rt_write_char(2, '\n');
            return 1;
        }
        context.node_index_open = 1;
    }
    if (context.way_index_path != 0) {
        if (osm_way_index_open(&context.way_index, context.way_index_path, context.error, sizeof(context.error)) != 0) {
            rt_write_cstr(2, "osm-buildings: ");
            rt_write_cstr(2, context.error[0] == '\0' ? "could not open way index" : context.error);
            rt_write_char(2, '\n');
            if (context.node_index_open) osm_node_index_close(&context.node_index);
            return 1;
        }
        context.way_index_open = 1;
    }
    if (context.spatial_index_path != 0) {
        if (osm_spatial_index_open(&context.spatial_index, context.spatial_index_path, context.error, sizeof(context.error)) != 0) {
            rt_write_cstr(2, "osm-buildings: ");
            rt_write_cstr(2, context.error[0] == '\0' ? "could not open spatial index" : context.error);
            rt_write_char(2, '\n');
            if (context.way_index_open) osm_way_index_close(&context.way_index);
            if (context.node_index_open) osm_node_index_close(&context.node_index);
            return 1;
        }
        context.spatial_index_open = 1;
    }
    output_stdout = output_is_stdout(out_path);
    if (!output_stdout) {
        temp_path = make_temp_path(out_path);
        if (temp_path == 0) {
            rt_write_cstr(2, "osm-buildings: out of memory\n");
            if (context.way_index_open) osm_way_index_close(&context.way_index);
            if (context.node_index_open) osm_node_index_close(&context.node_index);
            return 1;
        }
        write_path = temp_path;
    } else {
        write_path = out_path;
    }
    context.writer.fd = output_stdout ? 1 : platform_open_write(write_path, 0644U);
    if (context.writer.fd < 0) {
        rt_write_cstr(2, "osm-buildings: could not open output file\n");
        rt_free(temp_path);
        if (context.spatial_index_open) osm_spatial_index_close(&context.spatial_index);
        if (context.way_index_open) osm_way_index_close(&context.way_index);
        if (context.node_index_open) osm_node_index_close(&context.node_index);
        return 1;
    }
    context.writer.capacity = OSM_BUILDINGS_BUFFER_SIZE;
    context.writer.data = (unsigned char *)rt_malloc(context.writer.capacity);
    if (context.writer.data == 0) {
        rt_write_cstr(2, "osm-buildings: out of memory\n");
        if (!output_stdout) (void)platform_close(context.writer.fd);
        rt_free(temp_path);
        if (context.spatial_index_open) osm_spatial_index_close(&context.spatial_index);
        if (context.way_index_open) osm_way_index_close(&context.way_index);
        if (context.node_index_open) osm_node_index_close(&context.node_index);
        return 1;
    }
    if (context.write_header && write_header(&context.writer) != 0) {
        rt_write_cstr(2, "osm-buildings: could not write output header\n");
        rt_free(context.writer.data);
        if (!output_stdout) (void)platform_close(context.writer.fd);
        rt_free(temp_path);
        if (context.spatial_index_open) osm_spatial_index_close(&context.spatial_index);
        if (context.way_index_open) osm_way_index_close(&context.way_index);
        if (context.node_index_open) osm_node_index_close(&context.node_index);
        return 1;
    }
    rt_memset(&callbacks, 0, sizeof(callbacks));
    callbacks.node = on_node;
    callbacks.way = on_way;
    callbacks.relation = on_relation;
    parse_error[0] = '\0';
    if (pbf_stream_entities(pbf_path, &callbacks, &context, parse_error, sizeof(parse_error)) != 0 ||
        context.failed ||
        writer_flush(&context.writer) != 0) {
        rt_write_cstr(2, "osm-buildings: ");
        rt_write_cstr(2, context.failed ? (context.error[0] == '\0' ? "failed while writing or reading indexes" : context.error) : (parse_error[0] == '\0' ? "failed to parse PBF" : parse_error));
        rt_write_char(2, '\n');
        rt_free(context.writer.data);
        if (!output_stdout) {
            (void)platform_close(context.writer.fd);
            if (temp_path != 0) (void)platform_remove_file(temp_path);
        }
        rt_free(temp_path);
        if (context.spatial_index_open) osm_spatial_index_close(&context.spatial_index);
        if (context.way_index_open) osm_way_index_close(&context.way_index);
        if (context.node_index_open) osm_node_index_close(&context.node_index);
        return 1;
    }
    rt_free(context.writer.data);
    if (!output_stdout && platform_close(context.writer.fd) != 0) {
        rt_write_cstr(2, "osm-buildings: could not close output file\n");
        if (temp_path != 0) (void)platform_remove_file(temp_path);
        rt_free(temp_path);
        if (context.spatial_index_open) osm_spatial_index_close(&context.spatial_index);
        if (context.way_index_open) osm_way_index_close(&context.way_index);
        if (context.node_index_open) osm_node_index_close(&context.node_index);
        return 1;
    }
    if (!output_stdout && temp_path != 0 && platform_rename_path(temp_path, out_path) != 0) {
        rt_write_cstr(2, "osm-buildings: could not move temporary output into place\n");
        (void)platform_remove_file(temp_path);
        rt_free(temp_path);
        if (context.spatial_index_open) osm_spatial_index_close(&context.spatial_index);
        if (context.way_index_open) osm_way_index_close(&context.way_index);
        if (context.node_index_open) osm_node_index_close(&context.node_index);
        return 1;
    }
    rt_free(temp_path);
    if (context.spatial_index_open) osm_spatial_index_close(&context.spatial_index);
    if (context.way_index_open) osm_way_index_close(&context.way_index);
    if (context.node_index_open) osm_node_index_close(&context.node_index);

    stats_fd = output_stdout ? 2 : 1;
    rt_write_cstr(stats_fd, "building_rows_written: ");
    rt_write_uint(stats_fd, context.written);
    rt_write_char(stats_fd, '\n');
    rt_write_cstr(stats_fd, "node_rows: ");
    rt_write_uint(stats_fd, context.node_rows);
    rt_write_char(stats_fd, '\n');
    rt_write_cstr(stats_fd, "way_rows: ");
    rt_write_uint(stats_fd, context.way_rows);
    rt_write_char(stats_fd, '\n');
    rt_write_cstr(stats_fd, "relation_rows: ");
    rt_write_uint(stats_fd, context.relation_rows);
    rt_write_char(stats_fd, '\n');
    rt_write_cstr(stats_fd, "partial_geometry_rows: ");
    rt_write_uint(stats_fd, context.partial_geometry_rows);
    rt_write_char(stats_fd, '\n');
    return 0;
}
