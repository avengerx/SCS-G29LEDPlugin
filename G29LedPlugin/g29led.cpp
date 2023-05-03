#include "pch.h"
#include "g29led.h"
#include "truck.h"

#include <hidsdi.h>
#include <SetupAPI.h>
#include <time.h>

#define G29_LED_00000 0x00
#define G29_LED_10000 0x01
#define G29_LED_01000 0x02
#define G29_LED_11000 0x03
#define G29_LED_00100 0x04
#define G29_LED_10100 0x05
#define G29_LED_01100 0x06
#define G29_LED_11100 0x07
#define G29_LED_00010 0x08
#define G29_LED_10010 0x09
#define G29_LED_01010 0x0a
#define G29_LED_11010 0x0b
#define G29_LED_00110 0x0c
#define G29_LED_10110 0x0d
#define G29_LED_01110 0x0e
#define G29_LED_11110 0x0f
#define G29_LED_00001 0x10
#define G29_LED_10001 0x11
#define G29_LED_01001 0x12
#define G29_LED_11001 0x13
#define G29_LED_00101 0x14
#define G29_LED_10101 0x15
#define G29_LED_01101 0x16
#define G29_LED_11101 0x17
#define G29_LED_00011 0x18
#define G29_LED_10011 0x19
#define G29_LED_01011 0x1a
#define G29_LED_11011 0x1b
#define G29_LED_00111 0x1c
#define G29_LED_10111 0x1d
#define G29_LED_01111 0x1e
#define G29_LED_11111 0x1f

#define G29_LED_NONE G29_LED_00000
#define G29_LED_ALL G29_LED_11111

// FIXME: Use Regexp to match the device.
#define G29_sVPID L"VID_046D&PID_C24F&"
#define G29_sMI L"&MI_00"

static bool initialized = false;
static USHORT HIDPayloadLen = 0;
static WCHAR* HIDPath;
static HANDLE HIDHandle;

static unsigned char ledState = G29_LED_NONE;
static unsigned char prevLedState = ledState;
static float current_fuel;
static float max_fuel;

static const unsigned char fillStates[] = {
    G29_LED_00000,
    G29_LED_00001,
    G29_LED_00011,
    G29_LED_00111,
    G29_LED_01111,
    G29_LED_11111
};

static time_t lastInit = time(0);

static void detailedError(const WCHAR* msg) {
    LPVOID lpMsgBuf;
    DWORD leid = GetLastError();
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        leid,
        0,
        (LPWSTR)&lpMsgBuf,
        0, NULL);

    logErr(L"Error: %s: %s (0x%x)", msg, (LPWSTR)lpMsgBuf, leid);
    LocalFree(lpMsgBuf);
}

