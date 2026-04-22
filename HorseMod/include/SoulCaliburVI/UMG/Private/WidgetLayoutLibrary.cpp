#include "WidgetLayoutLibrary.h"

UWidgetLayoutLibrary::UWidgetLayoutLibrary() {
}

UVerticalBoxSlot* UWidgetLayoutLibrary::SlotAsVerticalBoxSlot(UWidget* Widget) {
    return NULL;
}

UUniformGridSlot* UWidgetLayoutLibrary::SlotAsUniformGridSlot(UWidget* Widget) {
    return NULL;
}

UOverlaySlot* UWidgetLayoutLibrary::SlotAsOverlaySlot(UWidget* Widget) {
    return NULL;
}

UHorizontalBoxSlot* UWidgetLayoutLibrary::SlotAsHorizontalBoxSlot(UWidget* Widget) {
    return NULL;
}

UGridSlot* UWidgetLayoutLibrary::SlotAsGridSlot(UWidget* Widget) {
    return NULL;
}

UCanvasPanelSlot* UWidgetLayoutLibrary::SlotAsCanvasSlot(UWidget* Widget) {
    return NULL;
}

UBorderSlot* UWidgetLayoutLibrary::SlotAsBorderSlot(UWidget* Widget) {
    return NULL;
}

void UWidgetLayoutLibrary::RemoveAllWidgets(UObject* WorldContextObject) {
}

bool UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(APlayerController* PlayerController, FVector WorldLocation, FVector2D& ScreenPosition) {
    return false;
}

FGeometry UWidgetLayoutLibrary::GetViewportWidgetGeometry(UObject* WorldContextObject) {
    return FGeometry{};
}

FVector2D UWidgetLayoutLibrary::GetViewportSize(UObject* WorldContextObject) {
    return FVector2D{};
}

float UWidgetLayoutLibrary::GetViewportScale(UObject* WorldContextObject) {
    return 0.0f;
}

FGeometry UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(APlayerController* PlayerController) {
    return FGeometry{};
}

bool UWidgetLayoutLibrary::GetMousePositionScaledByDPI(APlayerController* Player, float& LocationX, float& LocationY) {
    return false;
}

FVector2D UWidgetLayoutLibrary::GetMousePositionOnViewport(UObject* WorldContextObject) {
    return FVector2D{};
}

FVector2D UWidgetLayoutLibrary::GetMousePositionOnPlatform() {
    return FVector2D{};
}


