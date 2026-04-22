#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=PrimaryAssetId -FallbackName=PrimaryAssetId
#include "AsyncActionLoadPrimaryAssetBase.h"
#include "OnPrimaryAssetClassListLoadedDelegate.h"
#include "AsyncActionLoadPrimaryAssetClassList.generated.h"

class UAsyncActionLoadPrimaryAssetClassList;

UCLASS(Blueprintable)
class UAsyncActionLoadPrimaryAssetClassList : public UAsyncActionLoadPrimaryAssetBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnPrimaryAssetClassListLoaded Completed;
    
    UAsyncActionLoadPrimaryAssetClassList();

    UFUNCTION(BlueprintCallable)
    static UAsyncActionLoadPrimaryAssetClassList* AsyncLoadPrimaryAssetClassList(const TArray<FPrimaryAssetId>& PrimaryAssetList, const TArray<FName>& LoadBundles);
    
};

