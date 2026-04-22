#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIDataObject -FallbackName=UIDataObject
#include "LuxMenuSound.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxMenuSound : public UObject {
    GENERATED_BODY()
public:
    ULuxMenuSound();

    UFUNCTION(BlueprintCallable)
    void OnReceiveMenuEvent(const FUIDataObject& EventData);
    
    UFUNCTION(BlueprintCallable)
    void Initialize();
    
    UFUNCTION(BlueprintCallable)
    void Finalize();
    
};

