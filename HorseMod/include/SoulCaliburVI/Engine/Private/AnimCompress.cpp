#include "AnimCompress.h"

UAnimCompress::UAnimCompress() {
    this->Description = TEXT("None");
    this->bNeedsSkeleton = false;
    this->TranslationCompressionFormat = ACF_None;
    this->RotationCompressionFormat = ACF_Float96NoW;
    this->ScaleCompressionFormat = ACF_None;
    this->MaxCurveError = 0.00f;
}


