#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <ViGEm/Client.h>
#include <Windows.h>

#else
// Define DS4 structures for macOS to avoid dependency on ViGEm headers
#ifndef BYTE
typedef uint8_t BYTE;
#endif
#ifndef SHORT
typedef int16_t SHORT;
#endif
#ifndef USHORT
typedef uint16_t USHORT;
#endif

#pragma pack(push, 1)
typedef struct _DS4_TOUCH {
  BYTE bPacketCounter;
  BYTE bIsUpTrackingNum1;
  BYTE bTouchData1[3];
  BYTE bIsUpTrackingNum2;
  BYTE bTouchData2[3];
} DS4_TOUCH, *PDS4_TOUCH;

typedef struct _DS4_REPORT {
  BYTE bThumbLX;
  BYTE bThumbLY;
  BYTE bThumbRX;
  BYTE bThumbRY;
  USHORT wButtons;
  BYTE bSpecial;
  BYTE bTriggerL;
  BYTE bTriggerR;
  uint16_t wTimestamp;
  BYTE bBatteryLevel;
  SHORT wGyroX;
  SHORT wGyroY;
  SHORT wGyroZ;
  SHORT wAccelX;
  SHORT wAccelY;
  SHORT wAccelZ;
  BYTE _reserved1[5];
  BYTE bTouchPacketsN;
  DS4_TOUCH sCurrentTouch;
} DS4_REPORT, *PDS4_REPORT;

typedef struct _DS4_REPORT_EX {
  DS4_REPORT Report;
} DS4_REPORT_EX, *PDS4_REPORT_EX;
#pragma pack(pop)

#define DS4_BUTTON_SQUARE (1 << 4)
#define DS4_BUTTON_CROSS (1 << 5)
#define DS4_BUTTON_CIRCLE (1 << 6)
#define DS4_BUTTON_TRIANGLE (1 << 7)
#define DS4_BUTTON_SHOULDER_LEFT (1 << 8)
#define DS4_BUTTON_SHOULDER_RIGHT (1 << 9)
#define DS4_BUTTON_TRIGGER_LEFT (1 << 10)
#define DS4_BUTTON_TRIGGER_RIGHT (1 << 11)
#define DS4_BUTTON_SHARE (1 << 12)
#define DS4_BUTTON_OPTIONS (1 << 13)
#define DS4_BUTTON_THUMB_LEFT (1 << 14)
#define DS4_BUTTON_THUMB_RIGHT (1 << 15)

#define DS4_SPECIAL_BUTTON_PS (1 << 0)
#define DS4_SPECIAL_BUTTON_TOUCHPAD (1 << 1)

typedef enum _DS4_DPAD_DIRECTIONS {
  DS4_BUTTON_DPAD_NORTH = 0,
  DS4_BUTTON_DPAD_NORTHEAST = 1,
  DS4_BUTTON_DPAD_EAST = 2,
  DS4_BUTTON_DPAD_SOUTHEAST = 3,
  DS4_BUTTON_DPAD_SOUTH = 4,
  DS4_BUTTON_DPAD_SOUTHWEST = 5,
  DS4_BUTTON_DPAD_WEST = 6,
  DS4_BUTTON_DPAD_NORTHWEST = 7,
  DS4_BUTTON_DPAD_NONE = 8
} DS4_DPAD_DIRECTIONS;

#define DS4_SET_DPAD(pReport, dpad)                                            \
  (pReport)->wButtons = ((pReport)->wButtons & ~0xF) | (dpad)
#define DS4_REPORT_INIT(pReport)                                               \
  memset(pReport, 0, sizeof(DS4_REPORT));                                      \
  (pReport)->wButtons |= DS4_BUTTON_DPAD_NONE;                                 \
  (pReport)->bThumbLX = (pReport)->bThumbLY = (pReport)->bThumbRX =            \
      (pReport)->bThumbRY = 128;
#endif

enum class JoyConSide { Left, Right };
enum class JoyConOrientation { Upright, Sideways };
enum class GyroSource { Both, Left, Right };

struct StickData {
  int16_t x;
  int16_t y;
  BYTE rx;
  BYTE ry;
};

struct MotionData {
  SHORT gyroX, gyroY, gyroZ;
  SHORT accelX, accelY, accelZ;
};

// Pass side and orientation explicitly now:
DS4_REPORT_EX GenerateDS4Report(const std::vector<uint8_t> &buffer,
                                JoyConSide side, JoyConOrientation orientation);
DS4_REPORT_EX
GenerateDualJoyConDS4Report(const std::vector<uint8_t> &leftBuffer,
                            const std::vector<uint8_t> &rightBuffer,
                            GyroSource gyroSource);
DS4_REPORT_EX GenerateProControllerReport(const std::vector<uint8_t> &buffer);
DS4_REPORT_EX GenerateNSOGCReport(const std::vector<uint8_t> &buffer);

uint32_t ExtractButtonState(const std::vector<uint8_t> &buffer);
std::pair<int16_t, int16_t>
GetRawOpticalMouse(const std::vector<uint8_t> &buffer);
StickData DecodeJoystick(const std::vector<uint8_t> &buffer, JoyConSide side,
                         JoyConOrientation orientation);
MotionData DecodeMotion(const std::vector<uint8_t> &buffer);
