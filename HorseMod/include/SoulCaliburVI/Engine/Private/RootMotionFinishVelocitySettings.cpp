#include "RootMotionFinishVelocitySettings.h"

FRootMotionFinishVelocitySettings::FRootMotionFinishVelocitySettings() {
    this->mode = ERootMotionFinishVelocityMode::MaintainLastRootMotionVelocity;
    this->ClampVelocity = 0.00f;
}

