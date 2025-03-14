KERNELFLINGER_LOCAL_PATH := $(call my-dir)
KERNELFLINGER_CFLAGS := -Wa,--noexecstack -Wall -Wextra -Werror -mrdrnd -fwrapv

ifeq (clang, $(findstring clang, $(IAFW_CC)))
L_CC = $(IAFW_CC)
CC_VERSION = $(shell $(L_CC) --version)
# Clang support "-fno-delete-null-pointer-checks flags" when (version > 6)
MAJOR_VER := $(shell echo '$(CC_VERSION)' |\
               head -1 |\
               sed -n 's/.*clang version \([[:digit:]]\.[[:digit:]]\.[[:digit:]]\).*/\1/p' |\
               head -c 1)

ifeq ($(shell test $(MAJOR_VER) -gt 6; echo $$?), 0)
KERNELFLINGER_CFLAGS += -fno-delete-null-pointer-checks
endif
endif

ifeq ($(KERNELFLINGER_NON-ANDROID),true)
KERNELFLINGER_CFLAGS += -DFASTBOOT_FOR_NON_ANDROID
endif

KERNELFLINGER_CFLAGS += -DAVB_AB_I_UNDERSTAND_LIBAVB_AB_IS_DEPRECATED

ifeq ($(TARGET_UEFI_ARCH),x86_64)
    KERNELFLINGER_CFLAGS += -D__STDC_VERSION__=199901L
    KERNELFLINGER_CFLAGS += -DARCH_X86_64=1
endif

ifeq ($(TARGET_USE_SBL),true)
    KERNELFLINGER_CFLAGS += -DUSE_SBL
endif

ifeq ($(TARGET_USE_TRUSTY),true)
    KERNELFLINGER_CFLAGS += -DUSE_TRUSTY
endif

ifeq ($(TARGET_USE_MULTIBOOT),true)
    KERNELFLINGER_CFLAGS += -DUSE_MULTIBOOT
endif

ifeq ($(TARGET_USE_ACPI),true)
    KERNELFLINGER_CFLAGS += -DUSE_ACPI
endif
ifeq ($(TARGET_USE_ACPIO),true)
    KERNELFLINGER_CFLAGS += -DUSE_ACPIO
endif

ifeq ($(TARGET_USE_PRODUCT),true)
    KERNELFLINGER_CFLAGS += -DUSE_PRODUCT
endif

ifeq ($(IOC_USE_SLCAN),true)
    KERNELFLINGER_CFLAGS += -DIOC_USE_SLCAN
else
ifeq ($(IOC_USE_CBC),true)
    KERNELFLINGER_CFLAGS += -DIOC_USE_CBC
endif
endif

ifeq ($(TARGET_BUILD_VARIANT),user)
    KERNELFLINGER_CFLAGS += -DUSER -DUSERDEBUG
endif

ifeq ($(TARGET_BUILD_VARIANT),userdebug)
    KERNELFLINGER_CFLAGS += -DUSERDEBUG
endif

ifeq ($(TARGET_USE_TPM),true)
    KERNELFLINGER_CFLAGS += -DUSE_TPM -DSOFT_FUSE
endif

ifeq ($(TARGET_NO_DEVICE_UNLOCK),true)
    KERNELFLINGER_CFLAGS += -DNO_DEVICE_UNLOCK
endif

ifeq ($(HAL_AUTODETECT),true)
    KERNELFLINGER_CFLAGS += -DHAL_AUTODETECT
endif

ifeq ($(TARGET_USE_USERFASTBOOT),true)
    $(error Userfastboot is not supported anymore)
endif

ifeq ($(KERNELFLINGER_USE_POWER_BUTTON),true)
    KERNELFLINGER_CFLAGS += -DUSE_POWER_BUTTON
endif

KERNELFLINGER_CFLAGS += -DBOARD_BOOTIMAGE_PARTITION_SIZE=$(BOARD_BOOTIMAGE_PARTITION_SIZE)

# adb in crashmode allows to pull the entire RAM and MUST never be
# disabled allowed on a USER build for security reasons:
ifneq ($(TARGET_BUILD_VARIANT),user)
    KERNELFLINGER_CFLAGS += -DCRASHMODE_USE_ADB
