#include "RetainerBox.h"

URetainerBox::URetainerBox() {
    this->Phase = 0;
    this->PhaseCount = 1;
    this->EffectMaterial = NULL;
    this->TextureParameter = TEXT("Texture");
}

void URetainerBox::SetTextureParameter(FName NewTextureParameter) {
}

void URetainerBox::SetEffectMaterial(UMaterialInterface* NewEffectMaterial) {
}

UMaterialInstanceDynamic* URetainerBox::GetEffectMaterial() const {
    return NULL;
}


