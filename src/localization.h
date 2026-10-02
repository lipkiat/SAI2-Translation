#pragma once
#include "pe.h"

// Exactly one of destinationLanguage and outputLanguage must be specified.
bool ProcessLanguages(PE_HANDLE image, const wchar_t *sourceLanguage, const wchar_t *destinationLanguage,
                      const wchar_t *outputLanguage);
