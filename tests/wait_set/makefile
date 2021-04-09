include prorab.mk

$(eval $(call prorab-config, ../../config))

this_name := tests

this_srcs += main.cpp tests.cpp ../helpers/queue.cpp

this_ldlibs += -lopros -lutki -lpthread
this_ldflags += -L$(d)../../src/out/$(c)

this_no_install := true

$(eval $(prorab-build-app))

# add dependency on libopros.so
$(prorab_this_name): $(abspath $(d)../../src/out/$(c)/libopros$(dot_so))

include $(d)../test_target.mk

#include makefile for building opros
$(eval $(call prorab-include, ../../src/makefile))
