#pragma once
#include "CoreMinimal.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCacheTrack.h"
#include "GeometryCacheTrack_TransformAnimation.generated.h"

UCLASS(Blueprintable, CollapseCategories)
class GEOMETRYCACHE_API UGeometryCacheTrack_TransformAnimation : public UGeometryCacheTrack {
    GENERATED_BODY()
public:
    UGeometryCacheTrack_TransformAnimation();

    UFUNCTION(BlueprintCallable)
    void SetMesh(const FGeometryCacheMeshData& NewMeshData);
    
};

