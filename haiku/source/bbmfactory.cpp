// BigBubbleMuff (Haiku) — plug-in factory.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#include "bbmcontroller.h"
#include "bbmids.h"
#include "bbmprocessor.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory_constexpr.h"

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail, 2)

DEF_CLASS(bbmh::BigMuffProcessorUID, Steinberg::PClassInfo::kManyInstances,
          kVstAudioEffectClass, stringPluginName, Steinberg::Vst::kDistributable,
          "Fx|Distortion", FULL_VERSION_STR, kVstVersionString,
          bbmh::BigMuffProcessor::createInstance, nullptr)

DEF_CLASS(bbmh::BigMuffControllerUID, Steinberg::PClassInfo::kManyInstances,
          kVstComponentControllerClass, stringPluginName "Controller", 0, "",
          FULL_VERSION_STR, kVstVersionString, bbmh::BigMuffController::createInstance,
          nullptr)

END_FACTORY