endif

ifneq ($(strip $(KERNELFLINGER_USE_UI)),false)
    KERNELFLINGER_CFLAGS += -DUSE_UI
endif

ifeq ($(KERNELFLINGER_OS_SECURE_BOOT),true)
    KERNELFLINGER_CFLAGS += -DOS_SECURE_BOOT
endif

#android verifed boot support(libavb) is required by default
ifneq ($(KERNELFLINGER_DISABLE_DEBUG_PRINT),true)
    ifeq ($(TARGET_BUILD_VARIANT),userdebug)
        KERNELFLINGER_CFLAGS += -DAVB_ENABLE_DEBUG
    endif
endif

ifeq ($(BOARD_SLOT_AB_ENABLE),true)
    KERNELFLINGER_CFLAGS += -DUSE_SLOT
endif

ifeq ($(KERNELFLINGER_SUPPORT_USB_STORAGE),true)
    KERNELFLINGER_CFLAGS += -DUSB_STORAGE
    ifeq ($(KERNELFLINGER_SUPPORT_LIVE_BOOT),true)
        ifneq ($(TARGET_BUILD_VARIANT),user)
            KERNELFLINGER_CFLAGS += -DLIVE_BOOT
        else
            $(warning Live boot is only supported in eng and userdebug build)
        endif
    endif
endif

ifeq ($(BOARD_SD_PASS_THRU_ENABLE),true)
    KERNELFLINGER_CFLAGS += -DUSE_SD_PASS_THRU
endif

ifeq ($(PRODUCT_USE_DYNAMIC_PARTITIONS),true)
    KERNELFLINGER_CFLAGS += -DDYNAMIC_PARTITIONS
endif

ifeq ($(KERNELFLINGER_SUPPORT_KEYBOX_PROVISION),true)
    KERNELFLINGER_CFLAGS += -DFASTBOOT_KEYBOX_PROVISION
endif

KERNELFLINGER_STATIC_LIBRARIES := \
	libuefi_ssl_static \
	libuefi_crypto_static \
	libgnuefi \
	libsslsupport \
	libefi

include $(call all-subdir-makefiles)
LOCAL_PATH := $(KERNELFLINGER_LOCAL_PATH)

SHARED_CFLAGS := $(KERNELFLINGER_CFLAGS)
SHARED_STATIC_LIBRARIES := \
	$(KERNELFLINGER_STATIC_LIBRARIES) \
	libkernelflinger-$(TARGET_BUILD_VARIANT)

SHARED_STATIC_LIBRARIES += libedk2_tpm

include $(CLEAR_VARS)
LOCAL_MODULE := kernelflinger-$(TARGET_BUILD_VARIANT)


# if dm-verity is disabled for eng purpose skip the oem-cert

kf_intermediates := $(call intermediates-dir-for,EFI,kernelflingeravb)

AVB_PK := $(kf_intermediates)/avb_pk.bin
PADDED_AVB_PK := $(kf_intermediates)/avb_pk.padded.bin
AVB_PK_OBJ := $(kf_intermediates)/avb_pk.o
ifndef BOARD_AVB_KEY_PATH
BOOTLOADER_AVB_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
else
BOOTLOADER_AVB_KEY_PATH := $(BOARD_AVB_KEY_PATH)
endif

$(AVB_PK): $(BOOTLOADER_AVB_KEY_PATH)
	external/avb/avbtool.py extract_public_key --key $< --output $@

$(PADDED_AVB_PK): $(AVB_PK)
	$(call pad-binary, 4096)

ifeq ($(TARGET_UEFI_ARCH),x86_64)
    ELF_OUTPUT := elf64-x86-64
else
    ELF_OUTPUT := elf32-i386
endif

avb_sym_binary := $(shell echo _binary_$(PADDED_AVB_PK) | sed "s/[\/\.-]/_/g")
$(AVB_PK_OBJ): $(PADDED_AVB_PK)
	mkdir -p $(@D) && \
	$(EFI_OBJCOPY) --input binary --output $(ELF_OUTPUT) --binary-architecture i386 $< $@ && \
	$(EFI_OBJCOPY) --redefine-sym $(avb_sym_binary)_start=_binary_avb_pk_start \
                       --redefine-sym $(avb_sym_binary)_end=_binary_avb_pk_end \
                       --redefine-sym $(avb_sym_binary)_size=_binary_avb_pk_size \
                       --rename-section .data=.oemkeys $@ $@

