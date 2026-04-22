#include "StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

UStaticMeshComponent::UStaticMeshComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bBoundsChangeTriggersStreamingDataRebuild = true;
    this->bHasCustomNavigableGeometry = EHasCustomNavigableGeometry::Yes;
    this->ForcedLodModel = 0;
    this->PreviousLODLevel = 0;
    this->bOverrideMinLOD = false;
    this->MinLOD = 0;
    this->StaticMesh = NULL;
    this->bOverrideWireframeColor = false;
    this->bOverrideNavigationExport = false;
    this->bForceNavigationObstacle = true;
    this->bDisallowMeshPaintPerInstance = false;
    this->bIgnoreInstanceForTextureStreaming = false;
    this->bOverrideLightMapRes = false;
    this->OverriddenLightMapRes = 64;
    this->bCastDistanceFieldIndirectShadow = false;
    this->DistanceFieldIndirectShadowMinVisibility = 0.10f;
    this->bOverrideDistanceFieldSelfShadowBias = false;
    this->DistanceFieldSelfShadowBias = 0.00f;
    this->StreamingDistanceMultiplier = 1.00f;
    this->SubDivisionStepSize = 32;
    this->bUseSubDivisions = true;
    this->bUseDefaultCollision = false;
}

bool UStaticMeshComponent::SetStaticMesh(UStaticMesh* NewMesh) {
    return false;
}

void UStaticMeshComponent::SetForcedLodModel(int32 NewForcedLodModel) {
}

void UStaticMeshComponent::SetDistanceFieldSelfShadowBias(float NewValue) {
}

void UStaticMeshComponent::OnRep_StaticMesh(UStaticMesh* OldStaticMesh) {
}

void UStaticMeshComponent::GetLocalBounds(FVector& Min, FVector& Max) const {
}

void UStaticMeshComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    
    DOREPLIFETIME(UStaticMeshComponent, StaticMesh);
}


