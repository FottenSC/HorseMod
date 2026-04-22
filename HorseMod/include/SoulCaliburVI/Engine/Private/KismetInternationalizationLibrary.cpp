#include "KismetInternationalizationLibrary.h"

UKismetInternationalizationLibrary::UKismetInternationalizationLibrary() {
}

bool UKismetInternationalizationLibrary::SetCurrentLocale(const FString& Culture, const bool SaveToConfig) {
    return false;
}

bool UKismetInternationalizationLibrary::SetCurrentLanguageAndLocale(const FString& Culture, const bool SaveToConfig) {
    return false;
}

bool UKismetInternationalizationLibrary::SetCurrentLanguage(const FString& Culture, const bool SaveToConfig) {
    return false;
}

bool UKismetInternationalizationLibrary::SetCurrentCulture(const FString& Culture, const bool SaveToConfig) {
    return false;
}

bool UKismetInternationalizationLibrary::SetCurrentAssetGroupCulture(const FName AssetGroup, const FString& Culture, const bool SaveToConfig) {
    return false;
}

FString UKismetInternationalizationLibrary::GetCurrentLocale() {
    return TEXT("");
}

FString UKismetInternationalizationLibrary::GetCurrentLanguage() {
    return TEXT("");
}

FString UKismetInternationalizationLibrary::GetCurrentCulture() {
    return TEXT("");
}

FString UKismetInternationalizationLibrary::GetCurrentAssetGroupCulture(const FName AssetGroup) {
    return TEXT("");
}

void UKismetInternationalizationLibrary::ClearCurrentAssetGroupCulture(const FName AssetGroup, const bool SaveToConfig) {
}


