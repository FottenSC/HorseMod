#include "ParticleModuleTypeDataMesh.h"

UParticleModuleTypeDataMesh::UParticleModuleTypeDataMesh() {
    this->Mesh = NULL;
    this->CastShadows = false;
    this->DoCollisions = false;
    this->MeshAlignment = PSMA_MeshFaceCameraWithRoll;
    this->bOverrideMaterial = false;
    this->bOverrideDefaultMotionBlurSettings = false;
    this->bEnableMotionBlur = false;
    this->Pitch = 0.00f;
    this->Roll = 0.00f;
    this->Yaw = 0.00f;
    this->AxisLockOption = EPAL_NONE;
    this->bCameraFacing = false;
    this->CameraFacingUpAxisOption = CameraFacing_NoneUP;
    this->CameraFacingOption = XAxisFacing_NoUp;
    this->bApplyParticleRotationAsSpin = false;
    this->bFaceCameraDirectionRatherThanPosition = false;
    this->bCollisionsConsiderPartilceSize = true;
}


