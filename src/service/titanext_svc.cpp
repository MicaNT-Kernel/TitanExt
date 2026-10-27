#include "titanext/ext4_engine.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <mutex>
#include <filesystem>

#ifdef _WIN32
#include "titanext/winfsp_bridge.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsvc.h>
#endif

namespace titanext {

constexpr const wchar_t* TITANEXT_PIPE_NAME = L"\\\\.\\pipe\\titanext_ipc";
constexpr uint64_t TITANEXT_IPC_MAGIC = 0x4558543449504331ULL; // "EXT4IPC1"

struct MountedVolumeEntry {
    std::string devicePath;
    std::string driveLetter;
    std::string volumeLabel;
    std::shared_ptr<Ext4Engine> engine;
#ifdef _WIN32
    std::shared_ptr<WinFspMountSession> session;
#endif
};

class TitanExtDaemon {
public:
    static TitanExtDaemon& Get() {
        static TitanExtDaemon instance;
        return instance;
    }

    bool Start() {
        if (running_) return true;
        running_ = true;

        std::cout << "========================================================================\n";
        std::cout << "        TitanExt Windows NT Subsystem Service Daemon                    \n";
        std::cout << "========================================================================\n";
        std::cout << "[TitanExt Daemon] Initializing Ext4 filesystem subsystem...\n";

#ifdef _WIN32
        auto& fsp = WinFspApi::Get();
        if (fsp.isAvailable()) {
            std::cout << "[TitanExt Daemon] WinFsp dynamic engine loaded successfully.\n";
        } else {
            std::cerr << "[TitanExt Daemon] Warning: WinFsp dynamic library not found. Userland drive letter mounting will be disabled.\n";
        }
#endif

        ipcThread_ = std::thread(&TitanExtDaemon::IpcLoop, this);
        scrubThread_ = std::thread(&TitanExtDaemon::ScrubLoop, this);

        std::cout << "[TitanExt Daemon] Service successfully started. Listening on Named Pipe: \\\\.\\pipe\\titanext_ipc\n";
        return true;
    }

    void Stop() {
        if (!running_) return;
        running_ = false;

        std::cout << "[TitanExt Daemon] Stopping service daemon and unmounting volumes...\n";

        {
            std::lock_guard<std::mutex> lock(mountMutex_);
            for (auto& [drive, vol] : mountedVolumes_) {
#ifdef _WIN32
                if (vol.session) vol.session->unmount();
#endif
                if (vol.engine) vol.engine->Close();
            }
            mountedVolumes_.clear();
        }

        if (ipcThread_.joinable()) ipcThread_.join();
        if (scrubThread_.joinable()) scrubThread_.join();

        std::cout << "[TitanExt Daemon] Service stopped cleanly.\n";
    }

    bool MountVolume(const std::string& devPath, const std::string& driveLetter, const std::string& label, std::string& err) {
        std::lock_guard<std::mutex> lock(mountMutex_);
        if (mountedVolumes_.find(driveLetter) != mountedVolumes_.end()) {
            err = "Drive letter already mounted.";
            return false;
        }

        auto engine = std::make_shared<Ext4Engine>();
        if (!engine->Open(devPath, false)) {
            err = "Failed to open Ext4 volume at " + devPath;
            return false;
        }

        MountedVolumeEntry ent;
        ent.devicePath = devPath;
        ent.driveLetter = driveLetter;
        ent.volumeLabel = label.empty() ? engine->VolumeLabel() : label;
        ent.engine = engine;

#ifdef _WIN32
        ent.session = std::make_shared<WinFspMountSession>();
        std::wstring wLabel(ent.volumeLabel.begin(), ent.volumeLabel.end());
        if (!ent.session->mount(engine, driveLetter, wLabel)) {
            err = "WinFsp mount failed for drive " + driveLetter;
            return false;
        }
#endif
        mountedVolumes_[driveLetter] = ent;
        return true;
    }

