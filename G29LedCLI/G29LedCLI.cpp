#include <stdio.h>
#include <conio.h>
#include <Windows.h>
#include <hidsdi.h>
#include <SetupAPI.h>

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

USHORT HIDPayloadLen = 0;
WCHAR* HIDPath;
bool Verbose = false;

static unsigned const int G29_PID = 0xc24f;
static unsigned const int G29_VID = 0x046d;

// FIXME: Use Regexp to match the device.
static const WCHAR G29_sVPID[] = L"VID_046D&PID_C24F&";
static const WCHAR G29_sMI[] = L"&MI_00";

static unsigned __int64 rdtsc();

static byte ledState = 0x00;
static const byte fillStates[] = { 0x00, 0x10, 0x18, 0x1c, 0x1e, 0x1f };
static const byte rpmStates[] = { 0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f};
static const byte loneStates[] = { 0x01, 0x02, 0x04, 0x08, 0x10};
static const byte miss1States[] = { 0x0f, 0x17, 0x1b, 0x1d, 0x1e };

void detailedError(const WCHAR* msg);
void ledSync();
void loadHID();
HRESULT sendHIDPayload(byte cmd, byte arg1 = 0x00, byte arg2 = 0x00, byte arg3 = 0x00, byte arg4 = 0x00, byte arg5 = 0x00, byte arg6 = 0x00);
static HRESULT InitFuelGaugeAnimation();
static HRESULT ShutdownFuelGaugeAnimation();

