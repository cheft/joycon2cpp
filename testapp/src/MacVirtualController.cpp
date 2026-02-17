#include "MacVirtualController.h"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDDeviceKeys.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hidsystem/IOHIDUserDevice.h>
#include <iostream>
#include <mach/mach_time.h>
#include <vector>

MacVirtualController::MacVirtualController() : device_(nullptr) {}

MacVirtualController::~MacVirtualController() {
  if (device_) {
    IOHIDUserDeviceCancel(device_);
    CFRelease(device_);
  }
}

bool MacVirtualController::Initialize() {
  // DS4 Report Descriptor (approximate, standard DS4)
  uint8_t reportDescriptor[] = {
      0x05, 0x01,       // Usage Page (Generic Desktop)
      0x09, 0x05,       // Usage (Game Pad)
      0xA1, 0x01,       // Collection (Application)
      0x85, 0x01,       //   Report ID (1)
      0x09, 0x30,       //   Usage (X)
      0x09, 0x31,       //   Usage (Y)
      0x09, 0x32,       //   Usage (Z)
      0x09, 0x35,       //   Usage (Rz)
      0x15, 0x00,       //   Logical Minimum (0)
      0x26, 0xFF, 0x00, //   Logical Maximum (255)
      0x75, 0x08,       //   Report Size (8)
      0x95, 0x04,       //   Report Count (4)
      0x81, 0x02,       //   Input (Data,Var,Abs)
      0x09, 0x39,       //   Usage (Hat switch)
      0x15, 0x00,       //   Logical Minimum (0)
      0x25, 0x07,       //   Logical Maximum (7)
      0x35, 0x00,       //   Physical Minimum (0)
      0x46, 0x3B, 0x01, //   Physical Maximum (315)
      0x65, 0x14,       //   Unit (Eng Rot: Angular Pos)
      0x75, 0x04,       //   Report Size (4)
      0x95, 0x01,       //   Report Count (1)
      0x81, 0x42,       //   Input (Data,Var,Abs,Null)
      0x05, 0x09,       //   Usage Page (Button)
      0x19, 0x01,       //   Usage Minimum (Button 1)
      0x29, 0x0E,       //   Usage Maximum (Button 14)
      0x15, 0x00,       //   Logical Minimum (0)
      0x25, 0x01,       //   Logical Maximum (1)
      0x75, 0x01,       //   Report Size (1)
      0x95, 0x0E,       //   Report Count (14)
      0x81, 0x02,       //   Input (Data,Var,Abs)
      0x75, 0x06,       //   Report Size (6)
      0x95, 0x01,       //   Report Count (1)
      0x81, 0x01,       //   Input (Cnst,Ary,Abs)
      0x05, 0x01,       //   Usage Page (Generic Desktop)
      0x09, 0x33,       //   Usage (Rx)
      0x09, 0x34,       //   Usage (Ry)
      0x15, 0x00,       //   Logical Minimum (0)
      0x26, 0xFF, 0x00, //   Logical Maximum (255)
      0x75, 0x08,       //   Report Size (8)
      0x95, 0x02,       //   Report Count (2)
      0x81, 0x02,       //   Input (Data,Var,Abs)
      0xC0              // End Collection
  };

  CFDataRef descriptorData = CFDataCreate(kCFAllocatorDefault, reportDescriptor,
                                          sizeof(reportDescriptor));

  CFMutableDictionaryRef properties = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(properties, CFSTR(kIOHIDReportDescriptorKey),
                       descriptorData);
  CFDictionarySetValue(properties, CFSTR(kIOHIDProductKey),
                       CFSTR("Joy-Con (DualShock 4 Emulated)"));

  // Vendor ID and Product ID for DS4
  int vid = 0x054C;
  int pid = 0x05C4;
  CFNumberRef vidNum =
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid);
  CFNumberRef pidNum =
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);
  CFDictionarySetValue(properties, CFSTR(kIOHIDVendorIDKey), vidNum);
  CFDictionarySetValue(properties, CFSTR(kIOHIDProductIDKey), pidNum);

  device_ =
      IOHIDUserDeviceCreateWithProperties(kCFAllocatorDefault, properties, 0);

  CFRelease(descriptorData);
  CFRelease(properties);
  CFRelease(vidNum);
  CFRelease(pidNum);

  if (!device_) {
    return false;
  }

  IOHIDUserDeviceActivate(device_);
  return true;
}

bool MacVirtualController::UpdateReport(const VirtualControllerReport &report) {
  if (!device_)
    return false;

  // Construct DS4 report (simplified matching descriptor above)
  struct {
    uint8_t reportID;
    uint8_t leftX;
    uint8_t leftY;
    uint8_t rightX;
    uint8_t rightY;
    uint8_t buttons1; // Hat + buttons 1-4
    uint8_t buttons2; // Buttons 5-12
    uint8_t buttons3; // Buttons 13-14
    uint8_t triggerL;
    uint8_t triggerR;
  } ds4Report;

  ds4Report.reportID = 1;
  ds4Report.leftX = report.left_stick_x;
  ds4Report.leftY = report.left_stick_y;
  ds4Report.rightX = report.right_stick_x;
  ds4Report.rightY = report.right_stick_y;

  // DPAD/Buttons mapping
  ds4Report.buttons1 =
      (report.dpad & 0x0F) | (uint8_t)((report.buttons & 0x0F) << 4);
  ds4Report.buttons2 = (uint8_t)((report.buttons >> 4) & 0xFF);
  ds4Report.buttons3 = (uint8_t)((report.buttons >> 12) & 0x03);

  ds4Report.triggerL = report.left_trigger;
  ds4Report.triggerR = report.right_trigger;

  uint64_t timestamp = mach_absolute_time();
  IOReturn res = IOHIDUserDeviceHandleReportWithTimeStamp(
      device_, timestamp, (const uint8_t *)&ds4Report, sizeof(ds4Report));
  return res == kIOReturnSuccess;
}
