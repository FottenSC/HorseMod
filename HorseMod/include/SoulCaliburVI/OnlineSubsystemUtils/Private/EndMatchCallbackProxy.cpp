#include "EndMatchCallbackProxy.h"

UEndMatchCallbackProxy::UEndMatchCallbackProxy() {
}

UEndMatchCallbackProxy* UEndMatchCallbackProxy::EndMatch(UObject* WorldContextObject, APlayerController* PlayerController, TScriptInterface<ITurnBasedMatchInterface> MatchActor, const FString& MatchId, TEnumAsByte<EMPMatchOutcome::Type> LocalPlayerOutcome, TEnumAsByte<EMPMatchOutcome::Type> OtherPlayersOutcome) {
    return NULL;
}


