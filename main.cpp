#include <windows.h>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

// Forward the core DX11 entry points directly to the real system DLL in System32
#pragma comment(linker, "/export:D3D11CreateDevice=C:\\Windows\\System32\\d3d11.D3D11CreateDevice")
#pragma comment(linker, "/export:D3D11CreateDeviceAndSwapChain=C:\\Windows\\System32\\d3d11.D3D11CreateDeviceAndSwapChain")

// ============================================================================
// CONFIGURATION
// ============================================================================
// !! REPLACE THIS WITH YOUR ACTUAL DISCORD DEVELOPER APPLICATION ID !!
const std::string DISCORD_APPLICATION_ID = "1528189827089170582"; 
const int TICK_INTERVAL_MS = 15000;

// Internal structural logging helper
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

// IPC Protocol Hard-Mode Command Wrapper
// Frames must be written in a single unified byte array to avoid breaking the pipe.
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

// Safely attempts to discover and bind to any live running Discord desktop clients
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

        if (pipeHandle != INVALID_HANDLE_VALUE) {
            WriteProxyLog("Connected successfully to active pipe asset: " + pipeName);
            return pipeHandle;
        }
    }
    return INVALID_HANDLE_VALUE;
}

// Main background worker loop running completely out-of-band from rendering threads
void DiscordRpcWorkerThread() {
    WriteProxyLog("Background worker thread spun up successfully.");
    
    char tempPath[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tempPath)) {
        WriteProxyLog("CRITICAL: Unable to resolve local Windows %TEMP% path.");
        return;
    }
    std::string jsonPath = std::string(tempPath) + "rpc_addon_state.json";

    HANDLE pipe = INVALID_HANDLE_VALUE;

    while (true) {
        // Handle automated reconnection if the pipe is disconnected or closed
        if (pipe == INVALID_HANDLE_VALUE) {
            pipe = ConnectToDiscordPipe();
            if (pipe != INVALID_HANDLE_VALUE) {
                // Step 1: Perform Protocol Handshake (Opcode 0)
                std::string handshakeJson = "{\"v\":1,\"client_id\":\"" + DISCORD_APPLICATION_ID + "\"}";
                if (!SendIpcPacket(pipe, 0, handshakeJson)) {
                    WriteProxyLog("Handshake payload delivery failed. Dropping handle.");
                    CloseHandle(pipe);
                    pipe = INVALID_HANDLE_VALUE;
                } else {
                    WriteProxyLog("Handshake sent successfully.");
                }
            }
        }

        // If connected, read the latest file data out and feed it to the raw IPC pipe
        if (pipe != INVALID_HANDLE_VALUE) {
            std::ifstream jsonFile(jsonPath);
            if (jsonFile.is_open()) {
                std::string activityInnerJson((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());
                jsonFile.close();

                if (!activityInnerJson.empty()) {
                    // Get our current process ID to satisfy Discord's framework tracking
                    DWORD currentPid = GetCurrentProcessId();

                    // Step 2: Construct the full SET_ACTIVITY container payload (Opcode 1)
                    std::string fullPayload = "{"
                        "\"cmd\":\"SET_ACTIVITY\","
                        "\"args\":{"
                            "\"pid\":" + std::to_string(currentPid) + ","
                            "\"activity\":" + activityInnerJson +
                        "},"
                        "\"nonce\":\"stalker_rpc_nonce\""
                    "}";

                    if (!SendIpcPacket(pipe, 1, fullPayload)) {
                        WriteProxyLog("Failed to push presence frame to pipe. Closing link.");
                        CloseHandle(pipe);
                        pipe = INVALID_HANDLE_VALUE;
                    }
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(TICK_INTERVAL_MS));
    }
}

// Entry point structure
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        WriteProxyLog("SUCCESS: Custom d3d11.dll proxy hooked into X-Ray client footprint!");
        
        // Spawn the worker routine onto an independent thread immediately to safeguard engine frames
        std::thread rpcThread(DiscordRpcWorkerThread);
        rpcThread.detach(); 
    }
    return TRUE;
}