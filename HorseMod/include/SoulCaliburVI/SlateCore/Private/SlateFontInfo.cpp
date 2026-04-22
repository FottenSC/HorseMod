#include "SlateFontInfo.h"

FSlateFontInfo::FSlateFontInfo() {
    this->FontObject = NULL;
    this->FontMaterial = NULL;
    this->Size = 0;
    this->Hinting = EFontHinting::Default;
}

