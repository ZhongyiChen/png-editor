#include "png_viewer.h"


ImageData g_imageData = { NULL, 0, 0, 1 };

/**
 * Windows 应用程序的入口函数
 * 
 * @param hInstance        	当前程序实例的句柄
 * @param hPrevInstance     废弃参数（始终为 NULL）
 * @param lpCmdLine        	命令行参数字符串
 * @param nCmdShow        	窗口的初始显示状态（如最小化、最大化）
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 注册窗口类
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;                        // 指定窗口过程函数（WindowProc），用于处理窗口消息
    wc.hInstance = hInstance;                           // 绑定当前程序实例
    wc.lpszClassName = WINDOW_CLASS_NAME;               // 窗口类名
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);           // 设置默认光标为箭头（IDC_ARROW）
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);      // 设置窗口背景色为系统默认（COLOR_WINDOW + 1）
    
    // 向系统注册窗口类
    if (!RegisterClass(&wc)) {
        MessageBox(NULL, "Window Registration Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    // 创建窗口
    HWND hwnd = CreateWindow(
        WINDOW_CLASS_NAME,          // 窗口类名
        WINDOW_TITLE,               // 窗口标题
        WS_OVERLAPPEDWINDOW,        // 窗口样式（标准重叠窗口，含标题栏、边框、最小化/最大化按钮等）
        CW_USEDEFAULT,              // 初始 x 位置（默认）
        CW_USEDEFAULT,              // 初始 y 位置（默认）
        800,                        // 窗口宽度
        600,                        // 窗口高度
        NULL,                       // 父窗口句柄
        NULL,                       // 菜单句柄
        hInstance,                  // 程序实例句柄
        NULL                        // 附加数据
    );
    
    if (!hwnd) {
        // 如果窗口创建失败，弹出错误对话框并退出
        MessageBox(NULL, "Window Creation Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    ShowWindow(hwnd, nCmdShow);     // 根据 nCmdShow 显示窗口（如正常显示、最小化等）
    UpdateWindow(hwnd);             // 强制立即发送 WM_PAINT 消息，触发窗口首次绘制
    
    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {              // 从消息队列中获取消息
        TranslateMessage(&msg);                             // 将虚拟键消息（如按键）转换为字符消息
        DispatchMessage(&msg);                              // 将消息分发给窗口过程（WindowProc）
    }
    
    return msg.wParam;                                      // 返回 WM_QUIT 消息的 wParam（通常是退出代码）
}

/**
 * 处理发送到窗口的所有消息
 * 
 * @param hwnd        		窗口句柄
 * @param uMsg        		消息类型（如 WM_PAINT、WM_COMMAND）
 * @param wParam        	消息的附加参数 1
 * @param lParam        	消息的附加参数 2
 */
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        // 窗口创建时触发
        case WM_CREATE: {
            HMENU hMenu = CreateMenu();                     // 主菜单栏
            HMENU hFileMenu = CreatePopupMenu();            // 子菜单栏
            
            AppendMenu(hFileMenu, MF_STRING, 1, "&Open");   // ID = 1
            AppendMenu(hFileMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hFileMenu, MF_STRING, 2, "E&xit");   // ID = 2
            
            AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, "&File");  // 将子菜单附加到主菜单的 "File" 项
            SetMenu(hwnd, hMenu);                           // 将菜单绑定到窗口
            break;
        }
        
        // 菜单或控件触发
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                // Open
                case 1:
                    OpenImageFile(hwnd);
                    break;
                // Exit
                case 2:
                    PostQuitMessage(0);                     // 退出程序
                    break;
            }
            break;
        }
        
        // 窗口需要重绘时触发
        case WM_PAINT: {
            PAINTSTRUCT ps;                                         // 存储绘图信息（如需要重绘的区域）
            HDC hdc = BeginPaint(hwnd, &ps);                        // 获取窗口的设备上下文 - DC （必须与 EndPaint 成对调用，否则会导致绘图资源泄漏）
            
            if (g_imageData.bitmap) {
                HDC hdcMem = CreateCompatibleDC(hdc);               // 创建与窗口 DC 兼容的内存 DC，用于离线绘图
                HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, g_imageData.bitmap); // 将位图（bitmap）选入内存 DC，并保存旧位图句柄
                
                BITMAP bm;
                GetObject(g_imageData.bitmap, sizeof(bm), &bm);     // 获取位图信息（宽度、高度等），存储到 bm 结构体
                
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);                   // 获取窗口客户区的尺寸（不包括标题栏、边框等）
                
                int x = (clientRect.right - bm.bmWidth) / 2;        // 居中坐标 x
                int y = (clientRect.bottom - bm.bmHeight) / 2;      // 居中坐标 y
                
                // 将内存 DC 中的位图复制到窗口 DC。参数说明：
                //      hdc：目标 DC（窗口）
                //      (x, y)：目标起始坐标（居中位置）
                //      bm.bmWidth 和 bm.bmHeight：图像尺寸
                //      hdcMem：源 DC（内存 DC）
                //      (0, 0)：源位图起始坐标
                //      SRCCOPY：直接复制模式
                BitBlt(hdc, x, y, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);
                
                SelectObject(hdcMem, hbmOld);                       // 恢复内存 DC 的旧位图（避免资源泄漏）
                DeleteDC(hdcMem);                                   // 释放内存 DC
            } else {
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                
                // 显示提示文本。参数说明：
                //      hdc：目标 DC（窗口）
                //      文本内容：提示用户加载图像
                //      -1：自动计算文本长度
                //      &clientRect：文本绘制区域（整个客户区）
                //      DT_CENTER | DT_VCENTER：水平和垂直居中
                //      DT_SINGLELINE：单行文本
                DrawText(
                    hdc,
                    "No image loaded. Use File->Open to load a PNG image.",
                    -1,
                    &clientRect,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE
                );
            }
            
            EndPaint(hwnd, &ps);                                    // BeginPaint 和 EndPaint 必须成对调用
            break;
        }
        
        // 窗口大小改变时触发
        case WM_SIZE: {
            InvalidateRect(hwnd, NULL, TRUE);                       // 强制重绘窗口 (标记整个窗口为“脏区”，触发 WM_PAINT 消息)
            break;
        }
        
        // 窗口关闭时触发
        case WM_DESTROY: {
            CleanupImage(&g_imageData);                             // 释放位图资源
            PostQuitMessage(0);                                     // 发送退出消息，结束消息循环
            break;
        }
        
        // 未显式处理的消息（如系统菜单、窗口拖动等）
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);       // 必须调用 DefWindowProc，否则窗口行为会异常
    }
    
    return 0;
}

