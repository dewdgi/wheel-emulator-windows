#include "device_enumerator.h"
#include <iostream>

DeviceEnumerator::DeviceEnumerator(ScanCallback callback) {
    // No-op on Windows
}

DeviceEnumerator::~DeviceEnumerator() {
    Stop();
}

void DeviceEnumerator::Start() {
    // No-op
}

void DeviceEnumerator::Stop() {
    // No-op
}

void DeviceEnumerator::RequestScan(bool force) {
    // No-op
}

std::vector<std::string> DeviceEnumerator::EnumerateNow() const {
    return std::vector<std::string>();
}

void DeviceEnumerator::ThreadMain() {
    // No-op
}

std::vector<std::string> DeviceEnumerator::EnumerateEventNodes() {
    return std::vector<std::string>();
}
