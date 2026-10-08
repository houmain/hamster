
#include "LossyCompressor.h"
#include "libs/stb/stb_image.h"
#include "libs/stb/stb_image_write.h"
#include "libs/stb/stb_image_resize.h"
#include <memory>
#include <cstring>

namespace {
  struct FreeStbImage {
    void operator()(stbi_uc* image) const { stbi_image_free(image); }
  };
  using StbImagePtr = std::unique_ptr<stbi_uc, FreeStbImage>;

  bool is_opaque(stbi_uc* image, int width, int height, int components) {
    if (components != STBI_rgb_alpha)
      return true;
    for (auto i = 3; i < width * height * 4; i += 4)
      if (image[i] != 0xFF)
        return false;
    return true;
  }
} // namespace

std::optional<ByteVector> LossyCompressor::try_compress(ByteView data) {
  if (static_cast<int>(data.size()) < m_skip_files_below)
    return { };

  auto width = 0;
  auto height = 0;
  auto components = 0;
  auto image = StbImagePtr(stbi_load_from_memory(
    reinterpret_cast<const unsigned char*>(data.data()),
    static_cast<int>(data.size()), &width, &height, &components, 0));
  if (!image || !is_opaque(image.get(), width, height, components))
    return { };

  // limit dimensions
  const auto scale = std::max(
    width / static_cast<double>(m_max_image_width),
    height / static_cast<double>(m_max_image_height));
  if (scale > 1.0) {
    const auto resized_width = static_cast<int>(width / scale);
    const auto resized_height = static_cast<int>(height / scale);
    auto resized = StbImagePtr(static_cast<stbi_uc*>(
      std::malloc(static_cast<size_t>(resized_width * resized_height * components))));
    if (stbir_resize_uint8_srgb(
        image.get(), width, height, components * width,
        resized.get(), resized_width, resized_height, resized_width * components,
        components, STBIR_ALPHA_CHANNEL_NONE, 0)) {
      image = std::move(resized);
      width = resized_width;
      height = resized_height;
    }
  }

  // compress to JPEG
  auto compressed = ByteVector();
  const auto write_callback = [](void* user_data, void* data, int size) {
    auto& compressed = *static_cast<ByteVector*>(user_data);
    auto offset = compressed.size();
    compressed.resize(offset + static_cast<size_t>(size));
    std::memcpy(compressed.data() + offset, data, static_cast<size_t>(size));
  };
  if (!stbi_write_jpg_to_func(write_callback,
      &compressed, width, height, components, image.get(), m_jpeg_quality))
    return { };

  // check if size was reduced
  if (compressed.empty() ||
      compressed.size() >= static_cast<size_t>(data.size()))
    return { };

  return compressed;
}
