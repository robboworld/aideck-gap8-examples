io ?= uart
PMSIS_OS ?= freertos
CORE ?= 8
STREAM_RAW_IMAGES ?= 0
REPORT_EVERY ?= 1
DETECTOR_PERIOD_US ?= 0
INPUT_MODE ?= camera

APP = letter20_legacy_dory
GENERATED_SRCS = $(filter-out generated/src/main.c,$(wildcard generated/src/*.c))
APP_SRCS += main.c ../../../lib/cpx/src/com.c ../../../lib/cpx/src/cpx.c $(GENERATED_SRCS)
APP_INC += . generated/inc ../../../lib/cpx/inc
APP_CFLAGS += -DNUM_CORES=$(CORE) -O2 -DconfigUSE_TIMERS=1 -DINCLUDE_xTimerPendFunctionCall=1 -DTXQ_SIZE=5 -DRXQ_SIZE=5
APP_CFLAGS += -DGAP_SDK=1 -DTARGET_CHIP_FAMILY_GAP8 -DALWAYS_BLOCK_DMA_TRANSFERS -DSINGLE_CORE_DMA -fno-indirect-inlining -fno-tree-loop-distribute-patterns -DDORY_DISABLE_CHECKSUMS
APP_CFLAGS += -DSTREAM_RAW_IMAGES=$(STREAM_RAW_IMAGES) -DREPORT_EVERY=$(REPORT_EVERY) -DDETECTOR_PERIOD_US=$(DETECTOR_PERIOD_US)
APP_CFLAGS += -DSETUP_WIFI_AP=1
ifeq ($(INPUT_MODE),static)
  APP_CFLAGS += -DLETTER20_INPUT_STATIC=1
else ifeq ($(INPUT_MODE),camera)
  APP_CFLAGS += -DLETTER20_INPUT_CAMERA=1
else
  $(error INPUT_MODE must be camera or static)
endif
APP_LDFLAGS += -lm
USE_PMSIS_BSP = 1

include $(RULES_DIR)/pmsis_rules.mk
