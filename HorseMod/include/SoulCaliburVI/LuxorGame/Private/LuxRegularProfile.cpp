#include "LuxRegularProfile.h"
#include "ELuxBodyFrameType.h"
#include "ELuxBodyHeightType.h"
#include "ELuxRace.h"

ULuxRegularProfile::ULuxRegularProfile() {
    this->bodyHeight = ELuxBodyHeightType::Num;
    this->bodyFrameType = ELuxBodyFrameType::Num;
    this->bIgnoreBodySetting = true;
    this->Race = ELuxRace::ELR_Unknown;
    this->BodyScales.AddDefaulted(15);
    this->PARTS.AddDefaulted(25);
    this->partsColor.AddDefaulted(25);
    this->ExtraPartsSettings.AddDefaulted(3);
    this->sticker.AddDefaulted(13);
    this->Character = ELuxCharacter::ELC_MITSURUGI;
    this->SwingParams.AddDefaulted(2);
}


