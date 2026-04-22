#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=DataAsset -FallbackName=DataAsset
#include "LuxTraceKindSetting.h"
#include "LuxTraceKindDataAssetList.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxTraceKindDataAssetList : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FLuxTraceKindSetting> KindSettingTable;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FLuxTraceKindSetting> KindSettingTableSaint;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FLuxTraceKindSetting> KindSettingTableEvil;
    
    ULuxTraceKindDataAssetList();

};

