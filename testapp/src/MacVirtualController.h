#pragma once
#include "IVirtualController.h"
#include <memory>

class MacVirtualController : public IVirtualController {
public:
  MacVirtualController();
  ~MacVirtualController() override;

  bool Initialize() override;
  bool UpdateReport(const VirtualControllerReport &report) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
