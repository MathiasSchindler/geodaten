#include "compression/lzss.h"

#include "runtime.h"

typedef struct {
    size_t cost;
    unsigned short length;
    unsigned short distance;
} CompressionLzssOptimalState;

typedef struct {
    unsigned int window_size;
    unsigned int max_match;
    unsigned int length_bits;
} CompressionLzssProfile;

static const size_t COMPRESSION_LZSS_COST_MAX = ((size_t)-1) / 4U;
#define COMPRESSION_LZSS_OPTIMAL_MAX_STATES 2097152U
#define COMPRESSION_LZSS_LAZY_LOOKAHEAD 4U

static int compression_lzss_get_profile(unsigned int profile_id, CompressionLzssProfile *profile_out) {
    if (profile_out == 0) {
        return -1;
    }
    if (profile_id == COMPRESSION_LZSS_PROFILE_WIDE_WINDOW) {
        profile_out->window_size = 8192U;
        profile_out->max_match = 10U;
        profile_out->length_bits = 3U;
        return 0;
    }
    if (profile_id == COMPRESSION_LZSS_PROFILE_WIDE_MATCH) {
        profile_out->window_size = 4096U;
        profile_out->max_match = 18U;
        profile_out->length_bits = 4U;
        return 0;
    }
    if (profile_id == COMPRESSION_LZSS_PROFILE_MEDIUM_MATCH) {
        profile_out->window_size = 2048U;
        profile_out->max_match = 34U;
        profile_out->length_bits = 5U;
        return 0;
    }
    if (profile_id == COMPRESSION_LZSS_PROFILE_LONG_MATCH) {
        profile_out->window_size = 1024U;
        profile_out->max_match = 66U;
        profile_out->length_bits = 6U;
        return 0;
    }
    return -1;
}

static unsigned int compression_lzss_length_mask(const CompressionLzssProfile *profile) {
    return (1U << profile->length_bits) - 1U;
}

static size_t compression_lzss_find_match(const CompressionLzssProfile *profile, const unsigned char *input, size_t input_size, size_t position, unsigned int *distance_out) {
    size_t window_start = position > (size_t)profile->window_size ? position - (size_t)profile->window_size : 0U;
    size_t best_length = 0U;
    unsigned int best_distance = 0U;
    size_t candidate;

    if (position >= input_size) {
        *distance_out = 0U;
        return 0U;
    }

    for (candidate = window_start; candidate < position; ++candidate) {
        size_t length = 0U;
        size_t max_length = input_size - position;
        if (max_length > (size_t)profile->max_match) {
            max_length = (size_t)profile->max_match;
        }
        while (length < max_length && input[candidate + length] == input[position + length]) {
            length += 1U;
        }
        if (length > best_length && length >= COMPRESSION_LZSS_MIN_MATCH) {
            best_length = length;
            best_distance = (unsigned int)(position - candidate);
            if (best_length == (size_t)profile->max_match) {
                break;
            }
        }
    }

    *distance_out = best_distance;
    return best_length;
}

size_t compression_lzss_bound(size_t input_size) {
    size_t flag_count = (input_size + 7U) / 8U;

    if (input_size > ((size_t)-1) - flag_count) {
        return 0U;
    }
    return input_size + flag_count;
}

static int compression_lzss_add_cost(size_t left, size_t right, size_t *value_out) {
    if (left >= COMPRESSION_LZSS_COST_MAX || right > COMPRESSION_LZSS_COST_MAX - left) {
        return -1;
    }
    *value_out = left + right;
    return 0;
}

static size_t compression_lzss_state_index(size_t position, unsigned int slot) {
    return position * 8U + (size_t)slot;
}

static int compression_lzss_state_count(size_t input_size, size_t *state_count_out) {
    if (input_size > (((size_t)-1) / 8U) - 1U) {
        return -1;
    }
    *state_count_out = (input_size + 1U) * 8U;
    return 0;
}

