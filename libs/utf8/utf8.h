#ifndef UTF8_CPP_H
#define UTF8_CPP_H

#include <stdexcept>
#include <string>
#include <vector>

/**
 * Work between unicode code points and their UTF-8-encoded representation.
 */
namespace Utf8
{
    /**
     * The type we use to represent Unicode codepoints.
     */
    typedef uint32_t codepoint_t;

    /**
     * The type we use when talking about the integral value of bytes.
     */
    typedef unsigned char char_t;

    /**
     * The highest allowed codepoint.
     */
    static const codepoint_t MAX_CODEPOINT = 0x10FFFF;

    /**
     * Consume up to the last byte of the sequence, returning the codepoint.
     */
    codepoint_t readCodepoint(
        std::string::const_iterator& it, const std::string::const_iterator& end);

    /**
     * Write a codepoint to the provided string.
     */
    std::string& writeCodepoint(std::string& str, codepoint_t value);
}

#endif
