#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=ParticleModuleEventSendToGame -FallbackName=ParticleModuleEventSendToGame
#include "ELuxSEBankType.h"
#include "LuxParticleSoundEvent.generated.h"

UCLASS(Blueprintable, EditInlineNew)
class ULuxParticleSoundEvent : public UParticleModuleEventSendToGame {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELuxSEBankType BankType;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 CueId;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool UseLocation;
    
    ULuxParticleSoundEvent();

};

