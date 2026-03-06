# osrs-lua-enabler — GNU Make + MSVC
# Use `-` prefix for cl/link flags to avoid MSYS path mangling.

VCVARS_SH = tools/vcvars.sh
MAKEFLAGS += --no-print-directory

# --- MSVC environment bootstrap -----------------------------------------------
ifeq ($(shell which cl.exe 2>/dev/null),)

_%:
	@eval "$$($(VCVARS_SH) x64)" && $(MAKE) $*
_all:
	@eval "$$($(VCVARS_SH) x64)" && $(MAKE) all

%: _%
	@true
all: _all
	@true
.DEFAULT_GOAL := _all

else # cl.exe found — real build follows

CXX      = cl.exe
LINK     = link.exe

BUILD    = build
OBJDIR   = $(BUILD)/obj
EXTDIR   = external
OSCLIENT = C:/Program Files (x86)/Jagex Launcher/Games/Old School RuneScape/Client/osclient.exe

# --- Compiler flags -----------------------------------------------------------

CXXFLAGS = -std:c++20 -Zc:preprocessor -MT -EHsc -O2 -DNDEBUG -nologo -W3
CXX_INCLUDES = -Isrc -I$(EXTDIR)/libhat/include

DLL_LDFLAGS = -DLL -OPT:REF -OPT:ICF -NODEFAULTLIB:MSVCRT -nologo
DLL_LIBS    = kernel32.lib user32.lib

# --- Sources ------------------------------------------------------------------

LIBHAT_SRCS = \
	$(EXTDIR)/libhat/src/Scanner.cpp \
	$(EXTDIR)/libhat/src/System.cpp \
	$(EXTDIR)/libhat/src/os/win32/MemoryProtector.cpp \
	$(EXTDIR)/libhat/src/os/win32/Process.cpp \
	$(EXTDIR)/libhat/src/os/win32/Scanner.cpp \
	$(EXTDIR)/libhat/src/os/win32/System.cpp \
	$(EXTDIR)/libhat/src/arch/x86/System.cpp \
	$(EXTDIR)/libhat/src/arch/x86/SSE.cpp

MAIN_OBJ          = $(OBJDIR)/src/main.obj
LIBHAT_OBJS       = $(patsubst %.cpp,$(OBJDIR)/%.obj,$(LIBHAT_SRCS))
LIBHAT_AVX2_OBJ   = $(OBJDIR)/$(EXTDIR)/libhat/src/arch/x86/AVX2.obj
LIBHAT_AVX512_OBJ = $(OBJDIR)/$(EXTDIR)/libhat/src/arch/x86/AVX512.obj

ALL_OBJS   = $(MAIN_OBJ) $(LIBHAT_OBJS) $(LIBHAT_AVX2_OBJ) $(LIBHAT_AVX512_OBJ)
INJECT_OBJ = $(BUILD)/inject.obj

# --- Targets ------------------------------------------------------------------

.PHONY: all clean run _run-kill

all: $(BUILD)/lua_enabler.dll $(BUILD)/inject.exe

# Kill the client first (frees the DLL for relinking), then build, launch, inject.
run: _run-kill all
	@$(BUILD)/inject.exe $(BUILD)/lua_enabler.dll --launch "$(OSCLIENT)"

_run-kill:
	@taskkill //F //IM osclient.exe >/dev/null 2>&1 || true

# Remove build/. Retries with a force-kill if osclient is still holding the DLL.
clean:
	@rm -rf $(BUILD) 2>/dev/null || true
	@if [ -d "$(BUILD)" ]; then \
		taskkill //F //IM osclient.exe >/dev/null 2>&1 || true; \
		rm -rf "$(BUILD)"; \
	fi

# --- Build rules --------------------------------------------------------------

$(BUILD)/lua_enabler.dll: $(ALL_OBJS) | $(BUILD)
	$(LINK) $(DLL_LDFLAGS) -OUT:$@ $^ $(DLL_LIBS)

$(BUILD)/inject.exe: $(INJECT_OBJ) | $(BUILD)
	$(LINK) -nologo -OUT:$@ $^ kernel32.lib user32.lib

$(OBJDIR)/%.obj: %.cpp | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CXX_INCLUDES) -c $< -Fo$@

$(LIBHAT_AVX2_OBJ): $(EXTDIR)/libhat/src/arch/x86/AVX2.cpp | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CXX_INCLUDES) -arch:AVX2 -c $< -Fo$@

$(LIBHAT_AVX512_OBJ): $(EXTDIR)/libhat/src/arch/x86/AVX512.cpp | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CXX_INCLUDES) -arch:AVX512 -c $< -Fo$@

$(INJECT_OBJ): tools/inject.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -Fo$@

$(BUILD) $(OBJDIR):
	mkdir -p $@

endif # cl.exe check
