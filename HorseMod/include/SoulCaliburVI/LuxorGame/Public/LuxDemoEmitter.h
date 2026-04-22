#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=Actor -FallbackName=Actor
#include "ELuxDemoEmitterAttachBone.h"
#include "LuxDemoEmitter.generated.h"

class ALuxDemoHumanActor;
class UParticleSystemComponent;

UCLASS(Blueprintable)
class LUXORGAME_API ALuxDemoEmitter : public AActor {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UParticleSystemComponent* ParticleSystem;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ALuxDemoHumanActor* DemoCharacter;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELuxDemoEmitterAttachBone AttachBone;
    
    ALuxDemoEmitter(const FObjectInitializer& ObjectInitializer);

};

