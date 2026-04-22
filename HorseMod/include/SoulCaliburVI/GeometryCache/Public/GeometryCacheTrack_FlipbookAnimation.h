#pragma once
#include "CoreMinimal.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCacheTrack.h"
#include "GeometryCacheTrack_FlipbookAnimation.generated.h"

UCLASS(Blueprintable, CollapseCategories)
class GEOMETRYCACHE_API UGeometryCacheTrack_FlipbookAnimation : public UGeometryCacheTrack {
    GENERATED_BODY()
public:
private:
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 NumMeshSamples;
    
public:
    UGeometryCacheTrack_FlipbookAnimation();

    UFUNCTION(BlueprintCallable)
    void AddMeshSample(const FGeometryCacheMeshData& MeshData, const float SampleTime);
    
};

