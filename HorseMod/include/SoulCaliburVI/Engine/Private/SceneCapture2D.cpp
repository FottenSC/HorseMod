#include "SceneCapture2D.h"
#include "DrawFrustumComponent.h"
#include "SceneCaptureComponent2D.h"

ASceneCapture2D::ASceneCapture2D(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->CaptureComponent2D = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("NewSceneCaptureComponent2D"));
    this->DrawFrustum = CreateDefaultSubobject<UDrawFrustumComponent>(TEXT("DrawFrust0"));
    this->CaptureComponent2D->SetupAttachment(RootComponent);
    this->DrawFrustum->SetupAttachment(RootComponent);
}

void ASceneCapture2D::OnInterpToggle(bool bEnable) {
}


