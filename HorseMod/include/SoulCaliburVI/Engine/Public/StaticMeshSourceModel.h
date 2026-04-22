#pragma once
#include "CoreMinimal.h"
#include "MeshBuildSettings.h"
#include "MeshReductionSettings.h"
#include "StaticMeshSourceModel.generated.h"

USTRUCT(BlueprintType)
struct FStaticMeshSourceModel {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FMeshBuildSettings BuildSettings;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FMeshReductionSettings ReductionSettings;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float LODDistance;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float ScreenSize;
    
    ENGINE_API FStaticMeshSourceModel();
};

