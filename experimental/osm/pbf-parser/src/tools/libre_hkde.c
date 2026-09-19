/*
 * libre-hkde
 *
 * Converts the project's normalized, postcode-enriched address CSV plus the
 * aligned VG250 municipality assignment into the 24-column HK-DE 5.2 layout.
 * Deliberately unavailable values remain empty or use the format's documented
 * zero key. The output is a compatibility profile, not the ZSHH product.
 *
 * No libc, no external dependencies.
 */

#include "platform.h"
#include "runtime.h"

#define LIBRE_HKDE_BUFFER_SIZE 262144U
#define LIBRE_HKDE_FIELDS 14U
#define LIBRE_HKDE_ASSIGNMENT_FIELDS 7U
#define LIBRE_HKDE_FIELD_SIZE 2048U

typedef struct {
    int fd;
    unsigned char data[LIBRE_HKDE_BUFFER_SIZE];
    unsigned int offset;
    unsigned int length;
    int pushed;
    int pushed_value;
    int failed;
} LibreHkdeReader;

typedef struct {
    char values[LIBRE_HKDE_FIELDS][LIBRE_HKDE_FIELD_SIZE];
    unsigned int count;
} LibreHkdeAddress;

typedef struct {
    char values[LIBRE_HKDE_ASSIGNMENT_FIELDS][LIBRE_HKDE_FIELD_SIZE];
    unsigned int count;
} LibreHkdeAssignment;

typedef struct {
    int fd;
    unsigned char data[LIBRE_HKDE_BUFFER_SIZE];
    unsigned int used;
    int failed;
} LibreHkdeWriter;

typedef struct {
    unsigned long long records;
    unsigned long long municipality_assignments;
    unsigned long long missing_municipalities;
    unsigned long long empty_postcodes;
    unsigned long long empty_house_numbers;
    unsigned long long structured_house_keys;
    unsigned long long zero_administrative_keys;
    unsigned long long sanitized_semicolons;
} LibreHkdeStats;

static const char libre_hkde_header[] =
    "nba;oid;qua;landschl;land;regbezschl;regbez;kreisschl;kreis;"
    "gmdschl;gmd;ottschl;ott;strschl;str;hnr;adz;zone;ostwert;"
    "nordwert;postplz;postonm;postonmzus;postott\r\n";

static int libre_hkde_reader_get(LibreHkdeReader *reader) {
    long received;
    if (reader->pushed) {
        reader->pushed = 0;
        return reader->pushed_value;
    }
    if (reader->offset >= reader->length) {
        received = platform_read(reader->fd, reader->data, sizeof(reader->data));
        if (received < 0) {
            reader->failed = 1;
            return -1;
        }
        if (received == 0) return -1;
        reader->offset = 0U;
        reader->length = (unsigned int)received;
    }
    return (int)reader->data[reader->offset++];
}

static void libre_hkde_reader_unget(LibreHkdeReader *reader, int value) {
    reader->pushed = 1;
    reader->pushed_value = value;
}

static int libre_hkde_csv_record(
    LibreHkdeReader *reader,
    char *storage,
    unsigned int field_capacity,
    unsigned int field_size,
    unsigned int *field_count_out
) {
    unsigned int field = 0U;
    unsigned int length = 0U;
    int quoted = 0;
    int started = 0;

    rt_memset(storage, 0, (size_t)field_capacity * field_size);
    for (;;) {
        int ch = libre_hkde_reader_get(reader);
        if (ch < 0) {
            if (reader->failed) return -1;
            if (!started && field == 0U && length == 0U) return 0;
            if (quoted) return -1;
            *field_count_out = field + 1U;
            return 1;
        }
        started = 1;
        if (quoted) {
            if (ch == '"') {
                int next = libre_hkde_reader_get(reader);
                if (next == '"') {
                    ch = '"';
                } else {
                    quoted = 0;
                    if (next >= 0) libre_hkde_reader_unget(reader, next);
                    continue;
                }
            }
        } else {
            if (ch == '"' && length == 0U) {
                quoted = 1;
                continue;
            }
            if (ch == ',') {
                if (++field >= field_capacity) return -1;
                length = 0U;
                continue;
            }
            if (ch == '\n') {
                *field_count_out = field + 1U;
                return 1;
            }
            if (ch == '\r') continue;
        }
        if (length + 1U >= field_size) return -1;
        storage[(size_t)field * field_size + length++] = (char)ch;
    }
}

