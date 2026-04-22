#pragma once
#include "CoreMinimal.h"
#include "Channel.h"
#include "ActorChannel.generated.h"

class AActor;

UCLASS(Blueprintable, NonTransient)
class ENGINE_API UActorChannel : public UChannel {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    AActor* Actor;
    
    UActorChannel();

};

