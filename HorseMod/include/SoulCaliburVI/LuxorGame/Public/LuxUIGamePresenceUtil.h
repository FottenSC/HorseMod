#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
#include "ELuxGamePresence.h"
#include "LuxUIGamePresenceUtil.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxUIGamePresenceUtil : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    ULuxUIGamePresenceUtil();

    UFUNCTION(BlueprintCallable)
    static ELuxGamePresence ToEnum(const FString& InEnumString);
    
    UFUNCTION(BlueprintCallable)
    static void SetPresence(ELuxGamePresence InPresence);
    
};