LOCAL_GENERATED_SOURCES += $(AVB_PK_OBJ)
LOCAL_C_INCLUDES := \
	$(addprefix $(LOCAL_PATH)/,avb)

LOCAL_SRC_FILES := \
	kernelflinger.c
ifneq ($(strip $(KERNELFLINGER_USE_UI)),false)
	LOCAL_SRC_FILES += \
	ux.c
endif

LOCAL_STATIC_LIBRARIES := \
	libfastboot-$(TARGET_BUILD_VARIANT) \
	libefiusb-$(TARGET_BUILD_VARIANT) \
	libefitcp-$(TARGET_BUILD_VARIANT) \
	libtransport-$(TARGET_BUILD_VARIANT) \
	libheci-$(TARGET_BUILD_VARIANT) \
	libxbc-$(TARGET_BUILD_VARIANT)

ifeq ($(TARGET_USE_TRUSTY),true)
    LOCAL_STATIC_LIBRARIES += libqltipc-$(TARGET_BUILD_VARIANT)
endif

ifneq ($(TARGET_BUILD_VARIANT),user)
    LOCAL_STATIC_LIBRARIES += libadb-$(TARGET_BUILD_VARIANT)
endif

ifneq ($(TARGET_BUILD_VARIANT),user)
    LOCAL_SRC_FILES += unittest.c
endif

LOCAL_CFLAGS := $(SHARED_CFLAGS)


LOCAL_OBJCOPY_FLAGS := -j .oemkeys

LOCAL_STATIC_LIBRARIES += $(SHARED_STATIC_LIBRARIES)
LOCAL_MODULE_STEM := kernelflinger

LOCAL_STATIC_LIBRARIES += libavb_kernelflinger-$(TARGET_BUILD_VARIANT)

LOCAL_C_INCLUDES += \
	$(addprefix $(LOCAL_PATH)/,libkernelflinger) \
	$(addprefix $(LOCAL_PATH)/,libkernelflinger/fatfs/include) \
	$(addprefix $(LOCAL_PATH)/,libsslsupport) \
	$(addprefix $(LOCAL_PATH)/,libxbc)

include $(BUILD_EFI_EXECUTABLE)  # For kernelflinger-$(TARGET_BUILD_VARIANT)


include $(CLEAR_VARS)
LOCAL_MODULE := installer-$(TARGET_BUILD_VARIANT)
LOCAL_STATIC_LIBRARIES := \
	$(SHARED_STATIC_LIBRARIES) \
	libtransport-$(TARGET_BUILD_VARIANT) \
	libfastboot-for-installer-$(TARGET_BUILD_VARIANT) \
	libxbc-$(TARGET_BUILD_VARIANT)


SHARED_STATIC_LIBRARIES += libedk2_tpm

LOCAL_CFLAGS := $(SHARED_CFLAGS)
LOCAL_SRC_FILES := installer.c
LOCAL_MODULE_STEM := installer
LOCAL_C_INCLUDES := \
	$(addprefix $(LOCAL_PATH)/,libfastboot) \
	$(addprefix $(LOCAL_PATH)/,libsslsupport) \
	$(addprefix $(LOCAL_PATH)/,libkernelflinger) \
	$(addprefix $(LOCAL_PATH)/,libkernelflinger/fatfs/include) \
	$(addprefix $(LOCAL_PATH)/,libxbc)

kfins_intermediates := $(call intermediates-dir-for,EFI,kernelflingerins)

KFINS_AVB_PK := $(kfins_intermediates)/avb_pk.bin
KFINS_PADDED_AVB_PK := $(kfins_intermediates)/avb_pk.padded.bin
KFINS_AVB_PK_OBJ := $(kfins_intermediates)/avb_pk.o
ifndef BOARD_AVB_KEY_PATH
BOOTLOADER_AVB_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
else
BOOTLOADER_AVB_KEY_PATH := $(BOARD_AVB_KEY_PATH)
endif