    bool UnmountVolume(const std::string& driveLetter, std::string& err) {
        std::lock_guard<std::mutex> lock(mountMutex_);
        auto it = mountedVolumes_.find(driveLetter);
        if (it == mountedVolumes_.end()) {
            err = "Drive not found in active mounts.";
            return false;
        }

#ifdef _WIN32
        if (it->second.session) {
            it->second.session->unmount();
        }
#endif
        if (it->second.engine) {
            it->second.engine->Close();
        }
        mountedVolumes_.erase(it);
        return true;
    }

    std::string ListMountsJson() {
        std::lock_guard<std::mutex> lock(mountMutex_);
        std::stringstream ss;
        ss << "{\"mounts\":[";
        bool first = true;
        for (const auto& [drive, vol] : mountedVolumes_) {
            if (!first) ss << ",";
            ss << "{\"drive\":\"" << drive << "\","
               << "\"device\":\"" << vol.devicePath << "\","
               << "\"label\":\"" << vol.volumeLabel << "\","
               << "\"totalBytes\":" << (vol.engine ? vol.engine->TotalBytes() : 0) << "}";
            first = false;
        }
        ss << "]}";
        return ss.str();
    }

    uint32_t MountCount() const {
        return static_cast<uint32_t>(mountedVolumes_.size());
    }

private:
    TitanExtDaemon() = default;

    void IpcLoop() {
#ifdef _WIN32
        while (running_) {
            HANDLE hPipe = CreateNamedPipeW(
                TITANEXT_PIPE_NAME,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES,
                4096, 4096,
                1000,
                nullptr
            );

            if (hPipe == INVALID_HANDLE_VALUE) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            if (ConnectNamedPipe(hPipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
#pragma pack(push, 1)
                struct ReqHeader {
                    uint64_t magic;
                    uint32_t cmd;
                    uint32_t payloadLen;
                };
                struct RespHdr {
                    uint64_t magic;
                    uint32_t status;
                    uint32_t payloadLen;
                };
#pragma pack(pop)

                ReqHeader reqHdr{};
                DWORD bytesRead = 0;
                if (ReadFile(hPipe, &reqHdr, sizeof(reqHdr), &bytesRead, nullptr) && bytesRead == sizeof(reqHdr)) {
                    if (reqHdr.magic == TITANEXT_IPC_MAGIC) {
                        std::string reqPayload;
                        if (reqHdr.payloadLen > 0) {
                            reqPayload.resize(reqHdr.payloadLen);
                            DWORD total = 0;
                            while (total < reqHdr.payloadLen) {
                                DWORD chunk = 0;
                                if (!ReadFile(hPipe, reqPayload.data() + total, reqHdr.payloadLen - total, &chunk, nullptr) || chunk == 0) break;
                                total += chunk;
                            }
                        }

                        std::string respPayload;
                        uint32_t status = 0;

                        if (reqHdr.cmd == 6 /* IpcCmdServiceStatus */) {
                            respPayload = "{\"service\":\"TitanExt\",\"status\":\"running\",\"mountCount\":" + std::to_string(MountCount()) + "}";
                        } else if (reqHdr.cmd == 7 /* IpcCmdListMounts */) {
                            respPayload = ListMountsJson();
                        } else if (reqHdr.cmd == 2 /* IpcCmdVolumeMount */) {
                            std::stringstream ss(reqPayload);
                            std::string dev, drv, lbl;
                            ss >> dev >> drv >> lbl;
                            std::string err;
                            if (MountVolume(dev, drv, lbl, err)) {
                                respPayload = "{\"status\":\"mounted\",\"drive\":\"" + drv + "\"}";
                            } else {
                                status = 1;
                                respPayload = "{\"error\":\"" + err + "\"}";
                            }
                        } else if (reqHdr.cmd == 3 /* IpcCmdVolumeUnmount */) {
                            std::string err;
                            if (UnmountVolume(reqPayload, err)) {
                                respPayload = "{\"status\":\"unmounted\"}";
                            } else {
                                status = 1;
                                respPayload = "{\"error\":\"" + err + "\"}";
                            }
                        } else {
                            respPayload = "{\"status\":\"ok\"}";
                        }

                        RespHdr respHdr{TITANEXT_IPC_MAGIC, status, static_cast<uint32_t>(respPayload.size())};
                        DWORD written = 0;
                        WriteFile(hPipe, &respHdr, sizeof(respHdr), &written, nullptr);
                        if (!respPayload.empty()) {
                            WriteFile(hPipe, respPayload.data(), static_cast<DWORD>(respPayload.size()), &written, nullptr);
                        }
                    }
                }
            }

            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
        }
#endif
    }

    void ScrubLoop() {
        while (running_) {
            // Run every 24 hours, sleep in 10-second increments
            for (int i = 0; i < 8640 && running_; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(10));
            }

            if (!running_) break;

            std::lock_guard<std::mutex> lock(mountMutex_);
            for (auto& [drv, vol] : mountedVolumes_) {
                if (vol.engine) {
                    ScrubReport report{};
                    vol.engine->CheckAndScrub(report);
                    std::cout << "[TitanExt Scrubber] Autonomous check for " << drv
                              << " completed. Errors detected: " << report.errorsDetected << "\n";
                }
            }
        }
    }

