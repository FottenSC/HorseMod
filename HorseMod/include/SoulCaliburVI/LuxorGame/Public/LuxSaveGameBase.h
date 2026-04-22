#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=SaveGame -FallbackName=SaveGame
#include "LuxSaveGameBase.generated.h"

UCLASS(Abstract, Blueprintable)
class LUXORGAME_API ULuxSaveGameBase : public USaveGame {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame, meta=(AllowPrivateAccess=true))
    FString SlotName;
    
    ULuxSaveGameBase();

    UFUNCTION(BlueprintCallable)
    bool PerformSave(const FString& inSuffix);
    
};

