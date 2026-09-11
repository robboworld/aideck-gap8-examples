io ?= uart
PMSIS_OS ?= freertos
CORE ?= 8
EMBED_WEIGHTS ?= 1
# Infer every N streamed frames (1 = every frame; heavier on SoftAP).
INFER_EVERY_N ?= 2

APP = arrow2_stream_dory
# Shared DORY package from radio-only snapshot.
GENERATED_DIR = ../arrow2-dory/generated

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

APP_INC += . $(GENERATED_DIR)/inc ../../../lib/cpx/inc
APP_SRCS += \
  main.c \
  ../arrow2-dory/preprocess.c \
  ../arrow2-dory/inference.c \
  ../arrow2-dory/result_protocol.c \
  ../../../lib/cpx/src/com.c \
  ../../../lib/cpx/src/cpx.c \
  $(DORY_RUNTIME_SRCS)

APP_CFLAGS += \
  -DNUM_CORES=$(CORE) \
  -O2 \
  -DconfigUSE_TIMERS=1 \
  -DINCLUDE_xTimerPendFunctionCall=1 \
  -DTXQ_SIZE=96 \
  -DRXQ_SIZE=5 \
  -DSETUP_WIFI_AP=1 \
  -DARROW2_CAMERA_INPUT \
  -DARROW2_ENABLE_CPX \
  -DINFER_EVERY_N=$(INFER_EVERY_N) \
  -DGAP_SDK=1 \
  -DTARGET_CHIP_FAMILY_GAP8 \
  -DALWAYS_BLOCK_DMA_TRANSFERS \
  -DSINGLE_CORE_DMA \
  -fno-indirect-inlining \
  -fno-tree-loop-distribute-patterns \
  -DDORY_DISABLE_CHECKSUMS

ifeq ($(EMBED_WEIGHTS),1)
  APP_CFLAGS += -DARROW2_EMBED_WEIGHTS
endif

APP_LDFLAGS += -lm
USE_PMSIS_BSP = 1
CONFIG_GAP_LIB_JPEG = 1

include $(RULES_DIR)/pmsis_rules.mk