    std::atomic<bool> running_{false};
    std::mutex mountMutex_;
    std::map<std::string, MountedVolumeEntry> mountedVolumes_;
    std::thread ipcThread_;
    std::thread scrubThread_;
};

} // namespace titanext

#ifdef _WIN32
SERVICE_STATUS        g_ServiceStatus;
SERVICE_STATUS_HANDLE g_StatusHandle;

VOID WINAPI ServiceCtrlHandler(DWORD request) {
    switch (request) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
            titanext::TitanExtDaemon::Get().Stop();
            g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
            SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
            break;
        default:
            break;
    }
}

VOID WINAPI ServiceMain(DWORD, LPWSTR*) {
    g_StatusHandle = RegisterServiceCtrlHandlerW(L"TitanExt", ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    titanext::TitanExtDaemon::Get().Start();

    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    while (g_ServiceStatus.dwCurrentState == SERVICE_RUNNING) {
        Sleep(1000);
    }
}
#endif

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string cmd = argv[1];
#ifdef _WIN32
        if (cmd == "install") {
            wchar_t szPath[MAX_PATH];
            GetModuleFileNameW(nullptr, szPath, MAX_PATH);
            SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
            if (!scm) {
                std::cerr << "[-] Failed to open SCM. Run as Administrator.\n";
                return 1;
            }
            SC_HANDLE svc = CreateServiceW(
                scm, L"TitanExt", L"TitanExt Linux Filesystem Service",
                SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                szPath, nullptr, nullptr, nullptr, nullptr, nullptr
            );
            if (svc) {
                std::cout << "[+] TitanExt Service registered successfully.\n";
                CloseServiceHandle(svc);
            } else {
                std::cerr << "[-] Failed to create service: " << GetLastError() << "\n";
            }
            CloseServiceHandle(scm);
            return 0;
        } else if (cmd == "uninstall") {
            SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
            if (!scm) return 1;
            SC_HANDLE svc = OpenServiceW(scm, L"TitanExt", DELETE);
            if (svc) {
                DeleteService(svc);
                CloseServiceHandle(svc);
                std::cout << "[+] TitanExt Service uninstalled.\n";
            }
            CloseServiceHandle(scm);
            return 0;
        }
#endif
        if (cmd == "run") {
            titanext::TitanExtDaemon::Get().Start();
            std::cout << "[+] Press Ctrl+C to terminate debug daemon...\n";
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            return 0;
        }
    }

#ifdef _WIN32
    SERVICE_TABLE_ENTRYW ServiceTable[] = {
        {const_cast<LPWSTR>(L"TitanExt"), ServiceMain},
        {nullptr, nullptr}
    };
    if (!StartServiceCtrlDispatcherW(ServiceTable)) {
        std::cout << "Usage: titanext_svc [install | uninstall | run]\n";
    }
#else
    std::cout << "Starting TitanExt daemon in console mode...\n";
    titanext::TitanExtDaemon::Get().Start();
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
#endif
    return 0;
}
