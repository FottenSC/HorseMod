#include "GameMapsSettings.h"

UGameMapsSettings::UGameMapsSettings() {
    this->bUseSplitscreen = false;
    this->TwoPlayerSplitscreenLayout = ETwoPlayerSplitScreenType::Horizontal;
    this->ThreePlayerSplitscreenLayout = EThreePlayerSplitScreenType::FavorTop;
    this->bOffsetPlayerGamepadIds = false;
}


