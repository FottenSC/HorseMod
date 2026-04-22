#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=StringAssetReference -FallbackName=StringAssetReference
#include "LuxNamedAssetReference.h"
#include "LuxAssetPathsBase.generated.h"

UCLASS(Abstract, Blueprintable)
class LUXORGAME_API ULuxAssetPathsBase : public UObject {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FLuxNamedAssetReference> NamedReferences;
    
    ULuxAssetPathsBase();

    UFUNCTION(BlueprintCallable, BlueprintPure)
    TArray<FStringAssetReference> GetUAssetPaths() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    TArray<FString> GetRAssetPaths() const;
    
};

