#include "LuxUIAssetLoader.h"

ULuxUIAssetLoader::ULuxUIAssetLoader() {
    this->UIAssetPaths = NULL;
}

bool ULuxUIAssetLoader::IsCompleted() const {
    return false;
}

TArray<UObject*> ULuxUIAssetLoader::GetAssets() {
    return TArray<UObject*>();
}

UObject* ULuxUIAssetLoader::GetAsset(FName InAssetName) {
    return NULL;
}

ULuxUIAssetLoader* ULuxUIAssetLoader::CreateUIAssetLoader(ULuxUIAssetPaths* InUIAssetPaths) {
    return NULL;
}


