#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=PrimaryAssetId -FallbackName=PrimaryAssetId
#include "AsyncActionLoadPrimaryAssetBase.h"
#include "OnPrimaryAssetListLoadedDelegate.h"
#include "AsyncActionLoadPrimaryAssetList.generated.h"

class UAsyncActionLoadPrimaryAssetList;

UCLASS(Blueprintable)
class UAsyncActionLoadPrimaryAssetList : public UAsyncActionLoadPrimaryAssetBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnPrimaryAssetListLoaded Completed;
    
    UAsyncActionLoadPrimaryAssetList();

    UFUNCTION(BlueprintCallable)
    static UAsyncActionLoadPrimaryAssetList* AsyncLoadPrimaryAssetList(const TArray<FPrimaryAssetId>& PrimaryAssetList, const TArray<FName>& LoadBundles);
    
};

