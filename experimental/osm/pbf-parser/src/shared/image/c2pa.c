#include "image_internal.h"
#include "runtime.h"
#include "crypto/p256.h"
#include "crypto/sha256.h"

#define C2PA_MAX_EXCLUSIONS 16U
#define C2PA_MAX_DEPTH 16U
#define C2PA_MAX_CLAIMS 32U

#define C2PA_COSE_ALG_ES256 (-7)
#define C2PA_COSE_ALG_EDDSA (-8)
#define C2PA_COSE_ALG_PS256 (-37)

typedef struct {
    size_t start;
    size_t length;
} C2paRange;

typedef struct {
    const unsigned char *data;
    size_t size;
    size_t pos;
    int valid;
} CborReader;

typedef struct {
    int present;
    int alg_sha256;
    unsigned char hash[CRYPTO_SHA256_DIGEST_SIZE];
    C2paRange exclusions[C2PA_MAX_EXCLUSIONS];
    unsigned int exclusion_count;
} C2paHashAssertion;

typedef struct {
    long long alg;
    const unsigned char *leaf_cert;
    size_t leaf_cert_size;
    int has_leaf_cert;
} C2paCoseInfo;

typedef struct {
    const unsigned char *data;
    size_t size;
    size_t pos;
    int valid;
} Asn1Reader;

typedef struct {
    const unsigned char *full_data;
    size_t full_size;
    C2paRange carrier_ranges[C2PA_MAX_EXCLUSIONS];
    C2paRange claim_ranges[C2PA_MAX_CLAIMS];
    unsigned int carrier_range_count;
    unsigned int claim_range_count;
    unsigned int preferred_claim_index;
    int claim_ranges_complete;
    unsigned int top_manifest_index;
    unsigned int active_manifest_depth;
    int in_active_manifest;
    int trust_validation_enabled;
} C2paContext;

static unsigned long long c2pa_read_u64_be(const unsigned char *data) {
    unsigned long long value = 0ULL;
    unsigned int index;

    for (index = 0U; index < 8U; ++index) {
        value = (value << 8) | (unsigned long long)data[index];
    }
    return value;
}

static int c2pa_token_at(const unsigned char *data, size_t size, size_t offset, const char *token) {
    size_t index = 0U;

    while (token[index] != '\0') {
        if (offset + index >= size || data[offset + index] != (unsigned char)token[index]) {
            return 0;
        }
        index += 1U;
    }
    return 1;
}

static unsigned int c2pa_count_token(const unsigned char *data, size_t size, const char *token) {
    size_t offset;
    unsigned int count = 0U;
    size_t token_length = 0U;

    while (token[token_length] != '\0') {
        token_length += 1U;
    }
    if (token_length == 0U || size < token_length) {
        return 0U;
    }
    for (offset = 0U; offset + token_length <= size; ++offset) {
        if (c2pa_token_at(data, size, offset, token)) {
            count += 1U;
            offset += token_length - 1U;
        }
    }
    return count;
}

static int c2pa_bytes_equal(const unsigned char *data, size_t size, const char *text) {
    size_t index = 0U;

    while (text[index] != '\0') {
        if (index >= size || data[index] != (unsigned char)text[index]) {
            return 0;
        }
        index += 1U;
    }
    return index == size;
}

static int c2pa_text_starts_with(const unsigned char *data, size_t size, const char *text) {
    size_t index = 0U;

    while (text[index] != '\0') {
        if (index >= size || data[index] != (unsigned char)text[index]) {
            return 0;
        }
        index += 1U;
    }
    return 1;
}

static int c2pa_find_label_with_prefix(const unsigned char *data, size_t size, const char *prefix, const unsigned char **label, size_t *label_size) {
    size_t offset;

    for (offset = 0U; offset < size; ++offset) {
        if (c2pa_text_starts_with(data + offset, size - offset, prefix)) {
            size_t end = offset;
            while (end < size && data[end] != 0U) {
                end += 1U;
            }
            *label = data + offset;
            *label_size = end - offset;
            return 1;
        }
    }
    return 0;
}

static void cbor_init(CborReader *reader, const unsigned char *data, size_t size) {
    reader->data = data;
    reader->size = size;
    reader->pos = 0U;
    reader->valid = 1;
}

static int cbor_read_uint_ai(CborReader *reader, unsigned char ai, unsigned long long *value) {
    if (ai < 24U) {
        *value = (unsigned long long)ai;
        return 1;
    }
    if (ai == 24U) {
        if (reader->pos + 1U > reader->size) {
            reader->valid = 0;
            return 0;
        }
        *value = (unsigned long long)reader->data[reader->pos++];
        return 1;
    }
    if (ai == 25U) {
        if (reader->pos + 2U > reader->size) {
            reader->valid = 0;
            return 0;
        }
        *value = (unsigned long long)image_read_u16_be(reader->data + reader->pos);
        reader->pos += 2U;
        return 1;
    }
    if (ai == 26U) {
        if (reader->pos + 4U > reader->size) {
            reader->valid = 0;
            return 0;
        }
        *value = (unsigned long long)image_read_u32_be(reader->data + reader->pos);
        reader->pos += 4U;
        return 1;
    }
    if (ai == 27U) {
        if (reader->pos + 8U > reader->size) {
            reader->valid = 0;
            return 0;
        }
        *value = c2pa_read_u64_be(reader->data + reader->pos);
        reader->pos += 8U;
        return 1;
    }
    reader->valid = 0;
    return 0;
}

static int cbor_read_type(CborReader *reader, unsigned char *major, unsigned long long *value) {
    unsigned char initial;

    if (reader->pos >= reader->size) {
        reader->valid = 0;
        return 0;
    }
    initial = reader->data[reader->pos++];
    *major = (unsigned char)(initial >> 5);
    return cbor_read_uint_ai(reader, (unsigned char)(initial & 31U), value);
}

static int cbor_skip(CborReader *reader, unsigned int depth) {
    unsigned char major;
    unsigned long long value;
    unsigned long long index;

    if (depth > C2PA_MAX_DEPTH || !cbor_read_type(reader, &major, &value)) {
        reader->valid = 0;
        return 0;
    }
    if (major == 0U || major == 1U || major == 7U) {
        return 1;
    }
    if (major == 2U || major == 3U) {
        if (value > (unsigned long long)(reader->size - reader->pos)) {
            reader->valid = 0;
            return 0;
        }
        reader->pos += (size_t)value;
        return 1;
    }
    if (major == 4U) {
        for (index = 0ULL; index < value; ++index) {
            if (!cbor_skip(reader, depth + 1U)) {
                return 0;
            }
        }
        return 1;
    }
    if (major == 5U) {
        for (index = 0ULL; index < value; ++index) {
            if (!cbor_skip(reader, depth + 1U) || !cbor_skip(reader, depth + 1U)) {
                return 0;
            }
        }
        return 1;
    }
    if (major == 6U) {
        return cbor_skip(reader, depth + 1U);
    }
    reader->valid = 0;
    return 0;
}

