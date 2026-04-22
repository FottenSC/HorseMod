#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "MediaSource.generated.h"

UCLASS(Abstract, Blueprintable)
class MEDIAASSETS_API UMediaSource : public UObject {
    GENERATED_BODY()
public:
    UMediaSource();

    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool Validate() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FString GetUrl() const;
    
};

