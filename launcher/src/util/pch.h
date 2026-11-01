
#pragma once


#define LINKAGE_STYLE               LINKAGE_STYLE_STATIC



#include "../../../core/src/util/pch.h"
#include "../../../core/src/util/data_structures/thread_pool.h"
#include "../../../core/src/util/io/logger.h"
#include "../../../core/src/util/io/vfs.h"
#include "../../../core/src/util/io/directory_iterator.h"
#include "../../../core/src/util/io/serializer_yaml.h"
#include "../../../core/src/util/io/serializer_data.h"

#include "../../../core/src/plugin_system/i_plugin.h"
#include "../../../core/src/plugin_system/linkage.h"
#include "../../../core/src/plugin_system/plugin_manager.h"
#include "../../../core/src/platform/i_window.h"
#include "../../../core/src/event/event_bus.h"
#include "../../../core/src/event/application_event.h"
