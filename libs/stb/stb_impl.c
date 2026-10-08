
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#include "libs/stb/stb_image.h"


// set zlib compress function for stb PNG writer
// source: https://github.com/DanielGibson/stb/commit/1dae231b1ec01b403a937ffe04421ca4e287efc2

#include "zlib.h"

static unsigned char* compress_for_stbiw(
    unsigned char *data, int data_len, int *out_len, int quality) {

  uLongf bufSize = compressBound(data_len);
  unsigned char* buf = malloc(bufSize);
  if (buf == NULL)
    return NULL;

  if (compress2(buf, &bufSize, data, data_len, quality) != Z_OK) {
    free(buf);
    return NULL;
  }
  *out_len = bufSize;

  return buf;
}
#define STBIW_ZLIB_COMPRESS  compress_for_stbiw

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stdio.h>
#include "libs/stb/stb_image_write.h"


#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "libs/stb/stb_image_resize.h"
