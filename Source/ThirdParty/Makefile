LIBWEBRTC_MODULE = libwebrtc

TO_LOWER = $(shell echo $(1) | tr [:upper:] [:lower:])

ifeq (iosmac,$(SDK_VARIANT))
	DISABLE_LIBWEBRTC = 1
endif

ifneq (,$(DISABLE_LIBWEBRTC))
	LIBWEBRTC_MODULE =
endif

MODULES = ANGLE $(LIBWEBRTC_MODULE)

define build_target_for_each_module
	for dir in $(MODULES); do \
		${MAKE} $@ -C $$dir PATH_FROM_ROOT=$(PATH_FROM_ROOT)/$${dir}; \
		exit_status=$$?; \
		[ $$exit_status -ne 0 ] && exit $$exit_status; \
	done; true
endef

all:
	@$(build_target_for_each_module)

debug d:
	@$(build_target_for_each_module)

release r:
	@$(build_target_for_each_module)

release+assert ra:
	@$(build_target_for_each_module)

testing t:
	@$(build_target_for_each_module)

analyze:
	@$(build_target_for_each_module)

clean:
	@$(build_target_for_each_module)
