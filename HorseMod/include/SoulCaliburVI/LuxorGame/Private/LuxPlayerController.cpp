#include "LuxPlayerController.h"

ALuxPlayerController::ALuxPlayerController(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bShowMouseCursor = true;
    this->ClickEventKeys.AddDefaulted(1);
}


