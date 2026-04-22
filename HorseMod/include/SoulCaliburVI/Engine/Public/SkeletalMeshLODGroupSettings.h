#pragma once
#include "CoreMinimal.h"
#include "SkeletalMeshOptimizationSettings.h"
#include "SkeletalMeshLODGroupSettings.generated.h"

USTRUCT(BlueprintType)
struct FSkeletalMeshLODGroupSettings {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float ScreenSize;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FSkeletalMeshOptimizationSettings OptimizationSettings;
    
public:
    ENGINE_API FSkeletalMeshLODGroupSettings();
};

