#include "PrimaryAssetRules.h"

FPrimaryAssetRules::FPrimaryAssetRules() {
    this->Priority = 0;
    this->bApplyRecursively = false;
    this->ChunkId = 0;
    this->CookRule = EPrimaryAssetCookRule::Unknown;
}

