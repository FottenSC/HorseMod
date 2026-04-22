#include "AnimCompress_Automatic.h"

UAnimCompress_Automatic::UAnimCompress_Automatic() {
    this->Description = TEXT("Automatic");
    this->MaxEndEffectorError = 1.00f;
    this->bTryFixedBitwiseCompression = true;
    this->bTryPerTrackBitwiseCompression = true;
    this->bTryLinearKeyRemovalCompression = true;
    this->bTryIntervalKeyRemoval = true;
    this->bRunCurrentDefaultCompressor = true;
    this->bAutoReplaceIfExistingErrorTooGreat = false;
    this->bRaiseMaxErrorToExisting = false;
}


