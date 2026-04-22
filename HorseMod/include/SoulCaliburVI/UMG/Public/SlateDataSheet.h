#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "SlateDataSheet.generated.h"

class UTexture2D;

UCLASS(Blueprintable)
class UMG_API USlateDataSheet : public UObject {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    UTexture2D* DataTexture;
    
public:
    USlateDataSheet();

};

