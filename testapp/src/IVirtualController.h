#pragma once
#include <vector>
#include <cstdint>

struct VirtualControllerReport {
    uint8_t left_stick_x;
    uint8_t left_stick_y;
    uint8_t right_stick_x;
    uint8_t right_stick_y;
    uint16_t buttons; // DS4 button bitmask
    uint8_t dpad; // DS4 dpad value
    uint8_t left_trigger;
    uint8_t right_trigger;
};

class IVirtualController {
public:
    virtual ~IVirtualController() = default;
    virtual bool Initialize() = 0;
    virtual bool UpdateReport(const VirtualControllerReport& report) = 0;
};
