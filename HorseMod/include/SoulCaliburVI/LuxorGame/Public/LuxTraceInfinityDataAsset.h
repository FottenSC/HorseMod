#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=DataAsset -FallbackName=DataAsset
#include "LuxTraceInfinitySetting.h"
#include "LuxTraceInfinityDataAsset.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxTraceInfinityDataAsset : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FLuxTraceInfinitySetting> InfinitySettingTable;
    
    ULuxTraceInfinityDataAsset();

};

