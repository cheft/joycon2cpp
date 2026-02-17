#pragma once
#include <vector>
#include <string>
#include <functional>
#include <memory>

class IBluetoothDevice {
public:
    virtual ~IBluetoothDevice() = default;
    virtual std::string GetAddress() = 0;
    virtual std::string GetName() = 0;
    virtual bool Connect() = 0;
    virtual void Disconnect() = 0;
    virtual bool WriteCharacteristic(const std::string& service_uuid, const std::string& char_uuid, const std::vector<uint8_t>& data) = 0;
    virtual bool SubscribeNotification(const std::string& service_uuid, const std::string& char_uuid, std::function<void(const std::vector<uint8_t>&)> callback) = 0;
};

class IBluetoothManager {
public:
    virtual ~IBluetoothManager() = default;
    virtual bool Initialize() = 0;
    virtual std::shared_ptr<IBluetoothDevice> ScanAndConnect(const std::string& name_prefix, uint16_t manufacturer_id, const std::vector<uint8_t>& mfg_data_prefix) = 0;
};
