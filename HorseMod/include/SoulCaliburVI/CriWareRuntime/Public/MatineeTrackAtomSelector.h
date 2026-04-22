#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=InterpTrack -FallbackName=InterpTrack
#include "AtomSelectorTrackKeyframe.h"
#include "MatineeTrackAtomSelector.generated.h"

UCLASS(Blueprintable, CollapseCategories, MinimalAPI)
class UMatineeTrackAtomSelector : public UInterpTrack {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, EditFixedSize, meta=(AllowPrivateAccess=true))
    TArray<FAtomSelectorTrackKeyframe> KeyframeList;
    
    UMatineeTrackAtomSelector();

};

