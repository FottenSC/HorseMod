#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=DataAsset -FallbackName=DataAsset
#include "TextureData.h"
#include "StoreContentDataAsset.generated.h"

UCLASS(Blueprintable)
class ONLINESUBSYSTEMUTILS_API UStoreContentDataAsset : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FString StoreConfig;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FTextureData> StoreContentTextureArray;
    
    UStoreContentDataAsset();

};

