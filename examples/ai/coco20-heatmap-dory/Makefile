io ?= uart
PMSIS_OS ?= freertos
CORE ?= 8
STREAM_RAW_IMAGES ?= 0
REPORT_EVERY ?= 1
DETECTOR_PERIOD_US ?= 500000

APP = coco20_heatmap_dory
DORY_SRCS = generated/src/network.c generated/src/mem.c generated/src/net_utils.c generated/src/dory_dma.c generated/src/embedded_weights.c generated/src/BNReluConvolution0.c generated/src/BNReluConvolution1.c generated/src/BNReluConvolution2.c generated/src/BNReluConvolution3.c generated/src/Convolution4.c generated/src/pulp_nn_conv_Ho_parallel.c generated/src/pulp_nn_conv_HoWo_parallel.c generated/src/pulp_nn_conv_Co_parallel.c generated/src/pulp_nn_matmul.c generated/src/pulp_nn_utils.c generated/src/pulp_nn_conv_Ho_parallel_out_32.c
APP_SRCS += main.c ../../../lib/cpx/src/com.c ../../../lib/cpx/src/cpx.c $(DORY_SRCS)
APP_INC += . generated/inc ../../../lib/cpx/inc
APP_CFLAGS += -DNUM_CORES=$(CORE) -O2 -DconfigUSE_TIMERS=1 -DINCLUDE_xTimerPendFunctionCall=1 -DTXQ_SIZE=5 -DRXQ_SIZE=5
APP_CFLAGS += -DGAP_SDK=1 -DTARGET_CHIP_FAMILY_GAP8 -DALWAYS_BLOCK_DMA_TRANSFERS -DSINGLE_CORE_DMA -fno-indirect-inlining -fno-tree-loop-distribute-patterns -DDORY_DISABLE_CHECKSUMS
APP_CFLAGS += -DSTREAM_RAW_IMAGES=$(STREAM_RAW_IMAGES) -DREPORT_EVERY=$(REPORT_EVERY) -DDETECTOR_PERIOD_US=$(DETECTOR_PERIOD_US)
APP_CFLAGS += -DSETUP_WIFI_AP=1
APP_LDFLAGS += -lm
USE_PMSIS_BSP = 1

include $(RULES_DIR)/pmsis_rules.mk
