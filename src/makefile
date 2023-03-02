include prorab.mk
include prorab-license.mk
include prorab-clang-format.mk

$(eval $(call prorab-config, ../config))

this_name := opros

this_soname := $(shell cat $(d)soname.txt)

this_srcs := $(call prorab-src-dir,.)

this_ldlibs += -lutki

$(eval $(prorab-build-lib))

$(eval $(prorab-clang-format))

this_src_dir := 
this_license_file := ../LICENSE
$(eval $(prorab-license))
