#include "BTComposite_SimpleParallel.h"

UBTComposite_SimpleParallel::UBTComposite_SimpleParallel() {
    this->NodeName = TEXT("Simple Parallel");
    this->FinishMode = EBTParallelMode::AbortBackground;
}


