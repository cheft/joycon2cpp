#import "MacVirtualController.h"
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <iostream>

struct MacVirtualController::Impl {
  CGPoint lastMousePos;
  bool buttons[16];
};

MacVirtualController::MacVirtualController() : impl_(std::make_unique<Impl>()) {
  // Get initial cursor position
  CGEventRef event = CGEventCreate(NULL);
  impl_->lastMousePos = CGEventGetLocation(event);
  CFRelease(event);
  for (int i = 0; i < 16; i++)
    impl_->buttons[i] = false;
}

MacVirtualController::~MacVirtualController() {}

bool MacVirtualController::Initialize() {
  printf("Initializing MacVirtualController (CGEvent Mouse/Keyboard "
         "Emulation)...\n");
  return true; // CGEvent is always available
}

bool MacVirtualController::UpdateReport(const VirtualControllerReport &report) {
  @autoreleasepool {
    // Simple Mouse Movement (using Right Stick)
    float dx = (report.right_stick_x - 128) / 10.0f;
    float dy = (report.right_stick_y - 128) / 10.0f;

    if (std::abs(dx) > 1.0f || std::abs(dy) > 1.0f) {
      CGEventRef event = CGEventCreate(NULL);
      CGPoint pos = CGEventGetLocation(event);
      CFRelease(event);

      pos.x += dx;
      pos.y += dy;

      CGEventRef moveEvent = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved,
                                                     pos, kCGMouseButtonLeft);
      if (moveEvent) {
        CGEventPost(kCGHIDEventTap, moveEvent);
        CFRelease(moveEvent);
      }
    }

    // Button to Mouse Click Mapping (Example: Cross/A -> Left Click)
    // report.buttons mapping depends on GenerateDS4Report or
    // GenerateProControllerReport Usually: 0x0001=Square, 0x0002=Cross,
    // 0x0004=Circle, 0x0008=Triangle

    auto handleButton = [&](int bit, CGMouseButton button, CGEventType downType,
                            CGEventType upType) {
      bool isDown = (report.buttons & (1 << bit)) != 0;
      if (isDown != impl_->buttons[bit]) {
        CGEventRef event = CGEventCreate(NULL);
        CGPoint pos = CGEventGetLocation(event);
        CFRelease(event);

        CGEventRef clickEvent = CGEventCreateMouseEvent(
            NULL, isDown ? downType : upType, pos, button);
        if (clickEvent) {
          CGEventPost(kCGHIDEventTap, clickEvent);
          CFRelease(clickEvent);
        }
        impl_->buttons[bit] = isDown;
      }
    };

    handleButton(1, kCGMouseButtonLeft, kCGEventLeftMouseDown,
                 kCGEventLeftMouseUp); // Cross
    handleButton(2, kCGMouseButtonRight, kCGEventRightMouseDown,
                 kCGEventRightMouseUp); // Circle

    return true;
  }
}
