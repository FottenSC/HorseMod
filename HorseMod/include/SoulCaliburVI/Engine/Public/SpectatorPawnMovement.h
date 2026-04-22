#pragma once
#include "CoreMinimal.h"
#include "FloatingPawnMovement.h"
#include "SpectatorPawnMovement.generated.h"

UCLASS(Blueprintable, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class ENGINE_API USpectatorPawnMovement : public UFloatingPawnMovement {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bIgnoreTimeDilation: 1;
    
    USpectatorPawnMovement(const FObjectInitializer& ObjectInitializer);

};

