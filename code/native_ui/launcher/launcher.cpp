#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

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

bool WriteResourceToFile(int resourceId, const wchar_t* resourceType, const fs::path& outputPath) {
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

    UniqueHandle file(CreateFileW(outputPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) {
        return false;
    }

    DWORD written = 0;
    return WriteFile(file.get(), data, size, &written, nullptr) && written == size;
}

bool RunProcess(const fs::path& application, const std::wstring& commandLine, const fs::path& workDir, DWORD& exitCode, DWORD flags = 0) {
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
    WaitForSingleObject(processHandle.get(), INFINITE);
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

    const fs::path tempRoot = fs::temp_directory_path() / (L"aseapp_surface_builder_" + std::to_wstring(GetCurrentProcessId()));
    const fs::path payloadRoot = tempRoot / L"payload";
    const fs::path payloadZip = tempRoot / L"payload.zip";
    fs::create_directories(payloadRoot);

    if (!WriteResourceToFile(101, reinterpret_cast<const wchar_t*>(RT_RCDATA), payloadZip)) {
        ShowError(L"Embedded payload resource was not found.");
        return 1;
    }

    DWORD exitCode = 0;
    const fs::path powershell = L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    const std::wstring expandScript =
        L"Expand-Archive -LiteralPath '" + EscapePowerShellSingleQuoted(payloadZip.wstring()) +
        L"' -DestinationPath '" + EscapePowerShellSingleQuoted(payloadRoot.wstring()) + L"' -Force";
    if (!RunProcess(powershell, L"-NoProfile -ExecutionPolicy Bypass -Command " + QuoteArg(expandScript), tempRoot, exitCode, CREATE_NO_WINDOW)) {
        ShowError(L"Failed to expand the embedded ZIP. PowerShell may be unavailable.");
        return 1;
    }
    if (exitCode != 0) {
        ShowError(L"Failed to expand the embedded ZIP.");
        return static_cast<int>(exitCode);
    }

    const fs::path appExe = payloadRoot / L"bin\\ASEappNativeUI.exe";
    if (!fs::exists(appExe)) {
        ShowError(L"ASEappNativeUI.exe was not found after extraction.");
        fs::remove_all(tempRoot);
        return 1;
    }

    const fs::path pluginRoot = payloadRoot / L"plugins";
    const fs::path platformRoot = pluginRoot / L"platforms";
    const fs::path windowsPlatformPlugin = platformRoot / L"qwindows.dll";
    if (!fs::exists(windowsPlatformPlugin)) {
        ShowError(L"Qt platform plugin qwindows.dll was not found in the embedded payload.");
        fs::remove_all(tempRoot);
        return 1;
    }
    SetEnvironmentVariableW(L"QT_PLUGIN_PATH", pluginRoot.c_str());
    SetEnvironmentVariableW(L"QT_QPA_PLATFORM_PLUGIN_PATH", platformRoot.c_str());

    const std::wstring innerArgs = JoinArgs(argc, argv);
    if (!RunProcess(appExe, innerArgs, appExe.parent_path(), exitCode)) {
        ShowError(L"Failed to launch ASEappNativeUI.exe.");
        fs::remove_all(tempRoot);
        return 1;
    }

    fs::remove_all(tempRoot);
    return static_cast<int>(exitCode);
}
