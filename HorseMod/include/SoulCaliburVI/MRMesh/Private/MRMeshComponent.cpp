#include "MRMeshComponent.h"

UMRMeshComponent::UMRMeshComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->Material = NULL;
    this->MeshReconstructor = NULL;
    this->bEnableCollision = false;
}

UMeshReconstructorBase* UMRMeshComponent::GetReconstructor() const {
    return NULL;
}

void UMRMeshComponent::ConnectReconstructor(UMeshReconstructorBase* Reconstructor) {
}


