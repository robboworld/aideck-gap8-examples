io ?= uart
PMSIS_OS ?= freertos
INPUT_MODE ?= camera
CORE ?= 8
EMBED_WEIGHTS ?= 1

APP = arrow2_dory
GENERATED_DIR = generated

DORY_RUNTIME_SRCS = \
  $(GENERATED_DIR)/src/network.c \
  $(GENERATED_DIR)/src/mem.c \
  $(GENERATED_DIR)/src/net_utils.c \
  $(GENERATED_DIR)/src/dory_dma.c \
  $(GENERATED_DIR)/src/BNReluConvolution0.c \
  $(GENERATED_DIR)/src/BNReluConvolution2.c \
  $(GENERATED_DIR)/src/BNReluConvolution4.c \
  $(GENERATED_DIR)/src/Pooling1.c \
  $(GENERATED_DIR)/src/Pooling3.c \
  $(GENERATED_DIR)/src/Pooling5.c \
  $(GENERATED_DIR)/src/FullyConnected6.c \
  $(GENERATED_DIR)/src/pulp_nn_conv_Ho_parallel.c \
  $(GENERATED_DIR)/src/pulp_nn_matmul.c \
  $(GENERATED_DIR)/src/pulp_nn_maxpool.c \
  $(GENERATED_DIR)/src/pulp_nn_linear_out_32.c \
  $(GENERATED_DIR)/src/pulp_nn_utils.c

ifeq ($(EMBED_WEIGHTS),1)
  DORY_RUNTIME_SRCS += $(GENERATED_DIR)/src/embedded_weights.c
endif

APP_INC += . $(GENERATED_DIR)/inc
APP_CFLAGS += \
  -DNUM_CORES=$(CORE) \
  -O2 \
  -DconfigUSE_TIMERS=1 \
  -DINCLUDE_xTimerPendFunctionCall=1 \
  -DTXQ_SIZE=5 \
  -DRXQ_SIZE=5
APP_SRCS += ../../../lib/cpx/src/com.c ../../../lib/cpx/src/cpx.c
APP_INC += ../../../lib/cpx/inc

DORY_CFLAGS = \
  -DGAP_SDK=1 \
  -DTARGET_CHIP_FAMILY_GAP8 \
  -DALWAYS_BLOCK_DMA_TRANSFERS \
  -DSINGLE_CORE_DMA \
  -fno-indirect-inlining \
  -fno-tree-loop-distribute-patterns \
  -DDORY_DISABLE_CHECKSUMS

ifeq ($(EMBED_WEIGHTS),1)
  DORY_CFLAGS += -DARROW2_EMBED_WEIGHTS
else
  DORY_CFLAGS += \
    -DFLASH_TYPE=HYPERFLASH \
    -DUSE_HYPERFLASH \
    -DFS_READ_FS
endif

APP_SRCS += \
  main.c \
  preprocess.c \
  inference.c \
  result_protocol.c \
  $(DORY_RUNTIME_SRCS)
APP_CFLAGS += $(DORY_CFLAGS)
APP_LDFLAGS += -lm

ifeq ($(INPUT_MODE),camera)
  APP_CFLAGS += \
    -DARROW2_CAMERA_INPUT \
    -DARROW2_ENABLE_CPX
else ifeq ($(INPUT_MODE),static)
  APP_CFLAGS += \
    -DARROW2_STATIC_INPUT \
    -DARROW2_ENABLE_CPX
else
  $(error INPUT_MODE must be camera or static)
endif

ifneq ($(EMBED_WEIGHTS),1)
  READFS_FILES = \
    $(GENERATED_DIR)/hex/BNReluConvolution0_weights.hex \
    $(GENERATED_DIR)/hex/BNReluConvolution2_weights.hex \
    $(GENERATED_DIR)/hex/BNReluConvolution4_weights.hex \
    $(GENERATED_DIR)/hex/FullyConnected6_weights.hex \
    $(GENERATED_DIR)/hex/inputs.hex
  ifneq ($(DISABLE_FS_PARTITION_OFFSET),1)
    APP_CFLAGS += -DFS_PARTITIONTABLE_OFFSET=0x40000
  endif
endif

USE_PMSIS_BSP = 1

include $(RULES_DIR)/pmsis_rules.mk