int main()
{
    GUID hidIdx;
    HDEVINFO hidDevsHandle;
    SP_DEVINFO_DATA device;
    SP_DEVICE_INTERFACE_DATA devData;
    devData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    PSP_DEVICE_INTERFACE_DETAIL_DATA devDetails;

    DWORD memberIdx = 0, dwSize, dwType;
    PBYTE buf;

    HidD_GetHidGuid(&hidIdx);
    hidDevsHandle = SetupDiGetClassDevs(&hidIdx, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (hidDevsHandle == INVALID_HANDLE_VALUE) {
        detailedError(L"Unable to enumerate HID devices on system");
        exit(1);
    }

    unsigned short cnnnttt = 0;
    while (true) {
        cnnnttt++;
        if (cnnnttt > 200) {
            printf("Error: Iterated 200 times without listing all HID devices?\n");
            exit(1);
        }

        device.cbSize = sizeof(SP_DEVINFO_DATA);
        if (!SetupDiEnumDeviceInfo(hidDevsHandle, memberIdx, &device)) {
            printf("Error: Unable to locate a Logitech G29 steering wheel plugged to the system.");
            exit(1);
        }

        SetupDiGetDeviceRegistryProperty(hidDevsHandle, &device, SPDRP_HARDWAREID, &dwType, NULL, 0, &dwSize);
        if (dwSize > 0 && dwSize < 16384) {
            buf = (PBYTE)malloc(dwSize * sizeof(BYTE));

            //printf("Allocated buf with %lu entries of %zi bytes.\n", dwSize, sizeof(BYTE));
            if (SetupDiGetDeviceRegistryProperty(hidDevsHandle, &device, SPDRP_HARDWAREID, &dwType, buf, dwSize, NULL) &&
                wcsstr((WCHAR*)buf, (WCHAR*)&G29_sVPID) && wcsstr((WCHAR*)buf, (WCHAR*)&G29_sMI)) {
                
                wprintf(L"Found: %s\n", (WCHAR*)buf);

                SetupDiGetDeviceRegistryProperty(hidDevsHandle, &device, SPDRP_DEVICEDESC, &dwType, NULL, 0, &dwSize);
                if (dwSize <= 0 || dwSize > 16384) {
                    printf("Error: Unable to fetch device description from: %ws\n", (WCHAR*)buf);
                    exit(1);
                }

                free(buf);
                buf = (PBYTE)malloc(dwSize * sizeof(BYTE));

                if (!SetupDiGetDeviceRegistryProperty(hidDevsHandle, &device, SPDRP_DEVICEDESC, &dwType, buf, dwSize, NULL)) {
                    printf("Error: Unable to fetch device description from: %ws\n", (WCHAR*)buf);
                    exit(1);
                }

                wprintf(L"Device: %ws\n", (WCHAR*)buf);

                SetupDiEnumDeviceInterfaces(hidDevsHandle, NULL, &hidIdx, memberIdx, &devData);
                SetupDiGetDeviceInterfaceDetail(hidDevsHandle, &devData, NULL, 0, &dwSize, NULL);
                if (dwSize < 1 || dwSize > 16384) {
                    printf("Error: Unable to get device details.\n");
                    exit(1);
                }

                devDetails = (PSP_INTERFACE_DEVICE_DETAIL_DATA)malloc(dwSize);
                devDetails->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

                if (!SetupDiGetDeviceInterfaceDetail(hidDevsHandle, &devData, devDetails, dwSize, &dwSize, NULL)) {
                    printf("Error: Unable to get device details.\n");
                    exit(1);
                }

                //wprintf(L"Device path: %s\n", devDetails->DevicePath);

                dwSize = (lstrlen(devDetails->DevicePath) + 1) * sizeof(WCHAR);
                HIDPath = (WCHAR *)malloc(dwSize);
                memcpy_s(HIDPath, dwSize, devDetails->DevicePath, dwSize);
                //wprintf(L"Device path (copy): %s\n", DevHIDPath);
                free(devDetails);
                //wprintf(L"Device path (copy safe): %s\n", DevHIDPath);
                break;
            }
            free(buf);
        }
        memberIdx++;
    }

    wprintf(L"HID path: %s\n", HIDPath);

    loadHID();
    
    // Initialize the joystick in G29 native mode.
    //sendHIDPayload(0xf8, 0x0a);
    //sendHIDPayload(0xf8, 0x09, 0x05, 0x01, 0x01);

    char cmd = ' ';
    bool handled = false;
    unsigned short i = 0;
    byte fillStateCount = sizeof(fillStates) / sizeof(byte);
    byte rpmStateCount = sizeof(rpmStates) / sizeof(byte);
    byte loneStateCount = sizeof(loneStates) / sizeof(byte);
    byte miss1StateCount = sizeof(loneStates) / sizeof(byte);

    while (true) {
        printf("Type a command to change the LEDs in the G29 wheel.\n"
            "[kl]) prev, next light state\n"
            "[,.]) prev, next fill state\n"
            "[io]) prev, next rpm state\n"
            "e) truck electricity init animation (fuel gauge)\n"
            "r) truck shutdown animation (fuel gauge)\n"
            "f) star wars laser shot effect\n"
            "g) full animation effect\n"
            "v) toggle HID commands verbosity\n"
            "q) quit\n Choose an option ::> "
        );

        while (true) {
            handled = false;
            cmd = _getche();
            printf("\b \b");

            if (cmd == 'q') {
                printf("bail out\n");
                if (ledState != 0x00) {
                    ledState = 0x00;
                }
                handled = true;
            } else if (cmd == 'k') {
                if (ledState > 0x00) {
                    ledState--;
                }
                handled = true;
            } else if (cmd == 'l') {
                if (ledState < 0x1f) {
                    ledState++;
                }
                handled = true;
            } else if (cmd == ',') {
                for (i = 1; i < fillStateCount; i++) {
                    if (ledState == fillStates[i]) {
                        ledState = fillStates[i - 1];
                        handled = true;
                        break;
                    }
                }
                if (!handled) {
                    if (ledState != fillStates[0])
                        ledState = fillStates[0];
                    handled = true;
                }
            } else if (cmd == '.') {
                for (i = 0; i < fillStateCount - 1; i++) {
                    if (ledState == fillStates[i]) {
                        ledState = fillStates[i + 1];
                        handled = true;
                        break;
                    }
                }
                if (!handled) {
                    if (ledState != fillStates[fillStateCount - 1])
                        ledState = fillStates[0];
                    handled = true;
                }
            } else if (cmd == 'i') {
                for (i = 1; i < rpmStateCount; i++) {
                    if (ledState == rpmStates[i]) {
                        ledState = rpmStates[i - 1];
                        handled = true;
                        break;
                    }
                }
                if (!handled) {
                    if (ledState != rpmStates[0])
                        ledState = rpmStates[0];
                    handled = true;
                }
            } else if (cmd == 'o') {
                for (i = 0; i < rpmStateCount - 1; i++) {
                    if (ledState == rpmStates[i]) {
                        ledState = rpmStates[i + 1];
                        handled = true;
                        break;
                    }
                }
                if (!handled) {
                    if (ledState != rpmStates[rpmStateCount - 1])
                        ledState = rpmStates[0];
                    handled = true;
                }
            } else if (cmd == 'f') {
                printf("laser fire animation...");
                for (unsigned short j = 0; j < 4; j++) {
                    for (i = 0; i < loneStateCount; i++) {
                        ledState = loneStates[i];
                        ledSync();
                        Sleep(50 + (50 * i));
                    }
                    Sleep(200);
                    ledState = 0x00;
                    ledSync();
                    Sleep(100);
                }
                Sleep(500);
                printf(" done.\n");
                handled = true;
            } else if (cmd == 'g') {
                printf("tank fill animation...");
                ledState = 0x00;
                Sleep(150);
                for (i = 0; i < fillStateCount; i++) {
                    ledState = fillStates[i];
                    ledSync();
                    Sleep(150);
                }
                for (unsigned short j = 0; j < 10; j++) {
                    for (i = 0; i < miss1StateCount; i++) {
                        ledState = miss1States[i];
                        ledSync();
                        Sleep(50);
                    }
                }
                ledState = 0x1f;
                ledSync();
                ledState = 0x00;
                Sleep(500);
                printf(" done.\n");
                handled = true;
            } else if (cmd == 'v') {
                Verbose = !Verbose;
                printf("Verbose output is now %s.\n", Verbose ? "enabled" : "disabled");
                handled = true;
            } else if (cmd == 'e') {
                printf("start truck electricity...");
                InitFuelGaugeAnimation();
                printf(" startup complete.");
                break;
            } else if (cmd == 'r') {
                printf("shutdown truck electricity...");
                ShutdownFuelGaugeAnimation();
                printf(" shutdown complete.");
                break;
            }

            if (handled) {
                ledSync();
                break;
            }
        } 
        
        if (cmd == 'q') break;
    }
}

static unsigned __int64 rdtsc() {
    return __rdtsc();
}

void detailedError(const WCHAR* msg) {
    LPVOID lpMsgBuf;
    DWORD leid = GetLastError();
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        leid,
        0,
        (LPWSTR)&lpMsgBuf,
        0, NULL);

    wprintf(L"Error: %s: %s (0x%x)", msg, (LPWSTR)lpMsgBuf, leid);
    LocalFree(lpMsgBuf);
}

