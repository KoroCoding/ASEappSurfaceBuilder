#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
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

    HANDLE file = CreateFileW(outputPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    const bool ok = WriteFile(file, data, size, &written, nullptr) && written == size;
    CloseHandle(file);
    return ok;
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
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD innerExit = 0;
    GetExitCodeProcess(process.hProcess, &innerExit);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    exitCode = innerExit;
    return true;
}

void ShowError(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"ASEapp Surface Builder", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        ShowError(L"Failed to parse command-line arguments.");
        return 1;
    }

    const fs::path tempRoot = fs::temp_directory_path() / (L"aseapp_surface_builder_" + std::to_wstring(GetCurrentProcessId()));
    const fs::path payloadRoot = tempRoot / L"payload";
    const fs::path payloadZip = tempRoot / L"payload.zip";
    fs::create_directories(payloadRoot);

    if (!WriteResourceToFile(101, reinterpret_cast<const wchar_t*>(RT_RCDATA), payloadZip)) {
        ShowError(L"Embedded payload resource was not found.");
        LocalFree(argv);
        return 1;
    }

    DWORD exitCode = 0;
    const fs::path powershell = L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    const std::wstring expandScript =
        L"Expand-Archive -LiteralPath '" + EscapePowerShellSingleQuoted(payloadZip.wstring()) +
        L"' -DestinationPath '" + EscapePowerShellSingleQuoted(payloadRoot.wstring()) + L"' -Force";
    if (!RunProcess(powershell, L"-NoProfile -ExecutionPolicy Bypass -Command " + QuoteArg(expandScript), tempRoot, exitCode, CREATE_NO_WINDOW)) {
        ShowError(L"Failed to expand the embedded ZIP. PowerShell may be unavailable.");
        LocalFree(argv);
        return 1;
    }
    if (exitCode != 0) {
        ShowError(L"Failed to expand the embedded ZIP.");
        LocalFree(argv);
        return static_cast<int>(exitCode);
    }

    const fs::path appExe = payloadRoot / L"bin\\ASEappNativeUI.exe";
    if (!fs::exists(appExe)) {
        ShowError(L"ASEappNativeUI.exe was not found after extraction.");
        fs::remove_all(tempRoot);
        LocalFree(argv);
        return 1;
    }

    const fs::path pluginRoot = payloadRoot / L"plugins";
    const fs::path platformRoot = pluginRoot / L"platforms";
    const fs::path windowsPlatformPlugin = platformRoot / L"qwindows.dll";
    if (!fs::exists(windowsPlatformPlugin)) {
        ShowError(L"Qt platform plugin qwindows.dll was not found in the embedded payload.");
        fs::remove_all(tempRoot);
        LocalFree(argv);
        return 1;
    }
    SetEnvironmentVariableW(L"QT_PLUGIN_PATH", pluginRoot.c_str());
    SetEnvironmentVariableW(L"QT_QPA_PLATFORM_PLUGIN_PATH", platformRoot.c_str());

    const std::wstring innerArgs = JoinArgs(argc, argv);
    if (!RunProcess(appExe, innerArgs, appExe.parent_path(), exitCode)) {
        ShowError(L"Failed to launch ASEappNativeUI.exe.");
        fs::remove_all(tempRoot);
        LocalFree(argv);
        return 1;
    }

    fs::remove_all(tempRoot);
    LocalFree(argv);
    return static_cast<int>(exitCode);
}
