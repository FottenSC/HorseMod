#pragma once
#include "CoreMinimal.h"
#include "AnimCompress.h"
#include "AnimCompress_Automatic.generated.h"

UCLASS(Blueprintable, EditInlineNew, MinimalAPI)
class UAnimCompress_Automatic : public UAnimCompress {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float MaxEndEffectorError;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bTryFixedBitwiseCompression: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bTryPerTrackBitwiseCompression: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bTryLinearKeyRemovalCompression: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bTryIntervalKeyRemoval: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bRunCurrentDefaultCompressor: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAutoReplaceIfExistingErrorTooGreat: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bRaiseMaxErrorToExisting: 1;
    
    UAnimCompress_Automatic();

};