static int libre_hkde_writer_flush(LibreHkdeWriter *writer) {
    if (writer->failed || writer->used == 0U) return writer->failed ? -1 : 0;
    if (rt_write_all(writer->fd, writer->data, writer->used) != 0) {
        writer->failed = 1;
        return -1;
    }
    writer->used = 0U;
    return 0;
}

static int libre_hkde_writer_bytes(
    LibreHkdeWriter *writer,
    const char *data,
    unsigned int length
) {
    while (length != 0U) {
        unsigned int available = (unsigned int)sizeof(writer->data) - writer->used;
        unsigned int chunk = length < available ? length : available;
        if (available == 0U) {
            if (libre_hkde_writer_flush(writer) != 0) return -1;
            continue;
        }
        memcpy(writer->data + writer->used, data, chunk);
        writer->used += chunk;
        data += chunk;
        length -= chunk;
    }
    return 0;
}

static int libre_hkde_writer_text(LibreHkdeWriter *writer, const char *text) {
    return libre_hkde_writer_bytes(writer, text, (unsigned int)rt_strlen(text));
}

static int libre_hkde_writer_field(
    LibreHkdeWriter *writer,
    const char *text,
    LibreHkdeStats *stats
) {
    char buffer[LIBRE_HKDE_FIELD_SIZE];
    unsigned int used = 0U;
    while (*text != '\0') {
        char ch = *text++;
        if (ch == ';') {
            ch = ',';
            stats->sanitized_semicolons += 1U;
        }
        if (ch != '\r' && ch != '\n') {
            if (used + 1U >= sizeof(buffer)) return -1;
            buffer[used++] = ch;
        }
    }
    return libre_hkde_writer_bytes(writer, buffer, used);
}

static int libre_hkde_separator(LibreHkdeWriter *writer) {
    return libre_hkde_writer_bytes(writer, ";", 1U);
}

static int libre_hkde_parse_double(const char *text, double *value_out) {
    double value = 0.0;
    double fraction = 0.1;
    int negative = 0;
    int digits = 0;
    if (*text == '-') {
        negative = 1;
        ++text;
    } else if (*text == '+') {
        ++text;
    }
    while (*text >= '0' && *text <= '9') {
        value = value * 10.0 + (double)(*text++ - '0');
        digits = 1;
    }
    if (*text == '.' || *text == ',') {
        ++text;
        while (*text >= '0' && *text <= '9') {
            value += (double)(*text++ - '0') * fraction;
            fraction *= 0.1;
            digits = 1;
        }
    }
    if (!digits || *text != '\0') return -1;
    *value_out = negative ? -value : value;
    return 0;
}

static double libre_hkde_sqrt(double value) {
    double result = value > 1.0 ? value : 1.0;
    unsigned int index;
    for (index = 0U; index < 32U; ++index) {
        result = 0.5 * (result + value / result);
    }
    return result;
}

static double libre_hkde_sin(double value) {
    const double pi = 3.14159265358979323846;
    const double two_pi = 6.28318530717958647692;
    double square;
    while (value > pi) value -= two_pi;
    while (value < -pi) value += two_pi;
    if (value > pi / 2.0) value = pi - value;
    else if (value < -pi / 2.0) value = -pi - value;
    square = value * value;
    return value *
           (1.0 +
            square *
                (-1.0 / 6.0 +
                 square *
                     (1.0 / 120.0 +
                      square *
                          (-1.0 / 5040.0 +
                           square *
                               (1.0 / 362880.0 +
                                square *
                                    (-1.0 / 39916800.0 +
                                     square * (1.0 / 6227020800.0)))))));
}

