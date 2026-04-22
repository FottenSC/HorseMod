#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIObject -FallbackName=UIObject
#include "LuxUIGameUnlock.generated.h"

class ULuxUIGameContent;
class ULuxUIGameUnlock;

UCLASS(Blueprintable)
class LUXORGAME_API ULuxUIGameUnlock : public UUIObject {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    FString Identifier;
    
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TWeakObjectPtr<ULuxUIGameContent> CachedOwner;
    
public:
    ULuxUIGameUnlock();

    UFUNCTION(BlueprintCallable)
    bool Unlock(const FString& inUnlockId);
    
    UFUNCTION(BlueprintCallable)
    bool SetLock(const FString& inUnlockId, bool inValue);
    
    UFUNCTION(BlueprintCallable)
    bool SaveToSaveData();
    
    UFUNCTION(BlueprintCallable)
    bool Lock(const FString& inUnlockId);
    
    UFUNCTION(BlueprintCallable)
    bool LoadFromSaveData();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsLocked(const FString& inUnlockId) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static ULuxUIGameUnlock* GetGameUnlockProxy();
    
};

