#include <windows.h>
#include <shellapi.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace {
class UniqueHandle {
public:
    explicit UniqueHandle(HANDLE handle = nullptr) : m_handle(handle) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    bool valid() const { return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return m_handle; }

    void reset(HANDLE handle = nullptr) {
        if (valid()) {
            CloseHandle(m_handle);
        }
        m_handle = handle;
    }

private:
    HANDLE m_handle = nullptr;
};

class LocalArguments {
public:
    LocalArguments() : m_argv(CommandLineToArgvW(GetCommandLineW(), &m_argc)) {}
    ~LocalArguments() {
        if (m_argv != nullptr) {
            LocalFree(m_argv);
        }
    }

    LocalArguments(const LocalArguments&) = delete;
    LocalArguments& operator=(const LocalArguments&) = delete;

    bool valid() const { return m_argv != nullptr; }
    int argc() const { return m_argc; }
    wchar_t** argv() const { return m_argv; }

private:
    int m_argc = 0;
    LPWSTR* m_argv = nullptr;
};

void PumpPendingMessages() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

class SplashWindow {
public:
    SplashWindow() = default;
    ~SplashWindow() { CloseImmediately(); }

    SplashWindow(const SplashWindow&) = delete;
    SplashWindow& operator=(const SplashWindow&) = delete;

    bool Show(const std::wstring& status) {
        m_status = status;
        HINSTANCE instance = GetModuleHandleW(nullptr);

        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = SplashWindow::WindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = ClassName();
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        constexpr int width = 440;
        constexpr int height = 230;
        const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        const int x = (screenWidth - width) / 2;
        const int y = (screenHeight - height) / 2;
        m_hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            ClassName(),
            L"ASEapp Surface Builder",
            WS_POPUP,
            x,
            y,
            width,
            height,
            nullptr,
            nullptr,
            instance,
            this);
        if (m_hwnd == nullptr) {
            return false;
        }

        SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(m_hwnd);
        PumpPendingMessages();
        return true;
    }

    void SetStatus(const std::wstring& status) {
        m_status = status;
        if (m_hwnd == nullptr) {
            return;
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        UpdateWindow(m_hwnd);
        PumpPendingMessages();
    }

    void FadeOutAndClose() {
        if (m_hwnd == nullptr) {
            return;
        }
        for (int alpha = 255; alpha >= 0; alpha -= 17) {
            SetLayeredWindowAttributes(m_hwnd, 0, static_cast<BYTE>(alpha), LWA_ALPHA);
            PumpPendingMessages();
            Sleep(12);
        }
        CloseImmediately();
        PumpPendingMessages();
    }

private:
    static const wchar_t* ClassName() { return L"ASEappSurfaceBuilderSplashWindow"; }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
        }

        auto* self = reinterpret_cast<SplashWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self != nullptr && message == WM_PAINT) {
            self->Paint(hwnd);
            return 0;
        }
        if (message == WM_DESTROY && self != nullptr && self->m_hwnd == hwnd) {
            self->m_hwnd = nullptr;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void CloseImmediately() {
        if (m_hwnd != nullptr) {
            HWND hwnd = m_hwnd;
            m_hwnd = nullptr;
            DestroyWindow(hwnd);
        }
    }

    void Paint(HWND hwnd) const {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);

        HBRUSH background = CreateSolidBrush(RGB(248, 250, 252));
        FillRect(dc, &client, background);
        DeleteObject(background);

        HPEN border = CreatePen(PS_SOLID, 1, RGB(205, 213, 224));
        HBRUSH panel = CreateSolidBrush(RGB(255, 255, 255));
        HGDIOBJ oldPen = SelectObject(dc, border);
        HGDIOBJ oldBrush = SelectObject(dc, panel);
        RoundRect(dc, 8, 8, client.right - 8, client.bottom - 8, 22, 22);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(panel);
        DeleteObject(border);

        HICON icon = static_cast<HICON>(LoadImageW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(1),
            IMAGE_ICON,
            76,
            76,
            LR_DEFAULTCOLOR));
        if (icon != nullptr) {
            DrawIconEx(dc, 38, 66, icon, 76, 76, 0, nullptr, DI_NORMAL);
            DestroyIcon(icon);
        }

        SetBkMode(dc, TRANSPARENT);
        HFONT titleFont = CreateFontW(
            -25, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            VARIABLE_PITCH, L"Segoe UI");
        HFONT bodyFont = CreateFontW(
            -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            VARIABLE_PITCH, L"Segoe UI");

        HGDIOBJ oldFont = SelectObject(dc, titleFont);
        SetTextColor(dc, RGB(31, 41, 55));
        RECT title{135, 62, client.right - 34, 96};
        DrawTextW(dc, L"ASEapp Surface Builder", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(dc, bodyFont);
        SetTextColor(dc, RGB(83, 96, 113));
        RECT subtitle{135, 100, client.right - 34, 126};
        DrawTextW(dc, L"Loading the standalone application...", -1, &subtitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SetTextColor(dc, RGB(45, 127, 249));
        RECT status{135, 134, client.right - 34, 160};
        DrawTextW(dc, m_status.c_str(), -1, &status, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT track{135, 174, client.right - 42, 181};
        HBRUSH trackBrush = CreateSolidBrush(RGB(222, 229, 239));
        FillRect(dc, &track, trackBrush);
        DeleteObject(trackBrush);
        RECT progress{track.left, track.top, track.left + ((track.right - track.left) * 62 / 100), track.bottom};
        HBRUSH progressBrush = CreateSolidBrush(RGB(45, 127, 249));
        FillRect(dc, &progress, progressBrush);
        DeleteObject(progressBrush);

        SelectObject(dc, oldFont);
        DeleteObject(bodyFont);
        DeleteObject(titleFont);
        EndPaint(hwnd, &paint);
    }

    HWND m_hwnd = nullptr;
    std::wstring m_status;
};

std::wstring QuoteArg(const std::wstring& arg) {
    if (arg.empty()) {
        return L"\"\"";
    }
    const bool needsQuotes = arg.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!needsQuotes) {
        return arg;
    }

    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        if (backslashes > 0) {
            out.append(backslashes, L'\\');
            backslashes = 0;
        }
        out.push_back(ch);
    }
    if (backslashes > 0) {
        out.append(backslashes * 2, L'\\');
    }
    out.push_back(L'\"');
    return out;
}

