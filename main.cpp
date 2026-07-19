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
const std::chrono::milliseconds POLL_INTERVAL(500);

// ---------------------------------------------------------------------
// Debug logging
// ---------------------------------------------------------------------

void WriteDebugLog(const std::string& message) {
    char tempPath[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tempPath)) return;
    std::string logFilePath = std::string(tempPath) + "anomaly_rpc_debug_dll.log";
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

bool LooksLikeCompleteJson(const std::string& s) {
    if (s.empty()) return false;

    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos || end == std::string::npos) return false;
    if (s[start] != '{' || s[end] != '}') return false;

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = start; i <= end; ++i) {
        char c = s[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') { inString = true; continue; }
        if (c == '{') depth++;
        else if (c == '}') depth--;
        if (depth < 0) return false;
    }
    return depth == 0 && !inString;
}

std::string ReadStateFileSafely(const std::string& path) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::ifstream jsonFile(path, std::ios::binary);
        if (jsonFile.is_open()) {
            std::ostringstream ss;
            ss << jsonFile.rdbuf();
            std::string content = ss.str();
            if (LooksLikeCompleteJson(content)) {
                return content;
            }
        }
        Sleep(25);
    }
    return std::string();
}

// ---------------------------------------------------------------------
// Worker Thread
// ---------------------------------------------------------------------

void DiscordRpcWorkerThread() {
    WriteDebugLog("Worker thread started.");

    char tempPath[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tempPath)) {
        WriteDebugLog("GetTempPathA failed; worker thread exiting.");
        return;
    }
    std::string jsonPath = std::string(tempPath) + "rpc_addon_state.json";

    HANDLE pipe = INVALID_HANDLE_VALUE;
    bool connectionReady = false;
    std::string lastSentPayload;
    std::string pendingPayload;
    bool havePending = false;

    auto lastSendTime = std::chrono::steady_clock::now() - RATE_LIMIT_COOLDOWN;

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

        std::string currentJson = ReadStateFileSafely(jsonPath);
        if (!currentJson.empty() && currentJson != lastSentPayload && currentJson != pendingPayload) {
            pendingPayload = currentJson;
            havePending = true;
            WriteDebugLog("New state detected and queued.");
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

        std::this_thread::sleep_for(POLL_INTERVAL);
    }
}

// ---------------------------------------------------------------------
// Entry Point
// ---------------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    char tempPath[MAX_PATH];
    std::string jsonPath;

    if (GetTempPathA(MAX_PATH, tempPath)) {
        jsonPath = std::string(tempPath) + "rpc_addon_state.json";
    }

    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hModule);
        WriteDebugLog("DLL_PROCESS_ATTACH.");

        if (!jsonPath.empty()) {
            DeleteFileA(jsonPath.c_str());
        }

        std::thread rpcThread(DiscordRpcWorkerThread);
        rpcThread.detach();
        break;
    }

    case DLL_PROCESS_DETACH: {
        WriteDebugLog("DLL_PROCESS_DETACH.");
        if (!jsonPath.empty()) {
            DeleteFileA(jsonPath.c_str());
        }
        break;
    }
    }
    return TRUE;
}