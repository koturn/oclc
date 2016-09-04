ifeq ($(DEBUG),true)
    OPT_CFLAGS   := -O0 -g3 -ftrapv -fstack-protector-all -D_FORTIFY_SOURCE=2
# ifneq ($(shell echo $$OSTYPE),cygwin)
#     OPT_CFLAGS   := $(OPT_CFLAGS) -fsanitize=address -fno-omit-frame-pointer
# endif
    OPT_CXXFLAGS := $(OPT_CFLAGS) -D_GLIBCXX_DEBUG
    OPT_LDLIBS   := -lssp
else
ifeq ($(OPT),true)
    OPT_CFLAGS   := -flto -Ofast -march=native -DNDEBUG
    OPT_CXXFLAGS := $(OPT_CFLAGS)
    OPT_LDFLAGS  := -flto -s
else
ifeq ($(LTO),true)
    OPT_CFLAGS   := -flto -DNDEBUG
    OPT_CXXFLAGS := $(OPT_CFLAGS)
    OPT_LDFLAGS  := -flto
else
    OPT_CFLAGS   := -O3 -DNDEBUG
    OPT_CXXFLAGS := $(OPT_CFLAGS)
    OPT_LDFLAGS  := -s
endif
endif
endif

ifeq ($(OMP),true)
    OPT_CFLAGS   := $(OPT_CFLAGS) -fopenmp
    OPT_CXXFLAGS := $(OPT_CXXFLAGS) -fopenmp
    OPT_LDFLAGS  := $(OPT_LDFLAGS) -fopenmp
else
    OPT_CFLAGS   := $(OPT_CFLAGS) -Wno-unknown-pragmas
    OPT_CXXFLAGS := $(OPT_CXXFLAGS) -Wno-unknown-pragmas
endif

WARNING_CFLAGS := \
    -Wall \
    -Wextra \
    -Wcast-align \
    -Wcast-qual \
    -Wconversion \
    -Wfloat-equal \
    -Wformat=2 \
    -Wpointer-arith \
    -Wstrict-aliasing=2 \
    -Wswitch-enum \
    -Wwrite-strings \
    -pedantic

WARNING_CXXFLAGS := \
    $(WARNING_CFLAGS) \
    -Weffc++ \
    -Woverloaded-virtual

CC         := gcc $(if $(STDC), $(addprefix -std=, $(STDC)),-std=gnu11)
CXX        := g++ $(if $(STDCXX), $(addprefix -std=, $(STDCXX)),-std=gnu++14)
MAKE       := make
MKDIR      := mkdir -p
CP         := cp
RM         := rm -f
CTAGS      := ctags
# MACROS   :=
INCS       := -Ikotlib/include/
CFLAGS     := -pipe $(WARNING_CFLAGS) $(OPT_CFLAGS) $(INCS) $(MACROS)
CXXFLAGS   := -pipe $(WARNING_CXXFLAGS) $(OPT_CXXFLAGS) $(INCS) $(MACROS)
LDFLAGS    := -pipe $(OPT_LDFLAGS)
LDLIBS     := $(OPT_LDLIBS) -lOpenCL
CTAGSFLAGS := -R --languages=c,c++
TARGET     := oclc
OBJS       := $(addsuffix .o, main)
SRCS       := $(OBJS:.o=.cpp)
DEPENDS    := depends.mk

TEST_DIR   := test
TEST_BIN   := $(TEST_DIR)/main
TEST_SRC   := $(addsuffix .cpp, $(TEST_BIN))
KERNEL_BIN := kernel.bin
KERNEL_SRC := $(KERNEL_BIN:.bin=.cl)

ifeq ($(OS),Windows_NT)
    TARGET := $(addsuffix .exe, $(TARGET))
    TEST_BIN := $(addsuffix .exe, $(TEST_BIN))
else
    TARGET := $(addsuffix .out, $(TARGET))
    TEST_BIN := $(addsuffix .exe, $(TEST_BIN))
endif
INSTALLED_TARGET := $(if $(PREFIX), $(PREFIX),/usr/local)/bin/$(TARGET)

%.exe:
	$(CXX) $(LDFLAGS) $(filter %.c %.cpp %.cxx %.cc %.o, $^) $(LDLIBS) -o $@
%.out:
	$(CXX) $(LDFLAGS) $(filter %.c %.cpp %.cxx %.cc %.o, $^) $(LDLIBS) -o $@


.PHONY: all test depends syntax ctags install uninstall clean cleanobj
all: $(TARGET)
$(TARGET): $(OBJS)

$(foreach SRC,$(SRCS),$(eval $(subst \,,$(shell $(CXX) -MM $(SRC)))))

test: $(TEST_BIN) $(KERNEL_BIN)
	./$< $(KERNEL_BIN)

$(TEST_BIN): $(TEST_SRC)
	$(MAKE) -C $(@D)

$(KERNEL_BIN): $(KERNEL_SRC) $(TARGET)
	./$(TARGET) $<

depends:
	$(CXX) -MM $(SRCS) > $(DEPENDS)

syntax:
	$(CXX) $(SRCS) $(STD_CXXFLAGS) -fsyntax-only $(WARNING_CXXFLAGS) $(INCS) $(MACROS)

ctags:
	$(CTAGS) $(CTAGSFLAGS)

install: $(INSTALLED_TARGET)
$(INSTALLED_TARGET): $(TARGET)
	@[ ! -d $(@D) ] && $(MKDIR) $(@D) || :
	$(CP) $< $@

uninstall:
	$(RM) $(INSTALLED_TARGET)

clean:
	$(RM) $(TARGET) $(OBJS)
	$(MAKE) -C $(TEST_DIR) $@

cleanobj:
	$(RM) $(OBJS)
	$(MAKE) -C $(TEST_DIR) $@