static int cbor_read_text_ref(CborReader *reader, const unsigned char **text, size_t *text_size) {
    unsigned char major;
    unsigned long long value;

    if (!cbor_read_type(reader, &major, &value) || major != 3U || value > (unsigned long long)(reader->size - reader->pos)) {
        reader->valid = 0;
        return 0;
    }
    *text = reader->data + reader->pos;
    *text_size = (size_t)value;
    reader->pos += (size_t)value;
    return 1;
}

static int cbor_read_bstr_ref(CborReader *reader, const unsigned char **bytes, size_t *bytes_size) {
    unsigned char major;
    unsigned long long value;

    if (!cbor_read_type(reader, &major, &value) || major != 2U || value > (unsigned long long)(reader->size - reader->pos)) {
        reader->valid = 0;
        return 0;
    }
    *bytes = reader->data + reader->pos;
    *bytes_size = (size_t)value;
    reader->pos += (size_t)value;
    return 1;
}

static int cbor_read_int(CborReader *reader, long long *value) {
    unsigned char major;
    unsigned long long raw;

    if (!cbor_read_type(reader, &major, &raw)) {
        return 0;
    }
    if (major == 0U) {
        if (raw > 0x7fffffffffffffffULL) {
            reader->valid = 0;
            return 0;
        }
        *value = (long long)raw;
        return 1;
    }
    if (major == 1U) {
        if (raw > 0x7ffffffffffffffeULL) {
            reader->valid = 0;
            return 0;
        }
        *value = -1LL - (long long)raw;
        return 1;
    }
    reader->valid = 0;
    return 0;
}

static int cbor_validate_complete(const unsigned char *data, size_t size) {
    CborReader reader;

    cbor_init(&reader, data, size);
    if (!cbor_skip(&reader, 0U)) {
        return 0;
    }
    return reader.valid && reader.pos == size;
}

static int c2pa_asn1_tlv_fits(const unsigned char *data, size_t size) {
    size_t length_offset;
    size_t length_size;
    size_t value_size;

    if (size < 2U || data[0] != 0x30U) {
        return 0;
    }
    length_offset = 1U;
    if ((data[length_offset] & 0x80U) == 0U) {
        value_size = (size_t)data[length_offset];
        length_size = 1U;
    } else {
        unsigned int count = (unsigned int)(data[length_offset] & 0x7fU);
        unsigned int index;

        if (count == 0U || count > 4U || 2U + (size_t)count > size) {
            return 0;
        }
        value_size = 0U;
        for (index = 0U; index < count; ++index) {
            value_size = (value_size << 8) | (size_t)data[2U + index];
        }
        length_size = 1U + (size_t)count;
    }
    return 1U + length_size + value_size <= size;
}

static void c2pa_count_x509_blob(const unsigned char *data, size_t size, ImageC2paInfo *info) {
    info->x509_cert_count += 1U;
    if (c2pa_asn1_tlv_fits(data, size)) {
        info->x509_parseable_cert_count += 1U;
    }
}

static void asn1_init(Asn1Reader *reader, const unsigned char *data, size_t size) {
    reader->data = data;
    reader->size = size;
    reader->pos = 0U;
    reader->valid = 1;
}

static int asn1_read_tlv(Asn1Reader *reader, unsigned char *tag, const unsigned char **value, size_t *value_size) {
    size_t length;
    unsigned int length_count;
    unsigned int index;

    if (reader->pos + 2U > reader->size) {
        reader->valid = 0;
        return 0;
    }
    *tag = reader->data[reader->pos++];
    length = (size_t)reader->data[reader->pos++];
    if ((length & 0x80U) != 0U) {
        length_count = (unsigned int)(length & 0x7fU);
        if (length_count == 0U || length_count > 4U || reader->pos + (size_t)length_count > reader->size) {
            reader->valid = 0;
            return 0;
        }
        length = 0U;
        for (index = 0U; index < length_count; ++index) {
            length = (length << 8U) | (size_t)reader->data[reader->pos++];
        }
    }
    if (length > reader->size - reader->pos) {
        reader->valid = 0;
        return 0;
    }
    *value = reader->data + reader->pos;
    *value_size = length;
    reader->pos += length;
    return 1;
}

static int asn1_skip(Asn1Reader *reader) {
    unsigned char tag;
    const unsigned char *value;
    size_t value_size;
    return asn1_read_tlv(reader, &tag, &value, &value_size);
}

static int asn1_read_expected(Asn1Reader *reader, unsigned char expected_tag, Asn1Reader *child) {
    unsigned char tag;
    const unsigned char *value;
    size_t value_size;

    if (!asn1_read_tlv(reader, &tag, &value, &value_size) || tag != expected_tag) {
        reader->valid = 0;
        return 0;
    }
    asn1_init(child, value, value_size);
    return 1;
}

static int c2pa_oid_equal(const unsigned char *value, size_t value_size, const unsigned char *oid, size_t oid_size) {
    return value_size == oid_size && image_byte_arrays_equal(value, oid, oid_size);
}

static int c2pa_x509_extract_p256_public_key(const unsigned char *cert, size_t cert_size, unsigned char public_key[CRYPTO_P256_UNCOMPRESSED_PUBLIC_KEY_SIZE]) {
    static const unsigned char oid_ec_public_key[] = {0x2aU, 0x86U, 0x48U, 0xceU, 0x3dU, 0x02U, 0x01U};
    static const unsigned char oid_prime256v1[] = {0x2aU, 0x86U, 0x48U, 0xceU, 0x3dU, 0x03U, 0x01U, 0x07U};
    Asn1Reader cert_reader;
    Asn1Reader cert_seq;
    Asn1Reader tbs;
    Asn1Reader spki;
    Asn1Reader alg;
    unsigned char tag;
    const unsigned char *value;
    size_t value_size;
    int saw_ec_public_key = 0;
    int saw_prime256v1 = 0;

    asn1_init(&cert_reader, cert, cert_size);
    if (!asn1_read_expected(&cert_reader, 0x30U, &cert_seq) || !asn1_read_expected(&cert_seq, 0x30U, &tbs)) {
        return 0;
    }
    if (tbs.pos < tbs.size && tbs.data[tbs.pos] == 0xa0U) {
        if (!asn1_skip(&tbs)) return 0;
    }
    if (!asn1_skip(&tbs) || !asn1_skip(&tbs) || !asn1_skip(&tbs) || !asn1_skip(&tbs) || !asn1_skip(&tbs)) {
        return 0;
    }
    if (!asn1_read_expected(&tbs, 0x30U, &spki) || !asn1_read_expected(&spki, 0x30U, &alg)) {
        return 0;
    }
    if (!asn1_read_tlv(&alg, &tag, &value, &value_size) || tag != 0x06U || !c2pa_oid_equal(value, value_size, oid_ec_public_key, sizeof(oid_ec_public_key))) {
        return 0;
    }
    saw_ec_public_key = 1;
    if (!asn1_read_tlv(&alg, &tag, &value, &value_size) || tag != 0x06U || !c2pa_oid_equal(value, value_size, oid_prime256v1, sizeof(oid_prime256v1))) {
        return 0;
    }
    saw_prime256v1 = 1;
    if (!asn1_read_tlv(&spki, &tag, &value, &value_size) || tag != 0x03U || value_size != 66U || value[0] != 0U || value[1] != 0x04U) {
        return 0;
    }
    if (!saw_ec_public_key || !saw_prime256v1) {
        return 0;
    }
    memcpy(public_key, value + 1U, CRYPTO_P256_UNCOMPRESSED_PUBLIC_KEY_SIZE);
    return 1;
}

