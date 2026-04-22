#pragma once
#include "CoreMinimal.h"
#include "NavigationDataChunk.h"
#include "RecastNavMeshDataChunk.generated.h"

UCLASS(Blueprintable)
class ENGINE_API URecastNavMeshDataChunk : public UNavigationDataChunk {
    GENERATED_BODY()
public:
    URecastNavMeshDataChunk();

};

