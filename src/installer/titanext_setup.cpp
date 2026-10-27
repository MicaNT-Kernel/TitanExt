#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <gdiplus.h>

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <algorithm>
#include "titanext/compression.hpp"

namespace {

#pragma pack(push, 1)
struct PayloadFooter {
    uint64_t tocOffset{0};
    uint64_t payloadStart{0};
    char     magic[16]{0};
};
#pragma pack(pop)
static_assert(sizeof(PayloadFooter) == 32, "PayloadFooter must be 32 bytes");

struct EmbeddedFile {
    std::string name;
    uint64_t offset{0};
    uint64_t compSize{0};
    uint64_t uncompSize{0};
    uint32_t isCompressed{0};
};

bool ReadEmbeddedPayload(std::vector<EmbeddedFile>& outEntries) {
    wchar_t thisExe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, thisExe, MAX_PATH) == 0) return false;

    std::ifstream file(thisExe, std::ios::binary);
    if (!file.is_open()) return false;

    file.seekg(0, std::ios::end);
    uint64_t fileSize = static_cast<uint64_t>(file.tellg());
    if (fileSize < sizeof(PayloadFooter)) return false;

    file.seekg(fileSize - sizeof(PayloadFooter), std::ios::beg);
    PayloadFooter footer{};
    file.read(reinterpret_cast<char*>(&footer), sizeof(footer));

    if (std::memcmp(footer.magic, "TITANEXT_BUNDLE", 15) != 0) return false;
    if (footer.tocOffset >= fileSize || footer.payloadStart >= fileSize) return false;

    file.seekg(footer.tocOffset, std::ios::beg);
    uint32_t entryCount = 0;
    file.read(reinterpret_cast<char*>(&entryCount), sizeof(entryCount));
    if (entryCount == 0 || entryCount > 256) return false;

    outEntries.clear();
    for (uint32_t i = 0; i < entryCount; ++i) {
        uint32_t nameLen = 0;
        file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
        if (nameLen > 1024) return false;
        std::string name(nameLen, '\0');
        file.read(name.data(), nameLen);

        EmbeddedFile ent;
        ent.name = name;
        file.read(reinterpret_cast<char*>(&ent.offset), sizeof(ent.offset));
        file.read(reinterpret_cast<char*>(&ent.compSize), sizeof(ent.compSize));
        file.read(reinterpret_cast<char*>(&ent.uncompSize), sizeof(ent.uncompSize));
        file.read(reinterpret_cast<char*>(&ent.isCompressed), sizeof(ent.isCompressed));
        outEntries.push_back(std::move(ent));
    }
    return !outEntries.empty();
}

bool ExtractEmbeddedFile(const EmbeddedFile& ent, const std::wstring& outPath) {
    wchar_t thisExe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, thisExe, MAX_PATH) == 0) return false;

    std::ifstream file(thisExe, std::ios::binary);
    if (!file.is_open()) return false;

    file.seekg(ent.offset, std::ios::beg);
    std::vector<uint8_t> buffer(ent.compSize);
    file.read(reinterpret_cast<char*>(buffer.data()), ent.compSize);

    std::vector<uint8_t> decompressed;
    if (ent.isCompressed) {
        decompressed.resize(ent.uncompSize);
        if (!titanext::CompressionEngine::DecompressLZ4(buffer, decompressed)) {
            return false;
        }
    } else {
        decompressed = std::move(buffer);
    }

    std::error_code ec;
    std::filesystem::path p(outPath);
    std::filesystem::create_directories(p.parent_path(), ec);

    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(decompressed.data()), decompressed.size());
    return true;
}

enum {
    ID_EDIT_DEST = 1001,
    ID_BTN_BROWSE,
    ID_CHK_PATH,
    ID_CHK_WINFSP,
    ID_CHK_STARTMENU,
    ID_CHK_DESKTOP,
    ID_PROGRESS,
    ID_STATIC_STATUS,
    ID_BTN_INSTALL,
    ID_BTN_CANCEL
};

HINSTANCE g_hInstance{nullptr};
HWND g_hMainWnd{nullptr};
HWND g_hEditDest{nullptr};
HWND g_hBtnBrowse{nullptr};
HWND g_hChkPath{nullptr};
HWND g_hChkWinFsp{nullptr};
HWND g_hChkStartMenu{nullptr};
HWND g_hChkDesktop{nullptr};
HWND g_hProgress{nullptr};
HWND g_hStaticStatus{nullptr};
HWND g_hBtnInstall{nullptr};
HWND g_hBtnCancel{nullptr};

