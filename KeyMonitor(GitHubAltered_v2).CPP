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
#include <commdlg.h>
#include <cwchar>

// 全局变量
HHOOK g_keyboardHook = nullptr;
std::ofstream g_logFile;
bool g_hideWindow = false;
bool g_outputToFile = false;
bool g_silentMode = false;
std::atomic<bool> g_isRunning(true);
std::string g_outputFilename = "keylog.txt";
HINSTANCE g_hInstance = nullptr;
HWND g_hwndConsole = nullptr; // Original field, retained (unused by GUI).
HWND g_hwndMain = nullptr;
HWND g_hwndLog = nullptr;
HWND g_hwndStatus = nullptr;
HWND g_hwndStart = nullptr;
HWND g_hwndStop = nullptr;
HWND g_hwndSave = nullptr;
HWND g_hwndClear = nullptr;
HFONT g_font = nullptr;
std::string g_sessionLog;

// The monitoring controls are attached only to this visible window.
void StopMonitoring();
void AppendLog(const std::string& text);

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
    // Never process keystrokes when the GUI is stopped, hidden or minimized.
    if (!g_keyboardHook || !g_hwndMain || !IsWindow(g_hwndMain) ||
        !IsWindowVisible(g_hwndMain) || IsIconic(g_hwndMain)) {
        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
    }
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* pKey = (KBDLLHOOKSTRUCT*)lParam;
        
        bool keyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        
        // 检查退出组合键
        if (keyDown && IsExitCombination(pKey->vkCode, keyDown)) {
            StopMonitoring();
            PostQuitMessage(0); // Preserve the original Ctrl+Win application exit.
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
            
            // GUI presentation replaces console output; the original entry is unchanged.
            AppendLog(logEntry);
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


// Visible Win32 interface based on the supplied screenshot.
// The key-name mapping, modifier mapping and hook event rules above are preserved.
static const int ID_START = 101, ID_STOP = 102, ID_SAVE = 103, ID_CLEAR = 104;

void UpdateControls() {
    const bool running = (g_keyboardHook != nullptr);
    if (g_hwndStart) EnableWindow(g_hwndStart, !running);
    if (g_hwndStop) EnableWindow(g_hwndStop, running);
    if (g_hwndStatus) SetWindowTextA(g_hwndStatus,
        running ? "Monitoring active. Press Left Ctrl + Win to stop." : "Stopped. No monitoring.");
}

void AppendLog(const std::string& entry) {
    g_sessionLog += entry;
    if (!g_hwndLog) return;
    // Keep a live, read-only, horizontally and vertically scrollable event log.
    const int length = GetWindowTextLengthA(g_hwndLog);
    SendMessageA(g_hwndLog, EM_SETSEL, length, length);
    SendMessageA(g_hwndLog, EM_REPLACESEL, FALSE,
        reinterpret_cast<LPARAM>(entry.c_str()));
    SendMessageA(g_hwndLog, EM_SCROLLCARET, 0, 0);
}

void StopMonitoring() {
    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
    if (g_logFile.is_open()) {
        g_logFile << "=== Keyboard Monitoring Session Ended at "
                  << GetCurrentTimeString() << " ===\n\n";
        g_logFile.close();
    }
    g_keyState = KeyState{};
    UpdateControls();
}

void StartMonitoring() {
    if (g_keyboardHook || !g_hwndMain || !IsWindowVisible(g_hwndMain) || IsIconic(g_hwndMain)) return;
    const int consent = MessageBoxA(g_hwndMain,
        "This application monitors keyboard input across Windows applications, "
        "including potentially sensitive information.\n\n"
        "Do you consent to begin monitoring? Choose No to remain stopped.",
        "Keyboard Monitoring Consent", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (consent != IDYES) return;

    if (g_outputToFile) {
        g_logFile.open(g_outputFilename, std::ios::app);
        if (!g_logFile.is_open()) {
            MessageBoxA(g_hwndMain, "Unable to open the configured log file.",
                        "Keyboard Monitor", MB_OK | MB_ICONERROR);
            return;
        }
        g_logFile << "\n=== Keyboard Monitoring Session Started at "
                  << GetCurrentTimeString() << " ===\n";
        g_logFile.flush();
    }

    g_keyboardHook = SetWindowsHookExA(WH_KEYBOARD_LL, LowLevelKeyboardProc, g_hInstance, 0);
    if (!g_keyboardHook) {
        const DWORD error = GetLastError();
        if (g_logFile.is_open()) g_logFile.close();
        std::ostringstream message;
        message << "Keyboard hook installation failed. Windows error: " << error;
        MessageBoxA(g_hwndMain, message.str().c_str(), "Keyboard Monitor", MB_OK | MB_ICONERROR);
        return;
    }
    UpdateControls();
}

void SaveDisplayedLog() {
    char filename[MAX_PATH] = "KeyboardMonitorLog.txt";
    OPENFILENAMEA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_hwndMain;
    dialog.lpstrFilter = "Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = "txt";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameA(&dialog)) return;
    std::ofstream file(filename, std::ios::binary | std::ios::trunc);
    if (!file) {
        MessageBoxA(g_hwndMain, "Unable to save the log file.",
                    "Keyboard Monitor", MB_OK | MB_ICONERROR);
        return;
    }
    file.write(g_sessionLog.data(), static_cast<std::streamsize>(g_sessionLog.size()));
    if (!file) MessageBoxA(g_hwndMain, "Error while writing the log file.",
                            "Keyboard Monitor", MB_OK | MB_ICONERROR);
}

void Layout(HWND hwnd) {
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (g_hwndStart) MoveWindow(g_hwndStart, 22, 145, 237, 44, TRUE);
    if (g_hwndStop)  MoveWindow(g_hwndStop, 276, 145, 130, 44, TRUE);
    if (g_hwndSave)  MoveWindow(g_hwndSave, 425, 145, 146, 44, TRUE);
    if (g_hwndClear) MoveWindow(g_hwndClear, 590, 145, 132, 44, TRUE);
    if (g_hwndStatus) MoveWindow(g_hwndStatus, 23, 202, std::max(100, width - 45), 26, TRUE);
    if (g_hwndLog) MoveWindow(g_hwndLog, 22, 257, std::max(100, width - 56),
                             std::max(100, height - 289), TRUE);
}

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hwndMain = hwnd;
        g_font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto label = CreateWindowExA(0, "STATIC",
            "Monitors keyboard input across Windows applications. Start requires consent.",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 23, 65, 1090, 45, hwnd, nullptr, g_hInstance, nullptr);
        g_hwndStart = CreateWindowExA(0, "BUTTON", "Start (I consent)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,0,0,0, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_START)), g_hInstance, nullptr);
        g_hwndStop = CreateWindowExA(0, "BUTTON", "Stop",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,0,0,0, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STOP)), g_hInstance, nullptr);
        g_hwndSave = CreateWindowExA(0, "BUTTON", "Save log",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,0,0,0, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SAVE)), g_hInstance, nullptr);
        g_hwndClear = CreateWindowExA(0, "BUTTON", "Clear",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,0,0,0, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CLEAR)), g_hInstance, nullptr);
        g_hwndStatus = CreateWindowExA(0, "STATIC", "Stopped. No monitoring.",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 0,0,0,0, hwnd, nullptr, g_hInstance, nullptr);
        g_hwndLog = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            0,0,0,0, hwnd, nullptr, g_hInstance, nullptr);
        for (HWND child : {label, g_hwndStart, g_hwndStop, g_hwndSave, g_hwndClear, g_hwndStatus, g_hwndLog})
            SendMessageA(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        Layout(hwnd);
        UpdateControls();
        return 0;
    }
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) StopMonitoring();
        else Layout(hwnd);
        return 0;
    case WM_SHOWWINDOW:
        if (!wParam) StopMonitoring();
        break;
    case WM_ACTIVATE:
        // Never monitor while the visible window is minimized or absent.
        if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) StopMonitoring();
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_START: StartMonitoring(); return 0;
        case ID_STOP: StopMonitoring(); return 0;
        case ID_SAVE: SaveDisplayedLog(); return 0;
        case ID_CLEAR:
            g_sessionLog.clear();
            SetWindowTextA(g_hwndLog, "");
            return 0;
        }
        break;
    case WM_CLOSE:
        StopMonitoring();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        StopMonitoring();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    g_hInstance = instance;
    // Preserve optional -o logging, disable original hidden/silent modes.
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return 1;
    for (int i = 1; i < count; ++i) {
        if (wcscmp(args[i], L"-o") == 0 && i + 1 < count) {
            ++i;
            int bytes = WideCharToMultiByte(CP_UTF8, 0, args[i], -1, nullptr, 0, nullptr, nullptr);
            std::string filename(bytes, '\0');
            WideCharToMultiByte(CP_UTF8, 0, args[i], -1, &filename[0], bytes, nullptr, nullptr);
            filename.resize(bytes - 1);
            g_outputFilename = filename;
            g_outputToFile = true;
        } else {
            MessageBoxA(nullptr, "Unsupported option. Hidden and silent modes are disabled.\n"
                "Optional file logging: KeyMonitor.exe -o filename.txt",
                "Keyboard Monitor", MB_OK | MB_ICONERROR);
            LocalFree(args);
            return 1;
        }
    }
    LocalFree(args);
    WNDCLASSEXA windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = MainWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = "VisibleKeyboardMonitor";
    if (!RegisterClassExA(&windowClass)) return 1;
    HWND window = CreateWindowExA(0, windowClass.lpszClassName,
        "Visible Keyboard Monitor", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1180, 811,
        nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageA(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    StopMonitoring();
    return static_cast<int>(message.wParam);
}
