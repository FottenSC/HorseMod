#include "PlayerState.h"
#include "EngineMessage.h"
#include "Net/UnrealNetwork.h"

APlayerState::APlayerState(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bAlwaysRelevant = true;
    this->bNetLoadOnClient = false;
    this->bReplicates = true;
    const FProperty* p_RemoteRole = GetClass()->FindPropertyByName("RemoteRole");
    (*p_RemoteRole->ContainerPtrToValuePtr<TEnumAsByte<ENetRole>>(this)) = ROLE_SimulatedProxy;
    this->Score = 0.00f;
    this->Ping = 0;
    this->PlayerId = 0;
    this->bIsSpectator = false;
    this->bOnlySpectator = false;
    this->bIsABot = false;
    this->bIsInactive = false;
    this->bFromPreviousLevel = false;
    this->StartTime = 0;
    this->EngineMessageClass = UEngineMessage::StaticClass();
}



void APlayerState::OnRep_UniqueId() {
}

void APlayerState::OnRep_Score() {
}

void APlayerState::OnRep_PlayerName() {
}

void APlayerState::OnRep_bIsInactive() {
}

void APlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    
    DOREPLIFETIME(APlayerState, Score);
    DOREPLIFETIME(APlayerState, Ping);
    DOREPLIFETIME(APlayerState, PlayerName);
    DOREPLIFETIME(APlayerState, PlayerId);
    DOREPLIFETIME(APlayerState, bIsSpectator);
    DOREPLIFETIME(APlayerState, bOnlySpectator);
    DOREPLIFETIME(APlayerState, bIsABot);
    DOREPLIFETIME(APlayerState, bIsInactive);
    DOREPLIFETIME(APlayerState, bFromPreviousLevel);
    DOREPLIFETIME(APlayerState, StartTime);
    DOREPLIFETIME(APlayerState, UniqueId);
}


