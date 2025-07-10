#include "png_decoder.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// 读取32位大端整数
static uint32_t read_uint32_be(const uint8_t* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

/**
 * 计算 PNG 格式的 CRC32 校验值
 * @param crc   初始 CRC 值（对于首个块，传 0）
 * @param buf   输入数据缓冲区
 * @param len   数据长度
 * @return      更新后的 CRC32 值
 */
static uint32_t png_crc32(uint32_t crc, const uint8_t* buf, size_t len) {
    static uint32_t crc_table[256];
    static int crc_table_computed = 0;

    // 初始化 CRC 表（只需计算一次）
    if (!crc_table_computed) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            }
            crc_table[i] = c;
        }
        crc_table_computed = 1;
    }

    // 计算 CRC
    crc = crc ^ 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

int png_validate_signature(FILE* file) {
    uint8_t signature[PNG_SIGNATURE_SIZE];
    if (fread(signature, 1, PNG_SIGNATURE_SIZE, file) != PNG_SIGNATURE_SIZE) {
        return 0;
    }

    // 打印实际读取的签名
    fprintf(stderr, "Actual Signature: ");
    for (int i = 0; i < PNG_SIGNATURE_SIZE; i++) {
        fprintf(stderr, "\\x%02x", signature[i]);
    }
    fprintf(stderr, "\n");

    return memcmp(signature, PNG_SIGNATURE, PNG_SIGNATURE_SIZE) == 0;
}

int png_read_chunk(FILE* file, PNG_Chunk* chunk) {
    if (!file || !chunk) return 0;
    memset(chunk, 0, sizeof(PNG_Chunk));

    // 1. 读取长度
    uint8_t length_buf[4];
    if (fread(length_buf, 1, 4, file) != 4) return 0;
    chunk->length = read_uint32_be(length_buf);

    // 2. 检查长度有效性
    if (chunk->length > MAX_CHUNK_LENGTH) goto fail;

    // 3. 读取类型
    uint8_t type_buf[4];
    if (fread(type_buf, 1, 4, file) != 4) goto fail;
    chunk->type = read_uint32_be(type_buf);

    // 4. 读取数据
    if (chunk->length > 0) {
        chunk->data = malloc(chunk->length);
        if (!chunk->data || fread(chunk->data, 1, chunk->length, file) != chunk->length) {
            goto fail;
        }
    }

    // 5. 读取并验证CRC
    uint8_t crc_buf[4];
    if (fread(crc_buf, 1, 4, file) != 4) goto fail;
    chunk->crc = read_uint32_be(crc_buf);

    uint32_t calculated_crc = png_crc32(0, type_buf, 4);
    if (chunk->data) {
        calculated_crc = png_crc32(calculated_crc, chunk->data, chunk->length);
    }

    if (calculated_crc != chunk->crc) goto fail;

    return 1;

fail:
    if (chunk->data) free(chunk->data);
    memset(chunk, 0, sizeof(PNG_Chunk));
    return 0;
}

void png_free_chunk(PNG_Chunk* chunk) {
    if (chunk->data) {
        free(chunk->data);
    }
}

