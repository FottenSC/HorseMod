#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=DataAsset -FallbackName=DataAsset
#include "LuxPostProcessSettingListItem.h"
#include "LuxPostProcessSettingListDataAsset.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxPostProcessSettingListDataAsset : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FLuxPostProcessSettingListItem> PostProcessSettingList;
    
    ULuxPostProcessSettingListDataAsset();

};

