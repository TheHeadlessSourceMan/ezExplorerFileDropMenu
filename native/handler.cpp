#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objidl.h>
#include <shlwapi.h>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

const CLSID CLSID_ezExplorerFileDropMenu = {0x6d4d8ef0, 0x3e69, 0x4f5d, {0x8d, 0x5a, 0x1f, 0x8c, 0xb0, 0xf2, 0xb9, 0xc4}};
HINSTANCE g_instance = nullptr;
long g_objects = 0;
long g_locks = 0;
constexpr size_t kMaxCommandLine = 32767;

std::wstring InstallDirectory() {
    wchar_t* app_data = nullptr;
    size_t length = 0;
    _wdupenv_s(&app_data, &length, L"LOCALAPPDATA");
    std::wstring result = app_data ? app_data : L".";
    free(app_data);
    return result + L"\\ezExplorerFileDropMenu";
}

void Log(const std::wstring& message) {
    try {
        std::wstring directory = InstallDirectory() + L"\\logs";
        std::filesystem::create_directories(directory);
        int size = WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()), nullptr, 0, nullptr, nullptr);
        std::string utf8(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()), utf8.data(), size, nullptr, nullptr);
        std::ofstream output(std::filesystem::path(directory + L"\\handler.log"), std::ios::binary | std::ios::app);
        output << utf8 << "\n";
    } catch (...) {
    }
}

std::wstring ExpandEnvironment(const std::wstring& value) {
    DWORD size = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (!size) return value;
    std::wstring result(size, L'\0');
    ExpandEnvironmentStringsW(value.c_str(), result.data(), size);
    result.resize(size - 1);
    return result;
}

struct Entry {
    std::wstring name;
    std::wstring icon;
    std::wstring cmdline;
    HBITMAP bitmap = nullptr;
};

void AppendUtf8(std::string& out, unsigned long code) {
    if (code < 0x80) out += static_cast<char>(code);
    else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
    else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18)); out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

// Minimal reader for an array of flat objects whose interesting values are strings.
class JsonReader {
public:
    explicit JsonReader(const std::string& text) : text_(text) {}

    bool ReadEntries(std::vector<Entry>& entries) {
        SkipWhitespace();
        if (Peek() == '{') { Entry entry; if (!ReadObject(entry)) return false; entries.push_back(entry); return true; }
        if (!Consume('[')) return false;
        SkipWhitespace();
        if (Consume(']')) return true;
        for (;;) {
            Entry entry;
            if (!ReadObject(entry)) return false;
            entries.push_back(entry);
            SkipWhitespace();
            if (Consume(',')) continue;
            return Consume(']');
        }
    }

private:
    const std::string& text_;
    size_t position_ = 0;

    char Peek() const { return position_ < text_.size() ? text_[position_] : '\0'; }
    void SkipWhitespace() { while (position_ < text_.size() && strchr(" \t\r\n", text_[position_])) ++position_; }
    bool Consume(char expected) {
        SkipWhitespace();
        if (Peek() != expected) return false;
        ++position_;
        return true;
    }

    bool ReadHex4(unsigned long& value) {
        if (position_ + 4 > text_.size()) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text_[position_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= c - '0';
            else if (c >= 'a' && c <= 'f') value |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') value |= c - 'A' + 10;
            else return false;
        }
        return true;
    }