int png_parse_ihdr(PNG_Chunk* chunk, PNG_IHDR* ihdr) {
    if (chunk->length != 13) {
        return 0;
    }
    
    ihdr->width = read_uint32_be(chunk->data);
    ihdr->height = read_uint32_be(chunk->data + 4);
    ihdr->bit_depth = chunk->data[8];
    ihdr->color_type = chunk->data[9];
    ihdr->compression_method = chunk->data[10];
    ihdr->filter_method = chunk->data[11];
    ihdr->interlace_method = chunk->data[12];
    
    // 验证IHDR值
    if (ihdr->width == 0 || ihdr->height == 0) {
        return 0;
    }
    
    if (ihdr->compression_method != PNG_COMPRESSION_METHOD_DEFLATE) {
        return 0;
    }
    
    if (ihdr->filter_method != PNG_FILTER_METHOD_ADAPTIVE) {
        return 0;
    }
    
    if (ihdr->interlace_method != PNG_INTERLACE_METHOD_NONE && 
        ihdr->interlace_method != PNG_INTERLACE_METHOD_ADAM7) {
        return 0;
    }
    
    // 验证颜色类型和位深度组合
    switch (ihdr->color_type) {
        case PNG_COLOR_TYPE_GRAY:
            if (ihdr->bit_depth != 1 && ihdr->bit_depth != 2 && 
                ihdr->bit_depth != 4 && ihdr->bit_depth != 8 && 
                ihdr->bit_depth != 16) {
                return 0;
            }
            break;
        case PNG_COLOR_TYPE_RGB:
            if (ihdr->bit_depth != 8 && ihdr->bit_depth != 16) {
                return 0;
            }
            break;
        case PNG_COLOR_TYPE_PALETTE:
            if (ihdr->bit_depth != 1 && ihdr->bit_depth != 2 && 
                ihdr->bit_depth != 4 && ihdr->bit_depth != 8) {
                return 0;
            }
            break;
        case PNG_COLOR_TYPE_GRAY_ALPHA:
            if (ihdr->bit_depth != 8 && ihdr->bit_depth != 16) {
                return 0;
            }
            break;
        case PNG_COLOR_TYPE_RGBA:
            if (ihdr->bit_depth != 8 && ihdr->bit_depth != 16) {
                return 0;
            }
            break;
        default:
            return 0;
    }
    
    return 1;
}

int png_parse_plte(PNG_Chunk* chunk, PNG_PaletteEntry** palette, uint32_t* palette_size) {
    if (chunk->length % 3 != 0 || chunk->length > 768) {
        return 0;
    }
    
    *palette_size = chunk->length / 3;
    *palette = (PNG_PaletteEntry*)malloc(*palette_size * sizeof(PNG_PaletteEntry));
    if (!*palette) {
        return 0;
    }
    
    for (uint32_t i = 0; i < *palette_size; i++) {
        (*palette)[i].red = chunk->data[i * 3];
        (*palette)[i].green = chunk->data[i * 3 + 1];
        (*palette)[i].blue = chunk->data[i * 3 + 2];
    }
    
    return 1;
}

int png_parse_trns(PNG_Chunk* chunk, uint8_t** transparency, uint32_t* transparency_size) {
    *transparency = (uint8_t*)malloc(chunk->length);
    if (!*transparency) {
        return 0;
    }
    
    memcpy(*transparency, chunk->data, chunk->length);
    *transparency_size = chunk->length;
    
    return 1;
}

int png_process_idat(PNG_Chunk* chunk, uint8_t** image_data, uint32_t* image_data_size) {
    if (*image_data_size == 0) {
        *image_data = (uint8_t*)malloc(chunk->length);
        if (!*image_data) {
            return 0;
        }
        memcpy(*image_data, chunk->data, chunk->length);
        *image_data_size = chunk->length;
    } else {
        uint8_t* new_data = (uint8_t*)realloc(*image_data, *image_data_size + chunk->length);
        if (!new_data) {
            return 0;
        }
        *image_data = new_data;
        memcpy(*image_data + *image_data_size, chunk->data, chunk->length);
        *image_data_size += chunk->length;
    }
    
    return 1;
}

int png_decompress_data(uint8_t* compressed, uint32_t compressed_size, uint8_t** decompressed, uint32_t* decompressed_size) {
    z_stream stream;
    int ret;
    
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = compressed_size;
    stream.next_in = compressed;
    
    ret = inflateInit(&stream);
    if (ret != Z_OK) {
        return 0;
    }
    
    uint32_t buffer_size = 4096;
    uint32_t total_size = 0;
    *decompressed = (uint8_t*)malloc(buffer_size);
    if (!*decompressed) {
        inflateEnd(&stream);
        return 0;
    }
    
    do {
        stream.avail_out = buffer_size - total_size;
        stream.next_out = *decompressed + total_size;
        
        ret = inflate(&stream, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR) {
            free(*decompressed);
            inflateEnd(&stream);
            return 0;
        }
        
        total_size = buffer_size - stream.avail_out;
        
        if (stream.avail_out == 0) {
            buffer_size *= 2;
            uint8_t* new_buffer = (uint8_t*)realloc(*decompressed, buffer_size);
            if (!new_buffer) {
                free(*decompressed);
                inflateEnd(&stream);
                return 0;
            }
            *decompressed = new_buffer;
        }
    } while (ret != Z_STREAM_END);
    
    *decompressed_size = total_size;
    
    inflateEnd(&stream);
    
    // 调整缓冲区到实际大小
    uint8_t* final_buffer = (uint8_t*)realloc(*decompressed, *decompressed_size);
    if (final_buffer) {
        *decompressed = final_buffer;
    }
    
    return ret == Z_STREAM_END ? 1 : 0;
}

