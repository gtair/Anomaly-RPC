#include <windows.h>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

#pragma comment(linker, "/export:D3D11CreateDevice=C:\\Windows\\System32\\d3d11.D3D11CreateDevice")
#pragma comment(linker, "/export:D3D11CreateDeviceAndSwapChain=C:\\Windows\\System32\\d3d11.D3D11CreateDeviceAndSwapChain")

// Configuration
const std::string DISCORD_APPLICATION_ID = "1528189827089170582"; 
const int TICK_INTERVAL_MS = 15000;

void WriteProxyLog(const std::string& message) {
    char tempPath[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tempPath)) {
        std::string logFilePath = std::string(tempPath) + "anomaly_d3d11_proxy.log";
        std::ofstream logFile(logFilePath, std::ios::app);
        if (logFile.is_open()) {
            logFile << "[RPC_PROXY] " << message << "\n";
            logFile.close();
        }
    }
}

// IPC Communication
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

HANDLE ConnectToDiscordPipe() {
    for (int i = 0; i < 10; ++i) {
        std::string pipeName = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
        HANDLE pipeHandle = CreateFileA(
            pipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            NULL
        );
        if (pipeHandle != INVALID_HANDLE_VALUE) return pipeHandle;
    }
    return INVALID_HANDLE_VALUE;
}

// Worker Thread
void DiscordRpcWorkerThread() {
    char tempPath[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tempPath)) return;
    std::string jsonPath = std::string(tempPath) + "rpc_addon_state.json";
    
    HANDLE pipe = INVALID_HANDLE_VALUE;
    std::string lastSentPayload = "";

    while (true) {
        if (pipe == INVALID_HANDLE_VALUE) {
            pipe = ConnectToDiscordPipe();
            if (pipe != INVALID_HANDLE_VALUE) {
                std::string handshakeJson = "{\"v\":1,\"client_id\":\"" + DISCORD_APPLICATION_ID + "\"}";
                if (!SendIpcPacket(pipe, 0, handshakeJson)) {
                    CloseHandle(pipe);
                    pipe = INVALID_HANDLE_VALUE;
                }
            }
        }

        if (pipe != INVALID_HANDLE_VALUE) {
            std::ifstream jsonFile(jsonPath);
            if (jsonFile.is_open()) {
                std::string activityInnerJson((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());
                jsonFile.close();

                if (!activityInnerJson.empty() && activityInnerJson != lastSentPayload) {
                    DWORD currentPid = GetCurrentProcessId();
                    std::string fullPayload = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(currentPid) + ",\"activity\":" + activityInnerJson + "},\"nonce\":\"stalker_rpc_nonce\"}";

                    if (SendIpcPacket(pipe, 1, fullPayload)) {
                        lastSentPayload = activityInnerJson;
                    } else {
                        CloseHandle(pipe);
                        pipe = INVALID_HANDLE_VALUE;
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(TICK_INTERVAL_MS));
    }
}

// Entry Point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    char tempPath[MAX_PATH];
    std::string jsonPath;
    
    if (GetTempPathA(MAX_PATH, tempPath)) {
        jsonPath = std::string(tempPath) + "rpc_addon_state.json";
    }

    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hModule);
        
        if (!jsonPath.empty()) {
            DeleteFileA(jsonPath.c_str());
        }
        
        std::thread rpcThread(DiscordRpcWorkerThread);
        rpcThread.detach(); 
        break;
    }

    case DLL_PROCESS_DETACH: {
        if (!jsonPath.empty()) {
            DeleteFileA(jsonPath.c_str());
        }
        break;
    }
    }
    return TRUE;
}