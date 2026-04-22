#include "MovieSceneCapture.h"

UMovieSceneCapture::UMovieSceneCapture() {
    this->ProtocolSettings = NULL;
    this->bUseSeparateProcess = false;
    this->bCloseEditorWhenCaptureStarts = false;
    this->AdditionalCommandLineArguments = TEXT("-NOSCREENMESSAGES");
}


