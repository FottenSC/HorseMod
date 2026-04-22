#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxTexturePrintParam.h"
#include "LuxTexturePrinter.h"
#include "LuxCreationTextureSpec.generated.h"

class UTexture;

UCLASS(Blueprintable, Config=Game)
class LUXORGAME_API ULuxCreationTextureSpec : public UObject {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TArray<FLuxTexturePrinter> TexturePrinters;
    
public:
    ULuxCreationTextureSpec();

    UFUNCTION(BlueprintCallable)
    static UTexture* PrintTexture(const FLuxTexturePrintParam& InParam);
    
};

