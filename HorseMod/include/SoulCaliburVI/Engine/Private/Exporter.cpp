#include "Exporter.h"

UExporter::UExporter() {
    this->SupportedClass = NULL;
    this->ExportRootScope = NULL;
    this->PreferredFormatIndex = 0;
    this->TextIndent = 0;
    this->bText = false;
    this->bSelectedOnly = false;
    this->bForceFileOperations = false;
}


