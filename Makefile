.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET		:=	ClaudeFor3DS
BUILD		:=	build
SOURCES		:=	source source/platform source/core source/net source/claude source/app source/ui source/util library/qrcodegen
DATA		:=	data
INCLUDES	:=	source library library/stb library/qrcodegen
GRAPHICS	:=	gfx
GFXBUILD	:=	$(BUILD)
ROMFS		:=	romfs

RESOURCES	:=	resources
APP_INFO	:=	$(RESOURCES)/AppInfo
BANNER_AUDIO	:=	$(RESOURCES)/audio
BANNER_IMAGE	:=	$(RESOURCES)/banner
ICON		:=	$(RESOURCES)/icon.png
RSF		:=	$(TOPDIR)/$(RESOURCES)/$(TARGET).rsf

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections $(ARCH)
CFLAGS	+=	$(INCLUDE) -D__3DS__ -DRAPIDJSON_HAS_STDSTRING=1

CXXFLAGS	:=	$(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17 -Wno-psabi

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,--gc-sections -Wl,-Map,$(notdir $*.map)

LIBS	:=	-lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lcitro2d -lcitro3d -lctru -lm

LIBDIRS	:=	$(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD	:=	$(CXX)

export OFILES_SOURCES	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES		:=	$(OFILES_BIN) $(OFILES_SOURCES)
export HFILES		:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_ICON	:=	$(TOPDIR)/$(ICON)
export _3DSXDEPS	:=	$(OUTPUT).smdh
export _3DSXFLAGS	+=	--smdh=$(CURDIR)/$(TARGET).smdh --romfs=$(CURDIR)/$(ROMFS)

.PHONY: all clean cia 3dsx host

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

cia 3dsx: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile $@

host:
	@$(MAKE) --no-print-directory -f $(CURDIR)/host/Makefile -C $(CURDIR)

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf $(TARGET).cia banner.bnr icon.icn host/build

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

include $(TOPDIR)/$(APP_INFO)

VERSION_DEFS	:=	-DAPP_VERSION_MAJOR=$(APP_VERSION_MAJOR) -DAPP_VERSION_MINOR=$(APP_VERSION_MINOR) \
			-DAPP_VERSION_MICRO=$(APP_VERSION_MICRO)
CFLAGS		+=	$(VERSION_DEFS)
CXXFLAGS	+=	$(VERSION_DEFS)

ifneq ("$(wildcard $(TOPDIR)/$(BANNER_IMAGE).cgfx)","")
	BANNER_IMAGE_FILE	:=	$(TOPDIR)/$(BANNER_IMAGE).cgfx
	BANNER_IMAGE_ARG	:=	-ci $(BANNER_IMAGE_FILE)
else
	BANNER_IMAGE_FILE	:=	$(TOPDIR)/$(BANNER_IMAGE).png
	BANNER_IMAGE_ARG	:=	-i $(BANNER_IMAGE_FILE)
endif

ifneq ("$(wildcard $(TOPDIR)/$(BANNER_AUDIO).cwav)","")
	BANNER_AUDIO_FILE	:=	$(TOPDIR)/$(BANNER_AUDIO).cwav
	BANNER_AUDIO_ARG	:=	-ca $(BANNER_AUDIO_FILE)
else
	BANNER_AUDIO_FILE	:=	$(TOPDIR)/$(BANNER_AUDIO).wav
	BANNER_AUDIO_ARG	:=	-a $(BANNER_AUDIO_FILE)
endif

MAKEROM		?=	makerom
BANNERTOOL	?=	bannertool

MAKEROM_PARAMS	:=	-rsf $(RSF) -target t -exefslogo -elf $(OUTPUT).elf -icon icon.icn -banner banner.bnr \
			-DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(APP_PRODUCT_CODE)" \
			-DAPP_UNIQUE_ID="$(APP_UNIQUE_ID)" -DAPP_ROMFS="$(TOPDIR)/$(ROMFS)" \
			-major "$(APP_VERSION_MAJOR)" -minor "$(APP_VERSION_MINOR)" -micro "$(APP_VERSION_MICRO)"

.PHONY: all cia 3dsx

all: $(OUTPUT).3dsx $(OUTPUT).cia

3dsx: $(OUTPUT).3dsx

cia: $(OUTPUT).cia

$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OUTPUT).smdh	:	$(APP_ICON)
	@smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $(APP_ICON) $@

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	:	$(OFILES)

$(OUTPUT).cia	:	$(OUTPUT).elf banner.bnr icon.icn
	@$(MAKEROM) -f cia -o $@ -DAPP_ENCRYPTED=false $(MAKEROM_PARAMS)
	@echo built ... $(notdir $@)

banner.bnr	:	$(BANNER_IMAGE_FILE) $(BANNER_AUDIO_FILE)
	@$(BANNERTOOL) makebanner $(BANNER_IMAGE_ARG) $(BANNER_AUDIO_ARG) -o $@ > /dev/null
	@echo built ... $(notdir $@)

icon.icn	:	$(APP_ICON)
	@$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i $(APP_ICON) -o $@ > /dev/null
	@echo built ... $(notdir $@)

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
