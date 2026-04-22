#include "ParticleSystem.h"

UParticleSystem::UParticleSystem() {
    this->SystemUpdateMode = EPSUM_RealTime;
    this->UpdateTime_FPS = 60.00f;
    this->UpdateTime_Delta = 0.02f;
    this->WarmupTime = 0.00f;
    this->WarmupTickRate = 0.00f;
    this->PreviewComponent = NULL;
    this->CurveEdSetup = NULL;
    this->bOrientZAxisTowardCamera = false;
    this->LODDistanceCheckTime = 0.25f;
    this->LODMethod = PARTICLESYSTEMLODMETHOD_Automatic;
    this->bRegenerateLODDuplicate = false;
    this->bUseFixedRelativeBoundingBox = false;
    this->SecondsBeforeInactive = 0.00f;
    this->bShouldResetPeakCounts = false;
    this->bHasPhysics = false;
    this->bUseRealtimeThumbnail = false;
    this->ThumbnailImageOutOfDate = true;
    this->Delay = 0.00f;
    this->DelayLow = 0.00f;
    this->bUseDelayRange = false;
    this->bAutoDeactivate = true;
    this->MinTimeBetweenTicks = 0;
    this->InsignificantReaction = EParticleSystemInsignificanceReaction::Auto;
    this->InsignificanceDelay = 0.00f;
    this->MaxSignificanceLevel = EParticleSignificanceLevel::Critical;
    this->MacroUVRadius = 200.00f;
    this->OcclusionBoundsMethod = EPSOBM_ParticleBounds;
}

bool UParticleSystem::ContainsEmitterType(UClass* TypeData) {
    return false;
}


