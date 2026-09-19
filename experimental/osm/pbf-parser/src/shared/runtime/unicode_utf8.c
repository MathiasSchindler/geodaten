#include "runtime.h"

static size_t rt_utf8_sequence_length(unsigned char lead) {
    if ((lead & 0x80U) == 0U) {
        return 1U;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        return 2U;
    }
    if ((lead & 0xF0U) == 0xE0U) {
        return 3U;
    }
    if ((lead & 0xF8U) == 0xF0U) {
        return 4U;
    }
    return 0U;
}

int rt_utf8_decode(const char *text, size_t text_length, size_t *index_io, unsigned int *codepoint_out) {
    size_t index;
    unsigned char lead;
    size_t length;
    unsigned int codepoint;

    if (text == 0 || index_io == 0 || codepoint_out == 0) {
        return -1;
    }

    index = *index_io;
    if (index >= text_length) {
        return -1;
    }

    lead = (unsigned char)text[index];
    length = rt_utf8_sequence_length(lead);
    if (length == 0U || index + length > text_length) {
        *index_io = index + 1U;
        *codepoint_out = 0xfffdU;
        return -1;
    }

    if (length == 1U) {
        *index_io = index + 1U;
        *codepoint_out = (unsigned int)lead;
        return 0;
    }

    if (length == 2U) {
        unsigned char b1 = (unsigned char)text[index + 1U];
        if ((b1 & 0xc0U) != 0x80U) {
            *index_io = index + 1U;
            *codepoint_out = 0xfffdU;
            return -1;
        }
        codepoint = ((unsigned int)(lead & 0x1fU) << 6) | (unsigned int)(b1 & 0x3fU);
        if (codepoint < 0x80U) {
            *index_io = index + 1U;
            *codepoint_out = 0xfffdU;
            return -1;
        }
    } else if (length == 3U) {
        unsigned char b1 = (unsigned char)text[index + 1U];
        unsigned char b2 = (unsigned char)text[index + 2U];
        if ((b1 & 0xc0U) != 0x80U || (b2 & 0xc0U) != 0x80U) {
            *index_io = index + 1U;
            *codepoint_out = 0xfffdU;
            return -1;
        }
        codepoint = ((unsigned int)(lead & 0x0fU) << 12) |
                    ((unsigned int)(b1 & 0x3fU) << 6) |
                    (unsigned int)(b2 & 0x3fU);
        if (codepoint < 0x800U || (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            *index_io = index + 1U;
            *codepoint_out = 0xfffdU;
            return -1;
        }
    } else {
        unsigned char b1 = (unsigned char)text[index + 1U];
        unsigned char b2 = (unsigned char)text[index + 2U];
        unsigned char b3 = (unsigned char)text[index + 3U];
        if ((b1 & 0xc0U) != 0x80U || (b2 & 0xc0U) != 0x80U || (b3 & 0xc0U) != 0x80U) {
            *index_io = index + 1U;
            *codepoint_out = 0xfffdU;
            return -1;
        }
        codepoint = ((unsigned int)(lead & 0x07U) << 18) |
                    ((unsigned int)(b1 & 0x3fU) << 12) |
                    ((unsigned int)(b2 & 0x3fU) << 6) |
                    (unsigned int)(b3 & 0x3fU);
        if (codepoint < 0x10000U || codepoint > 0x10ffffU) {
            *index_io = index + 1U;
            *codepoint_out = 0xfffdU;
            return -1;
        }
    }

    *index_io = index + length;
    *codepoint_out = codepoint;
    return 0;
}

int rt_utf8_validate(const char *text, size_t text_length) {
    size_t index = 0;

    while (index < text_length) {
        unsigned int codepoint = 0;
        if (rt_utf8_decode(text, text_length, &index, &codepoint) != 0) {
            return -1;
        }
    }

    return 0;
}

unsigned long long rt_utf8_codepoint_count(const char *text, size_t text_length) {
    unsigned long long count = 0;
    size_t index = 0;

    while (text != 0 && index < text_length) {
        unsigned int codepoint = 0;
        size_t before = index;
        (void)rt_utf8_decode(text, text_length, &index, &codepoint);
        if (index == before) {
            index += 1U;
        }
        count += 1ULL;
    }

    return count;
}

int rt_utf8_encode(unsigned int codepoint, char *buffer, size_t buffer_size, size_t *length_out) {
    if (buffer == 0 || buffer_size == 0 || length_out == 0 ||
        (codepoint >= 0xd800U && codepoint <= 0xdfffU) || codepoint > 0x10ffffU) {
        return -1;
    }

    if (codepoint <= 0x7fU) {
        if (buffer_size < 1U) {
            return -1;
        }
        buffer[0] = (char)codepoint;
        *length_out = 1U;
        return 0;
    }

    if (codepoint <= 0x7ffU) {
        if (buffer_size < 2U) {
            return -1;
        }
        buffer[0] = (char)(0xc0U | (codepoint >> 6));
        buffer[1] = (char)(0x80U | (codepoint & 0x3fU));
        *length_out = 2U;
        return 0;
    }

    if (codepoint <= 0xffffU) {
        if (buffer_size < 3U) {
            return -1;
        }
        buffer[0] = (char)(0xe0U | (codepoint >> 12));
        buffer[1] = (char)(0x80U | ((codepoint >> 6) & 0x3fU));
        buffer[2] = (char)(0x80U | (codepoint & 0x3fU));
        *length_out = 3U;
        return 0;
    }

    if (buffer_size < 4U) {
        return -1;
    }
    buffer[0] = (char)(0xf0U | (codepoint >> 18));
    buffer[1] = (char)(0x80U | ((codepoint >> 12) & 0x3fU));
    buffer[2] = (char)(0x80U | ((codepoint >> 6) & 0x3fU));
    buffer[3] = (char)(0x80U | (codepoint & 0x3fU));
    *length_out = 4U;
    return 0;
}

unsigned int rt_unicode_simple_fold(unsigned int codepoint) {
    if (codepoint >= 'A' && codepoint <= 'Z') {
        return codepoint + 32U;
    }
    if ((codepoint >= 0x00c0U && codepoint <= 0x00d6U) ||
        (codepoint >= 0x00d8U && codepoint <= 0x00deU)) {
        return codepoint + 32U;
    }
    if (codepoint >= 0x0100U && codepoint <= 0x017fU) {
        if ((codepoint & 1U) == 0U) {
            return codepoint + 1U;
        }
        return codepoint;
    }
    if (codepoint >= 0x0391U && codepoint <= 0x03abU && codepoint != 0x03a2U) {
        return codepoint + 32U;
    }
    switch (codepoint) {
        case 0x0178U: return 0x00ffU;
        case 0x0181U: return 0x0253U;
        case 0x0186U: return 0x0254U;
        case 0x0189U: return 0x0256U;
        case 0x018aU: return 0x0257U;
        case 0x018eU: return 0x01ddU;
        case 0x018fU: return 0x0259U;
        case 0x0190U: return 0x025bU;
        case 0x0193U: return 0x0260U;
        case 0x0194U: return 0x0263U;
        case 0x0196U: return 0x0269U;
        case 0x0197U: return 0x0268U;
        case 0x019cU: return 0x026fU;
        case 0x019dU: return 0x0272U;
        case 0x019fU: return 0x0275U;
        case 0x01a6U: return 0x0280U;
        case 0x01a7U: return 0x01a8U;
        case 0x01acU: return 0x01adU;
        case 0x01aeU: return 0x0288U;
        case 0x01afU: return 0x01b0U;
        case 0x01b1U: return 0x028aU;
        case 0x01b2U: return 0x028bU;
        case 0x01b7U: return 0x0292U;
        case 0x03cfU: return 0x03d7U;
        default: return codepoint;
    }
}

int rt_unicode_is_space(unsigned int codepoint) {
    switch (codepoint) {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
        case '\v':
        case '\f':
        case 0x85U:
        case 0xa0U:
        case 0x1680U:
        case 0x2028U:
        case 0x2029U:
        case 0x202fU:
        case 0x205fU:
        case 0x3000U:
            return 1;
        default:
            break;
    }

    return codepoint >= 0x2000U && codepoint <= 0x200aU;
}

int rt_unicode_is_word(unsigned int codepoint) {
    if ((codepoint >= 'a' && codepoint <= 'z') ||
        (codepoint >= 'A' && codepoint <= 'Z') ||
        (codepoint >= '0' && codepoint <= '9') ||
        codepoint == '_') {
        return 1;
    }

    if (codepoint < 0x80U || rt_unicode_is_space(codepoint)) {
        return 0;
    }

    return (codepoint >= 0xa0U && codepoint <= 0x10ffffU) ? 1 : 0;
}
