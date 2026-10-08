#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objidl.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>

#pragma comment(lib, "shlwapi.lib")

const CLSID CLSID_SymbolicLinkExplorerContextMenu = {0x6d4d8ef0, 0x3e69, 0x4f5d, {0x8d, 0x5a, 0x1f, 0x8c, 0xb0, 0xf2, 0xb9, 0xc4}};
HINSTANCE g_instance = nullptr;
long g_objects = 0;
long g_locks = 0;
constexpr UINT kCommand = 1;

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::string JsonString(const std::wstring& value) {
    std::string result = "\"";
    for (unsigned char character : ToUtf8(value)) {
        switch (character) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += static_cast<char>(character); break;
        }
    }
    result += "\"";
    return result;
}

std::wstring InstallDirectory() {
    wchar_t* app_data = nullptr;
    size_t length = 0;
    _wdupenv_s(&app_data, &length, L"LOCALAPPDATA");
    std::wstring result = app_data ? app_data : L".";
    free(app_data);
    return result + L"\\SymbolicLinkExplorerContextMenu";
}

bool WriteRequest(const std::vector<std::wstring>& sources, const std::wstring& destination, std::wstring& request_path) {
    wchar_t temporary[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, temporary) == 0 || GetTempFileNameW(temporary, L"slx", 0, temporary) == 0) return false;
    request_path = temporary;
    std::ofstream output(std::filesystem::path(request_path), std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << "{\"sources\":[";
    for (size_t index = 0; index < sources.size(); ++index) {
        if (index) output << ',';
        output << JsonString(sources[index]);
    }
    output << "],\"destination\":" << JsonString(destination) << ",\"key_state\":0}\n";
    return output.good();
}

bool StartWorker(const std::wstring& request_path) {
    std::wstring worker = InstallDirectory() + L"\\worker.exe";
    std::wstring log = InstallDirectory() + L"\\logs\\worker.log";
    std::wstring command = L"\"" + worker + L"\" \"" + request_path + L"\" --log \"" + log + L"\"";
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process = {};
    std::vector<wchar_t> command_buffer(command.begin(), command.end());
    command_buffer.push_back(L'\0');
    BOOL started = CreateProcessW(worker.c_str(), command_buffer.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, InstallDirectory().c_str(), &startup, &process);
    if (!started) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

class Handler final : public IContextMenu, public IShellExtInit {
public:
    Handler() { ++g_objects; }
    ~Handler() { --g_objects; }
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
    HRESULT STDMETHODCALLTYPE QueryContextMenu(HMENU menu, UINT index, UINT first, UINT, UINT flags) override {
        if (flags & CMF_DEFAULTONLY) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        InsertMenuW(menu, index++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        InsertMenuW(menu, index, MF_BYPOSITION | MF_STRING, first + kCommand, L"Create symbolic links here");
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, kCommand + 1);
    }
    HRESULT STDMETHODCALLTYPE InvokeCommand(LPCMINVOKECOMMANDINFO info) override {
        if (!info || HIWORD(info->lpVerb) || LOWORD(info->lpVerb) != kCommand) return E_INVALIDARG;
        try {
            std::wstring request;
            if (!WriteRequest(sources_, destination_, request)) return E_FAIL;
            return StartWorker(request) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
        } catch (...) {
            return E_FAIL;
        }
    }
    HRESULT STDMETHODCALLTYPE GetCommandString(UINT_PTR, UINT flags, UINT*, LPSTR buffer, UINT length) override {
        if (!buffer || !length) return E_INVALIDARG;
        if (flags == GCS_VERBA) {
            lstrcpynA(buffer, "create_symbolic_links", static_cast<int>(length));
            return S_OK;
        }
        return E_NOTIMPL;
    }
private:
    LONG references_ = 1;
    std::wstring destination_;
    std::vector<std::wstring> sources_;
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

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { g_instance = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}

STDAPI DllCanUnloadNow() {
    return g_objects == 0 && g_locks == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** result) {
    if (clsid != CLSID_SymbolicLinkExplorerContextMenu) return CLASS_E_CLASSNOTAVAILABLE;
    Factory* factory = new (std::nothrow) Factory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT status = factory->QueryInterface(iid, result);
    factory->Release();
    return status;
}
