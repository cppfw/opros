include prorab.mk
include prorab-test.mk
include myci.mk

$(eval $(call myci-try-include, $(CONANBUILDINFO_DIR)conanbuildinfo.mak))

$(eval $(call prorab-config, ../../config))

this_name := tests

this_srcs += main.cpp tests.cpp ../helpers/queue.cpp

this_cxxflags += $(addprefix -I,$(CONAN_INCLUDE_DIRS))
this_ldflags += $(addprefix -L,$(CONAN_LIB_DIRS))

this_ldlibs += -lopros -lutki -lpthread
this_ldflags += -L$(d)../../src/out/$(c)

this_no_install := true

$(eval $(prorab-build-app))

# add dependency on libopros.so
$(prorab_this_name): $(abspath $(d)../../src/out/$(c)/libopros$(dot_so))

this_test_deps := $(prorab_this_name)
this_test_cmd:= $(prorab_this_name)
this_test_ld_path := ../../src/out/$(c) $(CONAN_LIB_DIRS)
$(eval $(prorab-test))

#include makefile for building opros
$(eval $(call prorab-include, ../../src/makefile))
