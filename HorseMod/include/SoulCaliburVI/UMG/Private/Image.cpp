#include "Image.h"

UImage::UImage() {
    this->Image = NULL;
}

void UImage::SetOpacity(float InOpacity) {
}

void UImage::SetColorAndOpacity(FLinearColor InColorAndOpacity) {
}

void UImage::SetBrushFromTextureDynamic(UTexture2DDynamic* Texture, bool bMatchSize) {
}

void UImage::SetBrushFromTexture(UTexture2D* Texture, bool bMatchSize) {
}

void UImage::SetBrushFromMaterial(UMaterialInterface* Material) {
}

void UImage::SetBrushFromAsset(USlateBrushAsset* Asset) {
}

void UImage::SetBrush(const FSlateBrush& InBrush) {
}

UMaterialInstanceDynamic* UImage::GetDynamicMaterial() {
    return NULL;
}


