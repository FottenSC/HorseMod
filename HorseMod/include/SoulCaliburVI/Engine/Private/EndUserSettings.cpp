#include "EndUserSettings.h"

UEndUserSettings::UEndUserSettings() {
    this->bSendAnonymousUsageDataToEpic = false;
    this->bSendMeanTimeBetweenFailureDataToEpic = false;
    this->bAllowUserIdInUsageData = false;
}


