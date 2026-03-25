#@TYPE: SOC
#@NAME: Sophgo CV180X
#@DESCRIPTION: Common config for CV180X family (Duo baseline)

require conf/machine/include/milkv-common.inc

# Kernel — use the unified recipe once Phase 3 is complete
# PREFERRED_PROVIDER_virtual/kernel will be set per-machine below

# CV1800B has no eMMC controller (0x04300000 is reserved)
# No TPU recipes needed for this family