int png_apply_filters(uint8_t* image_data, uint32_t image_data_size, PNG_IHDR* header) {
  if (!image_data || !header || header->width == 0 || header->height == 0) {
      return 0;
  }

  uint32_t width = header->width;
  uint8_t color_type = header->color_type;
  uint8_t bit_depth = header->bit_depth;

  // 计算每像素的字节数
  int bytes_per_pixel;
  switch (color_type) {
      case PNG_COLOR_TYPE_GRAY:
          bytes_per_pixel = (bit_depth >= 8) ? 1 : 0;
          break;
      case PNG_COLOR_TYPE_RGB:
          bytes_per_pixel = 3 * ((bit_depth >= 8) ? 1 : 0);
          break;
      case PNG_COLOR_TYPE_PALETTE:
          bytes_per_pixel = 1; // 索引总是一个字节
          break;
      case PNG_COLOR_TYPE_GRAY_ALPHA:
          bytes_per_pixel = 2 * ((bit_depth >= 8) ? 1 : 0);
          break;
      case PNG_COLOR_TYPE_RGBA:
          bytes_per_pixel = 4 * ((bit_depth >= 8) ? 1 : 0);
          break;
      default:
          return 0;
  }

  // 对于小于8位深度的灰度或调色板图像，每个字节包含多个像素
  if (bit_depth < 8 && (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)) {
      bytes_per_pixel = 1;
      width = (width * bit_depth + 7) / 8; // 计算每扫描线的字节数
  } else if (bit_depth == 16) {
      bytes_per_pixel *= 2; // 16位深度每个通道2字节
  }

  // 计算每扫描线的字节数（不包括过滤类型字节）
  uint32_t bytes_per_line = width * bytes_per_pixel;
  if (bytes_per_line == 0) {
      return 0;
  }

  // 计算预期的数据大小
  uint32_t expected_size = header->height * (bytes_per_line + 1);
  if (image_data_size < expected_size) {
      return 0;
  }

  // 分配临时缓冲区用于解过滤
  uint8_t* prev_line = (uint8_t*)calloc(bytes_per_line, 1);
  if (!prev_line) {
      return 0;
  }

  uint8_t* current_line = (uint8_t*)malloc(bytes_per_line);
  if (!current_line) {
      free(prev_line);
      return 0;
  }

  uint8_t* data_ptr = image_data;
  uint8_t* output_ptr = image_data;

  for (uint32_t y = 0; y < header->height; y++) {
      uint8_t filter_type = *data_ptr++;
      
      if (filter_type > 4) {
          free(prev_line);
          free(current_line);
          return 0;
      }

      // 复制当前行数据到临时缓冲区
      memcpy(current_line, data_ptr, bytes_per_line);
      data_ptr += bytes_per_line;

      // 根据过滤类型处理当前行
      for (uint32_t x = 0; x < bytes_per_line; x++) {
          uint8_t left = (x >= bytes_per_pixel) ? current_line[x - bytes_per_pixel] : 0;
          uint8_t above = prev_line[x];
          uint8_t upper_left = (x >= bytes_per_pixel) ? prev_line[x - bytes_per_pixel] : 0;

          switch (filter_type) {
              case 0: // None
                  break;
              case 1: // Sub
                  current_line[x] += left;
                  break;
              case 2: // Up
                  current_line[x] += above;
                  break;
              case 3: // Average
                  current_line[x] += (left + above) / 2;
                  break;
              case 4: // Paeth
              {
                  int p = left + above - upper_left;
                  int pa = abs(p - left);
                  int pb = abs(p - above);
                  int pc = abs(p - upper_left);

                  if (pa <= pb && pa <= pc) {
                      current_line[x] += left;
                  } else if (pb <= pc) {
                      current_line[x] += above;
                  } else {
                      current_line[x] += upper_left;
                  }
                  break;
              }
          }
      }

      // 复制处理后的行到输出
      memcpy(output_ptr, current_line, bytes_per_line);
      output_ptr += bytes_per_line;

      // 更新上一行
      memcpy(prev_line, current_line, bytes_per_line);
  }

  free(prev_line);
  free(current_line);

  // 调整数据大小（移除过滤类型字节）
  memmove(image_data, image_data + header->height, bytes_per_line * header->height);
  
  return 1;
}

