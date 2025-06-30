include prorab.mk
include prorab-test.mk

$(eval $(call prorab-config, ../../config))

this_name := tests

this_srcs += main.cpp tests.cpp ../helpers/queue.cpp

this_ldlibs += -l utki$(this_dbg)
this_ldlibs += -l pthread

this__libopros := ../../src/out/$(c)/libopros$(this_dbg)$(dot_so)

this_ldlibs += $(this__libopros)

this_no_install := true

$(eval $(prorab-build-app))

this_test_deps := $(prorab_this_name) $(this__libopros)
this_test_cmd:= $(prorab_this_name)
this_test_ld_path := ../../src/out/$(c)
$(eval $(prorab-test))

# include makefile for building opros
$(eval $(call prorab-include, ../../src/makefile))
