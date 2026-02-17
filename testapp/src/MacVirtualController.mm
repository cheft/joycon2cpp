#import "MacVirtualController.h"
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#import <cstdio>
#import <dispatch/dispatch.h>
#import <objc/message.h>

struct MacVirtualController::Impl {
  id virtualController;
  bool gcInitialized;
  CGPoint lastMousePos;
  bool buttons[16];
  bool mouseMode;
};

MacVirtualController::MacVirtualController() : impl_(std::make_unique<Impl>()) {
  impl_->virtualController = nil;
  impl_->gcInitialized = false;
  impl_->mouseMode = false;
  CGEventRef event = CGEventCreate(NULL);
  impl_->lastMousePos = CGEventGetLocation(event);
  CFRelease(event);
  for (int i = 0; i < 16; i++)
    impl_->buttons[i] = false;
}

MacVirtualController::~MacVirtualController() {
  if (impl_->virtualController) {
    SEL sel = sel_registerName("disconnect");
    if ([impl_->virtualController respondsToSelector:sel]) {
      ((void (*)(id, SEL))objc_msgSend)(impl_->virtualController, sel);
    }
    impl_->virtualController = nil;
  }
}

bool MacVirtualController::Initialize() {
  printf("[MacVirtualController] Starting initialization...\n");

  Class GCVClass = NSClassFromString(@"GCVirtualController");
  Class GCVConfigClass = NSClassFromString(@"GCVirtualControllerConfiguration");

  if (!GCVClass || !GCVConfigClass) {
    printf("[MacVirtualController] ERROR: GCVirtualController classes not "
           "found in runtime. Fallback to mouse mode.\n");
    impl_->mouseMode = true;
    return true;
  }

  __block bool finished = false;
  __block bool localSuccess = false;
  __block id localVC = nil;

  // We need to run this on the main thread, but if we are ALREADY on the main
  // thread, we must avoid dispatch_async + semaphore deadlock.

  void (^initBlock)(void) = ^{
    @try {
      id config = [[GCVConfigClass alloc] init];
      NSMutableSet *elements = [NSMutableSet set];
      [elements addObject:GCInputLeftThumbstick];
      [elements addObject:GCInputRightThumbstick];
      [elements addObject:GCInputLeftTrigger];
      [elements addObject:GCInputRightTrigger];
      [elements addObject:GCInputLeftShoulder];
      [elements addObject:GCInputRightShoulder];
      [elements addObject:GCInputButtonA];
      [elements addObject:GCInputButtonB];
      [elements addObject:GCInputButtonX];
      [elements addObject:GCInputButtonY];
      [elements addObject:GCInputLeftThumbstickButton];
      [elements addObject:GCInputRightThumbstickButton];
      [elements addObject:GCInputButtonMenu];
      [elements addObject:GCInputButtonOptions];
      [elements addObject:GCInputDirectionPad];

      [config setValue:elements forKey:@"elements"];

      SEL factorySel = sel_registerName("virtualControllerWithConfiguration:");
      localVC =
          ((id(*)(id, SEL, id))objc_msgSend)(GCVClass, factorySel, config);

      if (localVC) {
        SEL connectSel = sel_registerName("connectWithReplyHandler:");
        void (^replyHandler)(NSError *) = ^(NSError *error) {
          if (error) {
            printf("[MacVirtualController] Connection error: %s\n",
                   [[error localizedDescription] UTF8String]);
            localSuccess = false;
          } else {
            printf("[MacVirtualController] Connected successfully! Gamepad is "
                   "now visible to macOS.\n");
            localSuccess = true;
          }
          finished = true;
        };
        ((void (*)(id, SEL, id))objc_msgSend)(localVC, connectSel,
                                              replyHandler);
      } else {
        printf("[MacVirtualController] ERROR: Failed to instantiate "
               "GCVirtualController.\n");
        finished = true;
      }
    } @catch (NSException *e) {
      printf("[MacVirtualController] Exception during init: %s\n",
             [[e reason] UTF8String]);
      finished = true;
    }
  };

  if ([NSThread isMainThread]) {
    initBlock();
    // Run the loop while waiting for the connection callback
    NSDate *timeoutDate = [NSDate dateWithTimeIntervalSinceNow:5.0];
    while (!finished && [timeoutDate timeIntervalSinceNow] > 0) {
      [[NSRunLoop currentRunLoop]
             runMode:NSDefaultRunLoopMode
          beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }
  } else {
    dispatch_async(dispatch_get_main_queue(), initBlock);
    // Poll the finished flag (since we are on a background thread, this is
    // okay)
    int timeout = 50; // 5 seconds
    while (!finished && timeout-- > 0) {
      [NSThread sleepForTimeInterval:0.1];
    }
  }

  if (localSuccess && localVC) {
    impl_->virtualController = localVC;
    impl_->gcInitialized = true;
    impl_->mouseMode = false;
    printf("[MacVirtualController] Initialization COMPLETE.\n");
  } else {
    impl_->mouseMode = true;
    if (!finished) {
      printf("[MacVirtualController] ERROR: Initialization timed out.\n");
    }
    printf("[MacVirtualController] Falling back to mouse/keyboard mode.\n");
  }

  return true;
}

static void SetElementValue(id element, float value) {
  if (!element)
    return;
  @try {
    // Try KVC which is the standard way to update virtual controller local
    // state
    [element setValue:@(value) forKey:@"value"];
  } @catch (NSException *e) {
    // Fallback to internal _setValue:
    SEL sel = sel_registerName("_setValue:");
    if ([element respondsToSelector:sel]) {
      ((void (*)(id, SEL, float))objc_msgSend)(element, sel, value);
    }
  }
}

bool MacVirtualController::UpdateReport(const VirtualControllerReport &report) {
  @autoreleasepool {
    if (impl_->gcInitialized && impl_->virtualController && !impl_->mouseMode) {
      SEL controllerSel = sel_registerName("controller");
      id controller = ((id(*)(id, SEL))objc_msgSend)(impl_->virtualController,
                                                     controllerSel);
      if (!controller)
        return false;

      GCExtendedGamepad *gamepad = [controller extendedGamepad];
      if (!gamepad)
        return false;

      auto scale = [](uint8_t v) { return (v - 128.0f) / 128.0f; };

      // Update Sticks
      SetElementValue(gamepad.leftThumbstick.xAxis, scale(report.left_stick_x));
      SetElementValue(gamepad.leftThumbstick.yAxis,
                      -scale(report.left_stick_y));
      SetElementValue(gamepad.rightThumbstick.xAxis,
                      scale(report.right_stick_x));
      SetElementValue(gamepad.rightThumbstick.yAxis,
                      -scale(report.right_stick_y));

      // Update Triggers
      SetElementValue(gamepad.leftTrigger, report.left_trigger / 255.0f);
      SetElementValue(gamepad.rightTrigger, report.right_trigger / 255.0f);

      // Update Buttons
      // Bitmask: report.buttons was shifted >> 4 in testapp.cpp
      // Original DS4 Bits: 4=Sq, 5=X, 6=O, 7=Tri, 8=L1, 9=R1, 10=L2, 11=R2,
      // 12=Share, 13=Opt, 14=L3, 15=R3 After >> 4: 0=Sq, 1=X, 2=O, 3=Tri, 4=L1,
      // 5=R1, 6=L2, 7=R2, 8=Share, 9=Opt, 10=L3, 11=R3
      SetElementValue(gamepad.buttonX,
                      (report.buttons & 0x0001) ? 1.0f : 0.0f); // Square
      SetElementValue(gamepad.buttonA,
                      (report.buttons & 0x0002) ? 1.0f : 0.0f); // Cross
      SetElementValue(gamepad.buttonB,
                      (report.buttons & 0x0004) ? 1.0f : 0.0f); // Circle
      SetElementValue(gamepad.buttonY,
                      (report.buttons & 0x0008) ? 1.0f : 0.0f); // Triangle
      SetElementValue(gamepad.leftShoulder,
                      (report.buttons & 0x0010) ? 1.0f : 0.0f);
      SetElementValue(gamepad.rightShoulder,
                      (report.buttons & 0x0020) ? 1.0f : 0.0f);
      SetElementValue(gamepad.leftThumbstickButton,
                      (report.buttons & 0x0400) ? 1.0f : 0.0f);
      SetElementValue(gamepad.rightThumbstickButton,
                      (report.buttons & 0x0800) ? 1.0f : 0.0f);
      SetElementValue(gamepad.buttonMenu,
                      (report.buttons & 0x0200) ? 1.0f : 0.0f);
      SetElementValue(gamepad.buttonOptions,
                      (report.buttons & 0x0100) ? 1.0f : 0.0f);

      // Update D-Pad
      float dx = 0, dy = 0;
      switch (report.dpad) {
      case 0:
        dy = 1;
        break;
      case 1:
        dx = 1;
        dy = 1;
        break;
      case 2:
        dx = 1;
        break;
      case 3:
        dx = 1;
        dy = -1;
        break;
      case 4:
        dy = -1;
        break;
      case 5:
        dx = -1;
        dy = -1;
        break;
      case 6:
        dx = -1;
        break;
      case 7:
        dx = -1;
        dy = 1;
        break;
      }
      SetElementValue(gamepad.dpad.xAxis, dx);
      SetElementValue(gamepad.dpad.yAxis, dy);

      return true;
    } else {
      // Fallback: Mouse/Keyboard emulation via CGEvent
      static bool fallbackLogged = false;
      if (!fallbackLogged) {
        printf("[MacVirtualController] WARNING: Using fallback mouse emulation "
               "(Gamepad disabled).\n");
        fallbackLogged = true;
      }
      float dx = (report.right_stick_x - 128) / 10.0f;
      float dy = (report.right_stick_y - 128) / 10.0f;
      if (std::abs(dx) > 1.0f || std::abs(dy) > 1.0f) {
        CGEventRef ev = CGEventCreate(NULL);
        CGPoint p = CGEventGetLocation(ev);
        CFRelease(ev);
        p.x += dx;
        p.y += dy;
        CGEventRef move = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, p,
                                                  kCGMouseButtonLeft);
        if (move) {
          CGEventPost(kCGHIDEventTap, move);
          CFRelease(move);
        }
      }
      auto hBtn = [&](int bit, CGMouseButton button, CGEventType d,
                      CGEventType u) {
        bool isDown = (report.buttons & (1 << bit)) != 0;
        if (isDown != impl_->buttons[bit]) {
          CGEventRef ev = CGEventCreate(NULL);
          CGPoint p = CGEventGetLocation(ev);
          CFRelease(ev);
          CGEventRef click =
              CGEventCreateMouseEvent(NULL, isDown ? d : u, p, button);
          if (click) {
            CGEventPost(kCGHIDEventTap, click);
            CFRelease(click);
          }
          impl_->buttons[bit] = isDown;
        }
      };
      // Cross (bit 1 after shift) -> Left Click
      hBtn(1, kCGMouseButtonLeft, kCGEventLeftMouseDown, kCGEventLeftMouseUp);
      // Circle (bit 2 after shift) -> Right Click
      hBtn(2, kCGMouseButtonRight, kCGEventRightMouseDown,
           kCGEventRightMouseUp);
      return true;
    }
  }
}
