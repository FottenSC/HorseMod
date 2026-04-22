#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "EAppRegion.h"
#include "LuxRegionUtil.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxRegionUtil : public UObject {
    GENERATED_BODY()
public:
    ULuxRegionUtil();

    UFUNCTION(BlueprintCallable)
    static EAppRegion GetAppRegion();
    
};

