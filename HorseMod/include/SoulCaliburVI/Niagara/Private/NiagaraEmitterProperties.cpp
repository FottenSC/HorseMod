#include "NiagaraEmitterProperties.h"

UNiagaraEmitterProperties::UNiagaraEmitterProperties() {
    this->SpawnRate = 50.00f;
    this->bLocalSpace = false;
    this->Material = NULL;
    this->StartTime = 0.00f;
    this->EndTime = 0.00f;
    this->NumLoops = 0;
    this->CollisionMode = ENiagaraCollisionMode::None;
    this->RendererProperties = NULL;
    this->bInterpolatedSpawning = false;
}


