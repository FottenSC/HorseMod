#include "WidgetComponent.h"

UWidgetComponent::UWidgetComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->Space = EWidgetSpace::World;
    this->TimingPolicy = EWidgetTimingPolicy::RealTime;
    this->WidgetClass = NULL;
    this->bManuallyRedraw = false;
    this->bRedrawRequested = true;
    this->RedrawTime = 0.00f;
    this->bDrawAtDesiredSize = false;
    this->bReceiveHardwareInput = false;
    this->bWindowFocusable = true;
    this->OwnerPlayer = NULL;
    this->OpacityFromTexture = 1.00f;
    this->BlendMode = EWidgetBlendMode::Masked;
    this->bIsTwoSided = false;
    this->TickWhenOffscreen = false;
    this->Widget = NULL;
    this->BodySetup = NULL;
    this->RenderTarget = NULL;
    this->MaterialInstance = NULL;
    this->bAddedToScreen = false;
    this->bEditTimeUsable = false;
    this->SharedLayerName = TEXT("WidgetComponentScreenLayer");
    this->LayerZOrder = -100;
    this->GeometryMode = EWidgetGeometryMode::Plane;
    this->CylinderArcAngle = 180.00f;
}

void UWidgetComponent::SetWidget(UUserWidget* NewWidget) {
}

void UWidgetComponent::SetOwnerPlayer(ULocalPlayer* LocalPlayer) {
}

void UWidgetComponent::SetDrawSize(FVector2D Size) {
}

void UWidgetComponent::SetBackgroundColor(const FLinearColor NewBackgroundColor) {
}

void UWidgetComponent::RequestRedraw() {
}

UUserWidget* UWidgetComponent::GetUserWidgetObject() const {
    return NULL;
}

UTextureRenderTarget2D* UWidgetComponent::GetRenderTarget() const {
    return NULL;
}

ULocalPlayer* UWidgetComponent::GetOwnerPlayer() const {
    return NULL;
}

UMaterialInstanceDynamic* UWidgetComponent::GetMaterialInstance() const {
    return NULL;
}

FVector2D UWidgetComponent::GetDrawSize() const {
    return FVector2D{};
}


