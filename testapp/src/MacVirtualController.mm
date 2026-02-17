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
  printf("Initializing MacVirtualController (Dynamic Gamepad Emulation)...\n");

  Class GCVClass = NSClassFromString(@"GCVirtualController");
  Class GCVConfigClass = NSClassFromString(@"GCVirtualControllerConfiguration");

  if (!GCVClass || !GCVConfigClass) {
    printf("DEBUG: GCVirtualController classes not found in runtime.\n");
    impl_->mouseMode = true;
    return true;
  }

  __block bool success = false;
  __block id vc = nil;
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);

  dispatch_async(dispatch_get_main_queue(), ^{
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
      vc = ((id(*)(id, SEL, id))objc_msgSend)(GCVClass, factorySel, config);

      if (vc) {
        SEL connectSel = sel_registerName("connectWithReplyHandler:");
        void (^replyHandler)(NSError *) = ^(NSError *error) {
          if (error) {
            printf("GCVirtualController connection error: %s\n",
                   [[error localizedDescription] UTF8String]);
            success = false;
          } else {
            printf("GCVirtualController connected successfully! macOS should "
                   "now see a new gamepad.\n");
            success = true;
          }
          dispatch_semaphore_signal(sem);
        };
        ((void (*)(id, SEL, id))objc_msgSend)(vc, connectSel, replyHandler);
      } else {
        printf("Failed to instantiate GCVirtualController.\n");
        dispatch_semaphore_signal(sem);
      }
    } @catch (NSException *e) {
      printf("Exception during GCVirtualController init: %s\n",
             [[e reason] UTF8String]);
      dispatch_semaphore_signal(sem);
    }
  });

  dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC);
  if (dispatch_semaphore_wait(sem, timeout) != 0) {
    printf("GCVirtualController initialization timed out.\n");
    impl_->mouseMode = true;
  } else if (success && vc) {
    impl_->virtualController = vc;
    impl_->gcInitialized = true;
    impl_->mouseMode = false;
  } else {
    impl_->mouseMode = true;
    printf(
        "GCVirtualController failed. Falling back to mouse/keyboard mode.\n");
  }

  return true;
}

static void SetElementValue(id element, float value) {
  if (!element)
    return;
  @try {
    // Try KVC first which is usually enough for GCVirtualController elements
    [element setValue:@(value) forKey:@"value"];
  } @catch (NSException *e) {
    // Fallback to internal _setValue: if KVC fails
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
      SetElementValue(gamepad.buttonX, (report.buttons & 0x0001) ? 1.0f : 0.0f);
      SetElementValue(gamepad.buttonA, (report.buttons & 0x0002) ? 1.0f : 0.0f);
      SetElementValue(gamepad.buttonB, (report.buttons & 0x0004) ? 1.0f : 0.0f);
      SetElementValue(gamepad.buttonY, (report.buttons & 0x0008) ? 1.0f : 0.0f);
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
      hBtn(1, kCGMouseButtonLeft, kCGEventLeftMouseDown, kCGEventLeftMouseUp);
      hBtn(2, kCGMouseButtonRight, kCGEventRightMouseDown,
           kCGEventRightMouseUp);
      return true;
    }
  }
}
