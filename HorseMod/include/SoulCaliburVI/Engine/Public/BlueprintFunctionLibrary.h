#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=StringAssetReference -FallbackName=StringAssetReference
#include "BlueprintFunctionLibrary.generated.h"

UCLASS(Abstract, Blueprintable, MinimalAPI)
class UBlueprintFunctionLibrary : public UObject {
    GENERATED_BODY()
public:
    UBlueprintFunctionLibrary();

    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FStringAssetReference MakeStringAssetReference(const FString& AssetLongPathname);
    
};

