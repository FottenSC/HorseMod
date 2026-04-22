#include "FontData.h"

FFontData::FFontData() {
    this->Hinting = EFontHinting::Default;
    this->LoadingPolicy = EFontLoadingPolicy::LazyLoad;
    this->FontFaceAsset = NULL;
}