int png_convert_to_rgba(PNG_Image* image, uint8_t** output, uint32_t* output_size) {
  if (!image || !output || !output_size) {
      return 0;
  }

  uint32_t width = image->header.width;
  uint32_t height = image->header.height;
  uint8_t bit_depth = image->header.bit_depth;
  uint8_t color_type = image->header.color_type;

  *output_size = width * height * 4;
  *output = (uint8_t*)malloc(*output_size);
  if (!*output) {
      return 0;
  }

  uint8_t* src = image->image_data;
  uint8_t* dst = *output;
  uint32_t src_bpp = 0; // bytes per pixel in source
  uint32_t src_row_bytes = 0;

  // Calculate source bytes per pixel and row bytes
  switch (color_type) {
      case PNG_COLOR_TYPE_GRAY:
          src_bpp = (bit_depth >= 8) ? 1 : 0;
          src_row_bytes = (width * bit_depth + 7) / 8;
          break;
      case PNG_COLOR_TYPE_RGB:
          src_bpp = 3 * ((bit_depth >= 8) ? 1 : 0);
          src_row_bytes = width * src_bpp;
          break;
      case PNG_COLOR_TYPE_PALETTE:
          src_bpp = 1; // Always 1 byte per pixel (index)
          src_row_bytes = (width * bit_depth + 7) / 8;
          break;
      case PNG_COLOR_TYPE_GRAY_ALPHA:
          src_bpp = 2 * ((bit_depth >= 8) ? 1 : 0);
          src_row_bytes = width * src_bpp;
          break;
      case PNG_COLOR_TYPE_RGBA:
          src_bpp = 4 * ((bit_depth >= 8) ? 1 : 0);
          src_row_bytes = width * src_bpp;
          break;
      default:
          free(*output);
          *output = NULL;
          return 0;
  }

  if (bit_depth == 16) {
      src_bpp *= 2;
      src_row_bytes *= 2;
  }

  // Check if we have enough source data
  if (image->image_data_size < height * src_row_bytes) {
      free(*output);
      *output = NULL;
      return 0;
  }

  for (uint32_t y = 0; y < height; y++) {
      uint8_t* src_row = src + y * src_row_bytes;
      uint8_t* dst_row = dst + y * width * 4;

      for (uint32_t x = 0; x < width; x++) {
          uint8_t r = 0, g = 0, b = 0, a = 255;

          switch (color_type) {
              case PNG_COLOR_TYPE_GRAY:
                  if (bit_depth == 16) {
                      uint16_t gray = (src_row[x * 2] << 8) | src_row[x * 2 + 1];
                      r = g = b = (uint8_t)(gray >> 8);
                  } else if (bit_depth >= 8) {
                      r = g = b = src_row[x];
                  } else {
                      // For bit depths < 8, extract the correct bits
                      uint8_t shift = 8 - bit_depth - (x * bit_depth) % 8;
                      uint8_t mask = (1 << bit_depth) - 1;
                      uint8_t val = (src_row[(x * bit_depth) / 8] >> shift) & mask;
                      val = (val * 255) / ((1 << bit_depth) - 1);
                      r = g = b = val;
                  }
                  break;

              case PNG_COLOR_TYPE_RGB:
                  if (bit_depth == 16) {
                      r = src_row[x * 6];
                      g = src_row[x * 6 + 2];
                      b = src_row[x * 6 + 4];
                  } else {
                      r = src_row[x * 3];
                      g = src_row[x * 3 + 1];
                      b = src_row[x * 3 + 2];
                  }
                  break;

              case PNG_COLOR_TYPE_PALETTE:
                  if (!image->palette) {
                      free(*output);
                      *output = NULL;
                      return 0;
                  }
                  if (bit_depth == 16) {
                      // Shouldn't happen for palette, but handle anyway
                      uint16_t index = (src_row[x * 2] << 8) | src_row[x * 2 + 1];
                      if (index < image->palette_size) {
                          r = image->palette[index].red;
                          g = image->palette[index].green;
                          b = image->palette[index].blue;
                      }
                  } else {
                      uint8_t index;
                      if (bit_depth >= 8) {
                          index = src_row[x];
                      } else {
                          uint8_t shift = 8 - bit_depth - (x * bit_depth) % 8;
                          uint8_t mask = (1 << bit_depth) - 1;
                          index = (src_row[(x * bit_depth) / 8] >> shift) & mask;
                      }
                      if (index < image->palette_size) {
                          r = image->palette[index].red;
                          g = image->palette[index].green;
                          b = image->palette[index].blue;
                      }
                  }
                  // Handle transparency for palette
                  if (image->transparency) {
                      if (bit_depth >= 8) {
                          if (src_row[x] < image->transparency_size) {
                              a = image->transparency[src_row[x]];
                          }
                      } else {
                          uint8_t shift = 8 - bit_depth - (x * bit_depth) % 8;
                          uint8_t mask = (1 << bit_depth) - 1;
                          uint8_t index = (src_row[(x * bit_depth) / 8] >> shift) & mask;
                          if (index < image->transparency_size) {
                              a = image->transparency[index];
                          }
                      }
                  }
                  break;

              case PNG_COLOR_TYPE_GRAY_ALPHA:
                  if (bit_depth == 16) {
                      uint16_t gray = (src_row[x * 4] << 8) | src_row[x * 4 + 1];
                      uint16_t alpha = (src_row[x * 4 + 2] << 8) | src_row[x * 4 + 3];
                      r = g = b = (uint8_t)(gray >> 8);
                      a = (uint8_t)(alpha >> 8);
                  } else {
                      r = g = b = src_row[x * 2];
                      a = src_row[x * 2 + 1];
                  }
                  break;

              case PNG_COLOR_TYPE_RGBA:
                  if (bit_depth == 16) {
                      r = src_row[x * 8];
                      g = src_row[x * 8 + 2];
                      b = src_row[x * 8 + 4];
                      a = src_row[x * 8 + 6];
                  } else {
                      r = src_row[x * 4];
                      g = src_row[x * 4 + 1];
                      b = src_row[x * 4 + 2];
                      a = src_row[x * 4 + 3];
                  }
                  break;

              default:
                  free(*output);
                  *output = NULL;
                  return 0;
          }

          // Handle grayscale transparency
          if (color_type == PNG_COLOR_TYPE_GRAY && image->transparency) {
              if (bit_depth == 16) {
                  uint16_t gray = (src_row[x * 2] << 8) | src_row[x * 2 + 1];
                  if (image->transparency_size >= 2) {
                      uint16_t trans_gray = (image->transparency[0] << 8) | image->transparency[1];
                      a = (gray == trans_gray) ? 0 : 255;
                  }
              } else {
                  if (image->transparency_size >= 1) {
                      a = (src_row[x] == image->transparency[0]) ? 0 : 255;
                  }
              }
          }

          // Handle RGB transparency
          if (color_type == PNG_COLOR_TYPE_RGB && image->transparency) {
              if (bit_depth == 16) {
                  if (image->transparency_size >= 6) {
                      uint16_t r_val = (src_row[x * 6] << 8) | src_row[x * 6 + 1];
                      uint16_t g_val = (src_row[x * 6 + 2] << 8) | src_row[x * 6 + 3];
                      uint16_t b_val = (src_row[x * 6 + 4] << 8) | src_row[x * 6 + 5];
                      uint16_t trans_r = (image->transparency[0] << 8) | image->transparency[1];
                      uint16_t trans_g = (image->transparency[2] << 8) | image->transparency[3];
                      uint16_t trans_b = (image->transparency[4] << 8) | image->transparency[5];
                      a = (r_val == trans_r && g_val == trans_g && b_val == trans_b) ? 0 : 255;
                  }
              } else {
                  if (image->transparency_size >= 3) {
                      a = (src_row[x * 3] == image->transparency[0] &&
                           src_row[x * 3 + 1] == image->transparency[1] &&
                           src_row[x * 3 + 2] == image->transparency[2]) ? 0 : 255;
                  }
              }
          }

          dst_row[x * 4] = r;
          dst_row[x * 4 + 1] = g;
          dst_row[x * 4 + 2] = b;
          dst_row[x * 4 + 3] = a;
      }
  }

  return 1;
}

