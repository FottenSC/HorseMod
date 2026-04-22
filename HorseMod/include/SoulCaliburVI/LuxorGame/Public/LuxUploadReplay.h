#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxUploadReplay.generated.h"

class ULuxorMatchData;

UCLASS(Blueprintable)
class LUXORGAME_API ULuxUploadReplay : public UObject {
    GENERATED_BODY()
public:
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLuxFlushReplayFileComplete, bool, bSuccessful);
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnLuxFlushReplayFileComplete OnLuxFlushReplayFileComplete;
    
    ULuxUploadReplay();

    UFUNCTION(BlueprintCallable)
    void Initalize();
    
    UFUNCTION(BlueprintCallable)
    void FlushReplayFile(const TArray<uint8>& inDataArray, ULuxorMatchData* matchData);
    
    UFUNCTION(BlueprintCallable)
    void Finalize();
    
};