static double libre_hkde_cos(double value) {
    const double pi = 3.14159265358979323846;
    const double two_pi = 6.28318530717958647692;
    double sign = 1.0;
    double square;
    while (value > pi) value -= two_pi;
    while (value < -pi) value += two_pi;
    if (value > pi / 2.0) {
        value = pi - value;
        sign = -1.0;
    } else if (value < -pi / 2.0) {
        value = -pi - value;
        sign = -1.0;
    }
    square = value * value;
    return sign *
           (1.0 +
           square *
               (-1.0 / 2.0 +
                square *
                    (1.0 / 24.0 +
                     square *
                         (-1.0 / 720.0 +
                          square *
                              (1.0 / 40320.0 +
                               square *
                                   (-1.0 / 3628800.0 +
                                    square * (1.0 / 479001600.0)))))));
}

static void libre_hkde_utm32(
    double longitude,
    double latitude,
    double *easting_out,
    double *northing_out
) {
    const double a = 6378137.0;
    const double eccentricity_squared = 0.0066943800229007876;
    const double second_eccentricity_squared = 0.006739496775478856;
    const double scale = 0.9996;
    const double radians = 0.01745329251994329577;
    double phi = latitude * radians;
    double lambda_delta = (longitude - 9.0) * radians;
    double sin_phi = libre_hkde_sin(phi);
    double cos_phi = libre_hkde_cos(phi);
    double tan_phi = sin_phi / cos_phi;
    double n =
        a / libre_hkde_sqrt(1.0 - eccentricity_squared * sin_phi * sin_phi);
    double t = tan_phi * tan_phi;
    double c = second_eccentricity_squared * cos_phi * cos_phi;
    double alpha = cos_phi * lambda_delta;
    double e4 = eccentricity_squared * eccentricity_squared;
    double e6 = e4 * eccentricity_squared;
    double meridian =
        a *
        ((1.0 - eccentricity_squared / 4.0 - 3.0 * e4 / 64.0 -
          5.0 * e6 / 256.0) *
             phi -
         (3.0 * eccentricity_squared / 8.0 + 3.0 * e4 / 32.0 +
          45.0 * e6 / 1024.0) *
             libre_hkde_sin(2.0 * phi) +
         (15.0 * e4 / 256.0 + 45.0 * e6 / 1024.0) *
             libre_hkde_sin(4.0 * phi) -
         35.0 * e6 / 3072.0 * libre_hkde_sin(6.0 * phi));

    *easting_out =
        500000.0 +
        scale * n *
            (alpha + (1.0 - t + c) * alpha * alpha * alpha / 6.0 +
             (5.0 - 18.0 * t + t * t + 72.0 * c -
              58.0 * second_eccentricity_squared) *
                 alpha * alpha * alpha * alpha * alpha / 120.0);
    *northing_out =
        scale *
        (meridian +
         n * tan_phi *
             (alpha * alpha / 2.0 +
              (5.0 - t + 9.0 * c + 4.0 * c * c) *
                  alpha * alpha * alpha * alpha / 24.0 +
              (61.0 - 58.0 * t + t * t + 600.0 * c -
               330.0 * second_eccentricity_squared) *
                  alpha * alpha * alpha * alpha * alpha * alpha / 720.0));
}

static int libre_hkde_writer_coordinate(
    LibreHkdeWriter *writer,
    double value
) {
    unsigned long long scaled = (unsigned long long)(value * 1000.0 + 0.5);
    unsigned long long whole = scaled / 1000U;
    unsigned int fraction = (unsigned int)(scaled % 1000U);
    char buffer[32];
    char fraction_text[3];
    rt_unsigned_to_string(whole, buffer, sizeof(buffer));
    fraction_text[0] = (char)('0' + fraction / 100U);
    fraction_text[1] = (char)('0' + (fraction / 10U) % 10U);
    fraction_text[2] = (char)('0' + fraction % 10U);
    if (libre_hkde_writer_text(writer, buffer) != 0) return -1;
    if (libre_hkde_writer_bytes(writer, ".", 1U) != 0) return -1;
    return libre_hkde_writer_bytes(writer, fraction_text, sizeof(fraction_text));
}

