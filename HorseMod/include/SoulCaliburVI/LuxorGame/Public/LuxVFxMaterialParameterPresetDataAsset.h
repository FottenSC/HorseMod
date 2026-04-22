#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=DataAsset -FallbackName=DataAsset
#include "LuxVFxMaterialParamsInfo.h"
#include "LuxVFxMaterialParameterPresetDataAsset.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxVFxMaterialParameterPresetDataAsset : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FLuxVFxMaterialParamsInfo> MaterialParamsPresetInfoList;
    
    ULuxVFxMaterialParameterPresetDataAsset();

};

