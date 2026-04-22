#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=DataAsset -FallbackName=DataAsset
#include "LuxParticleSystemSettingListItem.h"
#include "LuxParticleSystemSettingListDataAsset.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxParticleSystemSettingListDataAsset : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FLuxParticleSystemSettingListItem> PSSettingList;
    
    ULuxParticleSystemSettingListDataAsset();

};