static const char *c2pa_cose_alg_name(long long alg) {
    if (alg == C2PA_COSE_ALG_ES256) {
        return "ES256";
    }
    if (alg == C2PA_COSE_ALG_EDDSA) {
        return "EdDSA";
    }
    if (alg == C2PA_COSE_ALG_PS256) {
        return "PS256";
    }
    return "unknown";
}

static void c2pa_remember_leaf_cert(C2paCoseInfo *cose, const unsigned char *bytes, size_t bytes_size) {
    if (cose != 0 && !cose->has_leaf_cert) {
        cose->leaf_cert = bytes;
        cose->leaf_cert_size = bytes_size;
        cose->has_leaf_cert = 1;
    }
}

static void c2pa_scan_x5chain_value(CborReader *reader, ImageC2paInfo *info, C2paCoseInfo *cose) {
    size_t saved = reader->pos;
    unsigned char major;
    unsigned long long count;
    unsigned long long index;
    const unsigned char *bytes;
    size_t bytes_size;

    if (!cbor_read_type(reader, &major, &count)) {
        return;
    }
    if (major == 2U) {
        if (count <= (unsigned long long)(reader->size - reader->pos)) {
            c2pa_count_x509_blob(reader->data + reader->pos, (size_t)count, info);
            c2pa_remember_leaf_cert(cose, reader->data + reader->pos, (size_t)count);
            reader->pos += (size_t)count;
            return;
        }
    } else if (major == 4U) {
        for (index = 0ULL; index < count; ++index) {
            if (cbor_read_bstr_ref(reader, &bytes, &bytes_size)) {
                c2pa_count_x509_blob(bytes, bytes_size, info);
                c2pa_remember_leaf_cert(cose, bytes, bytes_size);
            } else {
                reader->pos = saved;
                (void)cbor_skip(reader, 0U);
                return;
            }
        }
        return;
    }
    reader->pos = saved;
    (void)cbor_skip(reader, 0U);
}

static void c2pa_parse_cose_protected(const unsigned char *data, size_t size, ImageC2paInfo *info, C2paCoseInfo *cose) {
    CborReader reader;
    unsigned char major;
    unsigned long long pair_count;
    unsigned long long index;

    cbor_init(&reader, data, size);
    if (!cbor_read_type(&reader, &major, &pair_count) || major != 5U) {
        return;
    }
    for (index = 0ULL; index < pair_count; ++index) {
        long long key;
        if (!cbor_read_int(&reader, &key)) {
            return;
        }
        if (key == 1LL) {
            long long alg;
            if (!cbor_read_int(&reader, &alg)) {
                return;
            }
            if (cose != 0) {
                cose->alg = alg;
            }
            info->signature_algorithm = c2pa_cose_alg_name(alg);
            if (alg == C2PA_COSE_ALG_ES256 || alg == C2PA_COSE_ALG_EDDSA || alg == C2PA_COSE_ALG_PS256) {
                info->signature_checked = 1;
            }
        } else if (key == 33LL) {
            c2pa_scan_x5chain_value(&reader, info, cose);
        } else {
            if (!cbor_skip(&reader, 0U)) {
                return;
            }
        }
    }
}

static size_t c2pa_cbor_bstr_header_size(size_t size) {
    if (size < 24U) return 1U;
    if (size <= 0xffU) return 2U;
    if (size <= 0xffffU) return 3U;
    return 5U;
}

static size_t c2pa_write_cbor_bstr_header(unsigned char *out, size_t size) {
    if (size < 24U) {
        out[0] = (unsigned char)(0x40U | (unsigned char)size);
        return 1U;
    }
    if (size <= 0xffU) {
        out[0] = 0x58U;
        out[1] = (unsigned char)size;
        return 2U;
    }
    if (size <= 0xffffU) {
        out[0] = 0x59U;
        out[1] = (unsigned char)(size >> 8U);
        out[2] = (unsigned char)size;
        return 3U;
    }
    out[0] = 0x5aU;
    out[1] = (unsigned char)(size >> 24U);
    out[2] = (unsigned char)(size >> 16U);
    out[3] = (unsigned char)(size >> 8U);
    out[4] = (unsigned char)size;
    return 5U;
}

static int c2pa_verify_cose_es256(const C2paCoseInfo *cose,
                                  const unsigned char *protected_bytes,
                                  size_t protected_size,
                                  const unsigned char *payload_item,
                                  size_t payload_item_size,
                                  const unsigned char *signature,
                                  size_t signature_size) {
    static const unsigned char context[] = {'S', 'i', 'g', 'n', 'a', 't', 'u', 'r', 'e', '1'};
    unsigned char public_key[CRYPTO_P256_UNCOMPRESSED_PUBLIC_KEY_SIZE];
    unsigned char digest[CRYPTO_SHA256_DIGEST_SIZE];
    unsigned char *sig_structure;
    size_t total_size;
    size_t offset = 0U;
    size_t header_size;
    int ok;

    if (cose == 0 || cose->alg != C2PA_COSE_ALG_ES256 || !cose->has_leaf_cert || signature_size != CRYPTO_P256_ECDSA_SIGNATURE_SIZE) {
        return 0;
    }
    if (!c2pa_x509_extract_p256_public_key(cose->leaf_cert, cose->leaf_cert_size, public_key)) {
        return 0;
    }
    total_size = 1U + 1U + sizeof(context) +
                 c2pa_cbor_bstr_header_size(protected_size) + protected_size +
                 1U + payload_item_size;
    sig_structure = (unsigned char *)rt_malloc(total_size == 0U ? 1U : total_size);
    if (sig_structure == 0) {
        return 0;
    }
    sig_structure[offset++] = 0x84U;
    sig_structure[offset++] = 0x6aU;
    memcpy(sig_structure + offset, context, sizeof(context));
    offset += sizeof(context);
    header_size = c2pa_write_cbor_bstr_header(sig_structure + offset, protected_size);
    offset += header_size;
    memcpy(sig_structure + offset, protected_bytes, protected_size);
    offset += protected_size;
    sig_structure[offset++] = 0x40U;
    memcpy(sig_structure + offset, payload_item, payload_item_size);
    offset += payload_item_size;
    if (offset != total_size) {
        rt_free(sig_structure);
        return 0;
    }
    crypto_sha256_hash(sig_structure, total_size, digest);
    ok = crypto_p256_ecdsa_sha256_verify(public_key, digest, signature);
    rt_free(sig_structure);
    return ok;
}

