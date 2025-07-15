#include "png_decoder.h"
#include <stdint.h>
#include <windows.h>
#include <commdlg.h>

#define WINDOW_CLASS_NAME "PNGViewerWindow"
#define WINDOW_TITLE "PNG Viewer"

typedef struct {
    HBITMAP bitmap;             // Windows GDI 位图句柄，用于存储解码后的 PNG 图像数据
    uint32_t width;             // 图像像素宽度，用于计算显示位置和缩放比例
    uint32_t height;            // 图像像素高度，用于计算显示位置和缩放比例
    float scale;                // 当前缩放比例
} ImageData;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void OpenImageFile(HWND hwnd);
void DisplayImage(HWND hwnd, const char* filename);
void CleanupImage(ImageData* imageData);
