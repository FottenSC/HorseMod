#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector2D -FallbackName=Vector2D
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector4 -FallbackName=Vector4
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=PrimitiveComponent -FallbackName=PrimitiveComponent
#include "NiagaraScriptDataInterfaceInfo.h"
#include "NiagaraVariable.h"
#include "NiagaraComponent.generated.h"

class AActor;
class UNiagaraDataInterface;
class UNiagaraEffect;

UCLASS(Blueprintable, EditInlineNew, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class NIAGARA_API UNiagaraComponent : public UPrimitiveComponent {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UNiagaraEffect* Asset;
    
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FNiagaraVariable> EffectParameterLocalOverrides;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FNiagaraScriptDataInterfaceInfo> EffectDataInterfaceLocalOverrides;
    
    UPROPERTY(BlueprintReadWrite, DuplicateTransient, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TArray<UNiagaraDataInterface*> InstanceDataInterfaces;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bRenderingEnabled;
    
    UNiagaraComponent(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void SetRenderingEnabled(bool bInRenderingEnabled);
    
    UFUNCTION(BlueprintCallable)
    void SetNiagaraVariableVec4(const FString& InVariableName, const FVector4& inValue);
    
    UFUNCTION(BlueprintCallable)
    void SetNiagaraVariableVec3(const FString& InVariableName, FVector inValue);
    
    UFUNCTION(BlueprintCallable)
    void SetNiagaraVariableVec2(const FString& InVariableName, FVector2D inValue);
    
    UFUNCTION(BlueprintCallable)
    void SetNiagaraVariableFloat(const FString& InVariableName, float inValue);
    
    UFUNCTION(BlueprintCallable)
    void SetNiagaraVariableBool(const FString& InVariableName, bool inValue);
    
    UFUNCTION(BlueprintCallable)
    void SetNiagaraStaticMeshDataInterfaceActor(const FString& InVariableName, AActor* InSource);
    
    UFUNCTION(BlueprintCallable)
    void SetNiagaraEmitterSpawnRate(const FString& InEmitterName, float inValue);
    
    UFUNCTION(BlueprintCallable)
    void ResetEffect();
    
    UFUNCTION(BlueprintCallable)
    void ReinitializeEffect();
    
};