void loadHID() {
    HANDLE hidHandle = CreateFile(HIDPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (hidHandle == INVALID_HANDLE_VALUE) {
        detailedError(L"Cannot open joystick for reading its HID parameters");
        exit(1);
    }

    /*
    HIDD_ATTRIBUTES hAttr;
    hAttr.Size = sizeof(HIDD_ATTRIBUTES);
    if (!HidD_GetAttributes(hidHandle, &hAttr)) {
        detailedError(L"Unable to fetch joystick HID attributes");
        exit(1);
    }
    */

    PHIDP_PREPARSED_DATA data;
    if (!HidD_GetPreparsedData(hidHandle, &data)) {
        detailedError(L"Unable to fetch joystick's HID pre-parsed data");
        exit(1);
    }

    HIDP_CAPS hCaps;
    if (HidP_GetCaps(data, &hCaps) != HIDP_STATUS_SUCCESS) {
        detailedError(L"Unable to fetch joystick's HID capabilities");
        exit(1);
    }
    HidD_FreePreparsedData(data);

    HIDPayloadLen = hCaps.OutputReportByteLength;
    printf("Joystick HID packet size: %u bytes.\n", HIDPayloadLen);
}

HRESULT sendHIDPayload(byte cmd, byte arg1, byte arg2, byte arg3, byte arg4, byte arg5, byte arg6) {
    if (HIDPayloadLen == 0) {
        printf("Tried to send HID command before initialization.\n");
        exit(1);
    } else if (HIDPayloadLen < 8) {
        printf("HID device report packet size smaller than packets we need to send (%i/%i).\n", HIDPayloadLen, 8);
        exit(1);
    }
    byte *payload = new byte[HIDPayloadLen + 1];

    ZeroMemory(payload, HIDPayloadLen + 1);

    unsigned short idx = 1;
    payload[idx++] = cmd;
    payload[idx++] = arg1;
    payload[idx++] = arg2;
    payload[idx++] = arg3;
    payload[idx++] = arg4;
    payload[idx++] = arg5;
    payload[idx++] = arg6;

    HANDLE hidHandle = CreateFile(HIDPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (hidHandle == INVALID_HANDLE_VALUE) {
        detailedError(L"Cannot open the joystick for sending HID data");
        exit(1);
    }
    
    DWORD wrCnt;
    if (!WriteFile(hidHandle, payload, HIDPayloadLen, &wrCnt, NULL)) {
        printf("Tried to write: 0x00,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x.\n",
            cmd, arg1, arg2, arg3, arg4, arg5, arg6);
        detailedError(L"Cannot write data to joystick");
        exit(1);
    }

    //DWORD rdCnt = 0;
    //byte rdData[8192];
    /*if (!ReadFile(hidHandle, rdData, 8192, &rdCnt, NULL)) {
        detailedError(L"Cannot open the joystick for receiving HID data");
    }*/

    //printf("Read HID data: %i bytes.\n", rdCnt);


    CloseHandle(hidHandle);
    return S_OK;
}

void ledSync() {
    if (Verbose) printf("Syncing LEDs with value: 0x%02x\n", ledState);
    sendHIDPayload(0xf8, 0x12, ledState, 0x00, 0x00, 0x00, 0x01);
}

static HRESULT updateLEDs(unsigned char new_state) {
    if (new_state != ledState) {
        ledState = new_state;
        return sendHIDPayload(0xf8, 0x12, ledState, 0x00, 0x00, 0x00, 0x01);
    } else return S_OK;
}

#define UpdateChk(x) update_state = updateLEDs(x); if (update_state != S_OK) return update_state;

static HRESULT InitFuelGaugeAnimation() {
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
    size_t anim_len = sizeof(animation) / sizeof(unsigned char);
    unsigned char target_led_state = ledState;

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

static HRESULT ShutdownFuelGaugeAnimation() {
    size_t i;
    HRESULT update_state;
    unsigned char current_led_state = ledState;

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