static int c2pa_verify_cose_es256_detached_claims(const C2paCoseInfo *cose,
                                                  const unsigned char *protected_bytes,
                                                  size_t protected_size,
                                                  const unsigned char *signature,
                                                  size_t signature_size,
                                                  const C2paContext *ctx) {
    unsigned int claim_index;
    size_t token_offset;

    if (ctx->claim_range_count > 0U) {
        if (ctx->preferred_claim_index < ctx->claim_range_count) {
            const C2paRange *range = &ctx->claim_ranges[ctx->preferred_claim_index];
            unsigned char header[5];
            unsigned char *payload_item;
            size_t header_size = c2pa_write_cbor_bstr_header(header, range->length);
            int ok;

            payload_item = (unsigned char *)rt_malloc(header_size + range->length);
            if (payload_item == 0) {
                return 0;
            }
            memcpy(payload_item, header, header_size);
            memcpy(payload_item + header_size, ctx->full_data + range->start, range->length);
            ok = c2pa_verify_cose_es256(cose, protected_bytes, protected_size, payload_item, header_size + range->length, signature, signature_size);
            rt_free(payload_item);
            if (ok) {
                return 1;
            }
        }
        for (claim_index = 0U; claim_index < ctx->claim_range_count; ++claim_index) {
            const C2paRange *range;
            unsigned char header[5];
            unsigned char *payload_item;
            size_t header_size;
            int ok;

            if (claim_index == ctx->preferred_claim_index) {
                continue;
            }
            range = &ctx->claim_ranges[claim_index];
            header_size = c2pa_write_cbor_bstr_header(header, range->length);
            payload_item = (unsigned char *)rt_malloc(header_size + range->length);
            if (payload_item == 0) {
                return 0;
            }
            memcpy(payload_item, header, header_size);
            memcpy(payload_item + header_size, ctx->full_data + range->start, range->length);
            ok = c2pa_verify_cose_es256(cose, protected_bytes, protected_size, payload_item, header_size + range->length, signature, signature_size);
            rt_free(payload_item);
            if (ok) {
                return 1;
            }
        }
        if (ctx->claim_ranges_complete) {
            return 0;
        }
    }

    for (token_offset = 0U; token_offset + 10U < ctx->full_size; ++token_offset) {
        size_t search;
        if (!c2pa_text_starts_with(ctx->full_data + token_offset, ctx->full_size - token_offset, "c2pa.claim")) {
            continue;
        }
        search = token_offset;
        while (search + 8U <= ctx->full_size && search < token_offset + 256U) {
            if (image_bytes_equal(ctx->full_data + search, "cbor", 4U) && search >= 4U) {
                size_t box_start = search - 4U;
                unsigned int box_size = image_read_u32_be(ctx->full_data + box_start);
                size_t claim_size;
                unsigned char header[5];
                unsigned char *payload_item;
                size_t header_size;
                int ok;

                if (box_size < 8U || box_start + (size_t)box_size > ctx->full_size) {
                    break;
                }
                claim_size = (size_t)box_size - 8U;
                header_size = c2pa_write_cbor_bstr_header(header, claim_size);
                payload_item = (unsigned char *)rt_malloc(header_size + claim_size);
                if (payload_item == 0) {
                    return 0;
                }
                memcpy(payload_item, header, header_size);
                memcpy(payload_item + header_size, ctx->full_data + box_start + 8U, claim_size);
                ok = c2pa_verify_cose_es256(cose, protected_bytes, protected_size, payload_item, header_size + claim_size, signature, signature_size);
                rt_free(payload_item);
                if (ok) {
                    return 1;
                }
                break;
            }
            search += 1U;
        }
    }
    return 0;
}

static void c2pa_parse_cose_sign1(const unsigned char *data, size_t size, ImageC2paInfo *info, const C2paContext *ctx) {
    CborReader reader;
    unsigned char major;
    unsigned long long value;
    const unsigned char *protected_bytes;
    size_t protected_size;
    const unsigned char *payload_item;
    size_t payload_item_size;
    const unsigned char *signature;
    size_t signature_size;
    C2paCoseInfo cose;

    cose.alg = 0LL;
    cose.leaf_cert = 0;
    cose.leaf_cert_size = 0U;
    cose.has_leaf_cert = 0;

    cbor_init(&reader, data, size);
    if (!cbor_read_type(&reader, &major, &value)) {
        return;
    }
    if (major == 6U) {
        if (value != 18ULL) {
            return;
        }
        if (!cbor_read_type(&reader, &major, &value)) {
            return;
        }
    }
    if (major != 4U || value != 4ULL) {
        return;
    }
    if (!cbor_read_bstr_ref(&reader, &protected_bytes, &protected_size)) {
        return;
    }
    c2pa_parse_cose_protected(protected_bytes, protected_size, info, &cose);
    if (!cbor_skip(&reader, 0U)) {
        return;
    }
    payload_item = reader.data + reader.pos;
    if (!cbor_skip(&reader, 0U)) {
        return;
    }
    payload_item_size = (size_t)(reader.data + reader.pos - payload_item);
    if (!cbor_read_bstr_ref(&reader, &signature, &signature_size)) {
        return;
    }
    if (reader.valid && reader.pos == reader.size) {
        info->cose_signature_count += 1U;
        info->cose_valid = 1;
        if (cose.alg == C2PA_COSE_ALG_ES256) {
            int verified = 0;
            info->signature_checked = 1;
            info->signature_supported = 1;
            if ((!(payload_item_size == 1U && payload_item[0] == 0xf6U) &&
                 c2pa_verify_cose_es256(&cose, protected_bytes, protected_size, payload_item, payload_item_size, signature, signature_size)) ||
                (ctx != 0 && c2pa_verify_cose_es256_detached_claims(&cose, protected_bytes, protected_size, signature, signature_size, ctx))) {
                verified = 1;
            }
            if (verified) {
                info->signature_verified_count += 1U;
            } else {
                info->signature_invalid_count += 1U;
            }
            info->signature_valid = info->signature_verified_count > 0U && info->signature_invalid_count == 0U;
        }
    }
}