    bool ReadString(std::wstring& result) {
        if (!Consume('"')) return false;
        std::string utf8;
        while (position_ < text_.size()) {
            char c = text_[position_++];
            if (c == '"') {
                int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
                result.assign(size, L'\0');
                if (size) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), size);
                return true;
            }
            if (c != '\\') { utf8 += c; continue; }
            if (position_ >= text_.size()) return false;
            char escape = text_[position_++];
            switch (escape) {
            case 'n': utf8 += '\n'; break;
            case 'r': utf8 += '\r'; break;
            case 't': utf8 += '\t'; break;
            case 'b': utf8 += '\b'; break;
            case 'f': utf8 += '\f'; break;
            case 'u': {
                unsigned long code = 0;
                if (!ReadHex4(code)) return false;
                if (code >= 0xD800 && code < 0xDC00) {
                    unsigned long low = 0;
                    if (text_.compare(position_, 2, "\\u") == 0) {
                        position_ += 2;
                        if (!ReadHex4(low)) return false;
                    }
                    code = (low >= 0xDC00 && low < 0xE000) ? 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00) : 0xFFFD;
                } else if (code >= 0xDC00 && code < 0xE000) {
                    code = 0xFFFD;
                }
                AppendUtf8(utf8, code);
                break;
            }
            default: utf8 += escape; break;
            }
        }
        return false;
    }

    bool SkipValue() {
        SkipWhitespace();
        if (Peek() == '"') { std::wstring ignored; return ReadString(ignored); }
        if (Peek() == '{' || Peek() == '[') {
            int depth = 0;
            while (position_ < text_.size()) {
                char c = text_[position_];
                if (c == '"') { std::wstring ignored; if (!ReadString(ignored)) return false; continue; }
                ++position_;
                if (c == '{' || c == '[') ++depth;
                else if ((c == '}' || c == ']') && --depth == 0) return true;
            }
            return false;
        }
        while (position_ < text_.size() && !strchr(",}] \t\r\n", text_[position_])) ++position_;
        return true;
    }

    bool ReadObject(Entry& entry) {
        if (!Consume('{')) return false;
        SkipWhitespace();
        if (Consume('}')) return true;
        for (;;) {
            std::wstring key;
            if (!ReadString(key) || !Consume(':')) return false;
            SkipWhitespace();
            std::wstring* target = key == L"name" ? &entry.name : key == L"icon" ? &entry.icon : key == L"cmdline" ? &entry.cmdline : nullptr;
            if (target && Peek() == '"') { if (!ReadString(*target)) return false; }
            else if (!SkipValue()) return false;
            if (Consume(',')) continue;
            return Consume('}');
        }
    }
};

bool LoadEntries(std::vector<Entry>& entries) {
    std::ifstream input(std::filesystem::path(InstallDirectory() + L"\\menu.json"), std::ios::binary);
    if (!input) return false;
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    std::vector<Entry> parsed;
    if (!JsonReader(text).ReadEntries(parsed)) {
        Log(L"menu.json is not valid; expected an array of {name, icon, cmdline} objects");
        return false;
    }
    for (Entry& entry : parsed) {
        if (!entry.name.empty() && !entry.cmdline.empty()) entries.push_back(entry);
    }
    return true;
}

HBITMAP LoadMenuBitmap(const std::wstring& spec) {
    if (spec.empty()) return nullptr;
    int size = GetSystemMetrics(SM_CXSMICON);
    std::wstring path = ExpandEnvironment(spec);
    int index = 0;
    size_t comma = path.rfind(L',');
    if (comma != std::wstring::npos) {
        index = _wtoi(path.c_str() + comma + 1);
        path.resize(comma);
    }
    HICON icon = nullptr;
    if (_wcsicmp(PathFindExtensionW(path.c_str()), L".ico") == 0) {
        icon = static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, size, size, LR_LOADFROMFILE));
    } else {
        ExtractIconExW(path.c_str(), index, nullptr, &icon, 1);
    }
    if (!icon) return nullptr;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC context = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap) {
        HGDIOBJ previous = SelectObject(context, bitmap);
        DrawIconEx(context, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
        SelectObject(context, previous);
    }
    DeleteDC(context);
    ReleaseDC(nullptr, screen);
    DestroyIcon(icon);
    return bitmap;
}

// Follows CommandLineToArgvW rules so each substituted path stays one argument.
std::wstring EscapePath(const std::wstring& value, bool wrap, bool closingQuoteFollows) {
    std::wstring result = wrap ? L"\"" : L"";
    size_t backslashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++backslashes; continue; }
        if (c == L'"') { result.append(backslashes * 2 + 1, L'\\'); backslashes = 0; result += L'"'; continue; }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result += c;
    }
    result.append(wrap || closingQuoteFollows ? backslashes * 2 : backslashes, L'\\');
    if (wrap) result += L'"';
    return result;
}

std::wstring Expand(const std::wstring& pattern, const std::vector<std::wstring>& files, const std::wstring& target) {
    static const wchar_t* kFile = L"{file}";
    static const wchar_t* kFiles = L"{files}";
    static const wchar_t* kTarget = L"{targetDir}";
    std::wstring result;
    bool in_quotes = false;
    size_t backslashes = 0;
    for (size_t i = 0; i < pattern.size();) {
        wchar_t c = pattern[i];
        const wchar_t* token = nullptr;
        if (c == L'{') {
            for (const wchar_t* candidate : {kFile, kFiles, kTarget}) {
                if (pattern.compare(i, wcslen(candidate), candidate) == 0) { token = candidate; break; }
            }
        }
        if (!token) {
            if (c == L'"' && backslashes % 2 == 0) in_quotes = !in_quotes;
            backslashes = c == L'\\' ? backslashes + 1 : 0;
            result += c;
            ++i;
            continue;
        }
        i += wcslen(token);
        bool closing = i < pattern.size() && pattern[i] == L'"';
        std::vector<std::wstring> values;
        if (token == kTarget) values.push_back(target);
        else values = files;
        for (size_t index = 0; index < values.size(); ++index) {
            if (index) result += L' ';
            result += EscapePath(values[index], !in_quotes, closing);
        }
        backslashes = 0;
    }
    return result;
}

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    bool result = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated;
    CloseHandle(token);
    return result;
}

