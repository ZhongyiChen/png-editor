#include "png_decoder.h"
#include <windows.h>
#include <commdlg.h>

#define WINDOW_CLASS_NAME "PNGViewerWindow"
#define WINDOW_TITLE "PNG Viewer"

typedef struct {
    HBITMAP bitmap;
    int width;
    int height;
} ImageData;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void OpenImageFile(HWND hwnd);
void DisplayImage(HWND hwnd, const char* filename);
void CleanupImage(ImageData* imageData);

ImageData g_imageData = { NULL, 0, 0 };

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 注册窗口类
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    
    if (!RegisterClass(&wc)) {
        MessageBox(NULL, "Window Registration Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    // 创建窗口
    HWND hwnd = CreateWindow(
        WINDOW_CLASS_NAME,
        WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL
    );
    
    if (!hwnd) {
        MessageBox(NULL, "Window Creation Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    
    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return msg.wParam;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            // 创建菜单
            HMENU hMenu = CreateMenu();
            HMENU hFileMenu = CreatePopupMenu();
            
            AppendMenu(hFileMenu, MF_STRING, 1, "&Open");
            AppendMenu(hFileMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hFileMenu, MF_STRING, 2, "E&xit");
            
            AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, "&File");
            SetMenu(hwnd, hMenu);
            break;
        }
        
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case 1: // Open
                    OpenImageFile(hwnd);
                    break;
                case 2: // Exit
                    PostQuitMessage(0);
                    break;
            }
            break;
        }
        
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            if (g_imageData.bitmap) {
                HDC hdcMem = CreateCompatibleDC(hdc);
                HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, g_imageData.bitmap);
                
                BITMAP bm;
                GetObject(g_imageData.bitmap, sizeof(bm), &bm);
                
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                
                int x = (clientRect.right - bm.bmWidth) / 2;
                int y = (clientRect.bottom - bm.bmHeight) / 2;
                
                BitBlt(hdc, x, y, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);
                
                SelectObject(hdcMem, hbmOld);
                DeleteDC(hdcMem);
            } else {
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                
                DrawText(
                    hdc,
                    "No image loaded. Use File->Open to load a PNG image.",
                    -1,
                    &clientRect,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE
                );
            }
            
            EndPaint(hwnd, &ps);
            break;
        }
        
        case WM_SIZE: {
            InvalidateRect(hwnd, NULL, TRUE);
            break;
        }
        
        case WM_DESTROY: {
            CleanupImage(&g_imageData);
            PostQuitMessage(0);
            break;
        }
        
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    
    return 0;
}

void OpenImageFile(HWND hwnd) {
    OPENFILENAME ofn;
    char szFile[260] = {0};
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "PNG Files\0*.png\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    
    if (GetOpenFileName(&ofn)) {
        DisplayImage(hwnd, szFile);
    }
}

void DisplayImage(HWND hwnd, const char* filename) {
    PNG_Image pngImage;
    if (!png_read_file(filename, &pngImage)) {
        MessageBox(hwnd, "Failed to load PNG file", "Error", MB_ICONERROR | MB_OK);
        return;
    }
    
    // 转换为RGBA格式
    uint8_t* rgba_data = NULL;
    uint32_t rgba_size = 0;
    if (!png_convert_to_rgba(&pngImage, &rgba_data, &rgba_size)) {
        png_free_image(&pngImage);
        MessageBox(hwnd, "Failed to convert PNG to RGBA", "Error", MB_ICONERROR | MB_OK);
        return;
    }
    
    // 清理之前的图像
    CleanupImage(&g_imageData);
    
    // 创建位图
    HDC hdc = GetDC(hwnd);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = pngImage.header.width;
    bmi.bmiHeader.biHeight = -((int)pngImage.header.height); // 负高度表示从上到下的位图
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    g_imageData.bitmap = CreateDIBSection(
        hdc,
        &bmi,
        DIB_RGB_COLORS,
        NULL,
        NULL,
        0
    );
    
    if (!g_imageData.bitmap) {
        ReleaseDC(hwnd, hdc);
        png_free_image(&pngImage);
        free(rgba_data);
        MessageBox(hwnd, "Failed to create bitmap", "Error", MB_ICONERROR | MB_OK);
        return;
    }
    
    // 设置位图数据
    BITMAP bm;
    GetObject(g_imageData.bitmap, sizeof(bm), &bm);
    
    uint8_t* bits = (uint8_t*)bm.bmBits;
    memcpy(bits, rgba_data, rgba_size);
    
    g_imageData.width = pngImage.header.width;
    g_imageData.height = pngImage.header.height;
    
    // 清理
    ReleaseDC(hwnd, hdc);
    png_free_image(&pngImage);
    free(rgba_data);
    
    // 重绘窗口
    InvalidateRect(hwnd, NULL, TRUE);
    
    // 更新窗口标题
    char title[512];
    snprintf(title, sizeof(title), "%s - %s", WINDOW_TITLE, filename);
    SetWindowText(hwnd, title);
}

void CleanupImage(ImageData* imageData) {
    if (imageData->bitmap) {
        DeleteObject(imageData->bitmap);
        imageData->bitmap = NULL;
    }
    imageData->width = 0;
    imageData->height = 0;
}
