#ifndef PNG_DECODER_H
#define PNG_DECODER_H

#include <stdint.h>
#include <stdio.h>

/**
 * PNG文件签名
 * 
 * 0x89：           非 ASCII 字符，用于检测文件是否为二进制。
 * 0x50 0x4E 0x47： ASCII 字符 "PNG"。
 * 0x0D 0x0A：      DOS 换行符（\r\n）。
 * 0x1A：           DOS 文件结束符（EOF）。
 * 0x0A：           Unix 换行符（\n）。
 */
#define PNG_SIGNATURE "\x89PNG\r\n\x1a\n"
#define PNG_SIGNATURE_SIZE 8

// 长度检查 100MB
#define MAX_CHUNK_LENGTH (100 * 1024 * 1024)

// 块类型
#define PNG_CHUNK_IHDR 0x49484452
#define PNG_CHUNK_IDAT 0x49444154
#define PNG_CHUNK_IEND 0x49454E44
#define PNG_CHUNK_PLTE 0x504C5445
#define PNG_CHUNK_tRNS 0x74524E53

// 颜色类型
#define PNG_COLOR_TYPE_GRAY 0
#define PNG_COLOR_TYPE_RGB 2
#define PNG_COLOR_TYPE_PALETTE 3
#define PNG_COLOR_TYPE_GRAY_ALPHA 4
#define PNG_COLOR_TYPE_RGBA 6

// 压缩方法
#define PNG_COMPRESSION_METHOD_DEFLATE 0

// 过滤方法
#define PNG_FILTER_METHOD_ADAPTIVE 0

// 交织方法
#define PNG_INTERLACE_METHOD_NONE 0
#define PNG_INTERLACE_METHOD_ADAM7 1

typedef struct {
    uint32_t width;                 // 图像的宽度（像素），大端序
    uint32_t height;                // 图像的高度（像素），大端序
    uint8_t bit_depth;              // 每个颜色通道的位数（如 1、2、4、8、16）
    uint8_t color_type;             // 颜色类型（如灰度、RGB、调色板等），与 bit_depth 共同决定像素格式。
    uint8_t compression_method;     // 压缩方法（PNG 标准中固定为 0，表示 DEFLATE 压缩）
    uint8_t filter_method;          // 滤波方法（PNG 标准中固定为 0，表示自适应滤波）
    uint8_t interlace_method;       // 隔行扫描方法（0：非隔行；1：Adam7 隔行）
} PNG_IHDR;

typedef struct {
    uint32_t length;                // data 字段的实际字节数（不包括 type 和 crc）
    uint32_t type;                  // 4 字节的 ASCII 字符，标识 chunk 类型（如 IHDR、IDAT、IEND 等）
    uint8_t* data;                  // 可变长度数组。实际长度由 length 字段决定，可能为 0（如 IEND chunk 的 data 为空）
    uint32_t crc;                   // 循环冗余校验值，覆盖 type 和 data 字段，用于检测数据错误
} PNG_Chunk;

typedef struct {
    uint8_t red;                    // 红色分量
    uint8_t green;                  // 绿色分量
    uint8_t blue;                   // 蓝色分量
} PNG_PaletteEntry;

typedef struct {
    PNG_IHDR header;
    PNG_PaletteEntry* palette;
    uint32_t palette_size;
    uint8_t* transparency;
    uint32_t transparency_size;
    uint8_t* image_data;
    uint32_t image_data_size;
} PNG_Image;

int png_validate_signature(FILE* file);
int png_read_chunk(FILE* file, PNG_Chunk* chunk);
void png_free_chunk(PNG_Chunk* chunk);
int png_parse_ihdr(PNG_Chunk* chunk, PNG_IHDR* ihdr);
int png_parse_plte(PNG_Chunk* chunk, PNG_PaletteEntry** palette, uint32_t* palette_size);
int png_parse_trns(PNG_Chunk* chunk, uint8_t color_type, uint8_t** transparency, uint32_t* transparency_size);
int png_process_idat(PNG_Chunk* chunk, uint8_t** image_data, uint32_t* image_data_size);
int png_decompress_data(uint8_t* compressed, uint32_t compressed_size, uint8_t** decompressed, uint32_t* decompressed_size);
int png_apply_filters(uint8_t* image_data, uint32_t image_data_size, PNG_IHDR* header);
int png_convert_to_rgba(PNG_Image* image, uint8_t** output, uint32_t* output_size);
int png_read_file(const char* filename, PNG_Image* image);
void png_free_image(PNG_Image* image);

#endif // PNG_DECODER_H