// Re-runs the command through the "runas" verb, which shows the UAC prompt.
void RunElevated(const std::wstring& command, const std::wstring& directory) {
    size_t split = 0;
    std::wstring file;
    if (!command.empty() && command[0] == L'"') {
        size_t close = command.find(L'"', 1);
        if (close == std::wstring::npos) return;
        file = command.substr(1, close - 1);
        split = close + 1;
    } else {
        split = command.find(L' ');
        if (split == std::wstring::npos) split = command.size();
        file = command.substr(0, split);
    }
    std::wstring parameters = command.substr(split);
    SHELLEXECUTEINFOW info = {sizeof(info)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = file.c_str();
    info.lpParameters = parameters.c_str();
    info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) {
        Log(L"elevated launch failed (" + std::to_wstring(GetLastError()) + L"): " + command);
        return;
    }
    if (info.hProcess) CloseHandle(info.hProcess);
}

struct Job {
    std::wstring command;
    std::wstring directory;
    HMODULE module;
};

// Synchronous; returns the process exit code, or -1 if it could not be started.
// Overwrites logs\lastcommand.log with the command line, its stdout/stderr and the exit code.
int ExecuteCommand(const std::wstring& command, const std::wstring& directory) {
    static std::mutex serialize;  // keeps lastcommand.log coherent when several jobs start at once
    std::lock_guard<std::mutex> lock(serialize);
    Log(L"executing: " + command + L" (cwd: " + directory + L")");
    HANDLE output = INVALID_HANDLE_VALUE;
    try {
        std::wstring logs = InstallDirectory() + L"\\logs";
        std::filesystem::create_directories(logs);
        SECURITY_ATTRIBUTES inherit = {sizeof(inherit), nullptr, TRUE};
        output = CreateFileW((logs + L"\\lastcommand.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
    } catch (...) {
    }
    auto write = [&](const std::wstring& text) {
        if (output == INVALID_HANDLE_VALUE) return;
        int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string utf8(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(output, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    };
    write(L"cmdline: " + command + L"\r\ncwd: " + directory + L"\r\n--- output ---\r\n");
    std::wstring mutable_command = command;
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup = {sizeof(startup)};
    if (output != INVALID_HANDLE_VALUE) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = output;
        startup.hStdError = output;
        startup.hStdInput = INVALID_HANDLE_VALUE;
    }
    PROCESS_INFORMATION process = {};
    BOOL started = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, output != INVALID_HANDLE_VALUE,
        CREATE_NO_WINDOW, nullptr, directory.empty() ? nullptr : directory.c_str(), &startup, &process);
    int result = -1;
    if (started) {
        CloseHandle(process.hThread);
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hProcess);
        result = static_cast<int>(code);
        Log(L"command exited with " + std::to_wstring(code));
        write(L"\r\n--- exit code " + std::to_wstring(code) + L" ---\r\n");
        if (code != 0 && !IsElevated()) {
            Log(L"retrying elevated: " + command);
            write(L"retrying elevated (output not captured)\r\n");
            RunElevated(command, directory);
        }
    } else if (GetLastError() == ERROR_ELEVATION_REQUIRED) {
        write(L"\r\n--- elevation required; launching elevated (output not captured) ---\r\n");
        RunElevated(command, directory);
    } else {
        std::wstring failure = L"CreateProcess failed (" + std::to_wstring(GetLastError()) + L"): " + command;
        Log(failure);
        write(L"\r\n--- " + failure + L" ---\r\n");
    }
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    return result;
}

DWORD WINAPI RunJob(LPVOID parameter) {
    std::unique_ptr<Job> job(static_cast<Job*>(parameter));
    bool initialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    ExecuteCommand(job->command, job->directory);
    if (initialized) CoUninitialize();
    HMODULE module = job->module;
    job.reset();
    FreeLibraryAndExitThread(module, 0);
}

// Runs on a worker thread so Explorer is not blocked while waiting for the exit code.
bool Launch(const std::wstring& command, const std::wstring& directory) {
    if (command.size() >= kMaxCommandLine) {
        Log(L"command line too long (" + std::to_wstring(command.size()) + L" characters): " + command.substr(0, 200));
        return false;
    }
    HMODULE module = nullptr;
    // Pins the DLL so it stays loaded until the worker finishes.
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&RunJob), &module)) return false;
    auto* job = new (std::nothrow) Job{command, directory, module};
    HANDLE thread = job ? CreateThread(nullptr, 0, RunJob, job, 0, nullptr) : nullptr;
    if (!thread) {
        delete job;
        FreeLibrary(module);
        return false;
    }
    CloseHandle(thread);
    return true;
}

