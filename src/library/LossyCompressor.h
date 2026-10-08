#pragma once

#include "common.h"
#include <optional>

class ILossyCompressor {
public:
  virtual ~ILossyCompressor() = default;
  virtual std::optional<ByteVector> try_compress(ByteView data) = 0;
};

class LossyCompressor : public ILossyCompressor {
public:
  std::optional<ByteVector> try_compress(ByteView data) override;

private:
  int m_skip_files_below{ 100 << 10 };
  int m_max_image_width{ 1280 };
  int m_max_image_height{ 720 };
  int m_jpeg_quality{ 80 };
};
