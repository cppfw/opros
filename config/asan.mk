include $(config_dir)rel.mk

# no lint
this_lint_cmd :=

this_cxxflags += -fsanitize=address
this_ldflags += -fsanitize=address
