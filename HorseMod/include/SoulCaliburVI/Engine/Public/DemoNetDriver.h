#pragma once
#include "CoreMinimal.h"
#include "NetDriver.h"
#include "RollbackNetStartupActorInfo.h"
#include "DemoNetDriver.generated.h"

UCLASS(Blueprintable, NonTransient, Config=Engine)
class ENGINE_API UDemoNetDriver : public UNetDriver {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TMap<FString, FRollbackNetStartupActorInfo> RollbackNetStartupActors;
    
private:
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    float CheckpointSaveMaxMSPerFrame;
    
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bIsLocalReplay;
    
    UDemoNetDriver();

};

