#pragma once
#include "CoreMinimal.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCacheTrack.h"
#include "GeometryCacheTrack_TransformGroupAnimation.generated.h"

UCLASS(Blueprintable, CollapseCategories)
class GEOMETRYCACHE_API UGeometryCacheTrack_TransformGroupAnimation : public UGeometryCacheTrack {
    GENERATED_BODY()
public:
    UGeometryCacheTrack_TransformGroupAnimation();

    UFUNCTION(BlueprintCallable)
    void SetMesh(const FGeometryCacheMeshData& NewMeshData);
    
};

