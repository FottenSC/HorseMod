#pragma once
#include "CoreMinimal.h"
#include "TextureData.generated.h"

USTRUCT(BlueprintType)
struct ONLINESUBSYSTEMUTILS_API FTextureData {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<uint8> Buffer;
    
    FTextureData();
};

