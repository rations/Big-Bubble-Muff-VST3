// BigBubbleMuff — plug-in factory.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#include "plugin/controller.h"
#include "plugin/ids.h"
#include "plugin/processor.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory_constexpr.h"

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail, 2)

DEF_CLASS(bbm::kProcessorUID, Steinberg::PClassInfo::kManyInstances, kVstAudioEffectClass,
          stringPluginName, Steinberg::Vst::kDistributable, "Fx|Distortion",
          FULL_VERSION_STR, kVstVersionString, bbm::Processor::createInstance, nullptr)

DEF_CLASS(bbm::kControllerUID, Steinberg::PClassInfo::kManyInstances,
          kVstComponentControllerClass, stringPluginName "Controller", 0, "",
          FULL_VERSION_STR, kVstVersionString, bbm::Controller::createInstance, nullptr)

END_FACTORY
