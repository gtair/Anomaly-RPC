#include <windows.h>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <cstdio>

#pragma comment(linker, "/export:D3D11CreateDevice=C:\\Windows\\System32\\d3d11.D3D11CreateDevice")
#pragma comment(linker, "/export:D3D11CreateDeviceAndSwapChain=C:\\Windows\\System32\\d3d11.D3D11CreateDeviceAndSwapChain")

// Configuration
const std::string DISCORD_APPLICATION_ID = "1528189827089170582";
const std::chrono::milliseconds RATE_LIMIT_COOLDOWN(15000);
const std::chrono::milliseconds RECONNECT_RETRY_INTERVAL(3000);
const bool DEBUG_LOGGING_ENABLED = false;
//DO NOT CHANGE EITHER UNLESS ALSO CHANGING LOCATION IN THE LUA CODE
const char* RPC_SUBFOLDER = "AnomalyRPC";
const char* LOG_SUBFOLDER = "AnomalyRPC\\logs";

// ---------------------------------------------------------------------
// Shared folder helpers
// ---------------------------------------------------------------------

std::string GetTempSubdir(const char* subPath) {
    char tempPath[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tempPath)) return std::string();

    std::string dir = std::string(tempPath) + subPath + "\\";
    if (CreateDirectoryA(dir.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return dir;
    }
    return std::string();
}

std::string GetAnomalyRpcDir() {
    return GetTempSubdir(RPC_SUBFOLDER);
}

std::string GetAnomalyRpcLogDir() {
    GetAnomalyRpcDir();
    return GetTempSubdir(LOG_SUBFOLDER);
}

// ---------------------------------------------------------------------
// Debug logging
// ---------------------------------------------------------------------

void WriteDebugLog(const std::string& message) {
    if (!DEBUG_LOGGING_ENABLED) return;

    std::string dir = GetAnomalyRpcLogDir();
    if (dir.empty()) return;
    std::string logFilePath = dir + "anomaly_rpc_debug_dll.log";
    std::ofstream logFile(logFilePath, std::ios::app);
    if (!logFile.is_open()) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "[%02d:%02d:%02d.%03d] ",
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    logFile << timeBuf << message << "\n";
}


// ---------------------------------------------------------------------
// IPC Communication
// ---------------------------------------------------------------------

bool SendIpcPacket(HANDLE pipeHandle, uint32_t opcode, const std::string& jsonPayload) {
    uint32_t length = static_cast<uint32_t>(jsonPayload.length());
    std::vector<uint8_t> buffer(8 + length);
    memcpy(&buffer[0], &opcode, 4);
    memcpy(&buffer[4], &length, 4);
    if (length > 0) {
        memcpy(&buffer[8], jsonPayload.c_str(), length);
    }
    DWORD bytesWritten = 0;
    BOOL result = WriteFile(pipeHandle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesWritten, NULL);
    return (result && bytesWritten == buffer.size());
}

struct DrainResult {
    bool receivedData = false;
    bool pipeBroken = false;
    std::string data;
};

DrainResult DrainIpcResponse(HANDLE pipeHandle, int maxWaitMs = 2000) {
    DrainResult result;
    const int stepMs = 20;
    int waited = 0;
    DWORD bytesAvailable = 0;

    while (waited < maxWaitMs) {
        if (!PeekNamedPipe(pipeHandle, NULL, 0, NULL, &bytesAvailable, NULL)) {
            result.pipeBroken = true;
            return result;
        }
        if (bytesAvailable > 0) break;
        Sleep(stepMs);
        waited += stepMs;
    }

    if (bytesAvailable == 0) {
        return result;
    }

    std::vector<uint8_t> readBuf(bytesAvailable);
    DWORD bytesRead = 0;
    if (!ReadFile(pipeHandle, readBuf.data(), bytesAvailable, &bytesRead, NULL)) {
        result.pipeBroken = true;
        return result;
    }
    result.receivedData = true;
    result.data.assign(reinterpret_cast<char*>(readBuf.data()), bytesRead);
    return result;
}

HANDLE ConnectToDiscordPipe() {
    for (int i = 0; i < 10; ++i) {
        std::string pipeName = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
        HANDLE pipeHandle = CreateFileA(
            pipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );
        if (pipeHandle != INVALID_HANDLE_VALUE) {
            WriteDebugLog("Connected to pipe: " + pipeName);
            return pipeHandle;
        }
    }
    return INVALID_HANDLE_VALUE;
}

bool LooksComplete(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return false;
    return s[start] == '{' && s[end] == '}';
}

std::string ReadStateFileSafely(const std::string& path) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        HANDLE hFile = CreateFileA(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD fileSize = GetFileSize(hFile, NULL);
            if (fileSize != INVALID_FILE_SIZE && fileSize > 0) {
                std::string content;
                content.resize(fileSize);
                DWORD bytesRead = 0;
                BOOL ok = ReadFile(hFile, &content[0], fileSize, &bytesRead, NULL);
                CloseHandle(hFile);
                if (ok && bytesRead == fileSize && LooksComplete(content)) {
                    return content;
                }
            } else {
                CloseHandle(hFile);
            }
        }
        Sleep(15);
    }
    return std::string();
}

// ---------------------------------------------------------------------
// Worker Thread
// ---------------------------------------------------------------------

