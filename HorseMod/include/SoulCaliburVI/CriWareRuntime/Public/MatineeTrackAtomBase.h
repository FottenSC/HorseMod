#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=InterpTrack -FallbackName=InterpTrack
#include "MatineeTrackAtomBase.generated.h"

UCLASS(Abstract, Blueprintable, CollapseCategories, MinimalAPI)
class UMatineeTrackAtomBase : public UInterpTrack {
    GENERATED_BODY()
public:
    UMatineeTrackAtomBase();

};

