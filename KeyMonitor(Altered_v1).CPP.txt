#include <iostream>
#include <windows.h>
#include <string>
#include <fstream>
#include <vector>
#include <thread>
#include <atomic>
#include <shellapi.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

// 全局变量
HHOOK g_keyboardHook = nullptr;
std::ofstream g_logFile;
bool g_hideWindow = false;
bool g_outputToFile = false;
bool g_silentMode = false;
std::atomic<bool> g_isRunning(true);
std::string g_outputFilename = "keylog.txt";
HINSTANCE g_hInstance = nullptr;
HWND g_hwndConsole = nullptr;

// 键盘状态记录
struct KeyState {
    bool leftCtrlPressed = false;
    bool leftWinPressed = false;
};
KeyState g_keyState;

// 隐藏控制台窗口
void HideConsoleWindow() {
    if (g_hwndConsole == nullptr) {
        g_hwndConsole = GetConsoleWindow();
    }
    if (g_hwndConsole) {
        ShowWindow(g_hwndConsole, SW_HIDE);
    }
}

// 显示消息框
void ShowMessageBox(const char* title, const char* message) {
    MessageBoxA(nullptr, message, title, MB_OK | MB_ICONINFORMATION);
}

// 获取当前时间字符串
std::string GetCurrentTimeString() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    
    std::ostringstream oss;
    oss << std::setfill('0') 
        << std::setw(4) << st.wYear << "-"
        << std::setw(2) << st.wMonth << "-"
        << std::setw(2) << st.wDay << " "
        << std::setw(2) << st.wHour << ":"
        << std::setw(2) << st.wMinute << ":"
        << std::setw(2) << st.wSecond;
    
    return oss.str();
}

// 获取按键名称 - 修复版（添加 (void)extended 避免警告）
std::string GetKeyName(DWORD vkCode, DWORD scanCode, bool extended) {
    (void)extended; // 避免 unused parameter 警告
    
    // 特殊按键处理
    switch (vkCode) {
        case VK_BACK: return "[Backspace]";
        case VK_TAB: return "[Tab]";
        case VK_RETURN: return "[Enter]";
        case VK_PAUSE: return "[Pause]";
        case VK_CAPITAL: return "[CapsLock]";
        case VK_ESCAPE: return "[Esc]";
        case VK_SPACE: return "[Space]";
        case VK_PRIOR: return "[PageUp]";
        case VK_NEXT: return "[PageDown]";
        case VK_END: return "[End]";
        case VK_HOME: return "[Home]";
        case VK_LEFT: return "[Left]";
        case VK_UP: return "[Up]";
        case VK_RIGHT: return "[Right]";
        case VK_DOWN: return "[Down]";
        case VK_INSERT: return "[Insert]";
        case VK_DELETE: return "[Delete]";
        case VK_LWIN: return "[Win]";
        case VK_RWIN: return "[Win]";
        case VK_APPS: return "[Menu]";
        case VK_NUMLOCK: return "[NumLock]";
        case VK_SCROLL: return "[ScrollLock]";
        case VK_F1: return "[F1]";
        case VK_F2: return "[F2]";
        case VK_F3: return "[F3]";
        case VK_F4: return "[F4]";
        case VK_F5: return "[F5]";
        case VK_F6: return "[F6]";
        case VK_F7: return "[F7]";
        case VK_F8: return "[F8]";
        case VK_F9: return "[F9]";
        case VK_F10: return "[F10]";
        case VK_F11: return "[F11]";
        case VK_F12: return "[F12]";
        
        // 小键盘数字键 - 添加[NUM]标记并加上空格
        case VK_NUMPAD0: return "[NUM] 0";
        case VK_NUMPAD1: return "[NUM] 1";
        case VK_NUMPAD2: return "[NUM] 2";
        case VK_NUMPAD3: return "[NUM] 3";
        case VK_NUMPAD4: return "[NUM] 4";
        case VK_NUMPAD5: return "[NUM] 5";
        case VK_NUMPAD6: return "[NUM] 6";
        case VK_NUMPAD7: return "[NUM] 7";
        case VK_NUMPAD8: return "[NUM] 8";
        case VK_NUMPAD9: return "[NUM] 9";
        case VK_MULTIPLY: return "[NUM] *";
        case VK_ADD: return "[NUM] +";
        case VK_SUBTRACT: return "[NUM] -";
        case VK_DECIMAL: return "[NUM] .";
        case VK_DIVIDE: return "[NUM] /";
        
        // 控制键 - 区分左右
        case VK_LSHIFT: return "[LShift]";
        case VK_RSHIFT: return "[RShift]";
        case VK_LCONTROL: return "[LCtrl]";
        case VK_RCONTROL: return "[RCtrl]";
        case VK_LMENU: return "[LAlt]";
        case VK_RMENU: return "[RAlt]";
        
        // 通用控制键（不区分左右时使用）
        case VK_SHIFT: return "[Shift]";
        case VK_CONTROL: return "[Ctrl]";
        case VK_MENU: return "[Alt]";
        
        // OEM键
        case VK_OEM_1: return ";";
        case VK_OEM_PLUS: return "=";
        case VK_OEM_COMMA: return ",";
        case VK_OEM_MINUS: return "-";
        case VK_OEM_PERIOD: return ".";
        case VK_OEM_2: return "/";
        case VK_OEM_3: return "`";
        case VK_OEM_4: return "[";
        case VK_OEM_5: return "\\";
        case VK_OEM_6: return "]";
        case VK_OEM_7: return "'";
    }
    
    // 尝试获取可打印字符
    BYTE keyboardState[256];
    GetKeyboardState(keyboardState);
    
    // 获取键盘布局
    HKL layout = GetKeyboardLayout(0);
    
    // 转换虚拟键码到字符
    WCHAR wchar[10] = {0};
    int result = ToUnicodeEx(vkCode, scanCode, keyboardState, wchar, 10, 0, layout);
    
    if (result > 0) {
        char mbchar[10] = {0};
        WideCharToMultiByte(CP_ACP, 0, wchar, result, mbchar, 10, NULL, NULL);
        
        // 检查是否是可打印字符
        if (mbchar[0] >= 32 && mbchar[0] <= 126) {
            return std::string(mbchar);
        }
    }
    
    // 如果无法转换，返回虚拟键码
    std::ostringstream oss;
    oss << "[VK_" << vkCode << "]";
    return oss.str();
}

