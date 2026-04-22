#include "LuxAssetPathsBase.h"

ULuxAssetPathsBase::ULuxAssetPathsBase() {
}

TArray<FStringAssetReference> ULuxAssetPathsBase::GetUAssetPaths() const {
    return TArray<FStringAssetReference>();
}

TArray<FString> ULuxAssetPathsBase::GetRAssetPaths() const {
    return TArray<FString>();
}