std::wstring EscapePowerShellSingleQuoted(std::wstring text) {
    std::wstring escaped;
    escaped.reserve(text.size() + 8);
    for (wchar_t ch : text) {
        if (ch == L'\'') {
            escaped += L"''";
        } else {
            escaped.push_back(ch);
        }
    }
    return escaped;
}

std::wstring JoinArgs(int argc, wchar_t* argv[]) {
    std::wstring args;
    for (int i = 1; i < argc; ++i) {
        if (!args.empty()) {
            args.push_back(L' ');
        }
        args += QuoteArg(argv[i]);
    }
    return args;
}

std::wstring BuildSingleInstancePayload(int argc, wchar_t* argv[]) {
    std::wstring payload;
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i] != nullptr ? argv[i] : L"";
        if (arg.empty() || arg.rfind(L"--", 0) == 0) {
            continue;
        }
        fs::path path(arg);
        if (path.is_relative()) {
            path = fs::absolute(path);
        }
        if (!payload.empty()) {
            payload.push_back(L'\n');
        }
        payload += path.wstring();
    }
    if (payload.empty()) {
        payload = L"__activate__";
    }
    return payload;
}

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
    return utf8;
}

bool SendPayloadToExistingInstance(const std::wstring& payload, DWORD timeoutMs = 30000) {
    const std::wstring pipeName = L"\\\\.\\pipe\\ASEappSurfaceBuilder.SingleInstance";
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    const std::string utf8Payload = ToUtf8(payload);
    if (utf8Payload.empty()) {
        return false;
    }

    while (GetTickCount64() < deadline) {
        UniqueHandle pipe(CreateFileW(pipeName.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
        if (pipe.valid()) {
            DWORD written = 0;
            const BOOL ok = WriteFile(pipe.get(), utf8Payload.data(), static_cast<DWORD>(utf8Payload.size()), &written, nullptr);
            FlushFileBuffers(pipe.get());
            return ok && written == utf8Payload.size();
        }

        const DWORD error = GetLastError();
        if (error == ERROR_PIPE_BUSY) {
            WaitNamedPipeW(pipeName.c_str(), 250);
        } else {
            Sleep(250);
        }
    }
    return false;
}

void ActivateExistingWindow() {
    if (HWND existingWindow = FindWindowW(nullptr, L"ASEapp Surface Builder")) {
        ShowWindow(existingWindow, SW_RESTORE);
        SetForegroundWindow(existingWindow);
    }
}

struct ResourceBytes {
    const void* data = nullptr;
    DWORD size = 0;
};

bool LoadResourceBytes(int resourceId, const wchar_t* resourceType, ResourceBytes& bytes) {
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), resourceType);
    if (!resource) {
        return false;
    }
    HGLOBAL loaded = LoadResource(nullptr, resource);
    if (!loaded) {
        return false;
    }
    const DWORD size = SizeofResource(nullptr, resource);
    const void* data = LockResource(loaded);
    if (!data || size == 0) {
        return false;
    }
    bytes.data = data;
    bytes.size = size;
    return true;
}

