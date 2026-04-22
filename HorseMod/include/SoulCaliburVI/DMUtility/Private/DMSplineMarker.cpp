#include "DMSplineMarker.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=ArrowComponent -FallbackName=ArrowComponent
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=CameraComponent -FallbackName=CameraComponent

ADMSplineMarker::ADMSplineMarker(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<UArrowComponent>(TEXT("Root"));
    this->Direction = (UArrowComponent*)RootComponent;
    this->CameraViewer = CreateDefaultSubobject<UCameraComponent>(TEXT("CamView"));
    this->DesiredFOV = 50.00f;
    this->CameraViewer->SetupAttachment(RootComponent);
}