$(KFINS_AVB_PK): $(BOOTLOADER_AVB_KEY_PATH)
	external/avb/avbtool.py extract_public_key --key $< --output $@

$(KFINS_PADDED_AVB_PK): $(KFINS_AVB_PK)
	$(call pad-binary, 4096)

ifeq ($(TARGET_UEFI_ARCH),x86_64)
    ELF_OUTPUT := elf64-x86-64
else
    ELF_OUTPUT := elf32-i386
endif

kfins_avb_sym_binary := $(shell echo _binary_$(KFINS_PADDED_AVB_PK) | sed "s/[\/\.-]/_/g")
$(KFINS_AVB_PK_OBJ): $(KFINS_PADDED_AVB_PK)
	mkdir -p $(@D) && \
	$(EFI_OBJCOPY) --input binary --output $(ELF_OUTPUT) --binary-architecture i386 $< $@ && \
	$(EFI_OBJCOPY) --redefine-sym $(kfins_avb_sym_binary)_start=_binary_avb_pk_start \
                       --redefine-sym $(kfins_avb_sym_binary)_end=_binary_avb_pk_end \
                       --redefine-sym $(kfins_avb_sym_binary)_size=_binary_avb_pk_size \
                       --rename-section .data=.oemkeys $@ $@

LOCAL_GENERATED_SOURCES += $(KFINS_AVB_PK_OBJ)
LOCAL_C_INCLUDES += $(addprefix $(LOCAL_PATH)/,avb)
LOCAL_STATIC_LIBRARIES += libavb_kernelflinger-$(TARGET_BUILD_VARIANT)

ifneq ($(TARGET_USE_SBL),true)
include $(BUILD_EFI_EXECUTABLE) # For installer-$(TARGET_BUILD_VARIANT)
endif

ifeq ($(BOOTLOADER_SLOT), true)
ifeq ($(BOARD_SLOT_AB_ENABLE),true)
include $(CLEAR_VARS)
LOCAL_MODULE := kfld-$(TARGET_BUILD_VARIANT)
LOCAL_STATIC_LIBRARIES := \
	$(SHARED_STATIC_LIBRARIES)
LOCAL_CFLAGS := $(SHARED_CFLAGS)
LOCAL_SRC_FILES := kfld.c
LOCAL_MODULE_STEM := kfld
LOCAL_C_INCLUDES += $(addprefix $(LOCAL_PATH)/,avb)
LOCAL_C_INCLUDES += $(addprefix $(LOCAL_PATH)/,avb/libavb)
LOCAL_C_INCLUDES += $(addprefix $(LOCAL_PATH)/,avb/libavb_ab)
ifeq ($(TARGET_UEFI_ARCH),x86_64)
    ELF_OUTPUT := elf64-x86-64
else
    ELF_OUTPUT := elf32-i386
endif

include $(BUILD_EFI_EXECUTABLE) # For installer-$(TARGET_BUILD_VARIANT)
endif # BOARD_SLOT_AB_ENABLE
endif # BOOTLOADER_SLOT

ifeq ($(KERNELFLINGER_SUPPORT_NON_EFI_BOOT),true)

include $(CLEAR_VARS)
LOCAL_MODULE := kf4sbl-$(TARGET_BUILD_VARIANT)
LOCAL_MODULE_STEM := kf4sbl
LOCAL_CFLAGS := $(SHARED_CFLAGS)

ifeq ($(KERNELFLINGER_DISABLE_DEBUG_PRINT),true)
    LOCAL_CFLAGS += -D__DISABLE_DEBUG_PRINT
endif


LOCAL_STATIC_LIBRARIES += \
	libfastboot-$(TARGET_BUILD_VARIANT) \
	libefiusb-$(TARGET_BUILD_VARIANT) \
	libefitcp-$(TARGET_BUILD_VARIANT) \
	libtransport-$(TARGET_BUILD_VARIANT) \
	libheci-$(TARGET_BUILD_VARIANT) \
	$(SHARED_STATIC_LIBRARIES) \
	libpayload \
	libefiwrapper-$(TARGET_BUILD_VARIANT) \
	libefiwrapper_drivers-$(TARGET_BUILD_VARIANT) \
	efiwrapper-$(TARGET_BUILD_VARIANT) \
	libelfloader-$(TARGET_BUILD_VARIANT) \
	libxbc-$(TARGET_BUILD_VARIANT)

