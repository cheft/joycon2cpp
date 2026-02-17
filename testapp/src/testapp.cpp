#ifdef _WIN32
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

#pragma comment(lib, "setupapi.lib")
#include <ViGEm/Client.h>
#include <ViGEm/Common.h>
#include <Windows.h>
#include <conio.h> // For _kbhit()

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;
using namespace Windows::Foundation;
#else
#include "MacBluetoothManager.h"
#include "MacVirtualController.h"
#include <CoreGraphics/CoreGraphics.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

int kbhit(void) {
  struct termios oldt, newt;
  int ch;
  int oldf;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
  ch = getchar();
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  fcntl(STDIN_FILENO, F_SETFL, oldf);
  if (ch != EOF) {
    ungetc(ch, stdin);
    return 1;
  }
  return 0;
}
#endif

#define WORD uint16_t
#define VK_F12 0x6F // F12 on macOS? Actually CGKeyCode for F12 is 111 (0x6F)

#ifndef _WIN32
enum class GattCommunicationStatus { Success = 0, ProtocolError = 1 };
typedef int GattCharacteristic; // Dummy for macOS signatures
#define MOUSEEVENTF_MOVE 0x0001
#define MOUSEEVENTF_LEFTDOWN 0x0002
#define MOUSEEVENTF_LEFTUP 0x0004
#define MOUSEEVENTF_RIGHTDOWN 0x0008
#define MOUSEEVENTF_RIGHTUP 0x0010
#define MOUSEEVENTF_MIDDLEDOWN 0x0020
#define MOUSEEVENTF_MIDDLEUP 0x0040
#define MOUSEEVENTF_WHEEL 0x0800
#define MOUSEEVENTF_XDOWN 0x0080
#define MOUSEEVENTF_XUP 0x0100
#define XBUTTON1 0x0001
#define XBUTTON2 0x0002
#endif
#ifdef _WIN32
#define TSTR(s) L##s
#define TCOUT std::wcout
#define TCERR std::wcerr
#define TSTRING std::wstring
#define TGETLINE std::getline
#define TCIN std::wcin
#else
#include <iostream>
#define TSTR(s) s
#define TCOUT std::cout
#define TCERR std::cerr
#define TSTRING std::string
#define TGETLINE std::getline
#define TCIN std::cin
#endif

#include "JoyConDecoder.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

constexpr uint16_t JOYCON_MANUFACTURER_ID = 1363; // Nintendo
const std::vector<uint8_t> JOYCON_MANUFACTURER_PREFIX = {0x01, 0x00};
const TSTRING INPUT_REPORT_UUID = TSTR("ab7de9be-89fe-49ad-828f-118f09df7fd2");
const TSTRING WRITE_COMMAND_UUID = TSTR("649d4ac9-8eb7-4e6c-af44-1ea54fe5f005");

// GL/GR Button Mapping Configuration
enum class ButtonMapping {
  NONE,
  L3,       // Left stick click
  R3,       // Right stick click
  L1,       // Left shoulder
  R1,       // Right shoulder
  L2,       // Left trigger
  R2,       // Right trigger
  CROSS,    // X / A
  CIRCLE,   // O / B
  SQUARE,   // □ / X
  TRIANGLE, // △ / Y
  SHARE,    // Share / Back
  OPTIONS,  // Options / Start
  DPAD_UP,
  DPAD_DOWN,
  DPAD_LEFT,
  DPAD_RIGHT
};

// Single GL/GR mapping layout
struct GLGRLayout {
  std::string name;
  ButtonMapping glMapping;
  ButtonMapping grMapping;
};

// Pro Controller configuration with multiple layouts
struct ProControllerConfig {
  std::vector<GLGRLayout> layouts;
  int activeLayoutIndex = 0;
};

const std::string CONFIG_FILE = "joycon2cpp_config.json";

// Global config instance
ProControllerConfig g_proControllerConfig;

#ifdef _WIN32
PVIGEM_CLIENT vigem_client = nullptr;
#else
std::shared_ptr<IVirtualController> mac_controller = nullptr;
#endif

void InitializeViGEm() {
#ifdef _WIN32
  if (vigem_client != nullptr)
    return;

  vigem_client = vigem_alloc();
  if (vigem_client == nullptr) {
    TCERR << TSTR("Failed to allocate ViGEm client.\n");
    exit(1);
  }

  auto ret = vigem_connect(vigem_client);
  if (!VIGEM_SUCCESS(ret)) {
    TCERR << TSTR("Failed to connect to ViGEm bus: 0x") << std::hex << ret
          << TSTR("\n");
    exit(1);
  }

  TCOUT << TSTR("ViGEm client initialized and connected.\n");
#else
  if (mac_controller != nullptr)
    return;
  mac_controller = std::make_shared<MacVirtualController>();
  if (!mac_controller->Initialize()) {
    TCERR << TSTR("Failed to initialize Mac Virtual Controller\n");
    exit(1);
  }
  TCOUT << TSTR("Mac Virtual Controller initialized.\n");
#endif
}

void PrintRawNotification(const std::vector<uint8_t> &buffer) {
  std::cout << "[Raw Notification] ";
  for (auto b : buffer) {
    printf("%02X ", b);
  }
  std::cout << std::endl;
}

// Helper: Convert ButtonMapping to string
std::string ButtonMappingToString(ButtonMapping mapping) {
  switch (mapping) {
  case ButtonMapping::NONE:
    return "NONE";
  case ButtonMapping::L3:
    return "L3";
  case ButtonMapping::R3:
    return "R3";
  case ButtonMapping::L1:
    return "L1";
  case ButtonMapping::R1:
    return "R1";
  case ButtonMapping::L2:
    return "L2";
  case ButtonMapping::R2:
    return "R2";
  case ButtonMapping::CROSS:
    return "CROSS";
  case ButtonMapping::CIRCLE:
    return "CIRCLE";
  case ButtonMapping::SQUARE:
    return "SQUARE";
  case ButtonMapping::TRIANGLE:
    return "TRIANGLE";
  case ButtonMapping::SHARE:
    return "SHARE";
  case ButtonMapping::OPTIONS:
    return "OPTIONS";
  case ButtonMapping::DPAD_UP:
    return "DPAD_UP";
  case ButtonMapping::DPAD_DOWN:
    return "DPAD_DOWN";
  case ButtonMapping::DPAD_LEFT:
    return "DPAD_LEFT";
  case ButtonMapping::DPAD_RIGHT:
    return "DPAD_RIGHT";
  default:
    return "NONE";
  }
}

// Helper: Convert string to ButtonMapping
ButtonMapping StringToButtonMapping(const std::string &str) {
  if (str == "L3")
    return ButtonMapping::L3;
  if (str == "R3")
    return ButtonMapping::R3;
  if (str == "L1")
    return ButtonMapping::L1;
  if (str == "R1")
    return ButtonMapping::R1;
  if (str == "L2")
    return ButtonMapping::L2;
  if (str == "R2")
    return ButtonMapping::R2;
  if (str == "CROSS")
    return ButtonMapping::CROSS;
  if (str == "CIRCLE")
    return ButtonMapping::CIRCLE;
  if (str == "SQUARE")
    return ButtonMapping::SQUARE;
  if (str == "TRIANGLE")
    return ButtonMapping::TRIANGLE;
  if (str == "SHARE")
    return ButtonMapping::SHARE;
  if (str == "OPTIONS")
    return ButtonMapping::OPTIONS;
  if (str == "DPAD_UP")
    return ButtonMapping::DPAD_UP;
  if (str == "DPAD_DOWN")
    return ButtonMapping::DPAD_DOWN;
  if (str == "DPAD_LEFT")
    return ButtonMapping::DPAD_LEFT;
  if (str == "DPAD_RIGHT")
    return ButtonMapping::DPAD_RIGHT;
  return ButtonMapping::NONE;
}

// Simple JSON serialization for our config
std::string ConfigToJSON(const ProControllerConfig &config) {
  std::stringstream ss;
  ss << "{\n";
  ss << "  \"activeLayoutIndex\": " << config.activeLayoutIndex << ",\n";
  ss << "  \"layouts\": [\n";

  for (size_t i = 0; i < config.layouts.size(); ++i) {
    const auto &layout = config.layouts[i];
    ss << "    {\n";
    ss << "      \"name\": \"" << layout.name << "\",\n";
    ss << "      \"glMapping\": \"" << ButtonMappingToString(layout.glMapping)
       << "\",\n";
    ss << "      \"grMapping\": \"" << ButtonMappingToString(layout.grMapping)
       << "\"\n";
    ss << "    }";
    if (i < config.layouts.size() - 1)
      ss << ",";
    ss << "\n";
  }

  ss << "  ]\n";
  ss << "}\n";
  return ss.str();
}

