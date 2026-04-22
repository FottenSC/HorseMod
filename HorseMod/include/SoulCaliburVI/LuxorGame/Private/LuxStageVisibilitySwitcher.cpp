#include "LuxStageVisibilitySwitcher.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=ArrowComponent -FallbackName=ArrowComponent

ALuxStageVisibilitySwitcher::ALuxStageVisibilitySwitcher(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<UArrowComponent>(TEXT("ArrowComponent"));
    this->ArrowComponent = (UArrowComponent*)RootComponent;
    this->VisibilityCheckType = ELuxStageVisibilityCheckType::SVC_DIRECTION;
    this->AngleThresholdOffset = 0.00f;
    this->BackAngleThresholdOffset = 0.50f;
    this->DistanceThreshold = -1.00f;
    this->EnablePositionCheck = false;
}

void ALuxStageVisibilitySwitcher::SetEnableVisibilityCheck(bool Enabled) {
}


