#include "Blueprint.h"

UBlueprint::UBlueprint() {
    this->bRecompileOnLoad = true;
    this->ParentClass = NULL;
    this->PRIVATE_InnermostPreviousCDO = NULL;
    this->bHasBeenRegenerated = false;
    this->bIsRegeneratingOnLoad = false;
    this->SimpleConstructionScript = NULL;
    this->InheritableComponentHandler = NULL;
    this->BlueprintType = BPTYPE_Normal;
    this->BlueprintSystemVersion = 0;
    this->bNativize = false;
}


