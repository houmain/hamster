
#include "common.h"
#include "libs/utf8/utf8.h"

template<typename F>
std::string replace_codepoints(const std::string& input, F&& replace) {
  auto output = std::string();
  for (auto it = input.begin(); it != input.end(); )
    if (auto codepoint = replace(Utf8::readCodepoint(it, input.end())))
      Utf8::writeCodepoint(output, codepoint);
  return output;
}

std::string get_legal_filename(const std::string& filename) {
  // see: https://unicode.org/cldr/utility/confusables.jsp
  return replace_codepoints(filename,
    [](uint32_t codepoint) -> uint32_t {
      switch (codepoint) {
        case '/': return 0x2571; // BOX DRAWINGS LIGHT DIAGONAL UPPER RIGHT TO LOWER LEFT
#if 1 || defined(_WIN32)
        case '\\':return 0x2572; // BOX DRAWINGS LIGHT DIAGONAL UPPER LEFT TO LOWER RIGHT
        case '<': return 0x27E8; // MATHEMATICAL LEFT ANGLE BRACKET
        case '>': return 0x27E9; // MATHEMATICAL RIGHT ANGLE BRACKET
        case ':': return 0xA789; // MODIFIER LETTER COLON
        case '"': return 0x02EE; // MODIFIER LETTER DOUBLE APOSTROPHE
        case '|': return 0x2223; // DIVIDES
        case '*': return 0x2217; // ASTERISK OPERATOR
        case '?': return 0xFF1F; // FULLWIDTH QUESTION MARK
#endif
        default: return codepoint;
      }
    });
}
