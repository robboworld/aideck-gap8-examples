io ?= uart
PMSIS_OS ?= freertos
INPUT_MODE ?= camera
CORE ?= 8

APP = tiny_shapes_dory
GENERATED_DIR = generated
GENERATED_SRCS = $(wildcard $(GENERATED_DIR)/src/*.c)

APP_SRCS += \
  main.c \
  preprocess.c \
  inference.c \
  result_protocol.c \
  $(GENERATED_SRCS)

APP_INC += . $(GENERATED_DIR)/inc
APP_CFLAGS += \
  -DNUM_CORES=$(CORE) \
  -DGAP_SDK=1 \
  -DTARGET_CHIP_FAMILY_GAP8 \
  -DFLASH_TYPE=HYPERFLASH \
  -DUSE_HYPERFLASH \
  -DUSE_HYPERRAM \
  -DALWAYS_BLOCK_DMA_TRANSFERS \
  -DFS_READ_FS \
  -O2 \
  -fno-indirect-inlining \
  -fno-tree-loop-distribute-patterns
APP_LDFLAGS += -lm

ifeq ($(INPUT_MODE),camera)
  APP_SRCS += ../../../lib/cpx/src/com.c ../../../lib/cpx/src/cpx.c
  APP_INC += ../../../lib/cpx/inc
  APP_CFLAGS += \
    -DTINY_SHAPES_CAMERA_INPUT \
    -DTINY_SHAPES_ENABLE_CPX \
    -DDORY_DISABLE_CHECKSUMS \
    -DconfigUSE_TIMERS=1 \
    -DINCLUDE_xTimerPendFunctionCall=1 \
    -DTXQ_SIZE=5 \
    -DRXQ_SIZE=5
else ifeq ($(INPUT_MODE),static)
  APP_CFLAGS += -DTINY_SHAPES_STATIC_INPUT
else
  $(error INPUT_MODE must be camera or static)
endif

READFS_FILES = \
  $(GENERATED_DIR)/hex/BNReluConvolution0_weights.hex \
  $(GENERATED_DIR)/hex/BNReluConvolution2_weights.hex \
  $(GENERATED_DIR)/hex/BNReluConvolution4_weights.hex \
  $(GENERATED_DIR)/hex/FullyConnected6_weights.hex \
  $(GENERATED_DIR)/hex/inputs.hex

USE_PMSIS_BSP = 1
APP_CFLAGS += -DFS_PARTITIONTABLE_OFFSET=0x40000

include $(RULES_DIR)/pmsis_rules.mk