// 获取修饰键状态
std::string GetModifierState() {
    std::string modifiers;
    
    if (GetAsyncKeyState(VK_LCONTROL) & 0x8000) modifiers += "[LCtrl]";
    else if (GetAsyncKeyState(VK_RCONTROL) & 0x8000) modifiers += "[RCtrl]";
    else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) modifiers += "[Ctrl]";
    
    if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) modifiers += "[LShift]";
    else if (GetAsyncKeyState(VK_RSHIFT) & 0x8000) modifiers += "[RShift]";
    else if (GetAsyncKeyState(VK_SHIFT) & 0x8000) modifiers += "[Shift]";
    
    if (GetAsyncKeyState(VK_LMENU) & 0x8000) modifiers += "[LAlt]";
    else if (GetAsyncKeyState(VK_RMENU) & 0x8000) modifiers += "[RAlt]";
    else if (GetAsyncKeyState(VK_MENU) & 0x8000) modifiers += "[Alt]";
    
    if (GetAsyncKeyState(VK_LWIN) & 0x8000) modifiers += "[LWin]";
    else if (GetAsyncKeyState(VK_RWIN) & 0x8000) modifiers += "[RWin]";
    else if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) modifiers += "[Win]";
    
    return modifiers;
}

// 检查退出组合键 (Left Ctrl + Win)
bool IsExitCombination(DWORD vkCode, bool keyDown) {
    // 更新按键状态
    if (vkCode == VK_LCONTROL) {
        g_keyState.leftCtrlPressed = keyDown;
    } else if (vkCode == VK_LWIN) {
        g_keyState.leftWinPressed = keyDown;
    }
    
    // 检查左Ctrl和左Win是否同时按下
    if (g_keyState.leftCtrlPressed && g_keyState.leftWinPressed) {
        return true;
    }
    
    return false;
}

// 键盘钩子回调函数
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // Never process keystrokes while the visible console is unavailable.
    if (!g_hwndConsole || !IsWindowVisible(g_hwndConsole) || IsIconic(g_hwndConsole)) {
        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
    }
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* pKey = (KBDLLHOOKSTRUCT*)lParam;
        
        bool keyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        
        // 检查退出组合键
        if (keyDown && IsExitCombination(pKey->vkCode, keyDown)) {
            g_isRunning = false;
            PostQuitMessage(0);
            return 1; // 阻止传递
        }
        
        // 记录按键按下事件
        if (wParam == WM_KEYDOWN) {
            // 获取按键信息
            std::string keyName = GetKeyName(pKey->vkCode, pKey->scanCode, pKey->flags & LLKHF_EXTENDED);
            std::string modifiers = GetModifierState();
            
            // 构建日志条目
            std::string logEntry = "[" + GetCurrentTimeString() + "] ";
            
            if (!modifiers.empty()) {
                logEntry += modifiers + " + ";
            }
            
            logEntry += keyName + "\n";
            
            // 输出到文件
            if (g_outputToFile && g_logFile.is_open()) {
                g_logFile << logEntry;
                g_logFile.flush(); // 确保立即写入
            }
            
            // 只有在非静默模式且没有使用-o选项时才输出到控制台
            if (!g_silentMode && !g_outputToFile) {
                std::cout << logEntry;
            }
        }
        
        // 更新按键释放状态
        if (wParam == WM_KEYUP) {
            if (pKey->vkCode == VK_LCONTROL) {
                g_keyState.leftCtrlPressed = false;
            } else if (pKey->vkCode == VK_LWIN) {
                g_keyState.leftWinPressed = false;
            }
        }
    }
    
    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