static int compression_lzss_should_defer_match(const CompressionLzssProfile *profile, const unsigned char *input, size_t input_size, size_t position, size_t match_length) {
    unsigned int skip;
    unsigned int max_skip;

    if (match_length >= (size_t)profile->max_match || position + 1U >= input_size) {
        return 0;
    }
    max_skip = (unsigned int)(input_size - position - 1U);
    if (max_skip > COMPRESSION_LZSS_LAZY_LOOKAHEAD) {
        max_skip = COMPRESSION_LZSS_LAZY_LOOKAHEAD;
    }
    for (skip = 1U; skip <= max_skip; ++skip) {
        unsigned int next_distance = 0U;
        size_t next_match_length = compression_lzss_find_match(profile, input, input_size, position + (size_t)skip, &next_distance);

        if (next_match_length < COMPRESSION_LZSS_MIN_MATCH) {
            continue;
        }
        if ((skip == 1U && next_match_length > match_length) || next_match_length >= match_length + (size_t)skip) {
            return 1;
        }
    }
    return 0;
}

static int compression_lzss_compress_lazy(const CompressionLzssProfile *profile, const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out) {
    size_t input_offset = 0U;
    size_t output_offset = 0U;

    if ((input == 0 && input_size != 0U) || output == 0 || output_size_out == 0) {
        return -1;
    }

    while (input_offset < input_size) {
        size_t flag_offset;
        unsigned char flags = 0U;
        unsigned int bit;

        if (output_offset >= output_capacity) {
            return -1;
        }
        flag_offset = output_offset++;

        for (bit = 0U; bit < 8U && input_offset < input_size; ++bit) {
            unsigned int distance = 0U;
            size_t match_length = compression_lzss_find_match(profile, input, input_size, input_offset, &distance);

            if (compression_lzss_should_defer_match(profile, input, input_size, input_offset, match_length)) {
                match_length = 0U;
            }

            if (match_length >= COMPRESSION_LZSS_MIN_MATCH) {
                unsigned int token_distance = distance - 1U;
                unsigned int token_length = (unsigned int)(match_length - COMPRESSION_LZSS_MIN_MATCH);

                if (output_offset + 2U > output_capacity) {
                    return -1;
                }
                flags |= (unsigned char)(1U << bit);
                output[output_offset++] = (unsigned char)(token_distance & 0xffU);
                output[output_offset++] = (unsigned char)(((token_distance >> 8U) << profile->length_bits) | token_length);
                input_offset += match_length;
            } else {
                if (output_offset >= output_capacity) {
                    return -1;
                }
                output[output_offset++] = input[input_offset++];
            }
        }
        output[flag_offset] = flags;
    }

    *output_size_out = output_offset;
    return 0;
}

static int compression_lzss_compress_optimal(const CompressionLzssProfile *profile, const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out) {
    CompressionLzssOptimalState *states;
    size_t state_count;
    size_t position;
    size_t input_offset;
    size_t output_offset;
    size_t flag_offset;
    unsigned int slot;

    if (compression_lzss_state_count(input_size, &state_count) != 0 ||
        state_count > COMPRESSION_LZSS_OPTIMAL_MAX_STATES ||
        state_count > ((size_t)-1) / sizeof(*states)) {
        return -1;
    }
    states = (CompressionLzssOptimalState *)rt_malloc(state_count * sizeof(*states));
    if (states == 0) {
        return -1;
    }

    for (slot = 0U; slot < 8U; ++slot) {
        CompressionLzssOptimalState *state = &states[compression_lzss_state_index(input_size, slot)];
        state->cost = 0U;
        state->length = 0U;
        state->distance = 0U;
    }

    position = input_size;
    while (position > 0U) {
        unsigned int match_distance = 0U;
        size_t match_length;

        position -= 1U;
        match_length = compression_lzss_find_match(profile, input, input_size, position, &match_distance);

        for (slot = 0U; slot < 8U; ++slot) {
            unsigned int next_slot = (slot + 1U) & 7U;
            size_t token_header_cost = slot == 0U ? 1U : 0U;
            size_t best_cost = COMPRESSION_LZSS_COST_MAX;
            unsigned short best_length = 1U;
            unsigned short best_distance = 0U;
            size_t candidate_cost;
            size_t length;

            if (compression_lzss_add_cost(token_header_cost + 1U, states[compression_lzss_state_index(position + 1U, next_slot)].cost, &candidate_cost) == 0) {
                best_cost = candidate_cost;
            }

            for (length = COMPRESSION_LZSS_MIN_MATCH; length <= match_length; ++length) {
                if (compression_lzss_add_cost(token_header_cost + 2U, states[compression_lzss_state_index(position + length, next_slot)].cost, &candidate_cost) == 0 &&
                    candidate_cost < best_cost) {
                    best_cost = candidate_cost;
                    best_length = (unsigned short)length;
                    best_distance = (unsigned short)match_distance;
                }
            }

            states[compression_lzss_state_index(position, slot)].cost = best_cost;
            states[compression_lzss_state_index(position, slot)].length = best_length;
            states[compression_lzss_state_index(position, slot)].distance = best_distance;
        }
    }

    input_offset = 0U;
    output_offset = 0U;
    flag_offset = 0U;
    slot = 0U;
    while (input_offset < input_size) {
        CompressionLzssOptimalState *state = &states[compression_lzss_state_index(input_offset, slot)];

        if (slot == 0U) {
            if (output_offset >= output_capacity) {
                rt_free(states);
                return -1;
            }
            flag_offset = output_offset;
            output[flag_offset] = 0U;
            output_offset += 1U;
        }

        if (state->length >= COMPRESSION_LZSS_MIN_MATCH) {
            unsigned int token_distance = (unsigned int)state->distance - 1U;
            unsigned int token_length = (unsigned int)state->length - COMPRESSION_LZSS_MIN_MATCH;

            if (output_offset + 2U > output_capacity) {
                rt_free(states);
                return -1;
            }
            output[flag_offset] |= (unsigned char)(1U << slot);
            output[output_offset++] = (unsigned char)(token_distance & 0xffU);
            output[output_offset++] = (unsigned char)(((token_distance >> 8U) << profile->length_bits) | token_length);
            input_offset += (size_t)state->length;
        } else {
            if (output_offset >= output_capacity) {
                rt_free(states);
                return -1;
            }
            output[output_offset++] = input[input_offset++];
        }
        slot = (slot + 1U) & 7U;
    }

    rt_free(states);
    *output_size_out = output_offset;
    return 0;
}