// Simple JSON parsing for our config
bool JSONToConfig(const std::string &json, ProControllerConfig &config) {
  config.layouts.clear();
  config.activeLayoutIndex = 0;

  // Simple parser for our specific JSON structure
  size_t pos = 0;

  // Find activeLayoutIndex
  size_t activePos = json.find("\"activeLayoutIndex\"");
  if (activePos != std::string::npos) {
    size_t colonPos = json.find(':', activePos);
    size_t commaPos = json.find_first_of(",\n", colonPos);
    std::string value = json.substr(colonPos + 1, commaPos - colonPos - 1);
    // Trim whitespace
    value.erase(0, value.find_first_not_of(" \t\n\r"));
    value.erase(value.find_last_not_of(" \t\n\r") + 1);
    config.activeLayoutIndex = std::stoi(value);
  }

  // Find layouts array
  size_t layoutsStart = json.find("\"layouts\"");
  if (layoutsStart == std::string::npos)
    return false;

  size_t arrayStart = json.find('[', layoutsStart);
  if (arrayStart == std::string::npos)
    return false;

  // Parse each layout object
  pos = arrayStart + 1;
  while (pos < json.length()) {
    size_t objectStart = json.find('{', pos);
    if (objectStart == std::string::npos)
      break;

    size_t objectEnd = json.find('}', objectStart);
    if (objectEnd == std::string::npos)
      break;

    std::string objectStr =
        json.substr(objectStart, objectEnd - objectStart + 1);

    GLGRLayout layout;

    // Parse name
    size_t namePos = objectStr.find("\"name\"");
    if (namePos != std::string::npos) {
      size_t nameStart = objectStr.find('\"', namePos + 6);
      size_t nameEnd = objectStr.find('\"', nameStart + 1);
      layout.name = objectStr.substr(nameStart + 1, nameEnd - nameStart - 1);
    }

    // Parse glMapping
    size_t glPos = objectStr.find("\"glMapping\"");
    if (glPos != std::string::npos) {
      size_t glStart = objectStr.find('\"', glPos + 11);
      size_t glEnd = objectStr.find('\"', glStart + 1);
      std::string glStr = objectStr.substr(glStart + 1, glEnd - glStart - 1);
      layout.glMapping = StringToButtonMapping(glStr);
    }

    // Parse grMapping
    size_t grPos = objectStr.find("\"grMapping\"");
    if (grPos != std::string::npos) {
      size_t grStart = objectStr.find('\"', grPos + 11);
      size_t grEnd = objectStr.find('\"', grStart + 1);
      std::string grStr = objectStr.substr(grStart + 1, grEnd - grStart - 1);
      layout.grMapping = StringToButtonMapping(grStr);
    }

    config.layouts.push_back(layout);

    pos = objectEnd + 1;
    // Check if there's another object
    size_t nextComma = json.find(',', pos);
    size_t arrayEnd = json.find(']', pos);
    if (arrayEnd != std::string::npos &&
        (nextComma == std::string::npos || arrayEnd < nextComma)) {
      break;
    }
  }

  return !config.layouts.empty();
}

