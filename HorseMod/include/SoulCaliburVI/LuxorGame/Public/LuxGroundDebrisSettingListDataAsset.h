#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=DataAsset -FallbackName=DataAsset
#include "LuxGroundDebrisSettingListItem.h"
#include "LuxGroundDebrisSettingListDataAsset.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxGroundDebrisSettingListDataAsset : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FLuxGroundDebrisSettingListItem> GDSettingList;
    
    ULuxGroundDebrisSettingListDataAsset();

};

