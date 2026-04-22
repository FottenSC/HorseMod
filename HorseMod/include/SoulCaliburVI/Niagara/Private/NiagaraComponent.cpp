#include "NiagaraComponent.h"

UNiagaraComponent::UNiagaraComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bAutoActivate = true;
    this->Asset = NULL;
    this->bRenderingEnabled = true;
}

void UNiagaraComponent::SetRenderingEnabled(bool bInRenderingEnabled) {
}

void UNiagaraComponent::SetNiagaraVariableVec4(const FString& InVariableName, const FVector4& inValue) {
}

void UNiagaraComponent::SetNiagaraVariableVec3(const FString& InVariableName, FVector inValue) {
}

void UNiagaraComponent::SetNiagaraVariableVec2(const FString& InVariableName, FVector2D inValue) {
}

void UNiagaraComponent::SetNiagaraVariableFloat(const FString& InVariableName, float inValue) {
}

void UNiagaraComponent::SetNiagaraVariableBool(const FString& InVariableName, bool inValue) {
}

void UNiagaraComponent::SetNiagaraStaticMeshDataInterfaceActor(const FString& InVariableName, AActor* InSource) {
}

void UNiagaraComponent::SetNiagaraEmitterSpawnRate(const FString& InEmitterName, float inValue) {
}

void UNiagaraComponent::ResetEffect() {
}

void UNiagaraComponent::ReinitializeEffect() {
}