/**
 * 打开文件对话框并加载用户选择的 PNG 图像
 * 
 * @param hwnd        		窗口句柄，作为文件对话框的父窗口
 */
void OpenImageFile(HWND hwnd) {
    // Windows 文件对话框的结构体，用于配置对话框行为
    OPENFILENAME ofn;
    // 缓冲区，用于存储用户选择的文件路径（初始化为全零，260 是 Windows 文件路径的最大长度）
    char szFile[260] = {0};
    
    ZeroMemory(&ofn, sizeof(ofn));                              // 清空 ofn 结构体，避免未初始化数据
    ofn.lStructSize = sizeof(ofn);                              // 必须设置为 sizeof(OPENFILENAME)，供 Windows 验证版本
    ofn.hwndOwner = hwnd;                                       // 设置父窗口，使对话框模态化（阻止父窗口操作）
    ofn.lpstrFile = szFile;                                     // 指向文件路径缓冲区
    ofn.nMaxFile = sizeof(szFile);                              // 缓冲区大小
    ofn.lpstrFilter = "PNG Files\0*.png\0All Files\0*.*\0";     // 设置过滤器：显示 "PNG Files"，仅允许 .png 文件；显示 "All Files"，允许所有文件
    ofn.nFilterIndex = 1;                                       // 默认选中第一个过滤器
    ofn.lpstrFileTitle = NULL;                                  // 设为 NULL，表示不需要单独获取文件名（该文件名不含路径）
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;                                 // 设为 NULL，表示使用系统默认的初始目录
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;          // 设置对话框行为标志：用户只能选择 已存在的路径 或 已存在的文件
    
    // 显示标准 Windows 文件打开对话框
    if (GetOpenFileName(&ofn)) {
        // 如果用户选择了文件，则调用 DisplayImage 加载并显示该 PNG
        DisplayImage(hwnd, szFile);
    }
}

