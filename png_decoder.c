#include "png_decoder.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/**
 * 以大端序读取 32 位整数
 * 
 * @param data  32 位整数，在 x86 平台使用小端存储方式
 * 
 * @return      转换为大端存储方式的 32 位
 */
static uint32_t read_uint32_be(const uint8_t* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

/**
 * 计算 PNG 格式的 CRC32 校验值
 * 
 * @param crc   初始 CRC 值（对于首个块，传 0）
 * @param buf   指向要计算 CRC 的数据缓冲区的指针
 * @param len   数据缓冲区的长度
 * 
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
                // 0xEDB88320 的来由：
                // CRC-32 的标准正向多项式为 P(x) = x³² + x²⁶ + x²³ + x²² + x¹⁶ + x¹² + x¹¹ + x¹⁰ + x⁸ + x⁷ + x⁵ + x⁴ + x² + x¹ + x⁰
                // 忽略最高位 x³² 后将各项设 1 可获得二进制数  00000100 11000001 00011101 10110111
                // 对该二进制数进行位反转后可获得              11101101 10111000 10000011 00100000
                // 最后转换为十六进制数                       0xEDB88320
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

/**
 * 识别文件是否为 PNG 格式
 * 
 * @param file  指向已打开的 PNG 文件的 FILE 指针
 * 
 * @return      是否为有效的 PNG 签名，返回 1(真) 或 0(假)
 */
int png_validate_signature(FILE* file) {
    uint8_t signature[PNG_SIGNATURE_SIZE];
    
    if (fread(signature, 1, PNG_SIGNATURE_SIZE, file) != PNG_SIGNATURE_SIZE) {
        return 0;
    }

    // fprintf(stderr, "Actual Signature: ");
    // for (int i = 0; i < PNG_SIGNATURE_SIZE; i++) {
    //     fprintf(stderr, "\\x%02x", signature[i]);
    // }
    // fprintf(stderr, "\n");

    return memcmp(signature, PNG_SIGNATURE, PNG_SIGNATURE_SIZE) == 0;
}

/**
 * 读取并验证 PNG 文件的数据块
 * 
 * @param file  指向已打开的 PNG 文件的文件指针
 * @param chunk 指向 PNG_Chunk 结构体指针，用于存储读取的块数据
 * 
 * @return      是否成功读取并验证块，返回 1(真) 或 0(假)
 */
int png_read_chunk(FILE* file, PNG_Chunk* chunk) {
    if (!file || !chunk) return 0;
    
    // 将 chunk 内存清零，避免未初始化数据
    memset(chunk, 0, sizeof(PNG_Chunk));

    // 1. 读取块长度
    uint8_t length_buf[4];
    if (fread(length_buf, 1, 4, file) != 4) return 0;
    chunk->length = read_uint32_be(length_buf);

    // 2. 检查是否超出最大长度
    if (chunk->length > MAX_CHUNK_LENGTH) goto fail;

    // 3. 读取块类型
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

/**
 * 释放 chunk 的数据块内存
 * 
 * @param chunk 指向 PNG_Chunk 结构体指针
 * 
 * @return      空
 */
void png_free_chunk(PNG_Chunk* chunk) {
    if (chunk && chunk->data) {
        free(chunk->data);
        chunk->data = NULL;
    }
}

/**
 * 解析 IHDR (Image Header)块
 * 
 * @param chunk 指向包含 IHDR 块数据的 PNG_Chunk 结构体指针
 * @param ihdr  指向 PNG_IHDR 结构体指针
 * 
 * @return      是否解析成功，返回 1(真) 或 0(假)
 */
int png_parse_ihdr(PNG_Chunk* chunk, PNG_IHDR* ihdr) {
    // IHDR 块必须正好 13 字节长
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
    
    // 图像宽度或高度为 0 表示无效图像
    if (ihdr->width == 0 || ihdr->height == 0) {
        return 0;
    }
    
    // 只支持 DEFLATE 压缩方法
    if (ihdr->compression_method != PNG_COMPRESSION_METHOD_DEFLATE) {
        return 0;
    }
    
    // 只支持自适应滤波方法
    if (ihdr->filter_method != PNG_FILTER_METHOD_ADAPTIVE) {
        return 0;
    }
    
    // 只支持无隔行扫描或 Adam7 隔行扫描
    if (ihdr->interlace_method != PNG_INTERLACE_METHOD_NONE && 
        ihdr->interlace_method != PNG_INTERLACE_METHOD_ADAM7) {
        return 0;
    }
    
    // 验证颜色类型和位深度组合
    switch (ihdr->color_type) {
        // 灰度图像：支持 1,2,4,8,16 位深度
        case PNG_COLOR_TYPE_GRAY:
            if (ihdr->bit_depth != 1 && ihdr->bit_depth != 2 && 
                ihdr->bit_depth != 4 && ihdr->bit_depth != 8 && 
                ihdr->bit_depth != 16) {
                return 0;
            }
            break;
        // 真彩色图像：只支持 8,16 位深度
        case PNG_COLOR_TYPE_RGB:
            if (ihdr->bit_depth != 8 && ihdr->bit_depth != 16) {
                return 0;
            }
            break;
        // 调色板图像：支持 1,2,4,8 位深度
        case PNG_COLOR_TYPE_PALETTE:
            if (ihdr->bit_depth != 1 && ihdr->bit_depth != 2 && 
                ihdr->bit_depth != 4 && ihdr->bit_depth != 8) {
                return 0;
            }
            break;
        // 带 α 通道图像：只支持 8,16 位深度
        case PNG_COLOR_TYPE_GRAY_ALPHA:
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

/**
 * 解析 PLTE(Palette) 块（为后续图像数据解码提供颜色查找表）
 * 
 * @param chunk         指向包含 PLTE 块数据的 PNG_Chunk 结构体指针
 * @param palette       输出参数，指向调色板数组的指针
 * @param palette_size  输出参数，存储调色板条目数(即颜色数量)
 * 
 * @return      是否解析成功，返回 1(真) 或 0(假)
 */
int png_parse_plte(PNG_Chunk* chunk, PNG_PaletteEntry** palette, uint32_t* palette_size) {
    // 每个调色板条目占3字节(RGB)，所以总长度必须是 3 的倍数
    // 最大允许 768 字节 (256 个条目 × 3 字节/条目)
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

/**
 * 解析 tRNS(Transparency) 块（为后续渲染提供透明度信息）
 * 
 * @param chunk              指向包含 tRNS 块数据的 PNG_Chunk 结构体指针
 * @param transparency       输出参数，指向透明度数据数组的指针
 * @param transparency_size  输出参数，存储透明度数据长度
 * 
 * @return      是否解析成功，返回 1(真) 或 0(假)
 */
int png_parse_trns(PNG_Chunk* chunk, uint8_t color_type, uint8_t** transparency, uint32_t* transparency_size) {
    // 1. 参数NULL检查
    if (!chunk || !transparency || !transparency_size) {
        return 0;
    }

    // 2. 根据颜色类型验证长度合法性
    switch (color_type) {
        // 灰度图像：必须正好 2 字节(16 位灰度值)
        case PNG_COLOR_TYPE_GRAY:
            if (chunk->length != 2) return 0;
            break;

        // 真彩色图像：必须正好 6 字节(16 位 RGB 值)
        case PNG_COLOR_TYPE_RGB:
            if (chunk->length != 6) return 0;
            break;

        // 调色板图像：1-256 字节(每个调色板条目 1 字节)
        case PNG_COLOR_TYPE_PALETTE:
            if (chunk->length == 0 || chunk->length > 256) return 0;
            break;

        // 带 alpha 通道的图像不允许 tRNS 块
        case PNG_COLOR_TYPE_GRAY_ALPHA:
        case PNG_COLOR_TYPE_RGBA:
            return 0;
            
        default:
            return 0;
    }

    // 3. 分配内存
    *transparency = (uint8_t*)malloc(chunk->length);
    if (!*transparency) {
        return 0;
    }

    // 4. 复制数据
    memcpy(*transparency, chunk->data, chunk->length);
    *transparency_size = chunk->length;
    
    return 1;
}

/**
 * 处理 IDAT(Image Data) 块，合并多个 IDAT 块的数据 (以便后续解压)
 * 
 * @param chunk              当前 IDAT 块数据
 * @param image_data         指向当前图像数据缓冲区的指针
 * @param image_data_size    当前图像数据缓冲区的大小
 * 
 * @return      是否处理成功，返回 1(真) 或 0(假)
 */
int png_process_idat(PNG_Chunk* chunk, uint8_t** image_data, uint32_t* image_data_size) {
    if (!chunk || !image_data || !image_data_size) {
        return 0;
    }
    if (*image_data_size == 0) {
        //  处理首个 IDAT 块
        *image_data = (uint8_t*)malloc(chunk->length);
        if (!*image_data) {
            return 0;
        }
        // 将块数据复制到新缓冲区
        memcpy(*image_data, chunk->data, chunk->length);
        *image_data_size = chunk->length;
    } else {
        // 处理后续 IDAT 块，注意 realloc 可能返回新地址
        uint8_t* new_data = (uint8_t*)realloc(*image_data, *image_data_size + chunk->length);
        if (!new_data) {
            return 0;
        }
        *image_data = new_data;
        // 将新块数据追加到缓冲区末尾
        memcpy(*image_data + *image_data_size, chunk->data, chunk->length);
        *image_data_size += chunk->length;
    }
    
    return 1;
}

/**
 * 使用 zlib 解压图像数据 (将 DEFLATE 压缩的图像数据解压为原始像素数据)
 * 
 * @param compressed        压缩数据指针
 * @param compressed_size   压缩数据大小
 * @param decompressed      已解压数据指针
 * @param decompressed_size 已解压数据大小
 * 
 * @return      是否解压成功，返回 1(真) 或 0(假)
 */
int png_decompress_data(uint8_t* compressed, uint32_t compressed_size, uint8_t** decompressed, uint32_t* decompressed_size) {
    // 初始化 zlib 流，设置自定义内存分配器为 NULL(使用默认)
    z_stream stream;
    int ret;
    
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = compressed_size;
    stream.next_in = compressed;
    
    // 初始化 zlib 解压
    ret = inflateInit(&stream);
    if (ret != Z_OK) {
        return 0;
    }
    
    // 初始化解压缓冲区 (4KB)
    uint32_t buffer_size = 4096;
    uint32_t total_size = 0;
    *decompressed = (uint8_t*)malloc(buffer_size);
    if (!*decompressed) {
        // 分配内存失败则清理 zlib 并返回
        inflateEnd(&stream);
        return 0;
    }
    
    do {
        // 设置输出缓冲区剩余空间
        stream.avail_out = buffer_size - total_size;
        stream.next_out = *decompressed + total_size;
        
        // 执行解压
        ret = inflate(&stream, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR) {
            free(*decompressed);
            inflateEnd(&stream);
            return 0;
        }
        
        // 计算已解压数据大小
        total_size = buffer_size - stream.avail_out;
        
        if (stream.avail_out == 0) {
            // 缓冲区不足时双倍扩展
            buffer_size *= 2;
            uint8_t* new_buffer = (uint8_t*)realloc(*decompressed, buffer_size);
            if (!new_buffer) {
                // 重分配失败则清理资源并返回
                free(*decompressed);
                inflateEnd(&stream);
                return 0;
            }
            *decompressed = new_buffer;
        }
    } while (ret != Z_STREAM_END);
    
    // 设置最终解压大小
    *decompressed_size = total_size;
    
    // 结束 zlib 流
    inflateEnd(&stream);
    
    // 调整缓冲区到实际大小
    uint8_t* final_buffer = (uint8_t*)realloc(*decompressed, *decompressed_size);
    if (final_buffer) {
        *decompressed = final_buffer;
    }

    return ret == Z_STREAM_END ? 1 : 0;
}

/**
 * 将经过滤波压缩的扫描线数据还原为原始像素数据
 * 
 * @param image_data        压缩图像数据
 * @param image_data_size   压缩数据大小
 * @param header      		指向 PNG_IHDR 结构体指针
 * 
 * @return      是否还原成功，返回 1(真) 或 0(假)
 */
int png_apply_filters(uint8_t* image_data, uint32_t image_data_size, PNG_IHDR* header) {
	if (!image_data || !header || header->width == 0 || header->height == 0) {
		return 0;
	}

	uint32_t width = header->width;
	uint8_t color_type = header->color_type;
	uint8_t bit_depth = header->bit_depth;

	// 根据 PNG 规范确定每像素字节数
	int bytes_per_pixel;
	switch (color_type) {
		case PNG_COLOR_TYPE_GRAY:
			bytes_per_pixel = (bit_depth >= 8) ? 1 : 0;
			break;
		case PNG_COLOR_TYPE_RGB:
			bytes_per_pixel = 3 * ((bit_depth >= 8) ? 1 : 0);
			break;
		case PNG_COLOR_TYPE_PALETTE:
			bytes_per_pixel = 1;
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

	if (bit_depth < 8 && (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)) {
		// 特殊处理：对于位深小于 8 的灰度或调色板图像，多个像素可能打包在一个字节中
		bytes_per_pixel = 1;
		width = (width * bit_depth + 7) / 8; 		// 计算行宽
	} else if (bit_depth == 16) {
		bytes_per_pixel *= 2; 						// 16 位深度每个通道 2 字节
	}

	// 每个扫描行 (scanline) 的存储结构：[filter_type (1字节)][像素数据 (bytes_per_line 字节)]
	// 计算每扫描线的字节数 (不包括过滤类型字节，即扫面行的第一个字节)
	uint32_t bytes_per_line = width * bytes_per_pixel;
	if (bytes_per_line == 0) {
		return 0;
	}

	// 计算每行应占字节数 (+1 是因为每行有 filter type 字节)
	uint32_t expected_size = header->height * (bytes_per_line + 1);
	if (image_data_size < expected_size) {
		return 0;
	}

	// 分配临时缓冲区用于滤波处理：上一行和当前行
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
		uint8_t filter_type = *data_ptr++;		// 每行第一个字节是过滤类型
		
		if (filter_type > 4) {
			free(prev_line);
			free(current_line);
			return 0;
		}

		// 复制行数据到当前缓冲区
		memcpy(current_line, data_ptr, bytes_per_line);
		data_ptr += bytes_per_line;

		for (uint32_t x = 0; x < bytes_per_line; x++) {
			uint8_t left = (x >= bytes_per_pixel) ? current_line[x - bytes_per_pixel] : 0;
			uint8_t above = prev_line[x];
			uint8_t upper_left = (x >= bytes_per_pixel) ? prev_line[x - bytes_per_pixel] : 0;

			// 根据滤波类型处理当前行的每一个字节
			switch (filter_type) {
				// None
				case 0: 
					break;
				// Sub
				case 1: 
					current_line[x] += left;
					break;
				// Up
				case 2: 
					current_line[x] += above;
					break;
				// Average
				case 3: 
					current_line[x] += (left + above) / 2;
					break;
				// Paeth
				case 4: {
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
		memcpy(output_ptr, current_line, bytes_per_line + 1);
		output_ptr += bytes_per_line;

		// 将当前行作为前一行，以供下一行使用
		memcpy(prev_line, current_line, bytes_per_line + 1);
	}

	free(prev_line);
	free(current_line);

	return 1;
}

/**
 * 从任意 PNG 格式到标准 32 位 RGBA 格式的完整转换
 * 
 * @param image        		已解压的图像数据结构体
 * @param output   			输出缓冲区指针
 * @param output_size      	输出缓冲区大小
 * 
 * @return      是否还原成功，返回 1(真) 或 0(假)
 */
int png_convert_to_rgba(PNG_Image* image, uint8_t** output, uint32_t* output_size) {
	if (!image || !output || !output_size) {
		return 0;
	}

	uint32_t width = image->header.width;
	uint32_t height = image->header.height;
	uint8_t bit_depth = image->header.bit_depth;
	uint8_t color_type = image->header.color_type;

	*output_size = width * height * 4;		// 分配输出缓冲区内存：RGBA 格式，所以 x4
	*output = (uint8_t*)malloc(*output_size);
	if (!*output) {
		return 0;
	}

	uint8_t* src = image->image_data;
	uint8_t* dst = *output;
	uint32_t src_bpp = 0; 					// 源数据中每个像素占多少字节
	uint32_t src_row_bytes = 0;

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
			src_bpp = 1;
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
		// 16 位数据实际占用 2 字节
		src_bpp *= 2;
		src_row_bytes *= 2;
	}

	if (image->image_data_size < height * src_row_bytes) {
		// 验证源数据是否足够，不足时清理已分配内存
		free(*output);
		*output = NULL;
		return 0;
	}

	for (uint32_t y = 0; y < height; y++) {
		uint8_t* src_row = src + y * src_row_bytes;			// 计算当前处理行的起始位置
		uint8_t* dst_row = dst + y * width * 4;				// 目标缓冲区对应位置
		uint8_t r = 0, g = 0, b = 0, a = 255;

		for (uint32_t x = 0; x < width; x++) {
			switch (color_type) {
				case PNG_COLOR_TYPE_GRAY:
					if (bit_depth == 16) {
						uint16_t gray = (src_row[x * 2] << 8) | src_row[x * 2 + 1];
						r = g = b = (uint8_t)(gray >> 8);
					} else if (bit_depth >= 8) {
						r = g = b = src_row[x];
					} else {
						// 对于位深小于 8 的像素，提取正确的位
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

			if (color_type == PNG_COLOR_TYPE_GRAY && image->transparency) {
				// 检查 tRNS 块数据
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

			if (color_type == PNG_COLOR_TYPE_RGB && image->transparency) {
				// 检查 tRNS 块数据
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

			// DIB（BI_RGB）默认期望 B, G, R, A 顺序
			dst_row[x * 4] = b;
			dst_row[x * 4 + 1] = g;
			dst_row[x * 4 + 2] = r;
			dst_row[x * 4 + 3] = a;
		}
	}

	return 1;
}

/**
 * 读取 PNG 文件，将复杂的文件格式转换为可用的图像数据（入口函数）
 * 
 * @param filename   		PNG 文件绝对路径
 * @param image        		图像结构体
 * 
 * @return      是否还原成功，返回 1(真) 或 0(假)
 */
int png_read_file(const char* filename, PNG_Image* image) {
    FILE* file = fopen(filename, "rb");				// 以二进制模式打开文件
    if (!file) {
		// 文件打开失败
        return 0;
    }
    
    if (!png_validate_signature(file)) {
		// PNG签名无效
        fclose(file);
        return 0;
    }
    
    memset(image, 0, sizeof(PNG_Image));			// 清零图像结构体
    int has_ihdr = 0;								// IHDR 块标志
    int has_idat = 0;								// IDAT 块标志
    int has_iend = 0;								// IEND 块标志
    
    PNG_Chunk chunk;

	// 循环读取 PNG 块直到遇到 IEND 块
    while (!has_iend && png_read_chunk(file, &chunk)) {
        switch (chunk.type) {
            case PNG_CHUNK_IHDR:
                if (has_ihdr || !png_parse_ihdr(&chunk, &image->header)) {
					// 重复 IHDR 或解析失败
                    goto error_cleanup;
                }
                has_ihdr = 1;
                break;
                
            case PNG_CHUNK_PLTE:
                if (!has_ihdr || image->header.color_type == PNG_COLOR_TYPE_GRAY || 
                    image->header.color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
					// 非法颜色类型出现 PLTE
					goto error_cleanup;
                }
                if (!png_parse_plte(&chunk, &image->palette, &image->palette_size)) {
					// 调色板解析失败
                    goto error_cleanup;
                }
                break;
                
            case PNG_CHUNK_tRNS:
                if (!has_ihdr || image->header.color_type == PNG_COLOR_TYPE_GRAY_ALPHA || 
                    image->header.color_type == PNG_COLOR_TYPE_RGBA) {
					// 带 alpha 通道的图像不应有 tRNS
					goto error_cleanup;
                }
                if (!png_parse_trns(&chunk, image->header.color_type, &image->transparency, &image->transparency_size)) {
					// 透明度数据解析失败
                    goto error_cleanup;
                }
                break;
                
            case PNG_CHUNK_IDAT:
                if (!has_ihdr || has_iend) {
					// 必须在 IHDR 后 IEND 前
                    goto error_cleanup;
                }
                if (!png_process_idat(&chunk, &image->image_data, &image->image_data_size)) {
					// 图像数据处理失败
                    goto error_cleanup;
                }
                has_idat = 1;
                break;
                
            case PNG_CHUNK_IEND:
                if (!has_ihdr || !has_idat) {
					// 必须出现在 IHDR 和 IDAT 后
                    goto error_cleanup;
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
    
    // DEFLATE 解压缩图像数据
    uint8_t* decompressed = NULL;
    uint32_t decompressed_size = 0;
    if (!png_decompress_data(image->image_data, image->image_data_size, &decompressed, &decompressed_size)) {
        png_free_image(image);
        return 0;
    }
    
    free(image->image_data);
    image->image_data = decompressed;
    image->image_data_size = decompressed_size;
    
    // 对已解压图像数据应用扫描线滤波
    if (!png_apply_filters(image->image_data, image->image_data_size, &image->header)) {
        png_free_image(image);
        return 0;
    }
    
    return 1;

error_cleanup:
	png_free_chunk(&chunk);
	png_free_image(image);
	fclose(file);

	return 0;
}

/**
 * 释放 PNG_Image 结构体占用的所有动态内存
 * 
 * @param image        		图像结构体
 * 
 * @return      无
 */
void png_free_image(PNG_Image* image) {
	if (!image) {
		return;
	}
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