#include "PrimitiveWidget.h"

UPrimitiveWidget::UPrimitiveWidget() {
    this->Scale = 0.00f;
}

void UPrimitiveWidget::SetScale(float InScale) {
}

void UPrimitiveWidget::SetBrushFromTexture(UTexture2D* Texture, bool bMatchSize) {
}

void UPrimitiveWidget::SetBrushFromMaterial(UMaterialInterface* Material) {
}

void UPrimitiveWidget::SetBrushFromAsset(USlateBrushAsset* Asset) {
}

void UPrimitiveWidget::SetBrush(const FSlateBrush& InBrush) {
}

UMaterialInstanceDynamic* UPrimitiveWidget::GetDynamicMaterial() {
    return NULL;
}


