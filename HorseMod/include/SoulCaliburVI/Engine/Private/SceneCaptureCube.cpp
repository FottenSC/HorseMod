#include "SceneCaptureCube.h"
#include "DrawFrustumComponent.h"
#include "SceneCaptureComponentCube.h"

ASceneCaptureCube::ASceneCaptureCube(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->CaptureComponentCube = CreateDefaultSubobject<USceneCaptureComponentCube>(TEXT("NewSceneCaptureComponentCube"));
    this->DrawFrustum = CreateDefaultSubobject<UDrawFrustumComponent>(TEXT("DrawFrust0"));
    this->CaptureComponentCube->SetupAttachment(RootComponent);
    this->DrawFrustum->SetupAttachment(RootComponent);
}

void ASceneCaptureCube::OnInterpToggle(bool bEnable) {
}


