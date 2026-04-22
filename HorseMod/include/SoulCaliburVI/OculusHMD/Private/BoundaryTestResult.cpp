#include "BoundaryTestResult.h"

FBoundaryTestResult::FBoundaryTestResult() {
    this->IsTriggering = false;
    this->DeviceType = ETrackedDeviceType::None;
    this->ClosestDistance = 0.00f;
}

