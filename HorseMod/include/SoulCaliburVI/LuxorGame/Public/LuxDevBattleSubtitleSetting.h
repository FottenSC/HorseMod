#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxDevBattleSubtitleSetting.generated.h"

UCLASS(Blueprintable, Config=DevOnly)
class ULuxDevBattleSubtitleSetting : public UObject {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool SubtitleVisible;
    
    ULuxDevBattleSubtitleSetting();

};