static HRESULT loadHID() {
    HANDLE hidHandle = CreateFile(HIDPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (hidHandle == INVALID_HANDLE_VALUE) {
        detailedError(L"Cannot open joystick for reading its HID parameters");
        return GetLastError();
    }

    PHIDP_PREPARSED_DATA data;
    if (!HidD_GetPreparsedData(hidHandle, &data)) {
        detailedError(L"Unable to fetch joystick's HID pre-parsed data");
        return GetLastError();
    }

    HIDP_CAPS hCaps;
    if (HidP_GetCaps(data, &hCaps) != HIDP_STATUS_SUCCESS) {
        HidD_FreePreparsedData(data);
        detailedError(L"Unable to fetch joystick's HID capabilities");
        return GetLastError();
    }
    HidD_FreePreparsedData(data);

    HIDPayloadLen = hCaps.OutputReportByteLength;
    log("Joystick HID packet size: %u bytes.\n", HIDPayloadLen);

    CloseHandle(hidHandle);

    HIDHandle = CreateFile(HIDPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (HIDHandle == INVALID_HANDLE_VALUE) {
        detailedError(L"Cannot open the joystick for sending HID data");
        return GetLastError();
    }

    return S_OK;
}

static HRESULT sendHIDPayload(unsigned char cmd, unsigned char arg1 = 0x00, unsigned char arg2 = 0x00, unsigned char arg3 = 0x00, unsigned char arg4 = 0x00, unsigned char arg5 = 0x00, unsigned char arg6 = 0x00) {
    unsigned char* payload;
    unsigned short idx;
    DWORD wrCnt;

    if (!initialized && !(LoadController() == S_OK)) return ERROR_DEVICE_NOT_AVAILABLE;

    if (HIDPayloadLen == 0) {
        logErr("Tried to send HID command before complete initialization.\n");
        return ERROR_DEVICE_NOT_AVAILABLE;
    } else if (HIDPayloadLen < 8) {
        logErr("HID device report packet size smaller than packets we need to send (%i/%i).\n", HIDPayloadLen, 8);
        return ERROR_DEVICE_ENUMERATION_ERROR;
    }

    payload = new unsigned char[HIDPayloadLen + 1];

    ZeroMemory(payload, HIDPayloadLen + 1);

    idx = 1;
    payload[idx++] = cmd;
    payload[idx++] = arg1;
    payload[idx++] = arg2;
    payload[idx++] = arg3;
    payload[idx++] = arg4;
    payload[idx++] = arg5;
    payload[idx++] = arg6;

    if (HIDHandle == INVALID_HANDLE_VALUE) {
        SetLastError(ERROR_INVALID_HANDLE);
        logErr("Error: Joystick access handle not available while trying to change LEDs.");
        return GetLastError();
    }

    if (!WriteFile(HIDHandle, payload, HIDPayloadLen, &wrCnt, NULL)) {
        logErr("Tried to write: 0x00,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x.\n",
            cmd, arg1, arg2, arg3, arg4, arg5, arg6);
        detailedError(L"Cannot write data to joystick");
        return GetLastError();
    }

    return S_OK;
}

static HRESULT updateLEDs(unsigned char new_state) {
    if (new_state != ledState) {
        ledState = new_state;
        return sendHIDPayload(0xf8, 0x12, ledState, 0x00, 0x00, 0x00, 0x01);
    } else return S_OK;
}

static unsigned char ledStateFromFillState() {
    float fill_state;
    truck_data_access.lock();
    // TODO: make blink effect (so never return early)
    if (truck_data.fuel != current_fuel || truck_data.fuel_max != max_fuel) {
        current_fuel = truck_data.fuel;
        max_fuel = truck_data.fuel_max;
    }
    truck_data_access.unlock();

    fill_state = current_fuel / max_fuel;
    log("Fuel: %1.2f / %1.2f (%1.2f)", current_fuel, max_fuel, fill_state);
    if (fill_state < 0.15)      return G29_LED_00001;
    else if (fill_state < 0.25) return G29_LED_00011;
    else if (fill_state < 0.50) return G29_LED_00111;
    else if (fill_state < 0.75) return G29_LED_01111;
    else                        return G29_LED_11111;
}

HRESULT LoadController() {
    log("Loading controller.");

    GUID hidIdx;
    HDEVINFO hidDevsHandle;
    SP_DEVINFO_DATA device;
    SP_DEVICE_INTERFACE_DATA devData;
    devData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    PSP_DEVICE_INTERFACE_DETAIL_DATA devDetails;

    DWORD memberIdx = 0, dwSize, dwType;
    PBYTE buf;

    unsigned short loopguard;

    // TODO: Check last init attempt time to avoid init attempt flooding

    HidD_GetHidGuid(&hidIdx);
    hidDevsHandle = SetupDiGetClassDevs(&hidIdx, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (hidDevsHandle == INVALID_HANDLE_VALUE) {
        detailedError(L"Unable to enumerate HID devices on system");
        return ERROR_DEVICE_ENUMERATION_ERROR;
    }

    loopguard = 0;
    while (true) {
        if (loopguard++ > 200) {
            logErr("Error: Iterated 200 times without listing all HID devices?\n");
            return ERROR_INFLOOP_IN_RELOC_CHAIN;
        }

        device.cbSize = sizeof(SP_DEVINFO_DATA);
        if (!SetupDiEnumDeviceInfo(hidDevsHandle, memberIdx, &device)) {
            detailedError(L"Unable to locate a Logitech G29 steering wheel plugged to the system.");
            return GetLastError();
        }

        SetupDiGetDeviceRegistryProperty(hidDevsHandle, &device, SPDRP_HARDWAREID, &dwType, NULL, 0, &dwSize);
        if (dwSize > 0 && dwSize < 16384) {
            buf = (PBYTE)malloc(dwSize * sizeof(BYTE));

            //printf("Allocated buf with %lu entries of %zi bytes.\n", dwSize, sizeof(BYTE));
            if (SetupDiGetDeviceRegistryProperty(hidDevsHandle, &device, SPDRP_HARDWAREID, &dwType, buf, dwSize, NULL) &&
                wcsstr((WCHAR*)buf, (WCHAR*)&G29_sVPID) && wcsstr((WCHAR*)buf, (WCHAR*)&G29_sMI)) {

                log(L"Found: %s\n", (WCHAR*)buf);

                SetupDiGetDeviceRegistryProperty(hidDevsHandle, &device, SPDRP_DEVICEDESC, &dwType, NULL, 0, &dwSize);
                if (dwSize <= 0 || dwSize > 16384) {
                    logErr(L"Error: Unable to fetch device description from: %ws\n", (WCHAR*)buf);
                    return ERROR_INVALID_DEVICE_OBJECT_PARAMETER;
                }

                free(buf);
                buf = (PBYTE)malloc(dwSize * sizeof(BYTE));

                if (!SetupDiGetDeviceRegistryProperty(hidDevsHandle, &device, SPDRP_DEVICEDESC, &dwType, buf, dwSize, NULL)) {
                    logErr(L"Error: Unable to fetch device description from: %ws\n", (WCHAR*)buf);
                    detailedError(L"Unable to fetch device description");
                    return GetLastError();
                }

                log(L"Device: %ws\n", (WCHAR*)buf);

                SetupDiEnumDeviceInterfaces(hidDevsHandle, NULL, &hidIdx, memberIdx, &devData);
                SetupDiGetDeviceInterfaceDetail(hidDevsHandle, &devData, NULL, 0, &dwSize, NULL);
                if (dwSize < 1 || dwSize > 16384) {
                    logErr("Error: Unable to get device details.\n");
                    return ERROR_INVALID_DEVICE_OBJECT_PARAMETER;
                }

                devDetails = (PSP_INTERFACE_DEVICE_DETAIL_DATA)malloc(dwSize);
                if (!devDetails) {
                    SetLastError(ERROR_OUTOFMEMORY);
                    detailedError(L"Unable to allocate memory to store device information");
                    return GetLastError();
                }
                devDetails->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

                if (!SetupDiGetDeviceInterfaceDetail(hidDevsHandle, &devData, devDetails, dwSize, &dwSize, NULL)) {
                    detailedError(L"Unable to get device details.\n");
                    return GetLastError();
                }

                dwSize = (lstrlen(devDetails->DevicePath) + 1) * sizeof(WCHAR);
                HIDPath = (WCHAR*)malloc(dwSize);
                memcpy_s(HIDPath, dwSize, devDetails->DevicePath, dwSize);
                free(devDetails);
                break;
            }
            free(buf);
        }
        memberIdx++;
    }

    log(L"HID path: %s\n", HIDPath);

    loadHID();
    initialized = true;
    return S_OK;
}

HRESULT UnloadController() {
    CloseHandle(HIDHandle);
    HIDHandle = INVALID_HANDLE_VALUE;
    HIDPayloadLen = 0;
    free(HIDPath);
    HIDPath = NULL;
    initialized = false;

    return S_OK;
}

HRESULT ClearLEDs() {
    if (!initialized && !(LoadController() == S_OK)) return ERROR_DEVICE_NOT_AVAILABLE;

    log("Turning all LEDs off.");
    return updateLEDs(G29_LED_NONE);
}

HRESULT UpdateFuelLevel() {
    if (!initialized && (LoadController() != S_OK)) return ERROR_DEVICE_NOT_AVAILABLE;
    return updateLEDs(ledStateFromFillState());
}

#define UpdateChk(x) update_state = updateLEDs(x); if (update_state != S_OK) return update_state;

HRESULT InitFuelGaugeAnimation() {
    DWORD delay = 50;
    size_t i;
    HRESULT update_state;
    unsigned char animation[] = {
        G29_LED_00000,
        G29_LED_00001,
        G29_LED_00010,
        G29_LED_00100,
        G29_LED_01000,
        G29_LED_10000,
        G29_LED_11000,
        G29_LED_11100,
        G29_LED_11110,
        G29_LED_11111
    };
    unsigned char down_animation[] = {
        G29_LED_11111,
        G29_LED_01111,
        G29_LED_00111,
        G29_LED_00011,
        G29_LED_00001,
        G29_LED_00000
    };

    log("Playing \"truck electricity on\" animation.");
    size_t anim_len = sizeof(animation) / sizeof(unsigned char);
    unsigned char target_led_state = ledStateFromFillState();

    for (i = 0; i < anim_len; i++) {
        UpdateChk(animation[i]);
        Sleep(delay);
    }

    anim_len = sizeof(down_animation) / sizeof(unsigned char);
    for (i = 0; i < anim_len; i++) {
        UpdateChk(down_animation[i]);
        if (down_animation[i] == target_led_state) break;
        Sleep(delay);
    }

    for (i = 0; i < 3; i++) {
        Sleep(100);
        UpdateChk(G29_LED_NONE);
        Sleep(25);
        UpdateChk(target_led_state);
    }

    if (target_led_state == G29_LED_00001) {
        for (i = 0; i < 5; i++) {
            Sleep(100);
            UpdateChk(G29_LED_NONE);
            Sleep(50);
            UpdateChk(target_led_state);
        }
    }

    return S_OK;
}

HRESULT ShutdownFuelGaugeAnimation() {
    HRESULT update_state;
    unsigned char current_led_state = ledState;

    log("Playing \"truck electricity off\" animation.");

    UpdateChk(G29_LED_NONE);
    Sleep(25);
    UpdateChk(current_led_state);
    Sleep(200);
    UpdateChk(G29_LED_NONE);
    Sleep(30);
    UpdateChk(current_led_state);
    Sleep(10);
    UpdateChk(G29_LED_NONE);
    Sleep(45);
    UpdateChk(current_led_state);
    Sleep(160);
    UpdateChk(G29_LED_NONE);
    Sleep(50);
    UpdateChk(current_led_state);
    Sleep(25);
    UpdateChk(G29_LED_NONE);
    Sleep(70);
    UpdateChk(current_led_state);
    Sleep(10);
    UpdateChk(G29_LED_NONE);
    return S_OK;
}