std::vector<std::wstring> BuildCommands(const std::wstring& raw, const std::vector<std::wstring>& files, const std::wstring& target);

class Handler final : public IContextMenu, public IShellExtInit {
public:
    Handler() { ++g_objects; }
    ~Handler() {
        ClearEntries();
        --g_objects;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == IID_IUnknown || iid == IID_IContextMenu) *result = static_cast<IContextMenu*>(this);
        else if (iid == IID_IShellExtInit) *result = static_cast<IShellExtInit*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&references_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = static_cast<ULONG>(InterlockedDecrement(&references_));
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Initialize(LPCITEMIDLIST folder, IDataObject* data, HKEY) override {
        if (!folder || !data) return E_INVALIDARG;
        wchar_t destination[MAX_PATH] = {};
        if (!SHGetPathFromIDListW(folder, destination)) return E_INVALIDARG;
        destination_ = destination;
        FORMATETC format = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium = {};
        HRESULT result = data->GetData(&format, &medium);
        if (FAILED(result)) return result;
        HDROP drop = static_cast<HDROP>(medium.hGlobal);
        UINT count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
        sources_.clear();
        for (UINT index = 0; index < count; ++index) {
            UINT length = DragQueryFileW(drop, index, nullptr, 0);
            std::wstring source(length + 1, L'\0');
            DragQueryFileW(drop, index, source.data(), length + 1);
            source.resize(length);
            sources_.push_back(source);
        }
        ReleaseStgMedium(&medium);
        return sources_.empty() ? DV_E_FORMATETC : S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryContextMenu(HMENU menu, UINT index, UINT first, UINT last, UINT flags) override {
        if (flags & CMF_DEFAULTONLY) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        try {
            ClearEntries();
            if (!LoadEntries(entries_) || entries_.empty()) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
            UINT available = last >= first ? last - first + 1 : 0;
            if (entries_.size() > available) entries_.resize(available);
            InsertMenuW(menu, index++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
            for (size_t i = 0; i < entries_.size(); ++i) {
                Entry& entry = entries_[i];
                entry.bitmap = LoadMenuBitmap(entry.icon);
                MENUITEMINFOW item = {sizeof(item)};
                item.fMask = MIIM_ID | MIIM_STRING | MIIM_FTYPE;
                item.fType = MFT_STRING;
                item.wID = first + static_cast<UINT>(i);
                item.dwTypeData = entry.name.data();
                if (entry.bitmap) { item.fMask |= MIIM_BITMAP; item.hbmpItem = entry.bitmap; }
                InsertMenuItemW(menu, index++, TRUE, &item);
            }
            return MAKE_HRESULT(SEVERITY_SUCCESS, 0, static_cast<USHORT>(entries_.size()));
        } catch (...) {
            return E_FAIL;
        }
    }
    HRESULT STDMETHODCALLTYPE InvokeCommand(LPCMINVOKECOMMANDINFO info) override {
        Log(L"InvokeCommand verb=" + std::to_wstring(reinterpret_cast<uintptr_t>(info ? info->lpVerb : nullptr)) +
            L" entries=" + std::to_wstring(entries_.size()));
        if (!info || HIWORD(info->lpVerb) || LOWORD(info->lpVerb) >= entries_.size()) return E_INVALIDARG;
        try {
            bool ok = true;
            for (const std::wstring& command : BuildCommands(entries_[LOWORD(info->lpVerb)].cmdline, sources_, destination_))
                ok &= Launch(command, destination_);
            return ok ? S_OK : E_FAIL;
        } catch (...) {
            return E_FAIL;
        }
    }
    HRESULT STDMETHODCALLTYPE GetCommandString(UINT_PTR id, UINT flags, UINT*, LPSTR buffer, UINT length) override {
        if (!buffer || !length) return E_INVALIDARG;
        if (flags == GCS_VERBA) {
            std::string verb = "slx_" + std::to_string(id);
            lstrcpynA(buffer, verb.c_str(), static_cast<int>(length));
            return S_OK;
        }
        return E_NOTIMPL;
    }
private:
    void ClearEntries() {
        for (Entry& entry : entries_) if (entry.bitmap) DeleteObject(entry.bitmap);
        entries_.clear();
    }
    LONG references_ = 1;
    std::wstring destination_;
    std::vector<std::wstring> sources_;
    std::vector<Entry> entries_;
};

class Factory final : public IClassFactory {
public:
    Factory() { ++g_objects; }
    ~Factory() { --g_objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == IID_IUnknown || iid == IID_IClassFactory) *result = static_cast<IClassFactory*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&references_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = static_cast<ULONG>(InterlockedDecrement(&references_));
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** result) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        Handler* handler = new (std::nothrow) Handler();
        if (!handler) return E_OUTOFMEMORY;
        HRESULT status = handler->QueryInterface(iid, result);
        handler->Release();
        return status;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) ++g_locks; else --g_locks;
        return S_OK;
    }