void DiscordRpcWorkerThread() {
    WriteDebugLog("Worker thread started.");

    std::string rpcDir = GetAnomalyRpcDir();
    if (rpcDir.empty()) {
        WriteDebugLog("GetAnomalyRpcDir failed; worker thread exiting.");
        return;
    }
    std::string jsonPath = rpcDir + "rpc_addon_state.json";

    HANDLE hDirChange = FindFirstChangeNotificationA(
        rpcDir.c_str(),
        FALSE,
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE
    );
    bool haveDirWatch = (hDirChange != INVALID_HANDLE_VALUE);
    if (!haveDirWatch) {
        WriteDebugLog("FindFirstChangeNotification failed; falling back to timed polling.");
    }

    HANDLE pipe = INVALID_HANDLE_VALUE;
    bool connectionReady = false;
    std::string lastSentPayload;
    std::string pendingPayload;
    bool havePending = false;

    auto lastSendTime = std::chrono::steady_clock::now() - RATE_LIMIT_COOLDOWN;

    {
        std::string initialJson = ReadStateFileSafely(jsonPath);
        if (!initialJson.empty()) {
            pendingPayload = initialJson;
            havePending = true;
        }
    }

    while (true) {
        if (pipe == INVALID_HANDLE_VALUE) {
            connectionReady = false;
            pipe = ConnectToDiscordPipe();

            if (pipe != INVALID_HANDLE_VALUE) {
                std::string handshakeJson = "{\"v\":1,\"client_id\":\"" + DISCORD_APPLICATION_ID + "\"}";
                if (SendIpcPacket(pipe, 0, handshakeJson)) {
                    WriteDebugLog("Handshake sent, waiting for READY.");
                    DrainResult dr = DrainIpcResponse(pipe, 3000);

                    if (dr.pipeBroken) {
                        WriteDebugLog("Pipe broke while waiting for handshake response.");
                        CloseHandle(pipe);
                        pipe = INVALID_HANDLE_VALUE;
                    } else if (dr.receivedData) {
                        if (dr.data.find("READY") != std::string::npos) {
                            WriteDebugLog("READY received, connection is live.");
                        } else {
                            WriteDebugLog("Handshake response received (no READY marker seen): " + dr.data);
                        }
                        connectionReady = true;
                    } else {
                        WriteDebugLog("No handshake response within timeout; will retry connection.");
                        CloseHandle(pipe);
                        pipe = INVALID_HANDLE_VALUE;
                    }
                } else {
                    WriteDebugLog("Failed to write handshake packet.");
                    CloseHandle(pipe);
                    pipe = INVALID_HANDLE_VALUE;
                }
            } else {
                WriteDebugLog("Could not connect to any discord-ipc-N pipe.");
            }
        }

        if (pipe != INVALID_HANDLE_VALUE && connectionReady && havePending) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - lastSendTime;

            if (elapsed >= RATE_LIMIT_COOLDOWN) {
                DWORD currentPid = GetCurrentProcessId();
                std::string fullPayload = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" +
                    std::to_string(currentPid) + ",\"activity\":" + pendingPayload +
                    "},\"nonce\":\"stalker_rpc_nonce\"}";

                if (SendIpcPacket(pipe, 1, fullPayload)) {
                    DrainResult dr = DrainIpcResponse(pipe);
                    if (dr.pipeBroken) {
                        WriteDebugLog("Pipe broke while draining activity response; will reconnect.");
                        CloseHandle(pipe);
                        pipe = INVALID_HANDLE_VALUE;
                    } else {
                        WriteDebugLog("Activity sent successfully.");
                        lastSentPayload = pendingPayload;
                        havePending = false;
                        lastSendTime = now;
                    }
                } else {
                    WriteDebugLog("Failed to write activity packet; will reconnect.");
                    CloseHandle(pipe);
                    pipe = INVALID_HANDLE_VALUE;
                }
            }
        }

        if (!haveDirWatch) {
            Sleep(1000);
            std::string currentJson = ReadStateFileSafely(jsonPath);
            if (!currentJson.empty() && currentJson != lastSentPayload && currentJson != pendingPayload) {
                pendingPayload = currentJson;
                havePending = true;
            }
            continue;
        }

        DWORD waitMs = INFINITE;
        if (pipe == INVALID_HANDLE_VALUE) {
            waitMs = static_cast<DWORD>(RECONNECT_RETRY_INTERVAL.count());
        } else if (havePending) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - lastSendTime;
            if (elapsed < RATE_LIMIT_COOLDOWN) {
                auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(RATE_LIMIT_COOLDOWN - elapsed);
                waitMs = static_cast<DWORD>(remaining.count());
            } else {
                waitMs = 0;
            }
        }

        DWORD waitResult = WaitForSingleObject(hDirChange, waitMs);
        if (waitResult == WAIT_OBJECT_0) {
            FindNextChangeNotification(hDirChange);

            std::string currentJson = ReadStateFileSafely(jsonPath);
            if (!currentJson.empty() && currentJson != lastSentPayload && currentJson != pendingPayload) {
                pendingPayload = currentJson;
                havePending = true;
                WriteDebugLog("New state detected and queued.");
            }
        }
    }

    if (haveDirWatch) {
        FindCloseChangeNotification(hDirChange);
    }
}

// ---------------------------------------------------------------------
// Entry Point
// ---------------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hModule);

        std::string rpcDir = GetAnomalyRpcDir();
        std::string logDir = GetAnomalyRpcLogDir();

        if (!rpcDir.empty()) {
            DeleteFileA((rpcDir + "rpc_addon_state.json").c_str());
        }
        if (!logDir.empty()) {
            DeleteFileA((logDir + "anomaly_rpc_debug_dll.log").c_str());
            DeleteFileA((logDir + "anomaly_rpc_debug.log").c_str());
        }

        WriteDebugLog("DLL_PROCESS_ATTACH.");

        std::thread rpcThread(DiscordRpcWorkerThread);
        rpcThread.detach();
        break;
    }

    case DLL_PROCESS_DETACH: {
        WriteDebugLog("DLL_PROCESS_DETACH.");
        break;
    }
    }
    return TRUE;
}