#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxGameSaveObject.generated.h"

UCLASS(Abstract, Blueprintable)
class LUXORGAME_API ULuxGameSaveObject : public UObject {
    GENERATED_BODY()
public:
    ULuxGameSaveObject();

    UFUNCTION(BlueprintCallable)
    void MarkDirtyForBP(int32 InFlags);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    int32 GetDirtyFlagsForBP() const;
    
    UFUNCTION(BlueprintCallable)
    void ClearDirtyForBP();
    
};