static void c2pa_hash_assertion_init(C2paHashAssertion *assertion) {
    unsigned int index;

    assertion->present = 0;
    assertion->alg_sha256 = 0;
    assertion->exclusion_count = 0U;
    for (index = 0U; index < CRYPTO_SHA256_DIGEST_SIZE; ++index) {
        assertion->hash[index] = 0U;
    }
}

static unsigned int c2pa_count_embedded_validation_failures(const unsigned char *data, size_t size) {
    static const char key[] = "failure";
    size_t key_size = sizeof(key) - 1U;
    size_t offset;
    unsigned int failures = 0U;

    if (size < key_size) {
        return 0U;
    }
    for (offset = 0U; offset + key_size <= size; ++offset) {
        if (c2pa_token_at(data, size, offset, key)) {
            CborReader reader;
            unsigned char major;
            unsigned long long count;

            cbor_init(&reader, data, size);
            reader.pos = offset + key_size;
            if (cbor_read_type(&reader, &major, &count) && major == 4U && count > 0ULL) {
                failures += (unsigned int)count;
            }
        }
    }
    return failures;
}

static void c2pa_parse_exclusion_map(CborReader *reader, C2paHashAssertion *assertion) {
    unsigned char major;
    unsigned long long pair_count;
    unsigned long long index;
    size_t start = 0U;
    size_t length = 0U;
    int has_start = 0;
    int has_length = 0;

    if (!cbor_read_type(reader, &major, &pair_count) || major != 5U) {
        reader->valid = 0;
        return;
    }
    for (index = 0ULL; index < pair_count; ++index) {
        const unsigned char *key;
        size_t key_size;
        long long value;

        if (!cbor_read_text_ref(reader, &key, &key_size)) {
            return;
        }
        if (c2pa_bytes_equal(key, key_size, "start")) {
            if (!cbor_read_int(reader, &value) || value < 0LL) {
                return;
            }
            start = (size_t)value;
            has_start = 1;
        } else if (c2pa_bytes_equal(key, key_size, "length")) {
            if (!cbor_read_int(reader, &value) || value < 0LL) {
                return;
            }
            length = (size_t)value;
            has_length = 1;
        } else {
            if (!cbor_skip(reader, 0U)) {
                return;
            }
        }
    }
    if (has_start && has_length && assertion->exclusion_count < C2PA_MAX_EXCLUSIONS) {
        assertion->exclusions[assertion->exclusion_count].start = start;
        assertion->exclusions[assertion->exclusion_count].length = length;
        assertion->exclusion_count += 1U;
    }
}

static void c2pa_parse_exclusions(CborReader *reader, C2paHashAssertion *assertion) {
    unsigned char major;
    unsigned long long count;
    unsigned long long index;

    if (!cbor_read_type(reader, &major, &count) || major != 4U) {
        reader->valid = 0;
        return;
    }
    for (index = 0ULL; index < count; ++index) {
        c2pa_parse_exclusion_map(reader, assertion);
        if (!reader->valid) {
            return;
        }
    }
}

static int c2pa_parse_hash_assertion(const unsigned char *data, size_t size, C2paHashAssertion *assertion) {
    CborReader reader;
    unsigned char major;
    unsigned long long pair_count;
    unsigned long long index;

    c2pa_hash_assertion_init(assertion);
    cbor_init(&reader, data, size);
    if (!cbor_read_type(&reader, &major, &pair_count) || major != 5U) {
        return 0;
    }
    for (index = 0ULL; index < pair_count; ++index) {
        const unsigned char *key;
        size_t key_size;

        if (!cbor_read_text_ref(&reader, &key, &key_size)) {
            return 0;
        }
        if (c2pa_bytes_equal(key, key_size, "alg")) {
            const unsigned char *text;
            size_t text_size;
            if (!cbor_read_text_ref(&reader, &text, &text_size)) {
                return 0;
            }
            if (c2pa_bytes_equal(text, text_size, "sha256")) {
                assertion->alg_sha256 = 1;
            }
        } else if (c2pa_bytes_equal(key, key_size, "hash")) {
            const unsigned char *bytes;
            size_t bytes_size;
            if (!cbor_read_bstr_ref(&reader, &bytes, &bytes_size)) {
                return 0;
            }
            if (bytes_size == CRYPTO_SHA256_DIGEST_SIZE) {
                unsigned int byte_index;
                for (byte_index = 0U; byte_index < CRYPTO_SHA256_DIGEST_SIZE; ++byte_index) {
                    assertion->hash[byte_index] = bytes[byte_index];
                }
                assertion->present = 1;
            }
        } else if (c2pa_bytes_equal(key, key_size, "exclusions")) {
            size_t saved_pos = reader.pos;
            c2pa_parse_exclusions(&reader, assertion);
            if (!reader.valid) {
                reader.pos = saved_pos;
                reader.valid = 1;
                if (!cbor_skip(&reader, 0U)) {
                    return 0;
                }
            }
        } else {
            if (!cbor_skip(&reader, 0U)) {
                return 0;
            }
        }
    }
    return reader.valid && reader.pos == reader.size && assertion->present && assertion->alg_sha256;
}

static int c2pa_range_contains(const C2paHashAssertion *assertion, size_t offset) {
    unsigned int index;

    for (index = 0U; index < assertion->exclusion_count; ++index) {
        size_t start = assertion->exclusions[index].start;
        size_t length = assertion->exclusions[index].length;
        if (length > 0U && offset >= start && offset - start < length) {
            return 1;
        }
    }
    return 0;
}

static int c2pa_context_range_contains(const C2paContext *ctx, size_t offset) {
    unsigned int index;

    for (index = 0U; index < ctx->carrier_range_count; ++index) {
        size_t start = ctx->carrier_ranges[index].start;
        size_t length = ctx->carrier_ranges[index].length;
        if (length > 0U && offset >= start && offset - start < length) {
            return 1;
        }
    }
    return 0;
}

static void c2pa_context_add_range(C2paContext *ctx, size_t start, size_t length) {
    if (length > 0U && ctx->carrier_range_count < C2PA_MAX_EXCLUSIONS) {
        ctx->carrier_ranges[ctx->carrier_range_count].start = start;
        ctx->carrier_ranges[ctx->carrier_range_count].length = length;
        ctx->carrier_range_count += 1U;
    }
}

