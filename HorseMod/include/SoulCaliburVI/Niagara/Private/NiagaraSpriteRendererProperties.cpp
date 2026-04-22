#include "NiagaraSpriteRendererProperties.h"

UNiagaraSpriteRendererProperties::UNiagaraSpriteRendererProperties() {
    this->Alignment = ENiagaraSpriteAlignment::Unaligned;
    this->FacingMode = ENiagaraSpriteFacingMode::FaceCamera;
    this->SortMode = ENiagaraSortMode::SortNone;
}


