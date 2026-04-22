#include "AssetManager.h"

UAssetManager::UAssetManager() {
    this->bIsGlobalAsyncScanEnvironment = false;
    this->bShouldGuessTypeAndName = false;
    this->bShouldUseSynchronousLoad = false;
    this->bIsBulkScanning = false;
    this->bIsManagementDatabaseCurrent = false;
    this->bUpdateManagementDatabaseAfterScan = false;
    this->NumberOfSpawnedNotifications = 0;
}


