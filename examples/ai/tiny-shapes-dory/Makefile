io ?= uart
PMSIS_OS ?= freertos
INPUT_MODE ?= camera
CORE ?= 8
BRINGUP ?= 0
# Embed weights in the binary for radio-only OTA (ReadFS mount fails on the
# GAP8 OTA flash layout without a JTAG-matched partition table pointer).
EMBED_WEIGHTS ?= 1
SKIP_RUN ?= 0
STAGE ?= 4

APP = tiny_shapes_dory
GENERATED_DIR = generated

# Only the kernels referenced by this TinyShapeCNN DORY graph.
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
  DORY_CFLAGS += -DTINY_SHAPES_EMBED_WEIGHTS
else
  DORY_CFLAGS += \
    -DFLASH_TYPE=HYPERFLASH \
    -DUSE_HYPERFLASH \
    -DFS_READ_FS
endif

ifeq ($(BRINGUP),1)
  APP_SRCS += main_bringup.c
  APP_CFLAGS += -DTINY_SHAPES_ENABLE_CPX
  ifeq ($(LINK_DORY),1)
    APP_SRCS += $(DORY_RUNTIME_SRCS) result_protocol.c
    APP_CFLAGS += -DLINK_DORY -DTINY_SHAPES_BRINGUP $(DORY_CFLAGS)
    ifeq ($(SKIP_RUN),1)
      APP_CFLAGS += -DTINY_SHAPES_SKIP_RUN
    endif
    APP_CFLAGS += -DTINY_SHAPES_STAGE=$(STAGE)
    APP_LDFLAGS += -lm
    ifneq ($(EMBED_WEIGHTS),1)
      READFS_FILES = \
        $(GENERATED_DIR)/hex/BNReluConvolution0_weights.hex \
        $(GENERATED_DIR)/hex/BNReluConvolution2_weights.hex \
        $(GENERATED_DIR)/hex/BNReluConvolution4_weights.hex \
        $(GENERATED_DIR)/hex/FullyConnected6_weights.hex \
        $(GENERATED_DIR)/hex/inputs.hex
    endif
    USE_PMSIS_BSP = 1
  endif
else
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
      -DTINY_SHAPES_CAMERA_INPUT \
      -DTINY_SHAPES_ENABLE_CPX
  else ifeq ($(INPUT_MODE),static)
    APP_CFLAGS += \
      -DTINY_SHAPES_STATIC_INPUT \
      -DTINY_SHAPES_ENABLE_CPX
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
endif

include $(RULES_DIR)/pmsis_rules.mk
