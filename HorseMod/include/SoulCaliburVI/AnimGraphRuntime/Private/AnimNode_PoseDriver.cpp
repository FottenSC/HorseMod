#include "AnimNode_PoseDriver.h"

FAnimNode_PoseDriver::FAnimNode_PoseDriver() {
    this->bOnlyDriveSelectedBones = false;
    this->DriveSource = EPoseDriverSource::Rotation;
    this->DriveOutput = EPoseDriverOutput::DrivePoses;
    this->TwistAxis = BA_X;
    this->Type = EPoseDriverType::SwingAndTwist;
    this->RadialScaling = 0.00f;
}

