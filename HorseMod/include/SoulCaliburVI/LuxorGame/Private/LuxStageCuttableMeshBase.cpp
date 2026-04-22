#include "LuxStageCuttableMeshBase.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CriWareRuntime -ObjectName=AtomComponent -FallbackName=AtomComponent
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=StaticMeshComponent -FallbackName=StaticMeshComponent
//CROSS-MODULE INCLUDE V2: -ModuleName=ProceduralMeshComponent -ObjectName=ProceduralMeshComponent -FallbackName=ProceduralMeshComponent

ALuxStageCuttableMeshBase::ALuxStageCuttableMeshBase(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("InitialStaticMesh"));
    this->MeshComponent = (UStaticMeshComponent*)RootComponent;
    this->ProceduralMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CuttableProceduralMesh"));
    this->SliceSEComponent = CreateDefaultSubobject<UAtomComponent>(TEXT("SliceSE"));
    this->SliceParticle = NULL;
    this->SliceLeafParticle = NULL;
    this->SLeapRate = 0.70f;
    this->TranslucentMaterial = NULL;
    this->TransparentMaterial = NULL;
    this->FadeFrame = 0;
    this->MinCuttableDistance = 10.00f;
    this->TestEnabled = false;
    this->MID_CuttedMaterial = NULL;
    this->Cutted_ProceduralMeshComponent = NULL;
    this->ProceduralMeshComponent->SetupAttachment(RootComponent);
    this->SliceSEComponent->SetupAttachment(RootComponent);
}

void ALuxStageCuttableMeshBase::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult) {
}

void ALuxStageCuttableMeshBase::Initialize() {
}