static int libre_hkde_key_parts(
    const char *text,
    char parts[6][8]
) {
    static const unsigned int expected_lengths[6] = {2U, 1U, 2U, 3U, 4U, 5U};
    unsigned int part = 0U;
    unsigned int length = 0U;
    rt_memset(parts, 0, 6U * 8U);
    while (*text != '\0' && part < 6U) {
        char ch = *text++;
        if (ch == ';') {
            if (length != expected_lengths[part]) return 0;
            if (part == 5U) return 1;
            ++part;
            length = 0U;
            continue;
        }
        if (ch < '0' || ch > '9' || length >= 7U) return 0;
        parts[part][length++] = ch;
    }
    return part == 5U && length == expected_lengths[5];
}

static int libre_hkde_same_text(const char *left, const char *right) {
    return rt_strcmp(left, right) == 0;
}

static int libre_hkde_house_number(
    const char *source_number,
    const char *source_suffix,
    char *number,
    char *suffix,
    LibreHkdeStats *stats
) {
    unsigned int number_length = 0U;
    unsigned int suffix_length = 0U;
    const char *remainder;
    while (
        source_number[number_length] >= '0' &&
        source_number[number_length] <= '9'
    ) {
        if (number_length + 1U >= LIBRE_HKDE_FIELD_SIZE) return -1;
        number[number_length] = source_number[number_length];
        number_length += 1U;
    }
    number[number_length] = '\0';
    remainder = source_number + number_length;
    if (number_length == 0U) {
        number[0] = '0';
        number[1] = '\0';
        remainder = source_number;
        stats->empty_house_numbers += 1U;
    } else if (libre_hkde_same_text(number, "0")) {
        stats->empty_house_numbers += 1U;
    }
    while (*remainder != '\0') {
        if (suffix_length + 1U >= LIBRE_HKDE_FIELD_SIZE) return -1;
        suffix[suffix_length++] = *remainder++;
    }
    while (*source_suffix != '\0') {
        if (suffix_length + 1U >= LIBRE_HKDE_FIELD_SIZE) return -1;
        suffix[suffix_length++] = *source_suffix++;
    }
    suffix[suffix_length] = '\0';
    return 0;
}

