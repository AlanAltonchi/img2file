#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <initguid.h>
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <exdisp.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <psapi.h>
#include <cstring>

static HWND g_hwnd = nullptr;
static HHOOK g_hook = nullptr;
static bool g_initialized = false;
static ULONG_PTR g_gdiplusToken = 0;
constexpr UINT WM_PASTE_IMAGE = WM_APP + 1;

// Init COM+GDI+ once on first paste, keep loaded
static void EnsureInitialized() {
    if (g_initialized) return;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput gdipInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdipInput, nullptr);
    g_initialized = true;
}

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK KeyboardProc(int, WPARAM, LPARAM);

// ---------------------------------------------------------------------------
// SendCtrlV — synthesize a Ctrl+V keystroke, temporarily unhooking to avoid
// our own hook catching it.
// ---------------------------------------------------------------------------
static void SendCtrlV() {
    if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = nullptr; }

    INPUT inputs[4] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));

    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc,
        GetModuleHandleW(nullptr), 0);
}

// ---------------------------------------------------------------------------
// GetExplorerFolderPath — resolve the folder path from an Explorer HWND.
// COM must be initialized before calling.
// ---------------------------------------------------------------------------
static bool GetExplorerFolderPath(HWND hwnd, wchar_t* path, size_t maxLen) {
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, 64);

    // Desktop
    if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0) {
        wchar_t* desktop = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop))) {
            wcsncpy(path, desktop, maxLen - 1);
            path[maxLen - 1] = L'\0';
            CoTaskMemFree(desktop);
            return true;
        }
        return false;
    }

    // Explorer window — enumerate IShellWindows
    IShellWindows* shellWindows = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL,
            IID_IShellWindows, (void**)&shellWindows)))
        return false;

    bool found = false;
    long count = 0;
    shellWindows->get_Count(&count);

    for (long i = 0; i < count && !found; i++) {
        VARIANT idx;
        VariantInit(&idx);
        idx.vt = VT_I4;
        idx.lVal = i;
        IDispatch* disp = nullptr;
        if (FAILED(shellWindows->Item(idx, &disp)) || !disp) continue;

        IWebBrowser2* wb = nullptr;
        if (SUCCEEDED(disp->QueryInterface(IID_IWebBrowser2, (void**)&wb))) {
            SHANDLE_PTR wbHwnd = 0;
            wb->get_HWND(&wbHwnd);
            if ((HWND)(LONG_PTR)wbHwnd == hwnd) {
                BSTR url = nullptr;
                wb->get_LocationURL(&url);
                if (url) {
                    DWORD len = (DWORD)maxLen;
                    if (SUCCEEDED(PathCreateFromUrlW(url, path, &len, 0))) {
                        found = true;
                    }
                    SysFreeString(url);
                }
            }
            wb->Release();
        }
        disp->Release();
    }
    shellWindows->Release();
    return found;
}

// ---------------------------------------------------------------------------
// GetPngEncoderClsid — find the GDI+ PNG encoder.
// ---------------------------------------------------------------------------
static bool GetPngEncoderClsid(CLSID* clsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return false;
    auto* codecs = (Gdiplus::ImageCodecInfo*)malloc(size);
    if (!codecs) return false;
    Gdiplus::GetImageEncoders(num, size, codecs);
    bool found = false;
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            *clsid = codecs[i].Clsid;
            found = true;
            break;
        }
    }
    free(codecs);
    return found;
}

