#pragma once
#include "CoreMinimal.h"
#include "SwarmDebugOptions.generated.h"

USTRUCT(BlueprintType)
struct FSwarmDebugOptions {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bDistributionEnabled: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bForceContentExport: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bInitialized: 1;
    
    ENGINE_API FSwarmDebugOptions();
};