static int libre_hkde_write_row(
    LibreHkdeWriter *writer,
    LibreHkdeAddress *address,
    LibreHkdeAssignment *assignment,
    LibreHkdeStats *stats
) {
    char key_parts[6][8];
    const char *government_district = "0";
    const char *county = "00";
    const char *municipality_code = "000";
    const char *municipality_name = address->values[5];
    const char *district_code = "0000";
    const char *street_code = "00000";
    char house_number[LIBRE_HKDE_FIELD_SIZE];
    char house_number_suffix[LIBRE_HKDE_FIELD_SIZE];
    double longitude;
    double latitude;
    double easting;
    double northing;

    if (libre_hkde_key_parts(address->values[13], key_parts)) {
        government_district = key_parts[1];
        county = key_parts[2];
        municipality_code = key_parts[3];
        district_code = key_parts[4];
        street_code = key_parts[5];
        stats->structured_house_keys += 1U;
    } else if (assignment != 0 && assignment->values[1][0] != '\0') {
        government_district = assignment->values[3];
        county = assignment->values[4];
        municipality_code = assignment->values[5];
        municipality_name = assignment->values[2];
        stats->municipality_assignments += 1U;
    } else {
        stats->zero_administrative_keys += 1U;
    }
    if (municipality_name[0] == '\0') stats->missing_municipalities += 1U;
    if (address->values[3][0] == '\0') stats->empty_postcodes += 1U;
    if (libre_hkde_house_number(
            address->values[8],
            address->values[9],
            house_number,
            house_number_suffix,
            stats
        ) != 0) return -1;
    if (libre_hkde_parse_double(address->values[10], &longitude) != 0 ||
        libre_hkde_parse_double(address->values[11], &latitude) != 0) {
        return -1;
    }
    libre_hkde_utm32(longitude, latitude, &easting, &northing);

#define WRITE_FIELD(value)                                                       \
    do {                                                                         \
        if (libre_hkde_writer_field(writer, (value), stats) != 0) return -1;     \
        if (libre_hkde_separator(writer) != 0) return -1;                         \
    } while (0)

    WRITE_FIELD("N");
    WRITE_FIELD("");
    WRITE_FIELD("");
    WRITE_FIELD(address->values[2]);
    WRITE_FIELD(address->values[1]);
    WRITE_FIELD(government_district);
    WRITE_FIELD("");
    WRITE_FIELD(county);
    WRITE_FIELD("");
    WRITE_FIELD(municipality_code);
    WRITE_FIELD(municipality_name);
    WRITE_FIELD(district_code);
    WRITE_FIELD(address->values[6]);
    WRITE_FIELD(street_code);
    WRITE_FIELD(address->values[7]);
    WRITE_FIELD(house_number);
    WRITE_FIELD(house_number_suffix);
    WRITE_FIELD("32");
    if (libre_hkde_writer_coordinate(writer, easting) != 0) return -1;
    if (libre_hkde_separator(writer) != 0) return -1;
    if (libre_hkde_writer_coordinate(writer, northing) != 0) return -1;
    if (libre_hkde_separator(writer) != 0) return -1;
    WRITE_FIELD(address->values[3]);
    WRITE_FIELD(address->values[4]);
    WRITE_FIELD("");
    if (libre_hkde_writer_field(writer, "", stats) != 0) return -1;
    if (libre_hkde_writer_bytes(writer, "\r\n", 2U) != 0) return -1;
#undef WRITE_FIELD
    stats->records += 1U;
    return 0;
}

static int libre_hkde_write_report(
    const char *path,
    const LibreHkdeStats *stats
) {
    int fd = platform_open_write(path, 0644U);
    if (fd < 0) return -1;
#define REPORT_VALUE(label, value)                                               \
    do {                                                                         \
        if (rt_write_cstr(fd, label) != 0 || rt_write_uint(fd, value) != 0 ||    \
            rt_write_char(fd, '\n') != 0) {                                      \
            platform_close(fd);                                                  \
            return -1;                                                           \
        }                                                                        \
    } while (0)
    REPORT_VALUE("records=", stats->records);
    REPORT_VALUE("municipality_assignments=", stats->municipality_assignments);
    REPORT_VALUE("missing_municipalities=", stats->missing_municipalities);
    REPORT_VALUE("empty_postcodes=", stats->empty_postcodes);
    REPORT_VALUE("empty_house_numbers=", stats->empty_house_numbers);
    REPORT_VALUE("structured_house_keys=", stats->structured_house_keys);
    REPORT_VALUE("zero_administrative_keys=", stats->zero_administrative_keys);
    REPORT_VALUE("sanitized_semicolons=", stats->sanitized_semicolons);
#undef REPORT_VALUE
    return platform_close(fd);
}

