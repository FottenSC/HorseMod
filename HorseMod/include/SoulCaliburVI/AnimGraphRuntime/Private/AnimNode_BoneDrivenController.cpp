#include "AnimNode_BoneDrivenController.h"

FAnimNode_BoneDrivenController::FAnimNode_BoneDrivenController() {
    this->SourceComponent = EComponentType::None;
    this->DrivingCurve = NULL;
    this->Multiplier = 0.00f;
    this->bUseRange = false;
    this->RangeMin = 0.00f;
    this->RangeMax = 0.00f;
    this->RemappedMin = 0.00f;
    this->RemappedMax = 0.00f;
    this->DestinationMode = EDrivenDestinationMode::Bone;
    this->TargetComponent = EComponentType::None;
    this->bAffectTargetTranslationX = false;
    this->bAffectTargetTranslationY = false;
    this->bAffectTargetTranslationZ = false;
    this->bAffectTargetRotationX = false;
    this->bAffectTargetRotationY = false;
    this->bAffectTargetRotationZ = false;
    this->bAffectTargetScaleX = false;
    this->bAffectTargetScaleY = false;
    this->bAffectTargetScaleZ = false;
    this->ModificationMode = EDrivenBoneModificationMode::AddToInput;
}