std::uint64_t Fnv1a64(const void* data, DWORD size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint64_t hash = 1469598103934665603ull;
    for (DWORD i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::wstring Hex64(std::uint64_t value) {
    constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring out(16, L'0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = hex[value & 0x0f];
        value >>= 4;
    }
    return out;
}

std::wstring PayloadResourceKey(int resourceId, const wchar_t* resourceType) {
    ResourceBytes bytes;
    if (!LoadResourceBytes(resourceId, resourceType, bytes)) {
        return {};
    }
    return Hex64(Fnv1a64(bytes.data, bytes.size)) + L"_" + std::to_wstring(bytes.size);
}

std::wstring GetEnvironmentString(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) {
        return {};
    }
    std::wstring value(length, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), length);
    if (written == 0 || written >= length) {
        return {};
    }
    value.resize(written);
    return value;
}

fs::path CachedPayloadRoot(const std::wstring& payloadKey) {
    if (payloadKey.empty()) {
        return {};
    }
    const std::wstring localAppData = GetEnvironmentString(L"LOCALAPPDATA");
    if (localAppData.empty()) {
        return {};
    }
    return fs::path(localAppData) / L"ASEappSurfaceBuilder" / L"standalone-cache" / payloadKey / L"payload";
}

bool PayloadIsUsable(const fs::path& payloadRoot) {
    if (payloadRoot.empty()) {
        return false;
    }
    return fs::exists(payloadRoot / L"bin\\ASEappNativeUI.exe")
        && fs::exists(payloadRoot / L"plugins\\platforms\\qwindows.dll");
}