int compression_lzss_compress_profile(unsigned int profile_id, const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out) {
    CompressionLzssProfile profile;

    if ((input == 0 && input_size != 0U) || output == 0 || output_size_out == 0) {
        return -1;
    }
    if (compression_lzss_get_profile(profile_id, &profile) != 0) {
        return -1;
    }
    if (compression_lzss_compress_optimal(&profile, input, input_size, output, output_capacity, output_size_out) == 0) {
        return 0;
    }
    return compression_lzss_compress_lazy(&profile, input, input_size, output, output_capacity, output_size_out);
}

int compression_lzss_compress(const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out) {
    return compression_lzss_compress_profile(COMPRESSION_LZSS_PROFILE_DEFAULT, input, input_size, output, output_capacity, output_size_out);
}

int compression_lzss_decompress_profile(unsigned int profile_id, const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out) {
    CompressionLzssProfile profile;
    size_t input_offset = 0U;
    size_t output_offset = 0U;

    if ((input == 0 && input_size != 0U) || output == 0 || output_size_out == 0) {
        return -1;
    }
    if (compression_lzss_get_profile(profile_id, &profile) != 0) {
        return -1;
    }

    while (input_offset < input_size) {
        unsigned char flags = input[input_offset++];
        unsigned int bit;

        for (bit = 0U; bit < 8U && input_offset < input_size; ++bit) {
            if ((flags & (unsigned char)(1U << bit)) == 0U) {
                if (output_offset >= output_capacity) {
                    return -1;
                }
                output[output_offset++] = input[input_offset++];
            } else {
                unsigned int token;
                unsigned int distance;
                unsigned int length;
                unsigned int index;

                if (input_offset + 2U > input_size) {
                    return -1;
                }
                token = (unsigned int)input[input_offset] | ((unsigned int)input[input_offset + 1U] << 8U);
                input_offset += 2U;
                distance = ((token & 0x00ffU) | ((token >> profile.length_bits) & (((1U << (16U - profile.length_bits)) - 1U) & 0xff00U))) + 1U;
                length = ((token >> 8U) & compression_lzss_length_mask(&profile)) + COMPRESSION_LZSS_MIN_MATCH;
                if (distance > output_offset || output_offset + (size_t)length > output_capacity) {
                    return -1;
                }
                for (index = 0U; index < length; ++index) {
                    output[output_offset] = output[output_offset - distance];
                    output_offset += 1U;
                }
            }
        }
    }

    *output_size_out = output_offset;
    return 0;
}

int compression_lzss_decompress(const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out) {
    return compression_lzss_decompress_profile(COMPRESSION_LZSS_PROFILE_DEFAULT, input, input_size, output, output_capacity, output_size_out);
}
