#pragma once
#include "CoreMinimal.h"
#include "PrimaryAssetRules.h"
#include "PrimaryDataAsset.h"
#include "PrimaryAssetLabel.generated.h"

class UObject;

UCLASS(Blueprintable, MinimalAPI)
class UPrimaryAssetLabel : public UPrimaryDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FPrimaryAssetRules Rules;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bLabelAssetsInMyDirectory: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bIsRuntimeLabel: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<UObject*> ExplicitAssets;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<UClass*> ExplicitBlueprints;
    
    UPrimaryAssetLabel();

};

