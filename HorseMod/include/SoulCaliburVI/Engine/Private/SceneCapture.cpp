#include "SceneCapture.h"
#include "StaticMeshComponent.h"

ASceneCapture::ASceneCapture(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CamMesh0"));
    this->MeshComp = (UStaticMeshComponent*)RootComponent;
}


