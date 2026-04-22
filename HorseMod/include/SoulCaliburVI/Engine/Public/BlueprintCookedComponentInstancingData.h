#pragma once
#include "CoreMinimal.h"
#include "BlueprintComponentChangedPropertyInfo.h"
#include "BlueprintCookedComponentInstancingData.generated.h"

USTRUCT(BlueprintType)
struct ENGINE_API FBlueprintCookedComponentInstancingData {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bIsValid;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FBlueprintComponentChangedPropertyInfo> ChangedPropertyList;
    
    FBlueprintCookedComponentInstancingData();
};

