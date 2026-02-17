#pragma once
#include "IVirtualController.h"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hidsystem/IOHIDUserDevice.h>

class MacVirtualController : public IVirtualController {
public:
  MacVirtualController();
  ~MacVirtualController() override;

  bool Initialize() override;
  bool UpdateReport(const VirtualControllerReport &report) override;

private:
  IOHIDUserDeviceRef device_ = nullptr;
};
