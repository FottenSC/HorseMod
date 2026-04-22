#include "GarbageCollectionSettings.h"

UGarbageCollectionSettings::UGarbageCollectionSettings() {
    this->TimeBetweenPurgingPendingKillObjects = 60.00f;
    this->FlushStreamingOnGC = false;
    this->AllowParallelGC = true;
    this->CreateGCClusters = true;
    this->MergeGCClusters = false;
    this->ActorClusteringEnabled = true;
    this->BlueprintClusteringEnabled = false;
    this->UseDisregardForGCOnDedicatedServers = false;
    this->NumRetriesBeforeForcingGC = 0;
    this->MaxObjectsNotConsideredByGC = 0;
    this->SizeOfPermanentObjectPool = 0;
    this->MaxObjectsInGame = 2097152;
    this->MaxObjectsInEditor = 12582912;
}


