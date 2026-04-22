#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Class -FallbackName=Class
#include "BlueprintCookedComponentInstancingData.h"
#include "EventGraphFastCallPair.h"
#include "BlueprintGeneratedClass.generated.h"

class UActorComponent;
class UDynamicBlueprintBinding;
class UFunction;
class UInheritableComponentHandler;
class USimpleConstructionScript;
class UStructProperty;
class UTimelineTemplate;

UCLASS(Blueprintable)
class ENGINE_API UBlueprintGeneratedClass : public UClass {
    GENERATED_BODY()
public:
    UPROPERTY(AssetRegistrySearchable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 NumReplicatedProperties;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<UDynamicBlueprintBinding*> DynamicBindingObjects;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    TArray<UActorComponent*> ComponentTemplates;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<UTimelineTemplate*> Timelines;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    USimpleConstructionScript* SimpleConstructionScript;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UInheritableComponentHandler* InheritableComponentHandler;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UStructProperty* UberGraphFramePointerProperty;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UFunction* UberGraphFunction;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FEventGraphFastCallPair> FastCallPairs;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bHasInstrumentation;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TMap<FName, FBlueprintCookedComponentInstancingData> CookedComponentInstancingData;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bHasNativizedParent;
    
    UBlueprintGeneratedClass();

};