/**
 * 加载并显示图像
 * 
 * @param hwnd        		窗口句柄，用于显示图像和错误提示
 * @param filename   		PNG 文件绝对路径
 */
void DisplayImage(HWND hwnd, const char* filename) {
    PNG_Image pngImage;
    if (!png_read_file(filename, &pngImage)) {
        MessageBox(hwnd, "Failed to load PNG file", "Error", MB_ICONERROR | MB_OK);
        return;
    }
    
    // 转换为 RGBA 格式
    uint8_t* rgba_data = NULL;
    uint32_t rgba_size = 0;
    if (!png_convert_to_rgba(&pngImage, &rgba_data, &rgba_size)) {
        png_free_image(&pngImage);
        MessageBox(hwnd, "Failed to convert PNG to RGBA", "Error", MB_ICONERROR | MB_OK);
        return;
    }
    
    // 清理之前的图像
    CleanupImage(&g_imageData);
    
    // 创建 DIB（设备无关位图）
    HDC hdc = GetDC(hwnd);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = pngImage.header.width;
    bmi.bmiHeader.biHeight = -(pngImage.header.height);   // 负高度表示从上到下的位图（Windows 默认是从下到上）
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;                                  // 32 位 RGBA 格式
    bmi.bmiHeader.biCompression = BI_RGB;                           // 未压缩格式
    
    // 创建一块可以写入像素数据的位图
    g_imageData.bitmap = CreateDIBSection(
        hdc,
        &bmi,
        DIB_RGB_COLORS,
        NULL,
        NULL,
        0
    );
    
    if (!g_imageData.bitmap) {
        // 如果位图创建失败，释放 DC、PNG 数据和 RGBA 数据
        ReleaseDC(hwnd, hdc);
        png_free_image(&pngImage);
        free(rgba_data);
        MessageBox(hwnd, "Failed to create bitmap", "Error", MB_ICONERROR | MB_OK);
        return;
    }
    
    // 获取位图信息（如 bmBits，即像素数据指针）
    BITMAP bm;
    GetObject(g_imageData.bitmap, sizeof(bm), &bm);
    
    // 复制像素数据到位图
    uint8_t* bits = (uint8_t*)bm.bmBits;
    memcpy(bits, rgba_data, rgba_size);
    
    // 更新图像尺寸
    g_imageData.width = pngImage.header.width;
    g_imageData.height = pngImage.header.height;
    
    // 清理资源
    ReleaseDC(hwnd, hdc);
    png_free_image(&pngImage);
    free(rgba_data);
    
    // 重绘窗口，触发 WM_PAINT 消息
    InvalidateRect(hwnd, NULL, TRUE);
    
    // 更新窗口标题
    char title[512];
    snprintf(title, sizeof(title), "%s - %s", WINDOW_TITLE, filename);
    SetWindowText(hwnd, title);
}

/**
 * 清理图像数据 - 释放位图资源，重置图像尺寸
 */
void CleanupImage(ImageData* imageData) {
    if (!imageData) {
        return;
    }
    if (imageData->bitmap) {
        DeleteObject(imageData->bitmap);
        imageData->bitmap = NULL;
    }
    imageData->width = 0;
    imageData->height = 0;
    imageData->scale = 1;
}
