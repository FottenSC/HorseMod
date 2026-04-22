#include "ParticleModuleRequired.h"

UParticleModuleRequired::UParticleModuleRequired() {
    this->bSpawnModule = true;
    this->bUpdateModule = true;
    this->Material = NULL;
    this->ScreenAlignment = PSA_Square;
    this->MinFacingCameraBlendDistance = 0.00f;
    this->MaxFacingCameraBlendDistance = 0.00f;
    this->bUseLocalSpace = false;
    this->bKillOnDeactivate = false;
    this->bKillOnCompleted = false;
    this->SortMode = PSORTMODE_None;
    this->bUseLegacyEmitterTime = true;
    this->bRemoveHMDRoll = false;
    this->EmitterDuration = 1.00f;
    this->EmitterDurationLow = 0.00f;
    this->bEmitterDurationUseRange = false;
    this->bDurationRecalcEachLoop = false;
    this->EmitterLoops = 0;
    this->ParticleBurstMethod = EPBM_Instant;
    this->EmitterDelay = 0.00f;
    this->EmitterDelayLow = 0.00f;
    this->bEmitterDelayUseRange = false;
    this->bDelayFirstLoopOnly = false;
    this->InterpolationMethod = PSUVIM_None;
    this->SubImages_Horizontal = 1;
    this->SubImages_Vertical = 1;
    this->bScaleUV = false;
    this->RandomImageTime = 0.00f;
    this->RandomImageChanges = 0;
    this->bOverrideSystemMacroUV = false;
    this->MacroUVRadius = 0.00f;
    this->bUseMaxDrawCount = true;
    this->MaxDrawCount = 500;
    this->UVFlippingMode = EParticleUVFlipMode::None;
    this->CutoutTexture = NULL;
    this->BoundingMode = BVC_EightVertices;
    this->OpacitySourceMode = OSM_Alpha;
    this->AlphaThreshold = 0.10f;
    this->EmitterNormalsMode = ENM_CameraFacing;
    this->bOrbitModuleAffectsVelocityAlignment = false;
}


