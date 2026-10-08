#include "decode_content.h"
#include <brotli/decode.h>
#include <zlib.h>
#include <zstd.h>
#include <array>
#include <memory>

namespace {
  std::optional<std::string> inflate_text(std::string_view input, int bits, size_t limit) {
    auto stream = z_stream{};
    if (inflateInit2(&stream, bits) != Z_OK)
      return {};
    struct End {
      z_stream* stream;
      ~End() { inflateEnd(stream); }
    } end{&stream};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    auto output = std::string{};
    auto buffer = std::array<char, 65536>{};
    for (;;) {
      stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
      stream.avail_out = static_cast<uInt>(buffer.size());
      const auto before = stream.avail_in;
      const auto status = inflate(&stream, Z_NO_FLUSH);
      output.append(buffer.data(), buffer.size() - stream.avail_out);
      if (output.size() > limit)
        return {};
      if (status == Z_STREAM_END) {
        if (!stream.avail_in)
          return output;
        if (bits != 31 || inflateReset2(&stream, bits) != Z_OK)
          return {};
      } else if (status != Z_OK ||
                 (before == stream.avail_in && stream.avail_out == buffer.size()))
        return {};
    }
  }

  std::optional<std::string> decode_one(
    std::string_view input, std::string_view encoding, size_t limit) {
    if (encoding.empty() || iequals(encoding, "identity"))
      return input.size() <= limit ? std::optional<std::string>(std::string(input)) : std::nullopt;
    if (iequals(encoding, "gzip") || iequals(encoding, "x-gzip"))
      return inflate_text(input, 31, limit);
    if (iequals(encoding, "deflate")) {
      auto decoded = inflate_text(input, 15, limit);
      return decoded ? decoded : inflate_text(input, -15, limit);
    }
    auto output = std::string{};
    auto buffer = std::array<char, 65536>{};
    if (iequals(encoding, "br")) {
      const auto state =
        std::unique_ptr<BrotliDecoderState, decltype(&BrotliDecoderDestroyInstance)>(
          BrotliDecoderCreateInstance(nullptr, nullptr, nullptr), BrotliDecoderDestroyInstance);
      if (!state)
        return {};
      auto remaining = input.size();
      auto next = reinterpret_cast<const uint8_t*>(input.data());
      for (;;) {
        auto available = buffer.size();
        auto destination = reinterpret_cast<uint8_t*>(buffer.data());
        const auto result = BrotliDecoderDecompressStream(
          state.get(), &remaining, &next, &available, &destination, nullptr);
        output.append(buffer.data(), buffer.size() - available);
        if (output.size() > limit)
          return {};
        if (result == BROTLI_DECODER_RESULT_SUCCESS)
          return remaining == 0 ? std::optional<std::string>(output) : std::nullopt;
        if (result != BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT)
          return {};
      }
    }
    if (iequals(encoding, "zstd")) {
      const auto state = std::unique_ptr<ZSTD_DStream, decltype(&ZSTD_freeDStream)>(
        ZSTD_createDStream(), ZSTD_freeDStream);
      if (!state || ZSTD_isError(ZSTD_initDStream(state.get())))
        return {};
      if (ZSTD_isError(ZSTD_DCtx_setParameter(state.get(), ZSTD_d_windowLogMax, 25)))
        return {};
      auto source = ZSTD_inBuffer{input.data(), input.size(), 0};
      for (;;) {
        auto destination = ZSTD_outBuffer{buffer.data(), buffer.size(), 0};
        const auto before = source.pos;
        const auto result = ZSTD_decompressStream(state.get(), &destination, &source);
        if (ZSTD_isError(result))
          return {};
        output.append(buffer.data(), destination.pos);
        if (output.size() > limit)
          return {};
        if (result == 0 && source.pos == source.size)
          return output;
        if (source.pos == before && destination.pos == 0)
          return {};
      }
    }
    return {};
  }
}

std::optional<std::string> decode_content(
  ByteView data, std::string_view encoding, size_t limit) {
  if (data.size() > limit)
    return {};
  auto result = std::optional<std::string>(std::string(as_string_view(data)));
  while (!encoding.empty()) {
    const auto comma = encoding.rfind(',');
    result = decode_one(
      *result, trim(encoding.substr(comma == std::string_view::npos ? 0 : comma + 1)), limit);
    if (!result || comma == std::string_view::npos)
      return result;
    encoding = encoding.substr(0, comma);
  }
  return result;
}
