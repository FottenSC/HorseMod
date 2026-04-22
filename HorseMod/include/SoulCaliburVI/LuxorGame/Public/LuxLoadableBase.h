#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxLoadableBase.generated.h"

class ULuxAsyncLoader;

UCLASS(Abstract, Blueprintable)
class LUXORGAME_API ULuxLoadableBase : public UObject {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TArray<ULuxAsyncLoader*> Loaders;
    
public:
    ULuxLoadableBase();

};

