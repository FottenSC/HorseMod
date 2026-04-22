#include "LuxDemoEmitter.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=ParticleSystemComponent -FallbackName=ParticleSystemComponent
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=SceneComponent -FallbackName=SceneComponent

ALuxDemoEmitter::ALuxDemoEmitter(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CustomRoot0"));
    this->ParticleSystem = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("ParticleSystemComponent0"));
    this->DemoCharacter = NULL;
    this->AttachBone = ELuxDemoEmitterAttachBone::EAB_BASE;
    this->ParticleSystem->SetupAttachment(RootComponent);
}