// 解析命令行参数
void ParseCommandLine(int argc, char* argv[]) {
    // 显示帮助
    bool showHelp = false;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-o" && i + 1 < argc) {
            g_outputFilename = argv[++i];
            g_outputToFile = true;
            // File output does not enable silent operation.
        } else if (arg == "-h" || arg == "--silent") {
            std::cerr << "Hidden and silent operation are disabled.\n";
            exit(1);
        } else if (arg == "--help") {
            showHelp = true;
        } else {
            std::cerr << "Unknown parameter: " << arg << std::endl;
            std::cerr << "Use --help for help information" << std::endl;
            exit(1);
        }
    }
    
    if (showHelp) {
        std::cout << "Keyboard Monitor Usage Instructions:\n";
        std::cout << "  No parameters          Default run, display keys in console\n";
        std::cout << "  -o <filename>          Output logs to specified file (visible mode)\n";
        std::cout << "  -h                     Disabled: hidden window\n";
        std::cout << "  --silent               Disabled: silent mode\n";
        std::cout << "  --help                 Display help information\n";
        std::cout << "\nExit method: Press Left Ctrl + Win key combination\n";
        std::cout << "\nExamples:\n";
        std::cout << "  keymonitor.exe                 // Default run, console display\n";
        std::cout << "  keymonitor.exe -o log.txt      // Visible logging to file\n";
        std::cout << "  keymonitor.exe -h              // Disabled: hidden window\n";
        std::cout << "  keymonitor.exe -h --silent     // Disabled hidden/silent options\n";
        exit(0);
    }
}

// 创建系统托盘图标 - 修复版
void CreateSystemTrayIcon(HWND hwnd) {
    NOTIFYICONDATAA nid;
    memset(&nid, 0, sizeof(nid)); // 正确初始化为0
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = hwnd;
    nid.uID = 100;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 1;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strcpy_s(nid.szTip, sizeof(nid.szTip), "Keyboard Monitor Running");
    
    Shell_NotifyIconA(NIM_ADD, &nid);
    
    if (nid.hIcon) {
        DestroyIcon(nid.hIcon);
    }
}

// 移除系统托盘图标 - 修复版
void RemoveSystemTrayIcon(HWND hwnd) {
    NOTIFYICONDATAA nid;
    memset(&nid, 0, sizeof(nid)); // 正确初始化为0
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = hwnd;
    nid.uID = 100;
    
    Shell_NotifyIconA(NIM_DELETE, &nid);
}

// 消息处理函数（用于系统托盘）
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_USER + 1:
            if (lParam == WM_RBUTTONUP) {
                // 右键菜单
                HMENU hMenu = CreatePopupMenu();
                AppendMenuA(hMenu, MF_STRING, 1001, "Exit");
                
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            break;
            
        case WM_COMMAND:
            if (LOWORD(wParam) == 1001) {
                g_isRunning = false;
                PostQuitMessage(0);
            }
            break;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
    }
    
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// 创建隐藏窗口（用于接收消息）- 修复版
HWND CreateHiddenWindow() {
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc)); // 正确初始化为0
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = g_hInstance;
    wc.lpszClassName = "KeyboardMonitorClass";
    
    if (!RegisterClassExA(&wc)) {
        return NULL;
    }
    
    HWND hwnd = CreateWindowExA(
        0,
        "KeyboardMonitorClass",
        "Keyboard Monitor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        400, 300,
        NULL, NULL,
        g_hInstance,
        NULL
    );
    
    if (hwnd) {
        ShowWindow(hwnd, SW_HIDE);
        UpdateWindow(hwnd);
        CreateSystemTrayIcon(hwnd);
    }
    
    return hwnd;
}

// 检查管理员权限
bool IsRunningAsAdmin() {
    BOOL isElevated = FALSE;
    HANDLE hToken = NULL;
    
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD dwSize;
        
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
            isElevated = elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    
    return isElevated != FALSE;
}

// 初始化程序
bool InitializeProgram() {
    // 检查管理员权限
    if (!IsRunningAsAdmin()) {
        if (!g_silentMode) {
            std::cerr << "Warning: Recommended to run as administrator for better compatibility!" << std::endl;
        }
    }
    
    // 隐藏窗口
    if (g_hideWindow) {
        HideConsoleWindow();
    }
    
    // 打开日志文件
    if (g_outputToFile) {
        g_logFile.open(g_outputFilename, std::ios::app);
        if (!g_logFile.is_open()) {
            if (!g_silentMode) {
                std::cerr << "Unable to open log file: " << g_outputFilename << std::endl;
            }
            return false;
        }
        // 写入文件头
        g_logFile << "\n=== Keyboard Monitoring Session Started at " << GetCurrentTimeString() << " ===\n";
        g_logFile.flush();
    }
    
    return true;
}

