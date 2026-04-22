#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxDevSessionChannelSetting.generated.h"

UCLASS(Blueprintable, Config=GameUserSettings)
class LUXORSESSIONUTIL_API ULuxDevSessionChannelSetting : public UObject {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 mChannel;
    
    ULuxDevSessionChannelSetting();

};

