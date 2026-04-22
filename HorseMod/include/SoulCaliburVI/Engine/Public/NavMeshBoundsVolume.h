#pragma once
#include "CoreMinimal.h"
#include "NavAgentSelector.h"
#include "Volume.h"
#include "NavMeshBoundsVolume.generated.h"

UCLASS(Blueprintable, MinimalAPI)
class ANavMeshBoundsVolume : public AVolume {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FNavAgentSelector SupportedAgents;
    
    ANavMeshBoundsVolume(const FObjectInitializer& ObjectInitializer);

};