bool WriteResourceToFile(int resourceId, const wchar_t* resourceType, const fs::path& outputPath) {
    ResourceBytes bytes;
    if (!LoadResourceBytes(resourceId, resourceType, bytes)) {
        return false;
    }

    UniqueHandle file(CreateFileW(outputPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) {
        return false;
    }

    DWORD written = 0;
    return WriteFile(file.get(), bytes.data, bytes.size, &written, nullptr) && written == bytes.size;
}

bool RunProcess(
    const fs::path& application,
    const std::wstring& commandLine,
    const fs::path& workDir,
    DWORD& exitCode,
    DWORD flags = 0,
    SplashWindow* splashToCloseAfterStart = nullptr)
{
    std::wstring mutableCommandLine = commandLine;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    BOOL ok = CreateProcessW(
        application.c_str(),
        mutableCommandLine.empty() ? nullptr : mutableCommandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        flags,
        nullptr,
        workDir.empty() ? nullptr : workDir.c_str(),
        &startup,
        &process);
    if (!ok) {
        return false;
    }
    UniqueHandle processHandle(process.hProcess);
    UniqueHandle threadHandle(process.hThread);
    if (splashToCloseAfterStart != nullptr) {
        splashToCloseAfterStart->FadeOutAndClose();
    }
    for (;;) {
        const DWORD waitResult = WaitForSingleObject(processHandle.get(), 50);
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
        if (waitResult == WAIT_FAILED) {
            return false;
        }
        PumpPendingMessages();
    }
    DWORD innerExit = 0;
    GetExitCodeProcess(processHandle.get(), &innerExit);
    exitCode = innerExit;
    return true;
}

void ShowError(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"ASEapp Surface Builder", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    LocalArguments arguments;
    if (!arguments.valid()) {
        ShowError(L"Failed to parse command-line arguments.");
        return 1;
    }

    const int argc = arguments.argc();
    wchar_t** argv = arguments.argv();

    UniqueHandle singleInstanceMutex(CreateMutexW(nullptr, TRUE, L"Local\\ASEappSurfaceBuilder.StandaloneLauncher.Mutex"));
    if (singleInstanceMutex.valid() && GetLastError() == ERROR_ALREADY_EXISTS) {
        SendPayloadToExistingInstance(BuildSingleInstancePayload(argc, argv));
        ActivateExistingWindow();
        return 0;
    }

    SplashWindow splash;
    splash.Show(L"Preparing startup files...");

    DWORD exitCode = 0;
    const fs::path tempRoot = fs::temp_directory_path() / (L"aseapp_surface_builder_" + std::to_wstring(GetCurrentProcessId()));
    const fs::path payloadZip = tempRoot / L"payload.zip";
    const fs::path cachedPayloadRoot = CachedPayloadRoot(PayloadResourceKey(101, reinterpret_cast<const wchar_t*>(RT_RCDATA)));
    fs::path payloadRoot = cachedPayloadRoot.empty() ? tempRoot / L"payload" : cachedPayloadRoot;
    bool tempRootCreated = false;
    const auto ensureTempRoot = [&]() {
        if (!tempRootCreated) {
            fs::create_directories(tempRoot);
            tempRootCreated = true;
        }
    };
    const auto cleanupTempRoot = [&]() {
        if (tempRootCreated) {
            fs::remove_all(tempRoot);
        }
    };
    const auto cleanupFailedPayload = [&]() {
        if (!cachedPayloadRoot.empty() && payloadRoot == cachedPayloadRoot) {
            std::error_code error;
            fs::remove_all(payloadRoot, error);
        }
        cleanupTempRoot();
    };

    if (!PayloadIsUsable(payloadRoot)) {
        splash.SetStatus(L"Extracting embedded files...");
        ensureTempRoot();
        std::error_code payloadError;
        fs::remove_all(payloadRoot, payloadError);
        if (!payloadError) {
            fs::create_directories(payloadRoot, payloadError);
        }
        if (payloadError && !cachedPayloadRoot.empty()) {
            payloadRoot = tempRoot / L"payload";
            fs::create_directories(payloadRoot);
        }

        if (!WriteResourceToFile(101, reinterpret_cast<const wchar_t*>(RT_RCDATA), payloadZip)) {
            splash.FadeOutAndClose();
            cleanupFailedPayload();
            ShowError(L"Embedded payload resource was not found.");
            return 1;
        }

        const fs::path powershell = L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
        const std::wstring expandScript =
            L"Expand-Archive -LiteralPath '" + EscapePowerShellSingleQuoted(payloadZip.wstring()) +
            L"' -DestinationPath '" + EscapePowerShellSingleQuoted(payloadRoot.wstring()) + L"' -Force";
        if (!RunProcess(powershell, L"-NoProfile -ExecutionPolicy Bypass -Command " + QuoteArg(expandScript), tempRoot, exitCode, CREATE_NO_WINDOW)) {
            splash.FadeOutAndClose();
            cleanupFailedPayload();
            ShowError(L"Failed to expand the embedded ZIP. PowerShell may be unavailable.");
            return 1;
        }
        if (exitCode != 0) {
            splash.FadeOutAndClose();
            cleanupFailedPayload();
            ShowError(L"Failed to expand the embedded ZIP.");
            return static_cast<int>(exitCode);
        }
    } else {
        splash.SetStatus(L"Using cached startup files...");
    }

    const fs::path appExe = payloadRoot / L"bin\\ASEappNativeUI.exe";
    if (!fs::exists(appExe)) {
        splash.FadeOutAndClose();
        ShowError(L"ASEappNativeUI.exe was not found after extraction.");
        cleanupFailedPayload();
        return 1;
    }

    const fs::path pluginRoot = payloadRoot / L"plugins";
    const fs::path platformRoot = pluginRoot / L"platforms";
    const fs::path windowsPlatformPlugin = platformRoot / L"qwindows.dll";
    if (!fs::exists(windowsPlatformPlugin)) {
        splash.FadeOutAndClose();
        ShowError(L"Qt platform plugin qwindows.dll was not found in the embedded payload.");
        cleanupFailedPayload();
        return 1;
    }
    SetEnvironmentVariableW(L"QT_PLUGIN_PATH", pluginRoot.c_str());
    SetEnvironmentVariableW(L"QT_QPA_PLATFORM_PLUGIN_PATH", platformRoot.c_str());

    const std::wstring innerArgs = JoinArgs(argc, argv);
    splash.SetStatus(L"Starting native UI...");
    if (!RunProcess(appExe, innerArgs, appExe.parent_path(), exitCode, 0, &splash)) {
        splash.FadeOutAndClose();
        ShowError(L"Failed to launch ASEappNativeUI.exe.");
        cleanupTempRoot();
        return 1;
    }

    cleanupTempRoot();
    return static_cast<int>(exitCode);
}