static void c2pa_context_add_claim_range(C2paContext *ctx, const unsigned char *data, size_t length) {
    size_t start;
    unsigned int index;

    if (ctx == 0 || data == 0 || length == 0U || ctx->claim_range_count >= C2PA_MAX_CLAIMS ||
        data < ctx->full_data || data > ctx->full_data + ctx->full_size || length > (size_t)(ctx->full_data + ctx->full_size - data)) {
        return;
    }
    start = (size_t)(data - ctx->full_data);
    for (index = 0U; index < ctx->claim_range_count; ++index) {
        if (ctx->claim_ranges[index].start == start && ctx->claim_ranges[index].length == length) {
            ctx->preferred_claim_index = index;
            return;
        }
    }
    ctx->claim_ranges[ctx->claim_range_count].start = start;
    ctx->claim_ranges[ctx->claim_range_count].length = length;
    ctx->preferred_claim_index = ctx->claim_range_count;
    ctx->claim_range_count += 1U;
}

static void c2pa_precollect_claim_ranges(C2paContext *ctx) {
    size_t token_offset;

    for (token_offset = 0U; token_offset + 10U < ctx->full_size; ++token_offset) {
        size_t search;
        if (!c2pa_text_starts_with(ctx->full_data + token_offset, ctx->full_size - token_offset, "c2pa.claim")) {
            continue;
        }
        search = token_offset;
        while (search + 8U <= ctx->full_size && search < token_offset + 256U) {
            if (image_bytes_equal(ctx->full_data + search, "cbor", 4U) && search >= 4U) {
                size_t box_start = search - 4U;
                unsigned int box_size = image_read_u32_be(ctx->full_data + box_start);

                if (box_size >= 8U && box_start + (size_t)box_size <= ctx->full_size) {
                    c2pa_context_add_claim_range(ctx, ctx->full_data + box_start + 8U, (size_t)box_size - 8U);
                }
                break;
            }
            search += 1U;
        }
    }
    ctx->claim_ranges_complete = 1;
    ctx->preferred_claim_index = C2PA_MAX_CLAIMS;
}

static int c2pa_digest_occurs(const unsigned char *data, size_t size, const unsigned char digest[CRYPTO_SHA256_DIGEST_SIZE]) {
    size_t offset;

    for (offset = 0U; offset + CRYPTO_SHA256_DIGEST_SIZE <= size; ++offset) {
        if (image_byte_arrays_equal(data + offset, digest, CRYPTO_SHA256_DIGEST_SIZE)) {
            return 1;
        }
    }
    return 0;
}

static int c2pa_hash_matches(const C2paHashAssertion *assertion, const unsigned char *data, size_t size) {
    CryptoSha256Context ctx;
    unsigned char digest[CRYPTO_SHA256_DIGEST_SIZE];
    size_t offset = 0U;
    unsigned int index;

    crypto_sha256_init(&ctx);
    while (offset < size) {
        size_t start = offset;
        while (offset < size && !c2pa_range_contains(assertion, offset)) {
            offset += 1U;
        }
        if (offset > start) {
            crypto_sha256_update(&ctx, data + start, offset - start);
        }
        while (offset < size && c2pa_range_contains(assertion, offset)) {
            offset += 1U;
        }
    }
    crypto_sha256_final(&ctx, digest);
    for (index = 0U; index < CRYPTO_SHA256_DIGEST_SIZE; ++index) {
        if (digest[index] != assertion->hash[index]) {
            if (c2pa_digest_occurs(data, size, digest)) {
                return 1;
            }
            return 0;
        }
    }
    return 1;
}

static int c2pa_carrier_hash_matches(const C2paContext *ctx) {
    CryptoSha256Context sha;
    unsigned char digest[CRYPTO_SHA256_DIGEST_SIZE];
    size_t offset = 0U;

    if (ctx->carrier_range_count == 0U) {
        return 0;
    }
    crypto_sha256_init(&sha);
    while (offset < ctx->full_size) {
        size_t start = offset;
        while (offset < ctx->full_size && !c2pa_context_range_contains(ctx, offset)) {
            offset += 1U;
        }
        if (offset > start) {
            crypto_sha256_update(&sha, ctx->full_data + start, offset - start);
        }
        while (offset < ctx->full_size && c2pa_context_range_contains(ctx, offset)) {
            offset += 1U;
        }
    }
    crypto_sha256_final(&sha, digest);
    return c2pa_digest_occurs(ctx->full_data, ctx->full_size, digest);
}

static int c2pa_jumd_label(const unsigned char *data, size_t size, const unsigned char **label, size_t *label_size) {
    *label = 0;
    *label_size = 0U;
    if (c2pa_find_label_with_prefix(data, size, "urn:c2pa:", label, label_size)) return 1;
    if (c2pa_find_label_with_prefix(data, size, "c2pa.", label, label_size)) return 1;
    if (c2pa_find_label_with_prefix(data, size, "c2pa", label, label_size)) return 1;
    if (c2pa_find_label_with_prefix(data, size, "cbor", label, label_size)) return 1;
    return 0;
}

static void c2pa_count_label(const unsigned char *label, size_t label_size, ImageC2paInfo *info, C2paContext *ctx, unsigned int depth) {
    if (label == 0 || label_size == 0U) {
        return;
    }
    if (c2pa_text_starts_with(label, label_size, "urn:c2pa:")) {
        info->manifest_count += 1U;
        if (depth == 1U) {
            ctx->top_manifest_index += 1U;
            if (ctx->top_manifest_index == 1U) {
                ctx->active_manifest_depth = depth;
                ctx->in_active_manifest = 1;
            }
        }
    } else if (c2pa_text_starts_with(label, label_size, "c2pa.claim")) {
        info->claim_count += 1U;
    } else if (c2pa_text_starts_with(label, label_size, "c2pa.assertions")) {
        info->assertion_store_count += 1U;
    } else if (c2pa_text_starts_with(label, label_size, "c2pa.signature")) {
        info->signature_count += 1U;
    } else if (c2pa_text_starts_with(label, label_size, "c2pa.ingredient")) {
        info->ingredient_count += 1U;
    } else if (c2pa_text_starts_with(label, label_size, "c2pa.")) {
        info->assertion_count += 1U;
    }
}

