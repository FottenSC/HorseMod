#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=InterpTrackFloatBase -FallbackName=InterpTrackFloatBase
#include "MatineeTrackAtomFloatBase.generated.h"

UCLASS(Abstract, Blueprintable, CollapseCategories, MinimalAPI)
class UMatineeTrackAtomFloatBase : public UInterpTrackFloatBase {
    GENERATED_BODY()
public:
    UMatineeTrackAtomFloatBase();

};

