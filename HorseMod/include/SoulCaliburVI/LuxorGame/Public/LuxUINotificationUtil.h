#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
#include "LuxUINotificationUtil.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxUINotificationUtil : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    ULuxUINotificationUtil();

    UFUNCTION(BlueprintCallable)
    static void Notify(const FString& InNoticeID, const FString& InCommon1, const FString& InCommon2, const FString& InCommon3, const FString& InCommon4);
    
};