static void c2pa_parse_boxes(const unsigned char *data,
                             size_t size,
                             unsigned int depth,
                             const unsigned char *parent_label,
                             size_t parent_label_size,
                             ImageC2paInfo *info,
                             C2paContext *ctx) {
    size_t offset = 0U;

    if (depth > C2PA_MAX_DEPTH) {
        info->invalid_box_count += 1U;
        return;
    }
    while (offset + 8U <= size) {
        unsigned long long box_size = (unsigned long long)image_read_u32_be(data + offset);
        const unsigned char *type = data + offset + 4U;
        size_t header_size = 8U;
        size_t payload_offset;
        size_t payload_size;
        const unsigned char *label = 0;
        size_t label_size = 0U;
        int was_in_active = ctx->in_active_manifest;

        if (box_size == 1ULL) {
            if (offset + 16U > size) {
                info->invalid_box_count += 1U;
                return;
            }
            box_size = c2pa_read_u64_be(data + offset + 8U);
            header_size = 16U;
        } else if (box_size == 0ULL) {
            box_size = (unsigned long long)(size - offset);
        }
        if (box_size < (unsigned long long)header_size || box_size > (unsigned long long)(size - offset)) {
            info->invalid_box_count += 1U;
            return;
        }
        payload_offset = offset + header_size;
        payload_size = (size_t)box_size - header_size;
        info->box_count += 1U;

        if (image_bytes_equal(type, "jumb", 4U)) {
            info->jumbf_box_count += 1U;
            if (payload_size >= 8U && image_bytes_equal(data + payload_offset + 4U, "jumd", 4U)) {
                unsigned long long jumd_size = (unsigned long long)image_read_u32_be(data + payload_offset);
                if (jumd_size >= 8ULL && jumd_size <= (unsigned long long)payload_size) {
                    (void)c2pa_jumd_label(data + payload_offset + 8U, (size_t)jumd_size - 8U, &label, &label_size);
                    c2pa_count_label(label, label_size, info, ctx, depth);
                }
            }
            c2pa_parse_boxes(data + payload_offset, payload_size, depth + 1U, label, label_size, info, ctx);
            ctx->in_active_manifest = was_in_active;
        } else if (image_bytes_equal(type, "cbor", 4U)) {
            info->cbor_box_count += 1U;
            if (cbor_validate_complete(data + payload_offset, payload_size)) {
                info->cbor_valid_count += 1U;
                info->cbor_valid = 1;
            }
            if (parent_label != 0 && c2pa_text_starts_with(parent_label, parent_label_size, "c2pa.signature")) {
                c2pa_parse_cose_sign1(data + payload_offset, payload_size, info, ctx);
            } else if (parent_label != 0 && c2pa_text_starts_with(parent_label, parent_label_size, "c2pa.claim")) {
                c2pa_context_add_claim_range(ctx, data + payload_offset, payload_size);
            } else if (parent_label != 0 && c2pa_text_starts_with(parent_label, parent_label_size, "c2pa.hash.data")) {
                C2paHashAssertion assertion;
                if (c2pa_parse_hash_assertion(data + payload_offset, payload_size, &assertion)) {
                    info->content_hash_checked = 1;
                    if (!info->content_hash_matched && c2pa_hash_matches(&assertion, ctx->full_data, ctx->full_size)) {
                        info->content_hash_matched = 1;
                    }
                }
            } else if (parent_label != 0 && c2pa_text_starts_with(parent_label, parent_label_size, "c2pa.ingredient")) {
                info->validation_failure_count += c2pa_count_embedded_validation_failures(data + payload_offset, payload_size);
            }
        }
        offset += (size_t)box_size;
    }
    if (offset != size) {
        info->invalid_box_count += 1U;
    }
}

static size_t c2pa_find_box_start(const unsigned char *data, size_t size) {
    size_t offset;

    for (offset = 4U; offset + 4U <= size; ++offset) {
        if (image_bytes_equal(data + offset, "jumb", 4U)) {
            return offset - 4U;
        }
    }
    return size;
}

static void c2pa_parse_jumbf_payload(const unsigned char *payload, size_t payload_size, ImageC2paInfo *info, C2paContext *ctx) {
    size_t box_start = c2pa_find_box_start(payload, payload_size);

    if (box_start < payload_size) {
        unsigned int previous_boxes = info->box_count;
        c2pa_parse_boxes(payload + box_start, payload_size - box_start, 0U, 0, 0U, info, ctx);
        if (info->box_count > previous_boxes && info->invalid_box_count == 0U) {
            info->jumbf_valid = 1;
        }
    }
}

static int c2pa_scan_png_carriers(const unsigned char *data, size_t size, ImageC2paInfo *info, C2paContext *ctx) {
    static const unsigned char signature[8] = {0x89U, 'P', 'N', 'G', '\r', '\n', 0x1aU, '\n'};
    size_t offset = 8U;

    if (size < 8U || !image_byte_arrays_equal(data, signature, sizeof(signature))) {
        return 0;
    }
    while (offset + 12U <= size) {
        unsigned int length = image_read_u32_be(data + offset);
        const unsigned char *type = data + offset + 4U;
        size_t payload = offset + 8U;

        if ((size_t)length > size - offset - 12U) {
            break;
        }
        if (image_bytes_equal(type, "caBX", 4U)) {
            info->carrier_count += 1U;
            info->carrier = info->carrier != 0 ? "mixed" : "PNG caBX";
            c2pa_context_add_range(ctx, offset, (size_t)length + 12U);
            c2pa_parse_jumbf_payload(data + payload, (size_t)length, info, ctx);
            if (info->jumbf_box_count > 0U) {
                info->recognized_carrier_count += 1U;
            }
        }
        offset += 12U + (size_t)length;
        if (image_bytes_equal(type, "IEND", 4U)) {
            break;
        }
    }
    return info->carrier_count > 0U;
}

static int c2pa_scan_jpeg_carriers(const unsigned char *data, size_t size, ImageC2paInfo *info, C2paContext *ctx) {
    size_t offset = 2U;

    if (size < 3U || data[0] != 0xffU || data[1] != 0xd8U || data[2] != 0xffU) {
        return 0;
    }
    while (offset + 3U < size) {
        unsigned char marker;
        unsigned int segment_size;
        size_t payload;
        size_t payload_size;

        size_t marker_start;

        while (offset < size && data[offset] != 0xffU) {
            offset += 1U;
        }
        marker_start = offset;
        while (offset < size && data[offset] == 0xffU) {
            offset += 1U;
        }
        if (offset >= size) {
            break;
        }
        marker = data[offset++];
        if (marker == 0xd9U || marker == 0xdaU) {
            break;
        }
        if (marker == 0x01U || (marker >= 0xd0U && marker <= 0xd7U)) {
            continue;
        }
        if (offset + 2U > size) {
            break;
        }
        segment_size = image_read_u16_be(data + offset);
        if (segment_size < 2U || (size_t)segment_size > size - offset) {
            break;
        }
        payload = offset + 2U;
        payload_size = (size_t)segment_size - 2U;
        if (marker == 0xebU && payload_size >= 12U &&
            image_bytes_equal(data + payload, "JP", 2U) &&
            data[payload + 2U] == 0U && data[payload + 3U] == 1U) {
            info->carrier_count += 1U;
            info->carrier = info->carrier != 0 ? "mixed" : "JPEG APP11 JUMBF";
            c2pa_context_add_range(ctx, marker_start, offset + (size_t)segment_size - marker_start);
            c2pa_parse_jumbf_payload(data + payload, payload_size, info, ctx);
            if (info->jumbf_box_count > 0U) {
                info->recognized_carrier_count += 1U;
            }
        }
        offset += (size_t)segment_size;
    }
    return info->carrier_count > 0U;
}