// ---------------------------------------------------------------------------
// GetNextFilename — find the next available image_NNN.png in the folder.
// Uses CREATE_NEW for atomicity.
// ---------------------------------------------------------------------------
static bool GetNextFilename(const wchar_t* folder, wchar_t* out, size_t maxLen) {
    for (int i = 1; i < 100000; i++) {
        wchar_t name[32];
        if (i < 1000)
            wsprintfW(name, L"image_%03d.png", i);
        else
            wsprintfW(name, L"image_%d.png", i);

        wchar_t fullPath[MAX_PATH];
        wsprintfW(fullPath, L"%s\\%s", folder, name);

        HANDLE hFile = CreateFileW(fullPath, GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            wcsncpy(out, fullPath, maxLen - 1);
            out[maxLen - 1] = L'\0';
            return true;
        }
        if (GetLastError() != ERROR_FILE_EXISTS) return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// HandlePaste — init COM+GDI+ on first call, read clipboard, save PNG,
// then trim working set to reclaim physical RAM.
// ---------------------------------------------------------------------------
static void HandlePaste(HWND target) {
    EnsureInitialized();
    bool handled = false;

    if (OpenClipboard(nullptr)) {
        // If files on clipboard, let Explorer handle it
        if (IsClipboardFormatAvailable(CF_HDROP)) {
            CloseClipboard();
            goto cleanup;
        }

        UINT fmt = 0;
        if (IsClipboardFormatAvailable(CF_DIBV5)) fmt = CF_DIBV5;
        else if (IsClipboardFormatAvailable(CF_DIB)) fmt = CF_DIB;
        else if (IsClipboardFormatAvailable(CF_BITMAP)) fmt = CF_BITMAP;

        if (!fmt) { CloseClipboard(); goto cleanup; }

        Gdiplus::Bitmap* bmp = nullptr;

        if (fmt == CF_BITMAP) {
            HBITMAP hbm = (HBITMAP)GetClipboardData(CF_BITMAP);
            if (hbm) bmp = Gdiplus::Bitmap::FromHBITMAP(hbm, nullptr);
        } else {
            HANDLE hData = GetClipboardData(fmt);
            if (hData) {
                void* data = GlobalLock(hData);
                DWORD dataSize = (DWORD)GlobalSize(hData);
                if (data && dataSize > sizeof(BITMAPINFOHEADER)) {
                    auto* bih = (BITMAPINFOHEADER*)data;

                    DWORD colorTableSize = 0;
                    if (bih->biBitCount <= 8) {
                        colorTableSize = (bih->biClrUsed ? bih->biClrUsed : (1u << bih->biBitCount))
                                         * sizeof(RGBQUAD);
                    } else if (bih->biCompression == BI_BITFIELDS) {
                        colorTableSize = 3 * sizeof(DWORD);
                    }

                    BITMAPFILEHEADER bfh = {};
                    bfh.bfType = 0x4D42;
                    bfh.bfSize = sizeof(BITMAPFILEHEADER) + dataSize;
                    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + bih->biSize + colorTableSize;

                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPFILEHEADER) + dataSize);
                    if (hMem) {
                        void* mem = GlobalLock(hMem);
                        memcpy(mem, &bfh, sizeof(bfh));
                        memcpy((char*)mem + sizeof(bfh), data, dataSize);
                        GlobalUnlock(hMem);

                        IStream* stream = nullptr;
                        if (SUCCEEDED(CreateStreamOnHGlobal(hMem, TRUE, &stream))) {
                            bmp = Gdiplus::Bitmap::FromStream(stream);
                            stream->Release();
                        } else {
                            GlobalFree(hMem);
                        }
                    }
                }
                GlobalUnlock(hData);
            }
        }

        CloseClipboard();

        if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
            wchar_t folder[MAX_PATH] = {};
            if (GetExplorerFolderPath(target, folder, MAX_PATH)) {
                wchar_t filePath[MAX_PATH] = {};
                CLSID pngClsid;
                if (GetNextFilename(folder, filePath, MAX_PATH) && GetPngEncoderClsid(&pngClsid)) {
                    if (bmp->Save(filePath, &pngClsid, nullptr) == Gdiplus::Ok) {
                        handled = true;
                    } else {
                        DeleteFileW(filePath);
                    }
                }
            }
        }
        delete bmp;
    }

cleanup:
    // Trim working set — pages out DLL memory so physical RAM is reclaimed
    EmptyWorkingSet(GetCurrentProcess());

    if (!handled) SendCtrlV();
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PASTE_IMAGE) {
        HandlePaste((HWND)lParam);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// KeyboardProc — low-level keyboard hook. Only fast checks.
// ---------------------------------------------------------------------------
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (kb->vkCode == 'V' && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            HWND fg = GetForegroundWindow();
            wchar_t cls[64] = {};
            GetClassNameW(fg, cls, 64);
            if (wcscmp(cls, L"CabinetWClass") == 0 ||
                wcscmp(cls, L"Progman") == 0 ||
                wcscmp(cls, L"WorkerW") == 0) {
                PostMessageW(g_hwnd, WM_PASTE_IMAGE, 0, (LPARAM)fg);
                return 1;
            }
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// WinMain
// ---------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"img2file-singleton");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"Img2FileWnd";
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, nullptr, 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);

    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, hInstance, 0);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hook) UnhookWindowsHookEx(g_hook);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
