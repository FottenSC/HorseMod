#include "AnimNode_TransitionPoseEvaluator.h"

FAnimNode_TransitionPoseEvaluator::FAnimNode_TransitionPoseEvaluator() {
    this->DataSource = EEvaluatorDataSource::EDS_SourcePose;
    this->EvaluatorMode = EEvaluatorMode::EM_Standard;
    this->FramesToCachePose = 0;
    this->CacheFramesRemaining = 0;
}

