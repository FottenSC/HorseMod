#pragma once
#include "CoreMinimal.h"
#include "LuxStageActorBase.h"
#include "LuxStageBreakableWallActor.generated.h"

class ULuxParticleSystemComponent;
class ULuxSkeletalMeshComponent;
class ULuxStageMeshComponent;
class ULuxStageSkeletalMeshComponent;
class UParticleSystem;
class UParticleSystemComponent;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class LUXORGAME_API ALuxStageBreakableWallActor : public ALuxStageActorBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UStaticMeshComponent* BaseMeshComponent;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    ULuxStageMeshComponent* BaseTranslucentMeshComponent;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UStaticMeshComponent* BrokenMeshComponent;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    ULuxStageMeshComponent* BrokenTranslucentMeshComponent;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    ULuxSkeletalMeshComponent* BreakingMeshComponent;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    ULuxStageSkeletalMeshComponent* BreakingTranslucentMeshComponent;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 ID;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 FadeFrame;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UParticleSystem* ParticleSystem;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    ULuxParticleSystemComponent* ParticleSystemComponent;
    
    ALuxStageBreakableWallActor(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void OnParticleSystemFinished(UParticleSystemComponent* FinishedPSComponent);
    
    UFUNCTION(BlueprintCallable)
    void Broken(bool Immediately);
    
};

