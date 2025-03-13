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
static NOTIFYICONDATAW g_notification;
static std::vector<DeviceInfo> g_devices{};
static std::mutex g_devicesMutex{};

// Utility stuff
static void SetNotificationText(const wchar_t* text)
{
    wcscpy(g_notification.szTip, text);
    Shell_NotifyIconW(NIM_MODIFY, &g_notification);
}

static void RegisterTrayIcon(HWND hwnd)
{
    memset(&g_notification, 0, sizeof(g_notification));
    g_notification.cbSize = sizeof(g_notification);
    g_notification.hWnd = hwnd;
    g_notification.uCallbackMessage = WM_APP + 1;
    g_notification.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32516));
    g_notification.uFlags = NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP | NIF_ICON;
    Shell_NotifyIconW(NIM_ADD, &g_notification);

    g_notification.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_notification);
}

static void TryAddDevice(CorsairDeviceInfo& csrDevice)
{
    std::lock_guard lock(g_devicesMutex);
    // Don't add duplicates
    auto deviceIt = std::find_if(g_devices.begin(), g_devices.end(), [&](DeviceInfo& device) { return strcmp(device.m_id, csrDevice.id) == 0; });
    if(deviceIt != g_devices.end()) return;

    CorsairDataType dataType;
    CorsairPropertyFlag flags;
    CorsairGetDevicePropertyInfo(csrDevice.id, CorsairDevicePropertyId::CDPI_BatteryLevel, 0, &dataType, (uint32_t*)&flags);

    // Device doesn't have a battery level
    if(flags == CorsairPropertyFlag::CPF_None) return;

    auto& device = g_devices.emplace_back(DeviceInfo{});
    strcpy(device.m_id, csrDevice.id);
    strcpy(device.m_deviceName, csrDevice.model);

    printf("Added device %s\n", device.m_deviceName);
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
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// Main
int main()
{
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
    CorsairDeviceFilter filter;
    filter.deviceTypeMask = CDT_All;
    CorsairDeviceInfo infos[16];
    int deviceAmount = 0;
    CorsairGetDevices(&filter, ARRAYSIZE(infos), &infos[0], &deviceAmount);
    if(deviceAmount == 0)
    {
        printf("Failed to get devices");
        return 1;
    }

    // Save device infos
    for(int i = 0; i < deviceAmount; i++)
    {
        CorsairDeviceInfo& csrDevice = infos[i];
        TryAddDevice(csrDevice);
    }

    // Create window
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_DBLCLKS;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = GetModuleHandle(0);
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = L"icue-battery-class";
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExW(&wcex);
    HWND window = CreateWindowW(wcex.lpszClassName, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandle(0), NULL);

    // Create tray icon
    RegisterTrayIcon(window);

    // Update continously
    while(true)
    {
        if(!g_sdkConnected)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

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
            SetNotificationText(string.data());
        }

        g_devicesChanged = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    return 0;
}
