#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Rotator -FallbackName=Rotator
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=EAttachLocation -FallbackName=EAttachLocation
#include "NiagaraFunctionLibrary.generated.h"

class UNiagaraComponent;
class UNiagaraEffect;
class UObject;
class USceneComponent;

UCLASS(Blueprintable)
class NIAGARA_API UNiagaraFunctionLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    UNiagaraFunctionLibrary();

    UFUNCTION(BlueprintCallable)
    static UNiagaraComponent* SpawnEffectAttached(UNiagaraEffect* EffectTemplate, USceneComponent* AttachToComponent, FName AttachPointName, FVector Location, FRotator Rotation, TEnumAsByte<EAttachLocation::Type> LocationType, bool bAutoDestroy);
    
    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))
    static UNiagaraComponent* SpawnEffectAtLocation(UObject* WorldContextObject, UNiagaraEffect* EffectTemplate, FVector Location, FRotator Rotation, bool bAutoDestroy);
    
};

