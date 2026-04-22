#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=StringAssetReference -FallbackName=StringAssetReference
//CROSS-MODULE INCLUDE V2: -ModuleName=InputCore -ObjectName=Key -FallbackName=Key
#include "EMouseCaptureMode.h"
#include "EMouseLockMode.h"
#include "InputActionKeyMapping.h"
#include "InputAxisConfigEntry.h"
#include "InputAxisKeyMapping.h"
#include "InputSettings.generated.h"

class UInputSettings;

UCLASS(Blueprintable, DefaultConfig, Config=Input)
class ENGINE_API UInputSettings : public UObject {
    GENERATED_BODY()
public:
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, Config, EditAnywhere, EditFixedSize, meta=(AllowPrivateAccess=true))
    TArray<FInputAxisConfigEntry> AxisConfig;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAltEnterTogglesFullscreen: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bF11TogglesFullscreen: 1;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bUseMouseForTouch: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bEnableMouseSmoothing: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bEnableFOVScaling: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    float FOVScale;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    float DoubleClickTime;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bCaptureMouseOnLaunch;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    EMouseCaptureMode DefaultViewportMouseCaptureMode;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bDefaultViewportMouseLock;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    EMouseLockMode DefaultViewportMouseLockMode;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FInputActionKeyMapping> ActionMappings;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FInputAxisKeyMapping> AxisMappings;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bAlwaysShowTouchInterface;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bShowConsoleOnFourFingerTap;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    FStringAssetReference DefaultTouchInterface;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    FKey ConsoleKey;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FKey> ConsoleKeys;
    
    UInputSettings();

    UFUNCTION(BlueprintCallable)
    void SaveKeyMappings();
    
    UFUNCTION(BlueprintCallable)
    void RemoveAxisMapping(const FInputAxisKeyMapping& KeyMapping, bool bForceRebuildKeymaps);
    
    UFUNCTION(BlueprintCallable)
    void RemoveActionMapping(const FInputActionKeyMapping& KeyMapping, bool bForceRebuildKeymaps);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static UInputSettings* GetInputSettings();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetAxisNames(TArray<FName>& AxisNames) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetAxisMappingByName(const FName InAxisName, TArray<FInputAxisKeyMapping>& OutMappings) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetActionNames(TArray<FName>& ActionNames) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetActionMappingByName(const FName InActionName, TArray<FInputActionKeyMapping>& OutMappings) const;
    
    UFUNCTION(BlueprintCallable)
    void ForceRebuildKeymaps();
    
    UFUNCTION(BlueprintCallable)
    void AddAxisMapping(const FInputAxisKeyMapping& KeyMapping, bool bForceRebuildKeymaps);
    
    UFUNCTION(BlueprintCallable)
    void AddActionMapping(const FInputActionKeyMapping& KeyMapping, bool bForceRebuildKeymaps);
    
};

