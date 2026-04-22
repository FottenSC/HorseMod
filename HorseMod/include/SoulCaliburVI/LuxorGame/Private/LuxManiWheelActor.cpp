#include "LuxManiWheelActor.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CriWareRuntime -ObjectName=AtomComponent -FallbackName=AtomComponent
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=StaticMeshComponent -FallbackName=StaticMeshComponent

ALuxManiWheelActor::ALuxManiWheelActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("root_set01_component"));
    this->rool_set01_component = (UStaticMeshComponent*)RootComponent;
    this->rool_set02_component = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("root_set02_component"));
    this->sound_component = CreateDefaultSubobject<UAtomComponent>(TEXT("AtomComponent0"));
    this->rool_set02_component->SetupAttachment(RootComponent);
    this->sound_component->SetupAttachment(RootComponent);
}

void ALuxManiWheelActor::EventActorEndOverlap(AActor* OverlappedActor, AActor* OtherActor) {
}

void ALuxManiWheelActor::EventActorBeginOverlap(AActor* OverlappedActor, AActor* OtherActor) {
}


