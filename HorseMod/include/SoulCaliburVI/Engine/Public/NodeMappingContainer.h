#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "NodeMap.h"
#include "NodeMappingContainer.generated.h"

class UBlueprint;

UCLASS(Blueprintable)
class ENGINE_API UNodeMappingContainer : public UObject {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TMap<FName, FNodeMap> NodeMapping;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UBlueprint* SourceAsset;
    
public:
    UNodeMappingContainer();

};

