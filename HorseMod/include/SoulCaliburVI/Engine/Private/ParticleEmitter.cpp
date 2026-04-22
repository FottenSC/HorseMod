#include "ParticleEmitter.h"

UParticleEmitter::UParticleEmitter() {
    this->EmitterName = TEXT("Particle Emitter");
    this->SubUVDataOffset = 0;
    this->EmitterRenderMode = ERM_Normal;
    this->ConvertedModules = true;
    this->PeakActiveParticles = 0;
    this->InitialAllocationCount = 0;
    this->MediumDetailSpawnRateScale = 0.00f;
    this->QualityLevelSpawnRateScale = 1.00f;
    this->DetailMode = DM_Low;
    this->bIsSoloing = false;
    this->bCookedOut = false;
    this->bDisabledLODsKeepEmitterAlive = false;
    this->bDisableWhenInsignficant = false;
    this->SignificanceLevel = EParticleSignificanceLevel::Critical;
}


