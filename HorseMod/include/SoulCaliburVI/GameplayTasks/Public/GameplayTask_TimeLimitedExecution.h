#pragma once
#include "CoreMinimal.h"
#include "GameplayTask.h"
#include "GameplayTask_TimeLimitedExecution.generated.h"

UCLASS(Blueprintable, MinimalAPI)
class UGameplayTask_TimeLimitedExecution : public UGameplayTask {
    GENERATED_BODY()
public:
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTaskFinishDelegate);
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FTaskFinishDelegate OnFinished;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FTaskFinishDelegate OnTimeExpired;
    
    UGameplayTask_TimeLimitedExecution();

};

