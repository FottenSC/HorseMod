#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxReplayListItemData.h"
#include "LuxReplayListUtil.generated.h"

class ULuxorMatchData;

UCLASS(Blueprintable)
class LUXORGAME_API ULuxReplayListUtil : public UObject {
    GENERATED_BODY()
public:
    ULuxReplayListUtil();

    UFUNCTION(BlueprintCallable, BlueprintPure)
    static TArray<uint8> GetRaw(const FLuxReplayListItemData& ReplayListItemData);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool GetData(const TArray<uint8>& Raw, FLuxReplayListItemData& OutItemData);
    
    UFUNCTION(BlueprintCallable)
    static FLuxReplayListItemData CreateReplayListItemData(const ULuxorMatchData* matchData);
    
};

