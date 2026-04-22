#pragma once
#include "CoreMinimal.h"
#include "DestructibleParameters.h"
#include "FractureEffect.h"
#include "SkeletalMesh.h"
#include "DestructibleMesh.generated.h"

UCLASS(Blueprintable, MinimalAPI)
class UDestructibleMesh : public USkeletalMesh {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FDestructibleParameters DefaultDestructibleParameters;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, EditFixedSize, meta=(AllowPrivateAccess=true))
    TArray<FFractureEffect> FractureEffects;
    
    UDestructibleMesh();

};

