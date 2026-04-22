#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=PrimaryAssetId -FallbackName=PrimaryAssetId
#include "AsyncActionLoadPrimaryAssetBase.h"
#include "OnPrimaryAssetLoadedDelegate.h"
#include "AsyncActionLoadPrimaryAsset.generated.h"

class UAsyncActionLoadPrimaryAsset;

UCLASS(Blueprintable)
class UAsyncActionLoadPrimaryAsset : public UAsyncActionLoadPrimaryAssetBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnPrimaryAssetLoaded Completed;
    
    UAsyncActionLoadPrimaryAsset();

    UFUNCTION(BlueprintCallable)
    static UAsyncActionLoadPrimaryAsset* AsyncLoadPrimaryAsset(FPrimaryAssetId PrimaryAsset, const TArray<FName>& LoadBundles);
    
};