int main(int argc, char **argv) {
    LibreHkdeReader addresses;
    LibreHkdeReader assignments;
    LibreHkdeAddress address;
    LibreHkdeAssignment assignment;
    LibreHkdeWriter writer;
    LibreHkdeStats stats;
    int address_fd;
    int assignment_fd;
    int output_fd;
    int result;

    if (argc != 5) {
        rt_write_line(
            2,
            "usage: libre-hkde <adressen.csv> <basemap-gemeinden.csv> <output.csv> <report.txt>"
        );
        return 2;
    }
    address_fd = libre_hkde_same_text(argv[1], "-") ? 0 : platform_open_read(argv[1]);
    assignment_fd = platform_open_read(argv[2]);
    output_fd = libre_hkde_same_text(argv[3], "-") ? 1 : platform_open_write(argv[3], 0644U);
    if (address_fd < 0 || assignment_fd < 0 || output_fd < 0) {
        rt_write_line(2, "libre-hkde: cannot open input or output");
        return 1;
    }
    rt_memset(&addresses, 0, sizeof(addresses));
    rt_memset(&assignments, 0, sizeof(assignments));
    rt_memset(&writer, 0, sizeof(writer));
    rt_memset(&stats, 0, sizeof(stats));
    addresses.fd = address_fd;
    assignments.fd = assignment_fd;
    writer.fd = output_fd;

    result = libre_hkde_csv_record(
        &addresses,
        &address.values[0][0],
        LIBRE_HKDE_FIELDS,
        LIBRE_HKDE_FIELD_SIZE,
        &address.count
    );
    if (result != 1 || address.count != LIBRE_HKDE_FIELDS ||
        !libre_hkde_same_text(address.values[12], "datensatznummer")) {
        rt_write_line(2, "libre-hkde: unexpected address header");
        return 1;
    }
    result = libre_hkde_csv_record(
        &assignments,
        &assignment.values[0][0],
        LIBRE_HKDE_ASSIGNMENT_FIELDS,
        LIBRE_HKDE_FIELD_SIZE,
        &assignment.count
    );
    if (result != 1 || assignment.count != LIBRE_HKDE_ASSIGNMENT_FIELDS ||
        !libre_hkde_same_text(assignment.values[0], "bund_id")) {
        rt_write_line(2, "libre-hkde: unexpected assignment header");
        return 1;
    }
    if (libre_hkde_writer_text(&writer, libre_hkde_header) != 0) return 1;

    for (;;) {
        LibreHkdeAssignment *assignment_pointer = 0;
        result = libre_hkde_csv_record(
            &addresses,
            &address.values[0][0],
            LIBRE_HKDE_FIELDS,
            LIBRE_HKDE_FIELD_SIZE,
            &address.count
        );
        if (result == 0) break;
        if (result < 0 || address.count != LIBRE_HKDE_FIELDS) {
            rt_write_line(2, "libre-hkde: invalid address CSV record");
            return 1;
        }
        if (address.values[5][0] == '\0') {
            result = libre_hkde_csv_record(
                &assignments,
                &assignment.values[0][0],
                LIBRE_HKDE_ASSIGNMENT_FIELDS,
                LIBRE_HKDE_FIELD_SIZE,
                &assignment.count
            );
            if (result != 1 ||
                assignment.count != LIBRE_HKDE_ASSIGNMENT_FIELDS ||
                !libre_hkde_same_text(address.values[12], assignment.values[0])) {
                rt_write_line(2, "libre-hkde: municipality assignment mismatch");
                return 1;
            }
            assignment_pointer = &assignment;
        }
        if (libre_hkde_write_row(&writer, &address, assignment_pointer, &stats) != 0) {
            rt_write_line(2, "libre-hkde: cannot convert address record");
            return 1;
        }
    }
    result = libre_hkde_csv_record(
        &assignments,
        &assignment.values[0][0],
        LIBRE_HKDE_ASSIGNMENT_FIELDS,
        LIBRE_HKDE_FIELD_SIZE,
        &assignment.count
    );
    if (result != 0) {
        rt_write_line(2, "libre-hkde: unused municipality assignments");
        return 1;
    }
    if (libre_hkde_writer_flush(&writer) != 0 ||
        (output_fd != 1 && platform_close(output_fd) != 0) ||
        libre_hkde_write_report(argv[4], &stats) != 0) {
        rt_write_line(2, "libre-hkde: cannot finalize output");
        return 1;
    }
    if (address_fd != 0) platform_close(address_fd);
    platform_close(assignment_fd);
    rt_write_cstr(2, "libre-hkde: wrote ");
    rt_write_uint(2, stats.records);
    rt_write_line(2, " records");
    return 0;
}
