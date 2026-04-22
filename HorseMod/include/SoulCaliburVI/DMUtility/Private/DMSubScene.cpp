#include "DMSubScene.h"
#include "Templates/SubclassOf.h"

UDMSubScene::UDMSubScene() {
    this->bSuppressSubscene = false;
}

AActor* UDMSubScene::SpawnActorSubScene(TSubclassOf<AActor> InClass, const FTransform& InTransform) {
    return NULL;
}

void UDMSubScene::RequestCapture(UTextureRenderTarget2D* inRT, UMaterialInterface* inMat) {
}

bool UDMSubScene::IsBusy() {
    return false;
}