bool IsWinFspInstalled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WinFsp", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\WinFsp", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

HFONT g_hFontTitle{nullptr};
HFONT g_hFontSub{nullptr};
HFONT g_hFontNormal{nullptr};
HFONT g_hFontBold{nullptr};
HBRUSH g_hDarkBrush{nullptr};

ULONG_PTR g_gdiplusToken{0};
std::wstring g_HeroImagePath;
bool g_InstallFinished{false};

std::wstring GetModuleDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring ws(path);
    size_t pos = ws.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? ws.substr(0, pos) : L".";
}

std::wstring FindHeroImage() {
    std::wstring dir = GetModuleDir();
    std::vector<std::wstring> candidates = {
        dir + L"\\assets\\titanext_hero.jpg",
        dir + L"\\..\\assets\\titanext_hero.jpg",
        dir + L"\\titanext_hero.jpg",
        L"assets\\titanext_hero.jpg"
    };
    for (const auto& c : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(c, ec)) return c;
    }
    return L"";
}

void DrawAspectFill(Gdiplus::Graphics& graphics, Gdiplus::Image* pImg, float targetX, float targetY, float targetW, float targetH) {
    if (!pImg) return;
    float imgW = static_cast<float>(pImg->GetWidth());
    float imgH = static_cast<float>(pImg->GetHeight());
    if (imgW <= 0.0f || imgH <= 0.0f) return;

    float scale = (std::max)(targetW / imgW, targetH / imgH);
    float drawW = imgW * scale;
    float drawH = imgH * scale;
    float drawX = targetX + (targetW - drawW) / 2.0f;
    float drawY = targetY + (targetH - drawH) / 2.0f;

    Gdiplus::Region oldClip;
    graphics.GetClip(&oldClip);
    graphics.SetClip(Gdiplus::RectF(targetX, targetY, targetW, targetH));
    graphics.DrawImage(pImg, drawX, drawY, drawW, drawH);
    graphics.SetClip(&oldClip);
}

[[maybe_unused]] bool CreateShortcut(const std::wstring& targetPath, const std::wstring& shortcutPath, const std::wstring& description) {
    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&psl));
    if (SUCCEEDED(hr)) {
        psl->SetPath(targetPath.c_str());
        psl->SetDescription(description.c_str());
        psl->SetIconLocation(targetPath.c_str(), 0);
        IPersistFile* ppf = nullptr;
        hr = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
        if (SUCCEEDED(hr)) {
            hr = ppf->Save(shortcutPath.c_str(), TRUE);
            ppf->Release();
        }
        psl->Release();
    }
    return SUCCEEDED(hr);
}

bool AddToSystemPath(const std::wstring& dir) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        wchar_t currentPath[32768]{};
        DWORD bufSize = sizeof(currentPath);
        DWORD type = REG_EXPAND_SZ;
        RegQueryValueExW(hKey, L"Path", nullptr, &type, reinterpret_cast<LPBYTE>(currentPath), &bufSize);
        std::wstring ws(currentPath);
        if (ws.find(dir) == std::wstring::npos) {
            if (!ws.empty() && ws.back() != L';') ws += L';';
            ws += dir;
            RegSetValueExW(hKey, L"Path", 0, type, reinterpret_cast<const BYTE*>(ws.c_str()), static_cast<DWORD>((ws.size() + 1) * sizeof(wchar_t)));
            DWORD_PTR dwResult;
            SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"), SMTO_ABORTIFHUNG, 2000, &dwResult);
        }
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

bool RegisterUninstallEntry(const std::wstring& installDir) {
    HKEY hKey;
    const wchar_t subkey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TitanExt";
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        auto setStr = [&](const wchar_t* valName, const std::wstring& val) {
            RegSetValueExW(hKey, valName, 0, REG_SZ, reinterpret_cast<const BYTE*>(val.c_str()), static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
        };
        auto setDword = [&](const wchar_t* valName, DWORD val) {
            RegSetValueExW(hKey, valName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(DWORD));
        };

        setStr(L"DisplayName", L"TitanExt — Linux Ext4/3/2 Filesystem Storage Engine");
        setStr(L"DisplayVersion", L"1.0.0");
        setStr(L"Publisher", L"MicaNT-Kernel / Daniel Elliott");
        setStr(L"InstallLocation", installDir);
        setStr(L"UninstallString", L"\"" + installDir + L"\\titanext_setup.exe\" /uninstall");
        setStr(L"QuietUninstallString", L"\"" + installDir + L"\\titanext_setup.exe\" /uninstall /silent");
        setStr(L"DisplayIcon", installDir + L"\\titanext_mount.exe,0");
        setDword(L"EstimatedSize", 16000);
        setDword(L"NoModify", 1);
        setDword(L"NoRepair", 1);

        RegCloseKey(hKey);
        return true;
    }
    return false;
}

