#pragma once
#include "CoreMinimal.h"
#include "PaintedVertex.h"
#include "StaticMeshComponentLODInfo.generated.h"

USTRUCT(BlueprintType)
struct FStaticMeshComponentLODInfo {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FPaintedVertex> PaintedVertices;
    
    ENGINE_API FStaticMeshComponentLODInfo();
};

