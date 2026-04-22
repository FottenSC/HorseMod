#include "MeshReconstructorBase.h"

UMeshReconstructorBase::UMeshReconstructorBase() {
}

void UMeshReconstructorBase::StopReconstruction() {
}

void UMeshReconstructorBase::StartReconstruction() {
}

void UMeshReconstructorBase::PauseReconstruction() {
}

bool UMeshReconstructorBase::IsReconstructionStarted() const {
    return false;
}

bool UMeshReconstructorBase::IsReconstructionPaused() const {
    return false;
}

void UMeshReconstructorBase::DisconnectMRMesh() {
}

FMRMeshConfiguration UMeshReconstructorBase::ConnectMRMesh(UMRMeshComponent* Mesh) {
    return FMRMeshConfiguration{};
}


