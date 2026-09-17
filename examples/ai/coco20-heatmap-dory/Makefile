io ?= uart
PMSIS_OS ?= freertos
CORE ?= 8
STREAM_RAW_IMAGES ?= 0
# Print CRTP at most this often (µs). Ranking still runs every frame.
REPORT_PERIOD_US ?= 2000000
REPORT_EVERY ?= 1
# 0 = run as fast as capture/inference allow.
DETECTOR_PERIOD_US ?= 0
# camera = Himax live path; static = synthetic 160x160 (no Himax).
INPUT_MODE ?= camera

APP = coco20_heatmap_dory
# E28 (S1): BNRelu0-3 + Convolution4 head. (E39 used BNRelu0-6 — do not mix.)
DORY_SRCS = generated/src/network.c generated/src/mem.c generated/src/net_utils.c generated/src/dory_dma.c generated/src/embedded_weights.c generated/src/BNReluConvolution0.c generated/src/BNReluConvolution1.c generated/src/BNReluConvolution2.c generated/src/BNReluConvolution3.c generated/src/Convolution4.c generated/src/pulp_nn_conv_Ho_parallel.c generated/src/pulp_nn_conv_HoWo_parallel.c generated/src/pulp_nn_conv_Co_parallel.c generated/src/pulp_nn_matmul.c generated/src/pulp_nn_utils.c generated/src/pulp_nn_conv_Ho_parallel_out_32.c generated/src/pulp_nn_conv_Ho_parallel_int32.c generated/src/pulp_nn_pointwise_Ho_parallel.c generated/src/pulp_nn_pointwise_Co_parallel.c generated/src/pulp_nn_pointwise_HoWo_parallel.c generated/src/pulp_nn_pointwise_HoWo_parallel_32.c
APP_SRCS += main.c ../../../lib/cpx/src/com.c ../../../lib/cpx/src/cpx.c $(DORY_SRCS)
APP_INC += . generated/inc ../../../lib/cpx/inc
APP_CFLAGS += -DNUM_CORES=$(CORE) -O2 -DconfigUSE_TIMERS=1 -DINCLUDE_xTimerPendFunctionCall=1 -DTXQ_SIZE=24 -DRXQ_SIZE=8
APP_CFLAGS += -DGAP_SDK=1 -DTARGET_CHIP_FAMILY_GAP8 -DALWAYS_BLOCK_DMA_TRANSFERS -DSINGLE_CORE_DMA -fno-indirect-inlining -fno-tree-loop-distribute-patterns -DDORY_DISABLE_CHECKSUMS
APP_CFLAGS += -DSTREAM_RAW_IMAGES=$(STREAM_RAW_IMAGES) -DREPORT_EVERY=$(REPORT_EVERY) -DREPORT_PERIOD_US=$(REPORT_PERIOD_US) -DDETECTOR_PERIOD_US=$(DETECTOR_PERIOD_US)
APP_CFLAGS += -DSETUP_WIFI_AP=1
ifeq ($(INPUT_MODE),static)
  APP_CFLAGS += -DCOCO20_INPUT_STATIC=1
else ifeq ($(INPUT_MODE),camera)
  APP_CFLAGS += -DCOCO20_INPUT_CAMERA=1
else
  $(error INPUT_MODE must be camera or static)
endif
APP_LDFLAGS += -lm
USE_PMSIS_BSP = 1

include $(RULES_DIR)/pmsis_rules.mk