int png_read_file(const char* filename, PNG_Image* image) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        return 0;
    }
    
    if (!png_validate_signature(file)) {
        fclose(file);
        return 0;
    }
    
    memset(image, 0, sizeof(PNG_Image));
    int has_ihdr = 0;
    int has_idat = 0;
    int has_iend = 0;
    
    PNG_Chunk chunk;
    while (!has_iend && png_read_chunk(file, &chunk)) {
        switch (chunk.type) {
            case PNG_CHUNK_IHDR:
                if (has_ihdr || !png_parse_ihdr(&chunk, &image->header)) {
                    png_free_chunk(&chunk);
                    png_free_image(image);
                    fclose(file);
                    return 0;
                }
                has_ihdr = 1;
                break;
                
            case PNG_CHUNK_PLTE:
                if (!has_ihdr || image->header.color_type == PNG_COLOR_TYPE_GRAY || 
                    image->header.color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
                    png_free_chunk(&chunk);
                    png_free_image(image);
                    fclose(file);
                    return 0;
                }
                if (!png_parse_plte(&chunk, &image->palette, &image->palette_size)) {
                    png_free_chunk(&chunk);
                    png_free_image(image);
                    fclose(file);
                    return 0;
                }
                break;
                
            case PNG_CHUNK_tRNS:
                if (!has_ihdr || image->header.color_type == PNG_COLOR_TYPE_GRAY_ALPHA || 
                    image->header.color_type == PNG_COLOR_TYPE_RGBA) {
                    png_free_chunk(&chunk);
                    png_free_image(image);
                    fclose(file);
                    return 0;
                }
                if (!png_parse_trns(&chunk, &image->transparency, &image->transparency_size)) {
                    png_free_chunk(&chunk);
                    png_free_image(image);
                    fclose(file);
                    return 0;
                }
                break;
                
            case PNG_CHUNK_IDAT:
                if (!has_ihdr || has_iend) {
                    png_free_chunk(&chunk);
                    png_free_image(image);
                    fclose(file);
                    return 0;
                }
                if (!png_process_idat(&chunk, &image->image_data, &image->image_data_size)) {
                    png_free_chunk(&chunk);
                    png_free_image(image);
                    fclose(file);
                    return 0;
                }
                has_idat = 1;
                break;
                
            case PNG_CHUNK_IEND:
                if (!has_ihdr || !has_idat) {
                    png_free_chunk(&chunk);
                    png_free_image(image);
                    fclose(file);
                    return 0;
                }
                has_iend = 1;
                break;
                
            default:
                // 忽略其他块
                break;
        }
        
        png_free_chunk(&chunk);
    }
    
    fclose(file);
    
    if (!has_ihdr || !has_idat || !has_iend) {
        png_free_image(image);
        return 0;
    }
    
    // 解压缩图像数据
    uint8_t* decompressed = NULL;
    uint32_t decompressed_size = 0;
    if (!png_decompress_data(image->image_data, image->image_data_size, &decompressed, &decompressed_size)) {
        png_free_image(image);
        return 0;
    }
    
    free(image->image_data);
    image->image_data = decompressed;
    image->image_data_size = decompressed_size;
    
    // 应用过滤器
    if (!png_apply_filters(image->image_data, image->image_data_size, &image->header)) {
        png_free_image(image);
        return 0;
    }
    
    return 1;
}

void png_free_image(PNG_Image* image) {
    if (image->palette) {
        free(image->palette);
    }
    if (image->transparency) {
        free(image->transparency);
    }
    if (image->image_data) {
        free(image->image_data);
    }
    memset(image, 0, sizeof(PNG_Image));
}