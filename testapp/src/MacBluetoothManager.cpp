#include "MacBluetoothManager.h"
#include <chrono>
#include <iostream>
#include <thread>

MacBluetoothDevice::MacBluetoothDevice(SimpleBLE::Peripheral peripheral)
    : peripheral_(peripheral) {}

MacBluetoothDevice::~MacBluetoothDevice() { Disconnect(); }

std::string MacBluetoothDevice::GetAddress() { return peripheral_.address(); }

std::string MacBluetoothDevice::GetName() { return peripheral_.identifier(); }

bool MacBluetoothDevice::Connect() {
  try {
    peripheral_.connect();
    return true;
  } catch (...) {
    return false;
  }
}

void MacBluetoothDevice::Disconnect() {
  try {
    if (peripheral_.is_connected()) {
      peripheral_.disconnect();
    }
  } catch (...) {
  }
}

bool MacBluetoothDevice::WriteCharacteristic(const std::string &service_uuid,
                                             const std::string &char_uuid,
                                             const std::vector<uint8_t> &data) {
  try {
    peripheral_.write_request(service_uuid, char_uuid, data);
    return true;
  } catch (...) {
    return false;
  }
}

bool MacBluetoothDevice::SubscribeNotification(
    const std::string &service_uuid, const std::string &char_uuid,
    std::function<void(const std::vector<uint8_t> &)> callback) {
  try {
    peripheral_.notify(service_uuid, char_uuid,
                       [callback](SimpleBLE::ByteArray data) {
                         std::vector<uint8_t> buffer(data.begin(), data.end());
                         callback(buffer);
                       });
    return true;
  } catch (...) {
    return false;
  }
}

MacBluetoothManager::MacBluetoothManager() {}

MacBluetoothManager::~MacBluetoothManager() {}

bool MacBluetoothManager::Initialize() {
  adapters_ = SimpleBLE::Adapter::get_adapters();
  if (adapters_.empty()) {
    return false;
  }
  adapter_ = adapters_[0];
  initialized_ = true;
  return true;
}

std::shared_ptr<IBluetoothDevice> MacBluetoothManager::ScanAndConnect(
    const std::string &name_prefix, uint16_t manufacturer_id,
    const std::vector<uint8_t> &mfg_data_prefix) {
  if (!initialized_)
    return nullptr;

  std::shared_ptr<IBluetoothDevice> found_device = nullptr;
  std::mutex mtx;
  std::atomic<bool> done(false);

  adapter_.set_callback_on_scan_found([&](SimpleBLE::Peripheral peripheral) {
    if (done)
      return;

    bool match = false;

    // Check identifier
    if (peripheral.identifier().find(name_prefix) == 0) {
      match = true;
    }

    // Check manufacturer data
    std::map<uint16_t, SimpleBLE::ByteArray> mfg_data =
        peripheral.manufacturer_data();
    if (mfg_data.count(manufacturer_id)) {
      const auto &data = mfg_data[manufacturer_id];
      if (data.size() >= mfg_data_prefix.size()) {
        if (std::equal(mfg_data_prefix.begin(), mfg_data_prefix.end(),
                       data.begin())) {
          match = true;
        }
      }
    }

    if (match) {
      std::lock_guard<std::mutex> lock(mtx);
      if (!done) {
        found_device = std::make_shared<MacBluetoothDevice>(peripheral);
        done = true;
        adapter_.scan_stop();
      }
    }
  });

  adapter_.scan_start();

  // Wait for up to 30 seconds
  auto start = std::chrono::steady_clock::now();
  while (!done && (std::chrono::steady_clock::now() - start <
                   std::chrono::seconds(30))) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  adapter_.scan_stop();
  return found_device;
}
