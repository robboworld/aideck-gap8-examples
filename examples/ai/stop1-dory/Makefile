io ?= uart
PMSIS_OS ?= freertos
CORE ?= 8
STREAM_RAW_IMAGES ?= 0
REPORT_EVERY ?= 1
DETECTOR_PERIOD_US ?= 0
DETECTOR_EVERY_N ?= 8
INPUT_MODE ?= camera
STREAM_ONLY ?= 0
# int32 obj+cls gate (see main.c); ~float board 8.26 / float@0.6 operating point
STOP1_PRESENT_RAW_MIN ?= 197000
STOP1_TEMPORAL_REQUIRED ?= 2

APP = stop1_dory
APP_SRCS += main.c ../../../lib/cpx/src/com.c ../../../lib/cpx/src/cpx.c
APP_INC += . generated/inc ../../../lib/cpx/inc
APP_CFLAGS += -DNUM_CORES=$(CORE) -O2 -DconfigUSE_TIMERS=1 -DINCLUDE_xTimerPendFunctionCall=1
APP_CFLAGS += -DGAP_SDK=1 -DTARGET_CHIP_FAMILY_GAP8 -DDORY_DISABLE_CHECKSUMS
APP_CFLAGS += -DSTREAM_RAW_IMAGES=$(STREAM_RAW_IMAGES) -DREPORT_EVERY=$(REPORT_EVERY) -DDETECTOR_PERIOD_US=$(DETECTOR_PERIOD_US) -DDETECTOR_EVERY_N=$(DETECTOR_EVERY_N)
APP_CFLAGS += -DSTOP1_PRESENT_RAW_MIN=$(STOP1_PRESENT_RAW_MIN) -DSTOP1_TEMPORAL_REQUIRED=$(STOP1_TEMPORAL_REQUIRED)
APP_CFLAGS += -DSETUP_WIFI_AP=1
# Keep COM TX queue from blocking forever if ESP32 stops draining (TCP disconnect).
APP_CFLAGS += -DTXQ_SIZE=96
ifeq ($(INPUT_MODE),static)
  APP_CFLAGS += -DSTOP1_INPUT_STATIC=1
else ifeq ($(INPUT_MODE),camera)
  APP_CFLAGS += -DSTOP1_INPUT_CAMERA=1
else
  $(error INPUT_MODE must be camera or static)
endif

ifeq ($(STREAM_ONLY),1)
  APP_SRCS += network_stubs.c
  APP_CFLAGS += -DSTOP1_STREAM_ONLY=1
else
  GENERATED_SRCS = $(filter-out generated/src/main.c,$(wildcard generated/src/*.c))
  APP_SRCS += $(GENERATED_SRCS)
endif

APP_LDFLAGS += -lm
USE_PMSIS_BSP = 1
CONFIG_GAP_LIB_JPEG = 1

include $(RULES_DIR)/pmsis_rules.mk