// 显示启动信息
void ShowStartupInfo() {
    // 只有在非静默模式且没有使用-o选项时才显示启动信息
    {
        std::cout << "=== Windows Keyboard Monitor ===" << std::endl;
        std::cout << "Press Left Ctrl + Win to exit program" << std::endl;
        if (g_outputToFile) std::cout << "Recording keystrokes to: " << g_outputFilename << std::endl;
        
        if (g_outputToFile) {
            std::cout << "Logs saved to: " << g_outputFilename << std::endl;
        }
        
        if (g_hideWindow) {
            std::cout << "Program running in background" << std::endl;
        }
        
        std::cout << "==========================" << std::endl;
        std::cout << "Keyboard monitoring started..." << std::endl;
    }
}

// 清理资源
void CleanupResources(HWND hHiddenWindow) {
    // 卸载键盘钩子
    if (g_keyboardHook != nullptr) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
    
    // 移除系统托盘图标
    if (hHiddenWindow) {
        RemoveSystemTrayIcon(hHiddenWindow);
        DestroyWindow(hHiddenWindow);
    }
    
    // 关闭日志文件
    if (g_logFile.is_open()) {
        g_logFile << "=== Keyboard Monitoring Session Ended at " << GetCurrentTimeString() << " ===\n\n";
        g_logFile.close();
    }
}

// 显示退出信息
void ShowExitInfo() {
    // 显示退出消息
    if (g_hideWindow) {
        ShowMessageBox("Keyboard Monitor", "Program has ended");
    } else if (!g_silentMode && !g_outputToFile) {
        std::cout << "\nProgram exited" << std::endl;
        
        if (g_outputToFile) {
            std::cout << "Logs saved to: " << g_outputFilename << std::endl;
        }
    }
}

// 主函数
int main(int argc, char* argv[]) {
    g_hInstance = GetModuleHandle(NULL);
    
    // 解析命令行参数
    ParseCommandLine(argc, argv);
    
    // Explicit consent is mandatory before initializing logs or installing hooks.
    g_hwndConsole = GetConsoleWindow();
    if (!g_hwndConsole) {
        MessageBoxA(nullptr, "A visible console window is required. Monitoring will not start.",
                    "Keyboard Monitor", MB_OK | MB_ICONERROR);
        return 1;
    }
    ShowWindow(g_hwndConsole, SW_SHOW);
    if (IsIconic(g_hwndConsole)) ShowWindow(g_hwndConsole, SW_RESTORE);
    SetForegroundWindow(g_hwndConsole);
    const int consent = MessageBoxA(g_hwndConsole,
        "This application monitors keyboard input across Windows applications, "
        "including potentially sensitive information.\n\n"
        "Do you consent to begin monitoring? Choose No to exit.",
        "Keyboard Monitoring Consent", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (consent != IDYES) return 0;

    // 初始化程序
    if (!InitializeProgram()) {
        return 1;
    }
    
    // 创建隐藏窗口（用于消息循环）
    HWND hHiddenWindow = nullptr;
    if (g_hideWindow) {
        hHiddenWindow = CreateHiddenWindow();
    }
    
    // 安装键盘钩子
    g_keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, g_hInstance, 0);
    
    if (g_keyboardHook == NULL) {
        DWORD error = GetLastError();
        if (!g_silentMode) {
            std::cerr << "Keyboard hook installation failed! Error code: " << error << std::endl;
            
            if (error == 5) {
                std::cerr << "Please run this program as administrator!" << std::endl;
            }
        }
        
        CleanupResources(hHiddenWindow);
        return 1;
    }
    
    // 显示启动信息
    ShowStartupInfo();
    
    // Fail closed if the console is hidden, minimized, or closed.
    UINT_PTR visibilityTimer = SetTimer(nullptr, 1, 250, nullptr);
    if (!visibilityTimer) {
        CleanupResources(hHiddenWindow);
        return 1;
    }

    // 消息循环
    MSG msg;
    while (g_isRunning && GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_TIMER && msg.hwnd == nullptr && msg.wParam == visibilityTimer) {
            if (!IsWindow(g_hwndConsole) || !IsWindowVisible(g_hwndConsole) || IsIconic(g_hwndConsole)) {
                g_isRunning = false;
                break;
            }
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    KillTimer(nullptr, visibilityTimer);
    // 清理资源
    CleanupResources(hHiddenWindow);
    
    // 显示退出信息
    ShowExitInfo();
    
    return 0;
}
