#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=DataAsset -FallbackName=DataAsset
#include "LuxBusDackingItem.h"
#include "LuxReversalEdgeDackingItem.h"
#include "LuxBusDackingDataAsset.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxBusDackingDataAsset : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FLuxBusDackingItem> BusDackingList;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FLuxReversalEdgeDackingItem> ReversalEdgeDackingList;
    
    ULuxBusDackingDataAsset();

};

