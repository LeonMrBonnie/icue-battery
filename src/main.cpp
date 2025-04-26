#include "iCUESDK.h"

#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <mutex>
#include <Windows.h>

struct DeviceInfo
{
    CorsairDeviceId m_id{};
    char m_deviceName[CORSAIR_STRING_SIZE_M]{};
    int m_lastBatteryLevel = 0;

    int GetBatteryLevel()
    {
        CorsairProperty data;
        CorsairReadDeviceProperty(m_id, CorsairDevicePropertyId::CDPI_BatteryLevel, 0, &data);
        return data.value.int32;
    }
};

// Globals
static bool g_sdkConnected = false;
static bool g_devicesChanged = false;
static NOTIFYICONDATAW g_trayIcon{};
static std::vector<DeviceInfo> g_devices{};
static std::mutex g_devicesMutex{};

// Utility stuff
static void SetTrayIconText(const wchar_t* text)
{
    wcscpy(g_trayIcon.szTip, text);
    BOOL result = Shell_NotifyIconW(NIM_MODIFY, &g_trayIcon);
    if(!result) {
        printf("Failed to set tray text\n");
    }
}

static void RegisterTrayIcon(HWND hwnd)
{
    g_trayIcon.cbSize = sizeof(g_trayIcon);
    g_trayIcon.uID = 1;
    g_trayIcon.hWnd = hwnd;
    g_trayIcon.uCallbackMessage = WM_USER + 0x100;
    g_trayIcon.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32516));
    g_trayIcon.uFlags = NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP | NIF_ICON;
    g_trayIcon.uVersion = NOTIFYICON_VERSION_4;
    BOOL result = Shell_NotifyIconW(NIM_ADD, &g_trayIcon);
    if(!result) {
        printf("Failed to add tray icon\n");
    }

    Shell_NotifyIconW(NIM_SETVERSION, &g_trayIcon);
}

static void TryAddDevice(CorsairDeviceInfo& csrDevice)
{
    std::lock_guard lock(g_devicesMutex);
    // Don't add duplicates
    auto deviceIt = std::find_if(g_devices.begin(), g_devices.end(), [&](DeviceInfo& device) { return strcmp(device.m_id, csrDevice.id) == 0; });
    if(deviceIt != g_devices.end()) return;

    CorsairProperty properties;
    CorsairPropertyFlag flags;
    CorsairReadDeviceProperty(csrDevice.id, CorsairDevicePropertyId::CDPI_PropertyArray, 0, &properties);

    CorsairDataType_Int32Array& propertiesArr = properties.value.int32_array;
    bool hasBatteryLevel = false;
    for(uint32_t i = 0; i < propertiesArr.count; i++) {
        CorsairDevicePropertyId property = (CorsairDevicePropertyId)propertiesArr.items[i];
        if(property == CorsairDevicePropertyId::CDPI_BatteryLevel) {
            hasBatteryLevel = true;
            break;
        }
    }

    // Device doesn't have a battery level
    if(!hasBatteryLevel) return;

    auto& device = g_devices.emplace_back(DeviceInfo{});
    strcpy(device.m_id, csrDevice.id);
    strcpy(device.m_deviceName, csrDevice.model);

    printf("Added device %s\n", device.m_deviceName);
}

static void PollForDevices()
{
    // Get devices
    CorsairDeviceFilter filter;
    filter.deviceTypeMask = CDT_All;
    CorsairDeviceInfo infos[16];
    int deviceAmount = 0;
    CorsairGetDevices(&filter, ARRAYSIZE(infos), &infos[0], &deviceAmount);
    if(deviceAmount == 0)
    {
        printf("Failed to get devices");
        return;
    }

    // Save device infos
    for(int i = 0; i < deviceAmount; i++)
    {
        CorsairDeviceInfo& csrDevice = infos[i];
        TryAddDevice(csrDevice);
    }
}

// Callbacks
static void OnSessionStateChangedHandler(void*, const CorsairSessionStateChanged* eventData)
{
    if(eventData->state == CorsairSessionState::CSS_Connected)
    {
        g_sdkConnected = true;
    }
    else if(eventData->state == CorsairSessionState::CSS_Closed)
    {
        g_sdkConnected = false;
    }
}

static void OnCorsairEvent(void*, const CorsairEvent* event)
{
    if(event->id != CorsairEventId::CEI_DeviceConnectionStatusChangedEvent) return;

    const CorsairDeviceConnectionStatusChangedEvent* statusChangeEvent = event->deviceConnectionStatusChangedEvent;

    if(statusChangeEvent->isConnected)
    {
        // Try adding the new device
        CorsairDeviceInfo deviceInfo;
        CorsairGetDeviceInfo(statusChangeEvent->deviceId, &deviceInfo);
        TryAddDevice(deviceInfo);
    }
    else
    {
        std::lock_guard lock(g_devicesMutex);
        // Try removing the existing device
        auto deviceIt = std::find_if(g_devices.begin(), g_devices.end(), [&](DeviceInfo& device) { return strcmp(device.m_id, statusChangeEvent->deviceId) == 0; });
        if(deviceIt == g_devices.end()) return;
        printf("Removed device %s\n", deviceIt->m_deviceName);
        g_devices.erase(deviceIt);
    }
    g_devicesChanged = true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch(message) {
        case WM_USER + 0x100: {
            switch (LOWORD(lParam))
            {
                case WM_CONTEXTMENU: {
                    exit(0);
                    break;
                }
            }
            break;
        }
        default: return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

// Main
int main()
{
#ifndef DEBUG
    FreeConsole();
#endif

    // Init SDK
    CorsairError error = CorsairConnect(OnSessionStateChangedHandler, nullptr);
    if(error)
    {
        printf("Error: %d", (int)error);
        return 1;
    }

    // Wait for SDK to be ready
    while(true)
    {
        if(g_sdkConnected) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Listen for events
    CorsairSubscribeForEvents(OnCorsairEvent, nullptr);

    // Get devices
    PollForDevices();

    // Create window
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_DBLCLKS;
    wcex.lpfnWndProc = &WndProc;
    wcex.hInstance = GetModuleHandle(0);
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = L"icue-battery-class";
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExW(&wcex);
    HWND window = CreateWindowW(wcex.lpszClassName, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandle(NULL), NULL);

    // Create tray icon
    RegisterTrayIcon(window);

    // Create thread to handle main loop
    auto thread = std::thread([]() {
        while(true) {
            if(!g_sdkConnected)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }

            PollForDevices();

            // Update battery levels
            bool anyDeviceUpdated = g_devicesChanged;
            for(auto& device : g_devices)
            {
                int currentBatteryLevel = device.GetBatteryLevel();
                if(currentBatteryLevel != device.m_lastBatteryLevel)
                {
                    device.m_lastBatteryLevel = currentBatteryLevel;
                    anyDeviceUpdated = true;
                }
            }

            // Construct and update tooltip
            if(anyDeviceUpdated)
            {
                std::wostringstream stream;
                for(size_t i = 0; i < g_devices.size(); i++)
                {
                    auto& device = g_devices[i];
                    stream << device.m_deviceName << ": " << device.m_lastBatteryLevel << "%";
                    if(i != g_devices.size() - 1) stream << "\n";
                }
                if(g_devices.size() == 0) stream << "No devices connected";

                auto string = stream.str();
                SetTrayIconText(string.data());
            }

            g_devicesChanged = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    });

    // Send WndProc messages
    MSG msg;
    while(GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
