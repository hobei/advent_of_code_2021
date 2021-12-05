#include <common.hpp>
#include <algorithm>
#include <poplar/IPUModel.hpp>

using namespace poplar;

bool useIpuModel = false;

poplar::Device GetIPUDevice() {
  
  if(useIpuModel) {
      std::cout << "Creating an IPU Model\n";
    IPUModel ipuModel;
    Device device = ipuModel.createDevice();
    return device;
  } else {
    auto manager = DeviceManager::createDeviceManager();
    auto devices = manager.getDevices(poplar::TargetType::IPU, 1);
    std::cout << "Trying to attach to IPU\n";
    auto it = std::find_if(devices.begin(), devices.end(), [](Device &device) {
        return device.attach();
    });

    if (it == devices.end()) {
        std::cerr << "Error attaching to device\n";
        exit(-1);
    }

    Device device = std::move(*it);
    std::cout << "Attached to IPU " << device.getId() << std::endl;
    return device;
  }
}