bool PerformInstall(const std::wstring& dstDir, bool addPath, bool installWinFsp, bool, bool, HWND hProg, HWND hStat) {
    auto setStatus = [&](const std::wstring& text, int pos) {
        if (hStat) SetWindowTextW(hStat, text.c_str());
        if (hProg) SendMessageW(hProg, PBM_SETPOS, pos, 0);
    };

    setStatus(L"Preparing destination directory...", 10);
    std::error_code ec;
    std::filesystem::create_directories(dstDir, ec);

    std::wstring srcDir = GetModuleDir();

    // 1. Install WinFsp
    if (installWinFsp && !IsWinFspInstalled()) {
        setStatus(L"Installing WinFsp Filesystem Driver (WHQL)...", 15);
        std::wstring tempMsi = std::filesystem::temp_directory_path().wstring() + L"\\TitanExt\\winfsp.msi";
        bool foundMsi = false;

        std::vector<EmbeddedFile> entries;
        if (ReadEmbeddedPayload(entries)) {
            for (const auto& ef : entries) {
                if (ef.name.find("winfsp") != std::string::npos && ef.name.ends_with(".msi")) {
                    ExtractEmbeddedFile(ef, tempMsi);
                    foundMsi = true;
                    break;
                }
            }
        }

        if (!foundMsi) {
            std::wstring localMsi = srcDir + L"\\redist\\winfsp-2.1.25156.msi";
            if (std::filesystem::exists(localMsi, ec)) {
                std::filesystem::create_directories(std::filesystem::path(tempMsi).parent_path(), ec);
                std::filesystem::copy_file(localMsi, tempMsi, std::filesystem::copy_options::overwrite_existing, ec);
                foundMsi = true;
            }
        }

        if (foundMsi) {
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            std::wstring cmd = L"msiexec.exe /i \"" + tempMsi + L"\" /qn /norestart";
            std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
            cmdBuf.push_back(0);
            if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 60000);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
            std::filesystem::remove(tempMsi, ec);
        }
    }

    // 2. Extract Embedded Payload
    std::vector<EmbeddedFile> embeddedFiles;
    if (ReadEmbeddedPayload(embeddedFiles) && !embeddedFiles.empty()) {
        int idx = 0;
        for (const auto& ef : embeddedFiles) {
            int pct = 25 + static_cast<int>((idx * 45) / embeddedFiles.size());
            std::wstring wName(ef.name.begin(), ef.name.end());
            setStatus(L"Extracting " + wName + L"...", pct);
            ExtractEmbeddedFile(ef, dstDir + L"\\" + wName);
            idx++;
        }
    } else {
        setStatus(L"Copying TitanExt files...", 35);
        std::vector<std::wstring> exes = { L"titanext_mount.exe", L"titanext_format.exe", L"titanext_check.exe", L"titanext_svc.exe" };
        for (const auto& exe : exes) {
            std::wstring s = srcDir + L"\\" + exe;
            if (std::filesystem::exists(s, ec)) {
                std::filesystem::copy_file(s, dstDir + L"\\" + exe, std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
        std::wstring heroSrc = FindHeroImage();
        if (!heroSrc.empty()) {
            std::filesystem::create_directories(dstDir + L"\\assets", ec);
            std::filesystem::copy_file(heroSrc, dstDir + L"\\assets\\titanext_hero.jpg", std::filesystem::copy_options::overwrite_existing, ec);
        }
    }

    // Copy setup tool itself for uninstaller
    wchar_t thisExe[MAX_PATH];
    GetModuleFileNameW(nullptr, thisExe, MAX_PATH);
    std::filesystem::copy_file(thisExe, dstDir + L"\\titanext_setup.exe", std::filesystem::copy_options::overwrite_existing, ec);

    if (addPath) {
        setStatus(L"Configuring System PATH...", 75);
        AddToSystemPath(dstDir);
    }

    // Register Auto-Start Background Service
    setStatus(L"Registering TitanExt Windows Service...", 85);
    std::wstring svcExe = dstDir + L"\\titanext_svc.exe";
    if (std::filesystem::exists(svcExe, ec)) {
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + svcExe + L"\" install";
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back(0);
        if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 10000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    setStatus(L"Registering Add/Remove Programs entry...", 92);
    RegisterUninstallEntry(dstDir);

    setStatus(L"Installation complete! Ext4 drives ready to mount.", 100);
    return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            BOOL darkMode = TRUE;
            DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkMode, sizeof(darkMode));

            g_hFontTitle = CreateFontW(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontSub = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontBold = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontNormal = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hDarkBrush = CreateSolidBrush(RGB(22, 22, 26));

            g_HeroImagePath = FindHeroImage();

            // Form Controls
            int startX = 300;
            int width = 480;

            CreateWindowExW(0, L"STATIC", L"Installation Directory:", WS_CHILD | WS_VISIBLE, startX, 150, width, 20, hwnd, nullptr, g_hInstance, nullptr);
            g_hEditDest = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"C:\\Program Files\\TitanExt", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, startX, 175, 380, 26, hwnd, reinterpret_cast<HMENU>(ID_EDIT_DEST), g_hInstance, nullptr);
            g_hBtnBrowse = CreateWindowExW(0, L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, startX + 390, 174, 90, 28, hwnd, reinterpret_cast<HMENU>(ID_BTN_BROWSE), g_hInstance, nullptr);

            g_hChkWinFsp = CreateWindowExW(0, L"BUTTON", L"Install WinFsp User-Mode Driver (Required for drive letters)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, startX, 220, width, 22, hwnd, reinterpret_cast<HMENU>(ID_CHK_WINFSP), g_hInstance, nullptr);
            SendMessageW(g_hChkWinFsp, BM_SETCHECK, IsWinFspInstalled() ? BST_UNCHECKED : BST_CHECKED, 0);

            g_hChkPath = CreateWindowExW(0, L"BUTTON", L"Add TitanExt command-line tools to System PATH", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, startX, 250, width, 22, hwnd, reinterpret_cast<HMENU>(ID_CHK_PATH), g_hInstance, nullptr);
            SendMessageW(g_hChkPath, BM_SETCHECK, BST_CHECKED, 0);

            g_hChkStartMenu = CreateWindowExW(0, L"BUTTON", L"Register background auto-mount service (titanext_svc)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, startX, 280, width, 22, hwnd, reinterpret_cast<HMENU>(ID_CHK_STARTMENU), g_hInstance, nullptr);
            SendMessageW(g_hChkStartMenu, BM_SETCHECK, BST_CHECKED, 0);

            g_hProgress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, startX, 350, width, 18, hwnd, reinterpret_cast<HMENU>(ID_PROGRESS), g_hInstance, nullptr);
            SendMessageW(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

            g_hStaticStatus = CreateWindowExW(0, L"STATIC", L"Ready to install Ext4 storage suite.", WS_CHILD | WS_VISIBLE, startX, 380, width, 22, hwnd, reinterpret_cast<HMENU>(ID_STATIC_STATUS), g_hInstance, nullptr);

            g_hBtnInstall = CreateWindowExW(0, L"BUTTON", L"🚀 Install Now", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, startX + 220, 440, 140, 36, hwnd, reinterpret_cast<HMENU>(ID_BTN_INSTALL), g_hInstance, nullptr);
            g_hBtnCancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, startX + 370, 440, 110, 36, hwnd, reinterpret_cast<HMENU>(ID_BTN_CANCEL), g_hInstance, nullptr);

            // Apply Fonts
            SendMessageW(g_hEditDest, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);
            SendMessageW(g_hBtnBrowse, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);
            SendMessageW(g_hChkWinFsp, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);
            SendMessageW(g_hChkPath, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);
            SendMessageW(g_hChkStartMenu, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);
            SendMessageW(g_hStaticStatus, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);
            SendMessageW(g_hBtnInstall, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);
            SendMessageW(g_hBtnCancel, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);

            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, RGB(225, 230, 240));
            SetBkColor(hdc, RGB(22, 22, 26));
            return reinterpret_cast<LRESULT>(g_hDarkBrush);
        }

        case WM_CTLCOLORBTN: {
            return reinterpret_cast<LRESULT>(g_hDarkBrush);
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rc;
            GetClientRect(hwnd, &rc);
            int h = rc.bottom - rc.top;

            HBRUSH hOldBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, g_hDarkBrush));
            FillRect(hdc, &rc, g_hDarkBrush);

            // Left Hero Banner
            if (!g_HeroImagePath.empty()) {
                Gdiplus::Graphics graphics(hdc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                Gdiplus::Image heroImg(g_HeroImagePath.c_str());
                DrawAspectFill(graphics, &heroImg, 0.0f, 0.0f, 260.0f, static_cast<float>(h));

                Gdiplus::SolidBrush overlay(Gdiplus::Color(180, 15, 15, 20));
                graphics.FillRectangle(&overlay, 0, 0, 260, h);

                Gdiplus::FontFamily ff(L"Segoe UI");
                Gdiplus::Font fTitle(&ff, 14.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
                Gdiplus::Font fSub(&ff, 9.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
                Gdiplus::SolidBrush subBrush(Gdiplus::Color(200, 180, 210, 240));

                graphics.DrawString(L"TitanExt", -1, &fTitle, Gdiplus::PointF(20.0f, 30.0f), &textBrush);
                graphics.DrawString(L"Linux Ext4/3/2 Filesystem\nStorage Suite for Windows", -1, &fSub, Gdiplus::PointF(20.0f, 60.0f), &subBrush);
            }

            // Right Header Text
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(245, 245, 250));
            SelectObject(hdc, g_hFontTitle);
            TextOutW(hdc, 300, 30, L"Install TitanExt Storage Suite", 30);

            SetTextColor(hdc, RGB(160, 175, 195));
            SelectObject(hdc, g_hFontSub);
            TextOutW(hdc, 300, 65, L"High-performance native Ext4 driver and tools for Windows NT.", 61);

            SelectObject(hdc, hOldBrush);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == ID_BTN_CANCEL) {
                PostQuitMessage(0);
            } else if (id == ID_BTN_INSTALL) {
                if (g_InstallFinished) {
                    PostQuitMessage(0);
                    return 0;
                }

                wchar_t dstPath[MAX_PATH];
                GetWindowTextW(g_hEditDest, dstPath, MAX_PATH);
                std::wstring dir(dstPath);

                bool addPath = (SendMessageW(g_hChkPath, BM_GETCHECK, 0, 0) == BST_CHECKED);
                bool installWinFsp = (SendMessageW(g_hChkWinFsp, BM_GETCHECK, 0, 0) == BST_CHECKED);
                bool startMenu = (SendMessageW(g_hChkStartMenu, BM_GETCHECK, 0, 0) == BST_CHECKED);
                bool desktop = false;

                EnableWindow(g_hBtnInstall, FALSE);
                EnableWindow(g_hBtnBrowse, FALSE);
                EnableWindow(g_hEditDest, FALSE);
                EnableWindow(g_hChkPath, FALSE);
                EnableWindow(g_hChkWinFsp, FALSE);
                EnableWindow(g_hChkStartMenu, FALSE);

                std::thread([=]() {
                    PerformInstall(dir, addPath, installWinFsp, startMenu, desktop, g_hProgress, g_hStaticStatus);
                    g_InstallFinished = true;
                    SetWindowTextW(g_hBtnInstall, L"Finish");
                    EnableWindow(g_hBtnInstall, TRUE);
                }).detach();
            }
            break;
        }

        case WM_DESTROY: {
            if (g_hFontTitle) DeleteObject(g_hFontTitle);
            if (g_hFontSub) DeleteObject(g_hFontSub);
            if (g_hFontBold) DeleteObject(g_hFontBold);
            if (g_hFontNormal) DeleteObject(g_hFontNormal);
            if (g_hDarkBrush) DeleteObject(g_hDarkBrush);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR lpCmdLine, int nCmdShow) {
    g_hInstance = hInstance;

    std::wstring cmd(lpCmdLine);
    if (cmd.find(L"/silent") != std::wstring::npos) {
        wchar_t pf[MAX_PATH];
        SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, pf);
        std::wstring dst = std::wstring(pf) + L"\\TitanExt";
        PerformInstall(dst, true, true, true, false, nullptr, nullptr);
        return 0;
    }

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
        return 1;
    }

    INITCOMMONCONTROLSEX icex{
        .dwSize = sizeof(icex),
        .dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES
    };
    InitCommonControlsEx(&icex);

    const wchar_t CLASS_NAME[] = L"TitanExt_Installer_Class";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    g_hMainWnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"TitanExt Setup",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 830, 560,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_hMainWnd) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return static_cast<int>(msg.wParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    return wWinMain(hInstance, nullptr, GetCommandLineW(), nCmdShow);
}
