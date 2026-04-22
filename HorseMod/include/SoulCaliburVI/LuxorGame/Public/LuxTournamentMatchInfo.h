#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxTournamentMatchInfo.generated.h"

UCLASS(Blueprintable)
class ULuxTournamentMatchInfo : public UObject {
    GENERATED_BODY()
public:
    ULuxTournamentMatchInfo();

    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsSameMatch(const FString& InMatchId) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    int32 GetRound() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    int32 GetPosition() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FString GetOpponentDisplayName() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FString GetMatchId() const;
    
};

