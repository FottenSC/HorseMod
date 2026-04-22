#include "GameplayTagsSettings.h"

UGameplayTagsSettings::UGameplayTagsSettings() {
    this->ConfigFileName = TEXT("../../../SoulcaliburVI/Config/DefaultGameplayTags.ini");
    this->ImportTagsFromConfig = false;
    this->WarnOnInvalidTags = true;
    this->FastReplication = false;
    this->NumBitsForContainerSize = 6;
    this->NetIndexFirstBitSegment = 16;
}