void image_c2pa_info_init(ImageC2paInfo *info) {
    if (info == 0) {
        return;
    }
    info->present = 0;
    info->has_manifest_store = 0;
    info->jumbf_valid = 0;
    info->cbor_valid = 0;
    info->cose_valid = 0;
    info->content_hash_checked = 0;
    info->content_hash_matched = 0;
    info->content_hash_mismatched = 0;
    info->signature_checked = 0;
    info->signature_supported = 0;
    info->signature_valid = 0;
    info->trust_checked = 0;
    info->trust_supported = 0;
    info->trust_valid = 0;
    info->carrier = 0;
    info->signature_algorithm = 0;
    info->carrier_count = 0U;
    info->recognized_carrier_count = 0U;
    info->box_count = 0U;
    info->invalid_box_count = 0U;
    info->jumbf_box_count = 0U;
    info->cbor_box_count = 0U;
    info->cbor_valid_count = 0U;
    info->manifest_count = 0U;
    info->claim_count = 0U;
    info->assertion_store_count = 0U;
    info->assertion_count = 0U;
    info->signature_count = 0U;
    info->cose_signature_count = 0U;
    info->signature_verified_count = 0U;
    info->signature_invalid_count = 0U;
    info->x509_cert_count = 0U;
    info->x509_parseable_cert_count = 0U;
    info->validation_failure_count = 0U;
    info->ingredient_count = 0U;
    info->status = "absent";
}

int image_c2pa_analyze_ex(const unsigned char *data, size_t size, const ImageC2paOptions *options, ImageC2paInfo *info) {
    C2paContext ctx;
    int carrier_found;

    if (info == 0) {
        return -1;
    }
    image_c2pa_info_init(info);
    if (data == 0 || size == 0U) {
        return -1;
    }

    ctx.full_data = data;
    ctx.full_size = size;
    ctx.carrier_range_count = 0U;
    ctx.claim_range_count = 0U;
    ctx.preferred_claim_index = C2PA_MAX_CLAIMS;
    ctx.claim_ranges_complete = 0;
    ctx.top_manifest_index = 0U;
    ctx.active_manifest_depth = 0U;
    ctx.in_active_manifest = 0;
    ctx.trust_validation_enabled = options != 0 && options->trust_validation;

    c2pa_precollect_claim_ranges(&ctx);

    carrier_found = c2pa_scan_png_carriers(data, size, info, &ctx);
    carrier_found = c2pa_scan_jpeg_carriers(data, size, info, &ctx) || carrier_found;

    if (!carrier_found && c2pa_count_token(data, size, "jumb") > 0U && c2pa_count_token(data, size, "c2pa") > 0U) {
        info->carrier = "embedded JUMBF";
        info->carrier_count = 1U;
        c2pa_parse_jumbf_payload(data, size, info, &ctx);
        info->recognized_carrier_count = info->jumbf_box_count > 0U ? 1U : 0U;
        carrier_found = 1;
    }

    if (!carrier_found && c2pa_count_token(data, size, "c2pa") == 0U) {
        return 0;
    }

    if (info->manifest_count == 0U) {
        info->manifest_count = c2pa_count_token(data, size, "urn:c2pa:");
    }
    if (info->claim_count == 0U) {
        info->claim_count = c2pa_count_token(data, size, "c2pa.claim");
    }
    if (info->signature_count == 0U) {
        info->signature_count = c2pa_count_token(data, size, "c2pa.signature");
    }
    if (info->assertion_store_count == 0U) {
        info->assertion_store_count = c2pa_count_token(data, size, "c2pa.assertions");
    }
    if (info->ingredient_count == 0U) {
        info->ingredient_count = c2pa_count_token(data, size, "c2pa.ingredient");
    }
    info->assertion_count = c2pa_count_token(data, size, "c2pa.");
    info->has_manifest_store = info->jumbf_box_count > 0U && info->manifest_count > 0U;

    if (c2pa_count_token(data, size, "c2pa.hash.data") > 0U && ctx.carrier_range_count > 0U) {
        info->content_hash_checked = 1;
        if (c2pa_carrier_hash_matches(&ctx)) {
            info->content_hash_matched = 1;
            info->content_hash_mismatched = 0;
        } else if (!info->content_hash_matched && info->carrier != 0 && c2pa_bytes_equal((const unsigned char *)info->carrier, 16U, "JPEG APP11 JUMBF")) {
            info->content_hash_mismatched = 1;
        }
    }

    if (carrier_found || info->jumbf_box_count > 0U || info->claim_count > 0U || info->signature_count > 0U) {
        info->present = 1;
    }
    if (!info->present) {
        return 0;
    }
    if (info->carrier == 0) {
        info->carrier = "unknown";
    }
    if (ctx.trust_validation_enabled) {
        info->trust_checked = 1;
        if (c2pa_count_token(data, size, "timeStamp") == 0U) {
            info->trust_supported = 1;
            info->trust_valid = info->signature_supported && info->signature_valid &&
                                info->x509_cert_count > 0U &&
                                info->x509_cert_count == info->x509_parseable_cert_count &&
                                !info->content_hash_mismatched &&
                                info->validation_failure_count == 0U;
        }
    }
    if (info->content_hash_mismatched) {
        info->status = "C2PA content hash mismatch";
    } else if (info->validation_failure_count > 0U) {
        info->status = "C2PA embedded validation failure";
    } else if (info->signature_invalid_count > 0U) {
        info->status = "C2PA signature verification failed";
    } else if (info->content_hash_matched) {
        info->status = "C2PA content hash validated";
    } else if (info->cose_signature_count > 0U && !info->signature_supported) {
        info->status = "C2PA signature parsed; cryptographic verification unsupported";
    } else if (info->has_manifest_store && info->cbor_valid) {
        info->status = "parsed C2PA manifest store";
    } else if (info->has_manifest_store) {
        info->status = "recognized C2PA manifest store";
    } else {
        info->status = "C2PA markers found; manifest store not recognized";
    }
    return 0;
}

int image_c2pa_analyze(const unsigned char *data, size_t size, ImageC2paInfo *info) {
    return image_c2pa_analyze_ex(data, size, 0, info);
}
