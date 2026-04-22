#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=DataAsset -FallbackName=DataAsset
#include "LuxTracePartsDataAssetList.generated.h"

class ULuxTracePartsDataAsset;

UCLASS(Blueprintable)
class LUXORGAME_API ULuxTracePartsDataAssetList : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<ULuxTracePartsDataAsset*> PartsDataAssetTable;
    
    ULuxTracePartsDataAssetList();

};

