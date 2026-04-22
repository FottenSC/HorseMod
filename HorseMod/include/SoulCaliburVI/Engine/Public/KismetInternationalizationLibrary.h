#pragma once
#include "CoreMinimal.h"
#include "BlueprintFunctionLibrary.h"
#include "KismetInternationalizationLibrary.generated.h"

UCLASS(Blueprintable)
class ENGINE_API UKismetInternationalizationLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    UKismetInternationalizationLibrary();

    UFUNCTION(BlueprintCallable)
    static bool SetCurrentLocale(const FString& Culture, const bool SaveToConfig);
    
    UFUNCTION(BlueprintCallable)
    static bool SetCurrentLanguageAndLocale(const FString& Culture, const bool SaveToConfig);
    
    UFUNCTION(BlueprintCallable)
    static bool SetCurrentLanguage(const FString& Culture, const bool SaveToConfig);
    
    UFUNCTION(BlueprintCallable)
    static bool SetCurrentCulture(const FString& Culture, const bool SaveToConfig);
    
    UFUNCTION(BlueprintCallable)
    static bool SetCurrentAssetGroupCulture(const FName AssetGroup, const FString& Culture, const bool SaveToConfig);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FString GetCurrentLocale();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FString GetCurrentLanguage();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FString GetCurrentCulture();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FString GetCurrentAssetGroupCulture(const FName AssetGroup);
    
    UFUNCTION(BlueprintCallable)
    static void ClearCurrentAssetGroupCulture(const FName AssetGroup, const bool SaveToConfig);
    
};

