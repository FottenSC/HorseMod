#include "DMLockableActorBase.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=ArrowComponent -FallbackName=ArrowComponent
#include "DMLockableComponent.h"
#include "Net/UnrealNetwork.h"

ADMLockableActorBase::ADMLockableActorBase(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<UArrowComponent>(TEXT("CustomRoot"));
    this->CustomRoot = (UArrowComponent*)RootComponent;
    this->LockHandler = CreateDefaultSubobject<UDMLockableComponent>(TEXT("LockHandler"));
    this->NumLockers = 0;
    this->LockHandler->SetupAttachment(RootComponent);
}




void ADMLockableActorBase::OnSetLock(AActor* inLocker) {
}

void ADMLockableActorBase::OnReleaseLock(AActor* inLocker) {
}




void ADMLockableActorBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    
    DOREPLIFETIME(ADMLockableActorBase, Lockers);
}


