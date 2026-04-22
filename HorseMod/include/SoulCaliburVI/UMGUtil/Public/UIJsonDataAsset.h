#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=DataAsset -FallbackName=DataAsset
#include "UIDSCollectableDataInterface.h"
#include "UIDataObject.h"
#include "UIJsonDataAsset.generated.h"

UCLASS(Blueprintable)
class UMGUTIL_API UUIJsonDataAsset : public UDataAsset, public IUIDSCollectableDataInterface {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<uint8> Data;
    
public:
    UUIJsonDataAsset();

    UFUNCTION(BlueprintCallable, BlueprintPure)
    void Get(FUIDataObject& DataObject) const;
    

    // Fix for true pure virtual functions not being implemented
};