private:
    LONG references_ = 1;
};

// Splits "a|b|c" into parts.
std::vector<std::wstring> SplitArguments(const std::wstring& text) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (;;) {
        size_t bar = text.find(L'|', start);
        parts.push_back(text.substr(start, bar == std::wstring::npos ? bar : bar - start));
        if (bar == std::wstring::npos) return parts;
        start = bar + 1;
    }
}

// Same pattern handling as InvokeCommand: env expansion, then {file} once per file or {files} once.
std::vector<std::wstring> BuildCommands(const std::wstring& raw, const std::vector<std::wstring>& files, const std::wstring& target) {
    const std::wstring pattern = ExpandEnvironment(raw);
    std::vector<std::wstring> commands;
    if (pattern.find(L"{file}") != std::wstring::npos) {
        for (const std::wstring& source : files) commands.push_back(Expand(pattern, {source}, target));
    } else {
        commands.push_back(Expand(pattern, files, target));
    }
    return commands;
}

// Exported for testing. Writes the expanded command lines (one per line) to `output`; returns the required length.
extern "C" int WINAPI EzExpandCommandLine(const wchar_t* pattern, const wchar_t* target, const wchar_t* const* files,
    int fileCount, wchar_t* output, int capacity) {
    std::vector<std::wstring> list;
    for (int i = 0; i < fileCount; ++i) list.push_back(files[i]);
    std::wstring joined;
    for (const std::wstring& command : BuildCommands(pattern, list, target)) joined += command + L"\n";
    if (output && capacity > static_cast<int>(joined.size())) wcscpy_s(output, capacity, joined.c_str());
    return static_cast<int>(joined.size()) + 1;
}

// rundll32 handler.dll,ExpandW pattern|targetDir|file1|file2...  (result goes to handler.log)
extern "C" void CALLBACK ExpandW(HWND, HINSTANCE, LPWSTR arguments, int) {
    std::vector<std::wstring> parts = SplitArguments(arguments ? arguments : L"");
    Log(L"Expand: raw arguments: " + std::wstring(arguments ? arguments : L""));
    if (parts.size() < 3) { Log(L"Expand: expected pattern|targetDir|file1[|file2...]"); return; }
    for (const std::wstring& command : BuildCommands(parts[0], std::vector<std::wstring>(parts.begin() + 2, parts.end()), parts[1]))
        Log(L"Expand: " + command);
}

// Same arguments as ExpandW, but also runs each expanded command synchronously.
extern "C" void CALLBACK RunW(HWND, HINSTANCE, LPWSTR arguments, int) {
    std::vector<std::wstring> parts = SplitArguments(arguments ? arguments : L"");
    if (parts.size() < 3) { Log(L"Run: expected pattern|targetDir|file1[|file2...]"); return; }
    for (const std::wstring& command : BuildCommands(parts[0], std::vector<std::wstring>(parts.begin() + 2, parts.end()), parts[1])) {
        Log(L"Run: " + command);
        Log(L"Run: exit code " + std::to_wstring(ExecuteCommand(command, parts[1])));
    }
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { g_instance = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}

STDAPI DllCanUnloadNow() {
    return g_objects == 0 && g_locks == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** result) {
    if (clsid != CLSID_ezExplorerFileDropMenu) return CLASS_E_CLASSNOTAVAILABLE;
    Factory* factory = new (std::nothrow) Factory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT status = factory->QueryInterface(iid, result);
    factory->Release();
    return status;
}