// Load config from JSON file
bool LoadProControllerConfig(ProControllerConfig &config) {
  std::ifstream file(CONFIG_FILE);
  if (!file.is_open()) {
    return false;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  file.close();

  return JSONToConfig(buffer.str(), config);
}

// Save config to JSON file
void SaveProControllerConfig(const ProControllerConfig &config) {
  std::ofstream file(CONFIG_FILE);
  if (!file.is_open()) {
    std::cerr << "Failed to save config file.\n";
    return;
  }

  file << ConfigToJSON(config);
  file.close();
  std::cout << "Configuration saved to " << CONFIG_FILE << "\n";
}

// Prompt user for button mapping
ButtonMapping PromptForButtonMapping(const std::string &buttonName) {
  std::cout << "\nSelect mapping for " << buttonName << " button:\n";
  std::cout << "  1. L3 (Left Stick Click)\n";
  std::cout << "  2. R3 (Right Stick Click)\n";
  std::cout << "  3. L1 (Left Shoulder)\n";
  std::cout << "  4. R1 (Right Shoulder)\n";
  std::cout << "  5. L2 (Left Trigger)\n";
  std::cout << "  6. R2 (Right Trigger)\n";
  std::cout << "  7. Cross (X/A)\n";
  std::cout << "  8. Circle (O/B)\n";
  std::cout << "  9. Square (□/X)\n";
  std::cout << " 10. Triangle (△/Y)\n";
  std::cout << " 11. Share (Back)\n";
  std::cout << " 12. Options (Start)\n";
  std::cout << " 13. D-Pad Up\n";
  std::cout << " 14. D-Pad Down\n";
  std::cout << " 15. D-Pad Left\n";
  std::cout << " 16. D-Pad Right\n";
  std::cout << " 17. None (Disable)\n";
  std::cout << "Enter choice (1-17): ";

  int choice;
  std::cin >> choice;
  std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');

  switch (choice) {
  case 1:
    return ButtonMapping::L3;
  case 2:
    return ButtonMapping::R3;
  case 3:
    return ButtonMapping::L1;
  case 4:
    return ButtonMapping::R1;
  case 5:
    return ButtonMapping::L2;
  case 6:
    return ButtonMapping::R2;
  case 7:
    return ButtonMapping::CROSS;
  case 8:
    return ButtonMapping::CIRCLE;
  case 9:
    return ButtonMapping::SQUARE;
  case 10:
    return ButtonMapping::TRIANGLE;
  case 11:
    return ButtonMapping::SHARE;
  case 12:
    return ButtonMapping::OPTIONS;
  case 13:
    return ButtonMapping::DPAD_UP;
  case 14:
    return ButtonMapping::DPAD_DOWN;
  case 15:
    return ButtonMapping::DPAD_LEFT;
  case 16:
    return ButtonMapping::DPAD_RIGHT;
  case 17:
    return ButtonMapping::NONE;
  default:
    std::cout << "Invalid choice, defaulting to NONE.\n";
    return ButtonMapping::NONE;
  }
}

// Create default config with one empty layout
void CreateDefaultConfig() {
  g_proControllerConfig.layouts.clear();
  GLGRLayout defaultLayout;
  defaultLayout.name = "Layout 1";
  defaultLayout.glMapping = ButtonMapping::NONE;
  defaultLayout.grMapping = ButtonMapping::NONE;
  g_proControllerConfig.layouts.push_back(defaultLayout);
  g_proControllerConfig.activeLayoutIndex = 0;
  SaveProControllerConfig(g_proControllerConfig);
}

// Configure a single layout
void ConfigureLayout(GLGRLayout &layout) {
  std::cout << "\n=== Configure GL/GR Mapping ===\n";
  std::cout << "Layout Name: " << layout.name << "\n\n";

  layout.glMapping = PromptForButtonMapping("GL");
  layout.grMapping = PromptForButtonMapping("GR");

  std::cout << "\nLayout configured!\n";
  std::cout << "  GL -> " << ButtonMappingToString(layout.glMapping) << "\n";
  std::cout << "  GR -> " << ButtonMappingToString(layout.grMapping) << "\n";
}

// Layout management window
void ShowLayoutManagementWindow() {
  while (true) {
    std::cout << "\n==================================================\n";
    std::cout << "          GL/GR LAYOUT MANAGEMENT\n";
    std::cout << "==================================================\n";
    std::cout << "Current active layout: "
              << g_proControllerConfig
                     .layouts[g_proControllerConfig.activeLayoutIndex]
                     .name
              << "\n\n";
    std::cout << "Available Layouts:\n";

    for (size_t i = 0; i < g_proControllerConfig.layouts.size(); ++i) {
      const auto &layout = g_proControllerConfig.layouts[i];
      std::cout << "  " << (i + 1) << ". " << layout.name;
      if (i == static_cast<size_t>(g_proControllerConfig.activeLayoutIndex)) {
        std::cout << " [ACTIVE]";
      }
      std::cout << "\n";
      std::cout << "     GL: " << ButtonMappingToString(layout.glMapping)
                << " | GR: " << ButtonMappingToString(layout.grMapping) << "\n";
    }

    std::cout << "  " << (g_proControllerConfig.layouts.size() + 1)
              << ". [NEW]\n";
    std::cout << "  0. Exit Management Window\n\n";
    std::cout << "Enter number to edit layout: ";

    std::string input;
    std::getline(std::cin, input);

    if (input.empty())
      continue;

    int choice = std::stoi(input);

    if (choice == 0) {
      SaveProControllerConfig(g_proControllerConfig);
      std::cout << "Exiting layout management...\n";
      break;
    } else if (choice ==
               static_cast<int>(g_proControllerConfig.layouts.size() + 1)) {
      // Create new layout
      GLGRLayout newLayout;
      std::cout << "\nEnter name for new layout: ";
      std::getline(std::cin, newLayout.name);
      if (newLayout.name.empty()) {
        newLayout.name =
            "Layout " +
            std::to_string(g_proControllerConfig.layouts.size() + 1);
      }
      newLayout.glMapping = ButtonMapping::NONE;
      newLayout.grMapping = ButtonMapping::NONE;

      ConfigureLayout(newLayout);
      g_proControllerConfig.layouts.push_back(newLayout);
      SaveProControllerConfig(g_proControllerConfig);
      std::cout << "\nNew layout added!\n";
    } else if (choice > 0 &&
               choice <=
                   static_cast<int>(g_proControllerConfig.layouts.size())) {
      // Edit existing layout
      size_t index = choice - 1;
      std::cout << "\nEditing: " << g_proControllerConfig.layouts[index].name
                << "\n";
      std::cout << "1. Rename layout\n";
      std::cout << "2. Configure mappings\n";
      std::cout << "3. Delete layout\n";
      std::cout << "4. Set as active layout\n";
      std::cout << "0. Cancel\n";
      std::cout << "Choice: ";

      std::string editChoice;
      std::getline(std::cin, editChoice);

      if (editChoice == "1") {
        std::cout << "Enter new name: ";
        std::string newName;
        std::getline(std::cin, newName);
        if (!newName.empty()) {
          g_proControllerConfig.layouts[index].name = newName;
          SaveProControllerConfig(g_proControllerConfig);
        }
      } else if (editChoice == "2") {
        ConfigureLayout(g_proControllerConfig.layouts[index]);
        SaveProControllerConfig(g_proControllerConfig);
      } else if (editChoice == "3") {
        if (g_proControllerConfig.layouts.size() > 1) {
          std::cout << "Are you sure you want to delete this layout? (y/n): ";
          std::string confirm;
          std::getline(std::cin, confirm);
          if (confirm == "y" || confirm == "Y") {
            g_proControllerConfig.layouts.erase(
                g_proControllerConfig.layouts.begin() + index);
            if (g_proControllerConfig.activeLayoutIndex >=
                static_cast<int>(g_proControllerConfig.layouts.size())) {
              g_proControllerConfig.activeLayoutIndex =
                  static_cast<int>(g_proControllerConfig.layouts.size()) - 1;
            }
            SaveProControllerConfig(g_proControllerConfig);
            std::cout << "Layout deleted!\n";
          }
        } else {
          std::cout << "Cannot delete the last layout!\n";
        }
      } else if (editChoice == "4") {
        g_proControllerConfig.activeLayoutIndex = static_cast<int>(index);
        SaveProControllerConfig(g_proControllerConfig);
        std::cout << "Active layout changed to: "
                  << g_proControllerConfig.layouts[index].name << "\n";
      }
    }
  }
}

// Apply ButtonMapping to DS4 report
void ApplyButtonMapping(DS4_REPORT_EX &report, ButtonMapping mapping) {
  switch (mapping) {
  case ButtonMapping::L3:
    report.Report.wButtons |= DS4_BUTTON_THUMB_LEFT;
    break;
  case ButtonMapping::R3:
    report.Report.wButtons |= DS4_BUTTON_THUMB_RIGHT;
    break;
  case ButtonMapping::L1:
    report.Report.wButtons |= DS4_BUTTON_SHOULDER_LEFT;
    break;
  case ButtonMapping::R1:
    report.Report.wButtons |= DS4_BUTTON_SHOULDER_RIGHT;
    break;
  case ButtonMapping::L2:
    report.Report.bTriggerL = 255;
    break;
  case ButtonMapping::R2:
    report.Report.bTriggerR = 255;
    break;
  case ButtonMapping::CROSS:
    report.Report.wButtons |= DS4_BUTTON_CROSS;
    break;
  case ButtonMapping::CIRCLE:
    report.Report.wButtons |= DS4_BUTTON_CIRCLE;
    break;
  case ButtonMapping::SQUARE:
    report.Report.wButtons |= DS4_BUTTON_SQUARE;
    break;
  case ButtonMapping::TRIANGLE:
    report.Report.wButtons |= DS4_BUTTON_TRIANGLE;
    break;
  case ButtonMapping::SHARE:
    report.Report.wButtons |= DS4_BUTTON_SHARE;
    break;
  case ButtonMapping::OPTIONS:
    report.Report.wButtons |= DS4_BUTTON_OPTIONS;
    break;
  case ButtonMapping::DPAD_UP:
    DS4_SET_DPAD(reinterpret_cast<PDS4_REPORT>(&report.Report),
                 DS4_BUTTON_DPAD_NORTH);
    break;
  case ButtonMapping::DPAD_DOWN:
    DS4_SET_DPAD(reinterpret_cast<PDS4_REPORT>(&report.Report),
                 DS4_BUTTON_DPAD_SOUTH);
    break;
  case ButtonMapping::DPAD_LEFT:
    DS4_SET_DPAD(reinterpret_cast<PDS4_REPORT>(&report.Report),
                 DS4_BUTTON_DPAD_WEST);
    break;
  case ButtonMapping::DPAD_RIGHT:
    DS4_SET_DPAD(reinterpret_cast<PDS4_REPORT>(&report.Report),
                 DS4_BUTTON_DPAD_EAST);
    break;
  case ButtonMapping::NONE:
  default:
    // Do nothing
    break;
  }
}

// Send keyboard key press/release using Windows SendInput API or macOS CGEvent
void SendKeyboardInput(WORD virtualKey, bool keyDown) {
#ifdef _WIN32
  INPUT input = {};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = virtualKey;
  input.ki.dwFlags = keyDown ? 0 : KEYEVENTF_KEYUP;
  input.ki.time = 0;
  input.ki.dwExtraInfo = 0;

  SendInput(1, &input, sizeof(INPUT));
#else
  // macOS CoreGraphics keyboard emulation
  CGEventRef event =
      CGEventCreateKeyboardEvent(NULL, (CGKeyCode)virtualKey, keyDown);
  if (event) {
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
  }
#endif
}

// Send mouse input using Windows SendInput API or macOS CGEvent
void SendMouseInput(int dx, int dy, uint32_t flags, int mouseData = 0) {
#ifdef _WIN32
  INPUT input = {};
  input.type = INPUT_MOUSE;
  input.mi.dx = dx;
  input.mi.dy = dy;
  input.mi.dwFlags = flags;
  input.mi.mouseData = mouseData;
  SendInput(1, &input, sizeof(INPUT));
#else
  if (flags & MOUSEEVENTF_MOVE) {
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

  auto postClick = [&](CGEventType type, CGMouseButton button) {
    CGEventRef event = CGEventCreate(NULL);
    CGPoint pos = CGEventGetLocation(event);
    CFRelease(event);
    CGEventRef clickEvent = CGEventCreateMouseEvent(NULL, type, pos, button);
    if (clickEvent) {
      CGEventPost(kCGHIDEventTap, clickEvent);
      CFRelease(clickEvent);
    }
  };

  if (flags & MOUSEEVENTF_LEFTDOWN)
    postClick(kCGEventLeftMouseDown, kCGMouseButtonLeft);
  if (flags & MOUSEEVENTF_LEFTUP)
    postClick(kCGEventLeftMouseUp, kCGMouseButtonLeft);
  if (flags & MOUSEEVENTF_RIGHTDOWN)
    postClick(kCGEventRightMouseDown, kCGMouseButtonRight);
  if (flags & MOUSEEVENTF_RIGHTUP)
    postClick(kCGEventRightMouseUp, kCGMouseButtonRight);
  if (flags & MOUSEEVENTF_MIDDLEDOWN)
    postClick(kCGEventOtherMouseDown, kCGMouseButtonCenter);
  if (flags & MOUSEEVENTF_MIDDLEUP)
    postClick(kCGEventOtherMouseUp, kCGMouseButtonCenter);

  if (flags & MOUSEEVENTF_WHEEL) {
    // mouseData is in multiples of 120. 120 = 1 line.
    int lines = mouseData / 120;
    CGEventRef scrollEvent =
        CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitLine, 1, lines);
    if (scrollEvent) {
      CGEventPost(kCGHIDEventTap, scrollEvent);
      CFRelease(scrollEvent);
    }
  }

  if (flags & MOUSEEVENTF_XDOWN) {
    CGMouseButton btn =
        (mouseData == XBUTTON1) ? (CGMouseButton)3 : (CGMouseButton)4;
    postClick(kCGEventOtherMouseDown, btn);
  }
  if (flags & MOUSEEVENTF_XUP) {
    CGMouseButton btn =
        (mouseData == XBUTTON1) ? (CGMouseButton)3 : (CGMouseButton)4;
    postClick(kCGEventOtherMouseUp, btn);
  }
#endif
}

// Track button states to avoid repeated key presses
static bool g_screenshotButtonPressed = false;
static bool g_cButtonPressed = false;
static bool g_comboPressed = false;

// Flag for ZL+ZR+GL+GR combo to enter management window
static std::atomic<bool> g_openManagementWindow(false);

// Handle special Pro Controller buttons (Screenshot, C button, and combo
// detection)
void HandleSpecialProButtons(const std::vector<uint8_t> &buffer) {
  if (buffer.size() < 9)
    return;

  // Build button state from bytes 3-8
  uint64_t state = 0;
  for (int i = 3; i <= 8; ++i) {
    state = (state << 8) | buffer[i];
  }

  constexpr uint64_t BUTTON_SCREENSHOT_MASK = 0x000020000000; // Bit 29
  constexpr uint64_t BUTTON_C_MASK = 0x000040000000;          // Bit 30
  constexpr uint64_t BUTTON_GL_MASK = 0x000000000200;         // Bit 9
  constexpr uint64_t BUTTON_GR_MASK = 0x000000000100;         // Bit 8
  constexpr uint64_t TRIGGER_ZL_MASK = 0x000000800000;        // ZL trigger
  constexpr uint64_t TRIGGER_ZR_MASK = 0x008000000000;        // ZR trigger

  // Check for ZL+ZR+GL+GR combo to open management window (only trigger on
  // initial press)
  bool comboCurrentlyPressed =
      (state & TRIGGER_ZL_MASK) && (state & TRIGGER_ZR_MASK) &&
      (state & BUTTON_GL_MASK) && (state & BUTTON_GR_MASK);

  if (comboCurrentlyPressed && !g_comboPressed) {
    // Combo just pressed - trigger management window
    g_openManagementWindow.store(true);
    g_comboPressed = true;
  } else if (!comboCurrentlyPressed && g_comboPressed) {
    // Combo released - reset state
    g_comboPressed = false;
  }

  // Handle Screenshot button -> F12 key
  bool screenshotPressed = (state & BUTTON_SCREENSHOT_MASK) != 0;
  if (screenshotPressed && !g_screenshotButtonPressed) {
    // Button just pressed - send F12 key down
    SendKeyboardInput(VK_F12, true);
    g_screenshotButtonPressed = true;
  } else if (!screenshotPressed && g_screenshotButtonPressed) {
    // Button just released - send F12 key up
    SendKeyboardInput(VK_F12, false);
    g_screenshotButtonPressed = false;
  }

  // Handle C button -> Cycle through layouts
  bool cPressed = (state & BUTTON_C_MASK) != 0;
  if (cPressed && !g_cButtonPressed) {
    // Button just pressed - cycle to next layout
    if (!g_proControllerConfig.layouts.empty()) {
      int oldIndex = g_proControllerConfig.activeLayoutIndex;
      g_proControllerConfig.activeLayoutIndex =
          (g_proControllerConfig.activeLayoutIndex + 1) %
          g_proControllerConfig.layouts.size();
      SaveProControllerConfig(g_proControllerConfig);

      // Log layout change
      std::cout << "\n[Layout Changed] "
                << g_proControllerConfig.layouts[oldIndex].name << " -> "
                << g_proControllerConfig
                       .layouts[g_proControllerConfig.activeLayoutIndex]
                       .name
                << " (GL: "
                << ButtonMappingToString(
                       g_proControllerConfig
                           .layouts[g_proControllerConfig.activeLayoutIndex]
                           .glMapping)
                << ", GR: "
                << ButtonMappingToString(
                       g_proControllerConfig
                           .layouts[g_proControllerConfig.activeLayoutIndex]
                           .grMapping)
                << ")\n";
    }
    g_cButtonPressed = true;
  } else if (!cPressed && g_cButtonPressed) {
    g_cButtonPressed = false;
  }
}

// Apply GL/GR mappings to Pro Controller report using active layout
void ApplyGLGRMappings(DS4_REPORT_EX &report,
                       const std::vector<uint8_t> &buffer) {
  if (buffer.size() < 9)
    return;

  // Check if we have any layouts
  if (g_proControllerConfig.layouts.empty())
    return;

  // Get the active layout
  int layoutIndex = g_proControllerConfig.activeLayoutIndex;
  if (layoutIndex < 0 ||
      layoutIndex >= static_cast<int>(g_proControllerConfig.layouts.size())) {
    layoutIndex = 0;
    g_proControllerConfig.activeLayoutIndex = 0;
  }

  const GLGRLayout &activeLayout = g_proControllerConfig.layouts[layoutIndex];

  // Build button state from bytes 3-8 (same as in JoyConDecoder)
  uint64_t state = 0;
  for (int i = 3; i <= 8; ++i) {
    state = (state << 8) | buffer[i];
  }

  constexpr uint64_t BUTTON_GL_MASK = 0x000000000200; // Bit 9
  constexpr uint64_t BUTTON_GR_MASK = 0x000000000100; // Bit 8

  // Apply mappings if buttons are pressed
  if (state & BUTTON_GL_MASK) {
    ApplyButtonMapping(report, activeLayout.glMapping);
  }
  if (state & BUTTON_GR_MASK) {
    ApplyButtonMapping(report, activeLayout.grMapping);
  }
}

#ifdef _WIN32
void SendCustomCommands(GattCharacteristic const &characteristic) {
  std::vector<std::vector<uint8_t>> commands = {
      {0x0c, 0x91, 0x01, 0x02, 0x00, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00},
      {0x0c, 0x91, 0x01, 0x04, 0x00, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00}};

  for (const auto &cmd : commands) {
    auto writer = DataWriter();
    writer.WriteBytes(cmd);
    IBuffer buffer = writer.DetachBuffer();

    auto status =
        characteristic
            .WriteValueAsync(buffer, GattWriteOption::WriteWithoutResponse)
            .get();

    if (status == GattCommunicationStatus::Success) {
      TCOUT << TSTR("Command sent successfully.\n");
    } else {
      TCOUT << TSTR("Failed to send command.\n");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}

// Helper to send generic commands matching main.py structure
void SendGenericCommand(GattCharacteristic const &characteristic, uint8_t cmdId,
                        uint8_t subCmdId, const std::vector<uint8_t> &data) {
  if (!characteristic)
    return;

  DataWriter writer;

  // Structure: CmdID, 0x91, 0x01, SubCmdID, 0x00, Len, 0x00, 0x00, Data...
  writer.WriteByte(cmdId);
  writer.WriteByte(0x91);
  writer.WriteByte(0x01);
  writer.WriteByte(subCmdId);
  writer.WriteByte(0x00);
  writer.WriteByte(static_cast<uint8_t>(data.size()));
  writer.WriteByte(0x00);
  writer.WriteByte(0x00);

  // Write Data
  for (uint8_t b : data) {
    writer.WriteByte(b);
  }

  IBuffer buffer = writer.DetachBuffer();
  characteristic.WriteValueAsync(buffer, GattWriteOption::WriteWithoutResponse)
      .get();

  // Small delay to prevent flooding
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void SendSubcommand(GattCharacteristic const &characteristic, uint8_t subCmdId,
                    const std::vector<uint8_t> &data) {
  SendGenericCommand(characteristic, 0x01, subCmdId, data);
}

void SetPlayerLed(GattCharacteristic const &characteristic, uint8_t pattern) {
  std::vector<uint8_t> data(8, 0x00);
  data[0] = pattern;
  SendGenericCommand(characteristic, 0x01, 0x30, data);
}

void EnableIMU(GattCharacteristic const &characteristic, bool enable) {
  std::vector<uint8_t> data = {enable ? (uint8_t)0x01 : (uint8_t)0x00};
  SendGenericCommand(characteristic, 0x01, 0x40, data);
}

void SetFullReportMode(GattCharacteristic const &characteristic) {
  std::vector<uint8_t> data = {0x30};
  SendGenericCommand(characteristic, 0x01, 0x03, data);
}
#else
// Helper to send generic commands matching main.py structure for macOS
void SendGenericCommand(std::shared_ptr<IBluetoothDevice> device, uint8_t cmdId,
                        uint8_t subCmdId, const std::vector<uint8_t> &data) {
  if (!device)
    return;

  std::vector<uint8_t> full_data;
  full_data.push_back(cmdId);
  full_data.push_back(0x91);
  full_data.push_back(0x01);
  full_data.push_back(subCmdId);
  full_data.push_back(0x00);
  full_data.push_back(static_cast<uint8_t>(data.size()));
  full_data.push_back(0x00);
  full_data.push_back(0x00);
  for (uint8_t b : data) {
    full_data.push_back(b);
  }

  // We need a service UUID. On Joy-Con, it's usually a specific one, but we can
  // try to find it. For now, let's assume we use a placeholder or the
  // MacBluetoothManager handles it.
  device->WriteCharacteristic("", WRITE_COMMAND_UUID, full_data);

  // Small delay to prevent flooding
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void SendSubcommand(std::shared_ptr<IBluetoothDevice> device, uint8_t subCmdId,
                    const std::vector<uint8_t> &data) {
  SendGenericCommand(device, 0x01, subCmdId, data);
}

void SetPlayerLed(std::shared_ptr<IBluetoothDevice> device, uint8_t pattern) {
  std::vector<uint8_t> data(8, 0x00);
  data[0] = pattern;
  SendGenericCommand(device, 0x01, 0x30, data);
}

void EnableIMU(std::shared_ptr<IBluetoothDevice> device, bool enable) {
  std::vector<uint8_t> data = {enable ? (uint8_t)0x01 : (uint8_t)0x00};
  SendGenericCommand(device, 0x01, 0x40, data);
}

void SetFullReportMode(std::shared_ptr<IBluetoothDevice> device) {
  std::vector<uint8_t> data = {0x30};
  SendGenericCommand(device, 0x01, 0x03, data);
}

void SendCustomCommands(std::shared_ptr<IBluetoothDevice> device) {
  if (!device)
    return;

  std::vector<std::vector<uint8_t>> commands = {
      {0x0c, 0x91, 0x01, 0x02, 0x00, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00},
      {0x0c, 0x91, 0x01, 0x04, 0x00, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00}};

  for (const auto &cmd : commands) {
    device->WriteCharacteristic("", WRITE_COMMAND_UUID, cmd);
    TCOUT << TSTR("Custom command sent.\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}
#endif
#ifdef _WIN32
struct ConnectedJoyCon {
  BluetoothLEDevice device = nullptr;
  GattCharacteristic inputChar = nullptr;
  GattCharacteristic writeChar = nullptr;
};
#else
struct ConnectedJoyCon {
  std::shared_ptr<IBluetoothDevice> device = nullptr;
};
#endif

void EmitSound(ConnectedJoyCon &cj) {
  // CMD 0x0A, SUB 0x02, Data: 0x04 (Preset) + Padding (up to 8 bytes total)
  std::vector<uint8_t> data(8, 0x00);
  data[0] = 0x04; // Preset ID
#ifdef _WIN32
  SendGenericCommand(cj.writeChar, 0x0A, 0x02, data);
#else
  if (cj.device) {
    SendGenericCommand(cj.device, 0x0A, 0x02, data);
  }
#endif
}

void SetPlayerLEDs(ConnectedJoyCon &cj, uint8_t pattern) {
  // CMD 0x09, SUB 0x07, Data: pattern + Padding (up to 8 bytes total)
  std::vector<uint8_t> data(8, 0x00);
  data[0] = pattern;
#ifdef _WIN32
  SendGenericCommand(cj.writeChar, 0x09, 0x07, data);
#else
  if (cj.device) {
    SendGenericCommand(cj.device, 0x09, 0x07, data);
  }
#endif
}

ConnectedJoyCon WaitForJoyCon(const TSTRING &prompt) {
#ifdef _WIN32
  TCOUT << prompt << TSTR("\n");

  ConnectedJoyCon cj{};

  BluetoothLEDevice device = nullptr;
  bool connected = false;

  BluetoothLEAdvertisementWatcher watcher;

  std::mutex mtx;
  std::condition_variable cv;

  watcher.Received([&](auto const &, auto const &args) {
    std::unique_lock<std::mutex> lock(mtx);
    if (connected)
      return;

    auto mfg = args.Advertisement().ManufacturerData();
    for (uint32_t i = 0; i < mfg.Size(); i++) {
      auto section = mfg.GetAt(i);
      if (section.CompanyId() != JOYCON_MANUFACTURER_ID)
        continue;
      auto reader = DataReader::FromBuffer(section.Data());
      std::vector<uint8_t> data(reader.UnconsumedBufferLength());
      reader.ReadBytes(data);
      if (data.size() >= JOYCON_MANUFACTURER_PREFIX.size() &&
          std::equal(JOYCON_MANUFACTURER_PREFIX.begin(),
                     JOYCON_MANUFACTURER_PREFIX.end(), data.begin())) {
        device = BluetoothLEDevice::FromBluetoothAddressAsync(
                     args.BluetoothAddress())
                     .get();
        if (!device)
          return;

        connected = true;
        watcher.Stop();
        cv.notify_one();
        return;
      }
    }
  });

  watcher.ScanningMode(BluetoothLEScanningMode::Active);
  watcher.Start();

  TCOUT << TSTR("Scanning for Joy-Con... (Waiting up to 30 seconds)\n");

  {
    std::unique_lock<std::mutex> lock(mtx);
    if (!cv.wait_for(lock, std::chrono::seconds(30),
                     [&]() { return connected; })) {
      watcher.Stop();
      TCERR << TSTR("Timeout: Joy-Con not found.\n");
      exit(1);
    }
  }

  cj.device = device;

  auto servicesResult = device.GetGattServicesAsync().get();
  if (servicesResult.Status() != GattCommunicationStatus::Success) {
    TCERR << TSTR("Failed to get GATT services.\n");
    exit(1);
  }

  for (auto service : servicesResult.Services()) {
    auto charsResult = service.GetCharacteristicsAsync().get();
    if (charsResult.Status() != GattCommunicationStatus::Success)
      continue;
    for (auto characteristic : charsResult.Characteristics()) {
      if (characteristic.Uuid() == guid(INPUT_REPORT_UUID))
        cj.inputChar = characteristic;
      else if (characteristic.Uuid() == guid(WRITE_COMMAND_UUID))
        cj.writeChar = characteristic;
    }
  }

  return cj;
#else
  TCOUT << prompt << TSTR("\n");

  static std::shared_ptr<MacBluetoothManager> ble_manager = nullptr;
  if (!ble_manager) {
    ble_manager = std::make_shared<MacBluetoothManager>();
    ble_manager->Initialize();
  }

  std::string prefix = ""; // Matching prefix if needed
  auto device = ble_manager->ScanAndConnect(prefix, JOYCON_MANUFACTURER_ID,
                                            JOYCON_MANUFACTURER_PREFIX);

  if (!device) {
    TCERR << TSTR("Timeout: Joy-Con not found.\n");
    exit(1);
  }

  if (!device->Connect()) {
    TCERR << TSTR("Failed to connect to Joy-Con.\n");
    exit(1);
  }

  ConnectedJoyCon cj{};
  cj.device = device;
  return cj;
#endif
}

enum ControllerType {
  SingleJoyCon = 1,
  DualJoyCon = 2,
  ProController = 3,
  NSOGCController = 4
};

struct PlayerConfig {
  ControllerType controllerType;
  JoyConSide joyconSide;
  JoyConOrientation joyconOrientation;
  GyroSource gyroSource;
};

// For single Joy-Con players, store controller + JoyCon info to keep alive
struct SingleJoyConPlayer {
  ConnectedJoyCon joycon;
#ifdef _WIN32
  PVIGEM_TARGET ds4Controller;
#endif
  JoyConSide side;
  JoyConOrientation orientation;

  // Mouse State
  int mouseMode = 0; // 0=Off, 1=Fast, 2=Normal, 3=Slow
  bool wasChatPressed = false;
  int16_t lastOpticalX = 0;
  int16_t lastOpticalY = 0;
  bool firstOpticalRead = true;

  // Scroll Accumulator
  float scrollAccumulator = 0.0f;

  // Button States for Edge Detection (to avoid rapid fire)
  bool mb4Pressed = false;
  bool mb5Pressed = false;

  // Previous button states for click emulation
  bool leftBtnPressed = false;
  bool rightBtnPressed = false;
  bool middleBtnPressed = false;
};

// For dual Joy-Con players, store both JoyCons, controller, thread, and
// running flag
struct DualJoyConPlayer {
  ConnectedJoyCon leftJoyCon;
  ConnectedJoyCon rightJoyCon;
  GyroSource gyroSource;
#ifdef _WIN32
  PVIGEM_TARGET ds4Controller;
#endif
  std::atomic<bool> running;
  std::thread updateThread;
};

// For Pro Controller players
struct ProControllerPlayer {
  ConnectedJoyCon controller;
#ifdef _WIN32
  PVIGEM_TARGET ds4Controller;
#endif
};

// Declare the Pro Controller report generator (implement in
// JoyConDecoder.cpp)
DS4_REPORT_EX GenerateProControllerReport(const std::vector<uint8_t> &buffer);

int main() {
#ifdef _WIN32
  init_apartment();
#endif

  int numPlayers;
  TCOUT << TSTR("How many players? ");
  TCIN >> numPlayers;
  TCIN.ignore();

  std::vector<PlayerConfig> playerConfigs;

  for (int i = 0; i < numPlayers; ++i) {
    PlayerConfig config{};
    TSTRING line;

    while (true) {
      TCOUT << TSTR("Player ") << (i + 1) << TSTR(":\n");
      TCOUT << TSTR("  What controller type? (1=Single JoyCon, 2=Dual JoyCon, ")
            << TSTR("3=Pro Controller, 4=NSO GC Controller): ");
      TGETLINE(TCIN, line);
      if (line == TSTR("1") || line == TSTR("2") || line == TSTR("3") ||
          line == TSTR("4")) {
        config.controllerType = static_cast<ControllerType>(
            std::stoi(std::string(line.begin(), line.end())));
        break;
      }
      TCOUT << TSTR("Invalid input. Please enter 1, 2, or 3.\n");
    }

    if (config.controllerType == SingleJoyCon) {
      while (true) {
        TCOUT << TSTR("  Which side? (L=Left, R=Right): ");
        TGETLINE(TCIN, line);
        if (line == TSTR("L") || line == TSTR("R") || line == TSTR("l") ||
            line == TSTR("r")) {
          config.joyconSide = (line == TSTR("L") || line == TSTR("l"))
                                  ? JoyConSide::Left
                                  : JoyConSide::Right;
          break;
        }
        TCOUT << TSTR("Invalid input. Please enter L or R.\n");
      }
      while (true) {
        TCOUT << TSTR("  What orientation? (U=Upright, S=Sideways): ");
        TGETLINE(TCIN, line);
        if (line == TSTR("U") || line == TSTR("S") || line == TSTR("u") ||
            line == TSTR("s")) {
          config.joyconOrientation = (line == TSTR("S") || line == TSTR("s"))
                                         ? JoyConOrientation::Sideways
                                         : JoyConOrientation::Upright;
          break;
        }
        TCOUT << TSTR("Invalid input. Please enter U or S.\n");
      }
    } else if (config.controllerType == DualJoyCon) {
      config.joyconSide = JoyConSide::Left;
      config.joyconOrientation = JoyConOrientation::Upright;

      while (true) {
        TCOUT << TSTR("Player ") << (i + 1) << TSTR(":\n");
        TCOUT << TSTR("  What should be used as Gyro Source? (B=Both JoyCons, ")
              << TSTR("L=Left JoyCon, R=Right JoyCon): ");
        TGETLINE(TCIN, line);

        if (line == TSTR("B")) {
          config.gyroSource = GyroSource::Both;
          break;
        } else if (line == TSTR("L")) {
          config.gyroSource = GyroSource::Left;
          break;
        } else if (line == TSTR("R")) {
          config.gyroSource = GyroSource::Right;
          break;
        }
        TCOUT << TSTR("Invalid input. Please enter B, L, or R.\n");
      }
    }

    playerConfigs.push_back(config);
  }

  InitializeViGEm();

  // Store all players to keep them alive
  std::vector<SingleJoyConPlayer> singlePlayers;
  std::vector<std::unique_ptr<DualJoyConPlayer>> dualPlayers;
  std::vector<ProControllerPlayer> proPlayers;

  for (int i = 0; i < numPlayers; ++i) {
    auto &config = playerConfigs[i];
    TCOUT << TSTR("Player ") << (i + 1) << TSTR(" setup...\n");

    if (config.controllerType == SingleJoyCon) {
      TSTRING sideStr = (config.joyconSide == JoyConSide::Left) ? TSTR("Left")
                                                                : TSTR("Right");
      TCOUT << TSTR("Please sync your single Joy-Con (") << sideStr
            << TSTR(") now.\n");

      ConnectedJoyCon cj = WaitForJoyCon(TSTR("Waiting for single Joy-Con..."));

#ifdef _WIN32
      // Request minimum BLE connection interval for lowest latency
      try {
        auto connectionParams =
            BluetoothLEPreferredConnectionParameters::ThroughputOptimized();
        cj.device.RequestPreferredConnectionParameters(connectionParams);
        TCOUT << TSTR("Requested ThroughputOptimized connection parameters ")
              << TSTR("for lower latency.\n");
      } catch (...) {
        TCOUT << TSTR(
            "Warning: Could not request preferred connection parameters.\n");
      }

      PVIGEM_TARGET ds4_controller = vigem_target_ds4_alloc();
      auto ret = vigem_target_add(vigem_client, ds4_controller);
      if (!VIGEM_SUCCESS(ret)) {
        TCERR << TSTR("Failed to add DS4 controller target: 0x") << std::hex
              << ret << TSTR("\n");
        exit(1);
      }
#else
      void *ds4_controller = nullptr;
#endif

      singlePlayers.push_back(
#ifdef _WIN32
          { cj, ds4_controller, config.joyconSide, config.joyconOrientation }
#else
          {cj, config.joyconSide, config.joyconOrientation}
#endif
      );
      auto &player = singlePlayers.back();

      TCOUT << TSTR("Press Enter to continue...\n");
#ifdef _WIN32
      player.joycon.inputChar.ValueChanged(
          [joyconSide = player.side, joyconOrientation = player.orientation,
           &player](GattCharacteristic const &,
                    GattValueChangedEventArgs const &args) {
            auto reader = DataReader::FromBuffer(args.CharacteristicValue());
            std::vector<uint8_t> buffer(reader.UnconsumedBufferLength());
            reader.ReadBytes(buffer);

            // Optical Mouse Toggle Logic (Only for Right Joy-Con/Joy-Con 2)
            if (joyconSide == JoyConSide::Right) {
              uint32_t btnState = ExtractButtonState(buffer);
              bool chatPressed = (btnState & 0x000040) != 0;

              if (chatPressed && !player.wasChatPressed) {
                player.mouseMode = (player.mouseMode + 1) % 4;
                const char *modeName = "OFF";
                uint8_t ledPattern = 0x01;
                if (player.mouseMode == 1) {
                  modeName = "FAST";
                  ledPattern = 0x02;
                } else if (player.mouseMode == 2) {
                  modeName = "NORMAL";
                  ledPattern = 0x04;
                } else if (player.mouseMode == 3) {
                  modeName = "SLOW";
                  ledPattern = 0x08;
                }

                TCOUT << TSTR("Optical Mouse Mode: ") << modeName << std::endl;
                SetPlayerLEDs(player.joycon, ledPattern);
                EmitSound(player.joycon);
              }
              player.wasChatPressed = chatPressed;

              if (player.mouseMode > 0) {
                auto [rawX, rawY] = GetRawOpticalMouse(buffer);
                if (player.firstOpticalRead) {
                  player.lastOpticalX = rawX;
                  player.lastOpticalY = rawY;
                  player.firstOpticalRead = false;
                } else {
                  int16_t dx = rawX - player.lastOpticalX;
                  int16_t dy = rawY - player.lastOpticalY;
                  player.lastOpticalX = rawX;
                  player.lastOpticalY = rawY;

                  if (dx != 0 || dy != 0) {
                    float sensitivity = 1.0f;
                    if (player.mouseMode == 1)
                      sensitivity = 1.0f;
                    else if (player.mouseMode == 2)
                      sensitivity = 0.6f;
                    else if (player.mouseMode == 3)
                      sensitivity = 0.3f;

                    int moveX = static_cast<int>(dx * sensitivity);
                    int moveY = static_cast<int>(dy * sensitivity);
                    SendMouseInput(moveX, moveY, MOUSEEVENTF_MOVE);
                  }
                }

                bool rPressed = (btnState & 0x004000) != 0;
                bool zrPressed = (btnState & 0x008000) != 0;
                bool stickPressed = (btnState & 0x000004) != 0;

                if (rPressed && !player.leftBtnPressed)
                  SendMouseInput(0, 0, MOUSEEVENTF_LEFTDOWN);
                else if (!rPressed && player.leftBtnPressed)
                  SendMouseInput(0, 0, MOUSEEVENTF_LEFTUP);
                player.leftBtnPressed = rPressed;

                if (zrPressed && !player.rightBtnPressed)
                  SendMouseInput(0, 0, MOUSEEVENTF_RIGHTDOWN);
                else if (!zrPressed && player.rightBtnPressed)
                  SendMouseInput(0, 0, MOUSEEVENTF_RIGHTUP);
                player.rightBtnPressed = zrPressed;

                if (stickPressed && !player.middleBtnPressed)
                  SendMouseInput(0, 0, MOUSEEVENTF_MIDDLEDOWN);
                else if (!stickPressed && player.middleBtnPressed)
                  SendMouseInput(0, 0, MOUSEEVENTF_MIDDLEUP);
                player.middleBtnPressed = stickPressed;

                auto stickData =
                    DecodeJoystick(buffer, joyconSide, joyconOrientation);
                const int SCROLL_DEADZONE = 4000;
                if (std::abs(stickData.y) > SCROLL_DEADZONE) {
                  float intensity = (std::abs(stickData.y) - SCROLL_DEADZONE) /
                                    (32767.0f - SCROLL_DEADZONE);
                  float speed = intensity * 40.0f;
                  if (stickData.y > 0)
                    player.scrollAccumulator -= speed;
                  else
                    player.scrollAccumulator += speed;

                  if (std::abs(player.scrollAccumulator) >= 120.0f) {
                    int clicks =
                        static_cast<int>(player.scrollAccumulator / 120.0f);
                    player.scrollAccumulator -= (clicks * 120.0f);
                    SendMouseInput(0, 0, MOUSEEVENTF_WHEEL, clicks * 120);
                  }
                } else {
                  player.scrollAccumulator = 0.0f;
                }

                const int BUTTON_THRESHOLD = 28000;
                if (stickData.x < -BUTTON_THRESHOLD) {
                  if (!player.mb4Pressed) {
                    SendMouseInput(0, 0, MOUSEEVENTF_XDOWN, XBUTTON1);
                    SendMouseInput(0, 0, MOUSEEVENTF_XUP, XBUTTON1);
                    player.mb4Pressed = true;
                  }
                } else {
                  player.mb4Pressed = false;
                }
                if (stickData.x > BUTTON_THRESHOLD) {
                  if (!player.mb5Pressed) {
                    SendMouseInput(0, 0, MOUSEEVENTF_XDOWN, XBUTTON2);
                    SendMouseInput(0, 0, MOUSEEVENTF_XUP, XBUTTON2);
                    player.mb5Pressed = true;
                  }
                } else {
                  player.mb5Pressed = false;
                }

                buffer[4] &= ~0x40;
                buffer[4] &= ~0x80;
                buffer[5] &= ~0x04;
                if (buffer.size() >= 16) {
                  buffer[13] = 0x00;
                  buffer[14] = 0x08;
                  buffer[15] = 0x80;
                }
              } else {
                player.firstOpticalRead = true;
              }
            }

            DS4_REPORT_EX report =
                GenerateDS4Report(buffer, joyconSide, joyconOrientation);
            vigem_target_ds4_update_ex(vigem_client, player.ds4Controller,
                                       report);
          });

      auto status =
          player.joycon.inputChar
              .WriteClientCharacteristicConfigurationDescriptorAsync(
                  GattClientCharacteristicConfigurationDescriptorValue::Notify)
              .get();
      if (status == GattCommunicationStatus::Success) {
        TCOUT << TSTR("Notifications enabled.\n");
        SendCustomCommands(player.joycon.writeChar);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        SetPlayerLEDs(player.joycon, 0x01);
        EmitSound(player.joycon);
      } else {
        TCOUT << TSTR("Failed to enable notifications.\n");
      }
#else
      if (player.joycon.device->SubscribeNotification(
              "", std::string(INPUT_REPORT_UUID),
              [joyconSide = player.side, joyconOrientation = player.orientation,
               &player](const std::vector<uint8_t> &buffer_in) {
                std::vector<uint8_t> buffer = buffer_in;

                // Optical Mouse Toggle Logic (Only for Right Joy-Con/Joy-Con 2)
                if (joyconSide == JoyConSide::Right) {
                  uint32_t btnState = ExtractButtonState(buffer);
                  bool chatPressed = (btnState & 0x000040) != 0;

                  if (chatPressed && !player.wasChatPressed) {
                    player.mouseMode = (player.mouseMode + 1) % 4;
                    const char *modeName = "OFF";
                    uint8_t ledPattern = 0x01;
                    if (player.mouseMode == 1) {
                      modeName = "FAST";
                      ledPattern = 0x02;
                    } else if (player.mouseMode == 2) {
                      modeName = "NORMAL";
                      ledPattern = 0x04;
                    } else if (player.mouseMode == 3) {
                      modeName = "SLOW";
                      ledPattern = 0x08;
                    }

                    TCOUT << TSTR("Optical Mouse Mode: ") << modeName
                          << std::endl;
                    SetPlayerLEDs(player.joycon, ledPattern);
                    EmitSound(player.joycon);
                  }
                  player.wasChatPressed = chatPressed;

                  if (player.mouseMode > 0) {
                    auto [rawX, rawY] = GetRawOpticalMouse(buffer);
                    if (player.firstOpticalRead) {
                      printf("DEBUG: buffer.size()=%zu, rawX=%d, rawY=%d\n",
                             buffer.size(), rawX, rawY);
                      player.lastOpticalX = rawX;
                      player.lastOpticalY = rawY;
                      player.firstOpticalRead = false;
                    } else {
                      int16_t dx = rawX - player.lastOpticalX;
                      int16_t dy = rawY - player.lastOpticalY;
                      player.lastOpticalX = rawX;
                      player.lastOpticalY = rawY;

                      if (dx != 0 || dy != 0) {
                        float sensitivity = 1.0f;
                        if (player.mouseMode == 1)
                          sensitivity = 1.0f;
                        else if (player.mouseMode == 2)
                          sensitivity = 0.6f;
                        else if (player.mouseMode == 3)
                          sensitivity = 0.3f;

                        int moveX = static_cast<int>(dx * sensitivity);
                        int moveY = static_cast<int>(dy * sensitivity);
                        SendMouseInput(moveX, moveY, MOUSEEVENTF_MOVE);
                      }
                    }

                    bool rPressed = (btnState & 0x004000) != 0;
                    bool zrPressed = (btnState & 0x008000) != 0;
                    bool stickPressed = (btnState & 0x000004) != 0;

                    if (rPressed && !player.leftBtnPressed)
                      SendMouseInput(0, 0, MOUSEEVENTF_LEFTDOWN);
                    else if (!rPressed && player.leftBtnPressed)
                      SendMouseInput(0, 0, MOUSEEVENTF_LEFTUP);
                    player.leftBtnPressed = rPressed;

                    if (zrPressed && !player.rightBtnPressed)
                      SendMouseInput(0, 0, MOUSEEVENTF_RIGHTDOWN);
                    else if (!zrPressed && player.rightBtnPressed)
                      SendMouseInput(0, 0, MOUSEEVENTF_RIGHTUP);
                    player.rightBtnPressed = zrPressed;

                    if (stickPressed && !player.middleBtnPressed)
                      SendMouseInput(0, 0, MOUSEEVENTF_MIDDLEDOWN);
                    else if (!stickPressed && player.middleBtnPressed)
                      SendMouseInput(0, 0, MOUSEEVENTF_MIDDLEUP);
                    player.middleBtnPressed = stickPressed;

                    auto stickData =
                        DecodeJoystick(buffer, joyconSide, joyconOrientation);
                    const int SCROLL_DEADZONE = 4000;
                    if (std::abs(stickData.y) > SCROLL_DEADZONE) {
                      float intensity =
                          (std::abs(stickData.y) - SCROLL_DEADZONE) /
                          (32767.0f - SCROLL_DEADZONE);
                      float speed = intensity * 40.0f;
                      if (stickData.y > 0)
                        player.scrollAccumulator -= speed;
                      else
                        player.scrollAccumulator += speed;

                      if (std::abs(player.scrollAccumulator) >= 120.0f) {
                        int clicks =
                            static_cast<int>(player.scrollAccumulator / 120.0f);
                        player.scrollAccumulator -= (clicks * 120.0f);
                        SendMouseInput(0, 0, MOUSEEVENTF_WHEEL, clicks * 120);
                      }
                    } else {
                      player.scrollAccumulator = 0.0f;
                    }

                    const int BUTTON_THRESHOLD = 28000;
                    if (stickData.x < -BUTTON_THRESHOLD) {
                      if (!player.mb4Pressed) {
                        SendMouseInput(0, 0, MOUSEEVENTF_XDOWN, XBUTTON1);
                        SendMouseInput(0, 0, MOUSEEVENTF_XUP, XBUTTON1);
                        player.mb4Pressed = true;
                      }
                    } else {
                      player.mb4Pressed = false;
                    }
                    if (stickData.x > BUTTON_THRESHOLD) {
                      if (!player.mb5Pressed) {
                        SendMouseInput(0, 0, MOUSEEVENTF_XDOWN, XBUTTON2);
                        SendMouseInput(0, 0, MOUSEEVENTF_XUP, XBUTTON2);
                        player.mb5Pressed = true;
                      }
                    } else {
                      player.mb5Pressed = false;
                    }

                    buffer[4] &= ~0x40;
                    buffer[4] &= ~0x80;
                    buffer[5] &= ~0x04;
                    if (buffer.size() >= 16) {
                      buffer[13] = 0x00;
                      buffer[14] = 0x08;
                      buffer[15] = 0x80;
                    }
                  } else {
                    player.firstOpticalRead = true;
                  }
                }

                DS4_REPORT_EX report =
                    GenerateDS4Report(buffer, joyconSide, joyconOrientation);

                VirtualControllerReport mac_report;
                mac_report.left_stick_x = report.Report.bThumbLX;
                mac_report.left_stick_y = report.Report.bThumbLY;
                mac_report.right_stick_x = report.Report.bThumbRX;
                mac_report.right_stick_y = report.Report.bThumbRY;
                mac_report.buttons = report.Report.wButtons;
                mac_report.dpad = 0;
                mac_report.left_trigger = report.Report.bTriggerL;
                mac_report.right_trigger = report.Report.bTriggerR;
                mac_controller->UpdateReport(mac_report);
              })) {
        TCOUT << TSTR("Notifications enabled.\n");
        SendCustomCommands(player.joycon.device);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        SetPlayerLEDs(player.joycon, 0x01);
        EmitSound(player.joycon);
      } else {
        TCERR << TSTR("Failed to enable notifications.\n");
      }
#endif
      TSTRING dummy;
      TGETLINE(TCIN, dummy);
    } else if (config.controllerType == DualJoyCon) {
      TCOUT << TSTR("Please sync your RIGHT Joy-Con now.\n");
      ConnectedJoyCon rightJoyCon =
          WaitForJoyCon(TSTR("Waiting for RIGHT Joy-Con..."));
#ifdef _WIN32
      if (rightJoyCon.writeChar) {
        SendCustomCommands(rightJoyCon.writeChar);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        SetPlayerLEDs(rightJoyCon.writeChar, 0x01);
        EmitSound(rightJoyCon.writeChar);
      }
#else
      if (rightJoyCon.device) {
        SendCustomCommands(rightJoyCon.device);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        SendGenericCommand(rightJoyCon.device, 0x09, 0x07,
                           {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
      }
#endif

      TCOUT << TSTR("Please sync your LEFT Joy-Con now.\n");
      ConnectedJoyCon leftJoyCon =
          WaitForJoyCon(TSTR("Waiting for LEFT Joy-Con..."));
#ifdef _WIN32
      if (leftJoyCon.writeChar) {
        SendCustomCommands(leftJoyCon.writeChar);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        SetPlayerLEDs(leftJoyCon.writeChar, 0x08);
        EmitSound(leftJoyCon.writeChar);
      }
#else
      if (leftJoyCon.device) {
        SendCustomCommands(leftJoyCon.device);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        SendGenericCommand(leftJoyCon.device, 0x09, 0x07,
                           {0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
      }
#endif

#ifdef _WIN32
      PVIGEM_TARGET ds4_controller = vigem_target_ds4_alloc();
      auto ret = vigem_target_add(vigem_client, ds4_controller);
      if (!VIGEM_SUCCESS(ret)) {
        TCERR << TSTR("Failed to add DS4 controller target: 0x") << std::hex
              << ret << TSTR("\n");
        exit(1);
      }
#else
      void *ds4_controller = nullptr;
#endif

      auto dualPlayer = std::make_unique<DualJoyConPlayer>();
      dualPlayer->leftJoyCon = leftJoyCon;
      dualPlayer->rightJoyCon = rightJoyCon;
      dualPlayer->gyroSource = config.gyroSource;
#ifdef _WIN32
      dualPlayer->ds4Controller = ds4_controller;
#endif
      dualPlayer->running.store(true);

      // Mutex-protected buffers for thread-safe data passing
      struct SharedBuffer {
        std::vector<uint8_t> data;
        std::mutex mtx;
      };
      auto leftShared = std::make_shared<SharedBuffer>();
      auto rightShared = std::make_shared<SharedBuffer>();

#ifdef _WIN32
      dualPlayer->leftJoyCon.inputChar.ValueChanged(
          [leftShared](GattCharacteristic const &,
                       GattValueChangedEventArgs const &args) {
            auto reader = DataReader::FromBuffer(args.CharacteristicValue());
            std::lock_guard<std::mutex> lock(leftShared->mtx);
            leftShared->data.resize(reader.UnconsumedBufferLength());
            reader.ReadBytes(leftShared->data);
          });

      dualPlayer->rightJoyCon.inputChar.ValueChanged(
          [rightShared](GattCharacteristic const &,
                        GattValueChangedEventArgs const &args) {
            auto reader = DataReader::FromBuffer(args.CharacteristicValue());
            std::lock_guard<std::mutex> lock(rightShared->mtx);
            rightShared->data.resize(reader.UnconsumedBufferLength());
            reader.ReadBytes(rightShared->data);
          });
#else
      if (dualPlayer->leftJoyCon.device->SubscribeNotification(
              "", std::string(INPUT_REPORT_UUID),
              [leftShared](const std::vector<uint8_t> &buffer) {
                std::lock_guard<std::mutex> lock(leftShared->mtx);
                leftShared->data = buffer;
              })) {
        TCOUT << TSTR("Left Joy-Con notifications enabled.\n");
      } else {
        TCERR << TSTR("Failed to enable left Joy-Con notifications!\n");
      }

      if (dualPlayer->rightJoyCon.device->SubscribeNotification(
              "", std::string(INPUT_REPORT_UUID),
              [rightShared](const std::vector<uint8_t> &buffer) {
                std::lock_guard<std::mutex> lock(rightShared->mtx);
                rightShared->data = buffer;
              })) {
        TCOUT << TSTR("Right Joy-Con notifications enabled.\n");
      } else {
        TCERR << TSTR("Failed to enable right Joy-Con notifications!\n");
      }
#endif

      dualPlayer->updateThread = std::thread(
          [dualPlayerPtr = dualPlayer.get(), leftShared, rightShared]() {
            while (dualPlayerPtr->running.load(std::memory_order_acquire)) {
              std::vector<uint8_t> leftBuf, rightBuf;
              {
                std::lock_guard<std::mutex> lock(leftShared->mtx);
                leftBuf = leftShared->data;
              }
              {
                std::lock_guard<std::mutex> lock(rightShared->mtx);
                rightBuf = rightShared->data;
              }

              if (leftBuf.empty() || rightBuf.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
              }

              DS4_REPORT_EX report = GenerateDualJoyConDS4Report(
                  leftBuf, rightBuf, dualPlayerPtr->gyroSource);

#ifdef _WIN32
              vigem_target_ds4_update_ex(vigem_client,
                                         dualPlayerPtr->ds4Controller, report);
#else
              VirtualControllerReport mac_report;
              mac_report.left_stick_x = report.Report.bThumbLX;
              mac_report.left_stick_y = report.Report.bThumbLY;
              mac_report.right_stick_x = report.Report.bThumbRX;
              mac_report.right_stick_y = report.Report.bThumbRY;
              mac_report.buttons = report.Report.wButtons;
              mac_report.dpad = 0;
              mac_report.left_trigger = report.Report.bTriggerL;
              mac_report.right_trigger = report.Report.bTriggerR;
              mac_controller->UpdateReport(mac_report);
#endif
              std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
          });

      dualPlayers.push_back(std::move(dualPlayer));
      TCOUT << TSTR("Dual Joy-Cons connected and configured. Press Enter to "
                    "continue...\n");
      TSTRING dummy;
      TGETLINE(TCIN, dummy);
    } else if (config.controllerType == ProController) {
      TCOUT << TSTR("Please sync your Pro Controller now.\n");
      ConnectedJoyCon proController =
          WaitForJoyCon(TSTR("Waiting for Pro Controller..."));

#ifdef _WIN32
      PVIGEM_TARGET ds4_controller = vigem_target_ds4_alloc();
      auto ret = vigem_target_add(vigem_client, ds4_controller);
      if (!VIGEM_SUCCESS(ret)) {
        TCERR << TSTR("Failed to add DS4 controller target: 0x") << std::hex
              << ret << TSTR("\n");
        exit(1);
      }
      proController.inputChar.ValueChanged(
          [ds4_controller](GattCharacteristic const &,
                           GattValueChangedEventArgs const &args) {
            auto reader = DataReader::FromBuffer(args.CharacteristicValue());
            std::vector<uint8_t> buffer(reader.UnconsumedBufferLength());
            reader.ReadBytes(buffer);
            DS4_REPORT_EX report = GenerateProControllerReport(buffer);
            vigem_target_ds4_update_ex(vigem_client, ds4_controller, report);
          });
#else
      proController.device->SubscribeNotification(
          "", std::string(INPUT_REPORT_UUID),
          [](const std::vector<uint8_t> &buffer) {
            DS4_REPORT_EX report = GenerateProControllerReport(buffer);
            VirtualControllerReport mac_report;
            mac_report.left_stick_x = report.Report.bThumbLX;
            mac_report.left_stick_y = report.Report.bThumbLY;
            mac_report.right_stick_x = report.Report.bThumbRX;
            mac_report.right_stick_y = report.Report.bThumbRY;
            mac_report.buttons = report.Report.wButtons;
            mac_report.dpad = 0;
            mac_report.left_trigger = report.Report.bTriggerL;
            mac_report.right_trigger = report.Report.bTriggerR;
            mac_controller->UpdateReport(mac_report);
          });
#endif
      proPlayers.push_back({proController
#ifdef _WIN32
                            ,
                            ds4_controller
#endif
      });
      TCOUT << TSTR("Pro Controller connected. Press Enter to continue...\n");
      TSTRING dummy;
      TGETLINE(TCIN, dummy);
    }
  }

  TCOUT << TSTR("All players connected.\n");
  TCOUT << TSTR("- Press Enter to exit\n\n");

  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
#ifdef _WIN32
    if (_kbhit()) {
#else
    if (kbhit()) {
#endif
      TSTRING dummy;
      TGETLINE(TCIN, dummy);
      break;
    }
  }

  // Cleanup
  for (auto &dp : dualPlayers) {
    dp->running.store(false);
    if (dp->updateThread.joinable())
      dp->updateThread.join();
#ifdef _WIN32
    vigem_target_remove(vigem_client, dp->ds4Controller);
    vigem_target_free(dp->ds4Controller);
#endif
  }
  for (auto &sp : singlePlayers) {
#ifdef _WIN32
    vigem_target_remove(vigem_client, sp.ds4Controller);
    vigem_target_free(sp.ds4Controller);
#endif
  }
  for (auto &pp : proPlayers) {
#ifdef _WIN32
    vigem_target_remove(vigem_client, pp.ds4Controller);
    vigem_target_free(pp.ds4Controller);
#endif
  }

#ifdef _WIN32
  if (vigem_client) {
    vigem_disconnect(vigem_client);
    vigem_free(vigem_client);
  }
#endif

  return 0;
}
