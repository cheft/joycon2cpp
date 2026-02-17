#pragma once
#include "IBluetoothManager.h"
#include <map>
#include <mutex>
#include <simpleble/SimpleBLE.h>

class MacBluetoothDevice : public IBluetoothDevice {
public:
  MacBluetoothDevice(SimpleBLE::Peripheral peripheral);
  ~MacBluetoothDevice() override;

  std::string GetAddress() override;
  std::string GetName() override;
  bool Connect() override;
  void Disconnect() override;
  bool WriteCharacteristic(const std::string &service_uuid,
                           const std::string &char_uuid,
                           const std::vector<uint8_t> &data) override;
  bool SubscribeNotification(
      const std::string &service_uuid, const std::string &char_uuid,
      std::function<void(const std::vector<uint8_t> &)> callback) override;

private:
  SimpleBLE::Peripheral peripheral_;
};

class MacBluetoothManager : public IBluetoothManager {
public:
  MacBluetoothManager();
  ~MacBluetoothManager() override;

  bool Initialize() override;
  std::shared_ptr<IBluetoothDevice>
  ScanAndConnect(const std::string &name_prefix, uint16_t manufacturer_id,
                 const std::vector<uint8_t> &mfg_data_prefix) override;

private:
  std::vector<SimpleBLE::Adapter> adapters_;
  SimpleBLE::Adapter adapter_;
  bool initialized_ = false;
};
