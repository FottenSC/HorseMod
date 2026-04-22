#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=DataAsset -FallbackName=DataAsset
#include "LuxBattleDramaticVoiceMatchData.h"
#include "LuxDramaticVoiceDataAsset.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxDramaticVoiceDataAsset : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FLuxBattleDramaticVoiceMatchData> MatchDataArray;
    
    ULuxDramaticVoiceDataAsset();

};