ifeq ($(TARGET_USE_TRUSTY),true)
    LOCAL_STATIC_LIBRARIES += libqltipc-$(TARGET_BUILD_VARIANT)
endif

LOCAL_STATIC_LIBRARIES += libavb_kernelflinger-$(TARGET_BUILD_VARIANT)

ifneq ($(TARGET_BUILD_VARIANT),user)
    LOCAL_STATIC_LIBRARIES += libadb-$(TARGET_BUILD_VARIANT)
endif
#Replace kf4sbl.c by kernelflinger.c
LOCAL_SRC_FILES := \
	kernelflinger.c

ifneq ($(TARGET_BUILD_VARIANT),user)
    LOCAL_SRC_FILES += unittest.c
endif

ifneq ($(strip $(KERNELFLINGER_USE_UI)),false)
    LOCAL_SRC_FILES += \
        ux.c
endif

keys4abl_intermediates := $(call intermediates-dir-for,ABL,keys4abl)

ABL_AVB_PK := $(keys4abl_intermediates)/avb_pk.bin
ABL_PADDED_AVB_PK := $(keys4abl_intermediates)/avb_pk.padded.bin
ABL_AVB_PK_OBJ := $(keys4abl_intermediates)/avb_pk.o
ifndef BOARD_AVB_KEY_PATH
BOOTLOADER_AVB_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
else
BOOTLOADER_AVB_KEY_PATH := $(BOARD_AVB_KEY_PATH)
endif

$(ABL_AVB_PK): $(BOOTLOADER_AVB_KEY_PATH)
	external/avb/avbtool.py extract_public_key --key $< --output $@

$(ABL_PADDED_AVB_PK): $(ABL_AVB_PK)
	$(call pad-binary, 4096)

ifeq ($(TARGET_IAFW_ARCH),x86_64)
    ELF_OUTPUT := elf64-x86-64
else
    ELF_OUTPUT := elf32-i386
endif

avb_sym_binary := $(shell echo _binary_$(ABL_PADDED_AVB_PK) | sed "s/[\/\.-]/_/g")
$(ABL_AVB_PK_OBJ): $(ABL_PADDED_AVB_PK)
	mkdir -p $(@D) && \
	$(EFI_OBJCOPY) --input binary --output $(ELF_OUTPUT) --binary-architecture i386 $< $@ && \
	$(EFI_OBJCOPY) --redefine-sym $(avb_sym_binary)_start=_binary_avb_pk_start \
                       --redefine-sym $(avb_sym_binary)_end=_binary_avb_pk_end \
                       --redefine-sym $(avb_sym_binary)_size=_binary_avb_pk_size \
                       --rename-section .data=.oemkeys $@ $@

LOCAL_GENERATED_SOURCES += $(ABL_AVB_PK_OBJ)
LOCAL_C_INCLUDES := \
	$(addprefix $(LOCAL_PATH)/,avb)
LOCAL_C_INCLUDES += \
	$(addprefix $(LOCAL_PATH)/,libkernelflinger)
LOCAL_C_INCLUDES += \
	$(addprefix $(LOCAL_PATH)/,libkernelflinger/fatfs/source)

LOCAL_C_INCLUDES += \
	$(addprefix $(LOCAL_PATH)/,libsslsupport)
