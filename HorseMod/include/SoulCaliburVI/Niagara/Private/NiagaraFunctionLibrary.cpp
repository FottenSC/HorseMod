#include "NiagaraFunctionLibrary.h"

UNiagaraFunctionLibrary::UNiagaraFunctionLibrary() {
}

UNiagaraComponent* UNiagaraFunctionLibrary::SpawnEffectAttached(UNiagaraEffect* EffectTemplate, USceneComponent* AttachToComponent, FName AttachPointName, FVector Location, FRotator Rotation, TEnumAsByte<EAttachLocation::Type> LocationType, bool bAutoDestroy) {
    return NULL;
}

UNiagaraComponent* UNiagaraFunctionLibrary::SpawnEffectAtLocation(UObject* WorldContextObject, UNiagaraEffect* EffectTemplate, FVector Location, FRotator Rotation, bool bAutoDestroy) {
    return NULL;
}