include $(BUILD_SBL_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := fb4sbl-$(TARGET_BUILD_VARIANT)
LOCAL_MODULE_STEM := fb4sbl
LOCAL_CFLAGS := $(SHARED_CFLAGS)

LOCAL_CFLAGS += -D__FORCE_FASTBOOT

LOCAL_STATIC_LIBRARIES += \
	libfastboot-$(TARGET_BUILD_VARIANT) \
	libefiusb-$(TARGET_BUILD_VARIANT) \
	libefitcp-$(TARGET_BUILD_VARIANT) \
	libtransport-$(TARGET_BUILD_VARIANT) \
	libheci-$(TARGET_BUILD_VARIANT) \
	$(SHARED_STATIC_LIBRARIES) \
	libpayload \
	libefiwrapper-$(TARGET_BUILD_VARIANT) \
	libefiwrapper_drivers-$(TARGET_BUILD_VARIANT) \
	efiwrapper-$(TARGET_BUILD_VARIANT) \
	libelfloader-$(TARGET_BUILD_VARIANT) \
	libxbc-$(TARGET_BUILD_VARIANT)

ifeq ($(TARGET_USE_TRUSTY),true)
    LOCAL_STATIC_LIBRARIES += libqltipc-$(TARGET_BUILD_VARIANT)
endif

ifneq ($(TARGET_BUILD_VARIANT),user)
    LOCAL_STATIC_LIBRARIES += libadb-$(TARGET_BUILD_VARIANT)
endif
LOCAL_STATIC_LIBRARIES += libavb_kernelflinger-$(TARGET_BUILD_VARIANT)
#reuse kernelflinger.c
LOCAL_SRC_FILES := \
	kernelflinger.c

ifneq ($(strip $(KERNELFLINGER_USE_UI)),false)
    LOCAL_SRC_FILES += \
        ux.c
endif

ifneq ($(TARGET_BUILD_VARIANT),user)
    LOCAL_SRC_FILES += unittest.c
endif

LOCAL_GENERATED_SOURCES += $(ABL_AVB_PK_OBJ)
LOCAL_C_INCLUDES := \
	$(addprefix $(LOCAL_PATH)/,avb)
LOCAL_C_INCLUDES += \
	$(addprefix $(LOCAL_PATH)/,libkernelflinger)
LOCAL_C_INCLUDES += \
	$(addprefix $(LOCAL_PATH)/,libkernelflinger/fatfs/source)

LOCAL_C_INCLUDES += \
	$(addprefix $(LOCAL_PATH)/,libsslsupport)
include $(BUILD_SBL_EXECUTABLE)

include $(CLEAR_VARS)
# crash dump for sbl
LOCAL_MODULE := cd4sbl-$(TARGET_BUILD_VARIANT)
LOCAL_MODULE_STEM := cd4sbl
LOCAL_CFLAGS := $(SHARED_CFLAGS)

LOCAL_CFLAGS += -D__CRASH_DUMP

LOCAL_STATIC_LIBRARIES += \
	$(SHARED_STATIC_LIBRARIES) \
	libpayload \
	libefiwrapper-cd-$(TARGET_BUILD_VARIANT) \
	libefiwrapper_drivers-cd-$(TARGET_BUILD_VARIANT) \
	efiwrapper-cd-$(TARGET_BUILD_VARIANT)

LOCAL_SRC_FILES := \
	crashdump.c

include $(BUILD_SBL_EXECUTABLE)

endif  #KERNELFLINGER_SUPPORT_NON_EFI_BOOT


include $(CLEAR_VARS)
LOCAL_MODULE := kf4cic-$(TARGET_BUILD_VARIANT)
LOCAL_STATIC_LIBRARIES := \
	$(SHARED_STATIC_LIBRARIES)

LOCAL_CFLAGS := $(SHARED_CFLAGS)
LOCAL_CFLAGS := $(filter-out -DCRASHMODE_USE_ADB, $(LOCAL_CFLAGS))
LOCAL_SRC_FILES := kf4cic.c
LOCAL_MODULE_STEM := kf4cic

ifneq ($(strip $(KERNELFLINGER_USE_UI)),false)
	LOCAL_SRC_FILES += \
	ux.c
endif

LOCAL_OBJCOPY_FLAGS := -j .oemkeys

LOCAL_C_INCLUDES += \
	$(addprefix $(LOCAL_PATH)/,include/libkernelflinger) \
	$(addprefix $(LOCAL_PATH)/,libsslsupport)
LOCAL_C_INCLUDES += $(addprefix $(LOCAL_PATH)/,avb)

ifeq ($(TARGET_USE_TRUSTY),true)
    LOCAL_STATIC_LIBRARIES += libqltipc-$(TARGET_BUILD_VARIANT)
endif

LOCAL_STATIC_LIBRARIES += libavb_kernelflinger-$(TARGET_BUILD_VARIANT)

LOCAL_GENERATED_SOURCES += $(AVB_PK_OBJ)

include $(BUILD_EFI_EXECUTABLE) # For kf4cic-$(TARGET_BUILD_VARIANT)
