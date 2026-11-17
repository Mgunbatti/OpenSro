# nmake build for the static libraries and tests. Run toolchain\env.cmd first.
#
#   nmake            build all libraries and tests
#   nmake test       build and run the tests
#   nmake clean

# _SECURE_SCL_THROWS=1: the originals' checked iterators throw out_of_range / invalid_argument
# ("invalid vector<T> subscript") instead of calling _invalid_parameter.
CFLAGS  = /nologo /c /O2 /MT /EHsc /GS /W3 /Zi /DWIN32 /DNDEBUG /D_WINDOWS \
          /D_WIN32_WINNT=0x0500 /D_SECURE_SCL_THROWS=1 /IJMX_Library
LIBFLAGS = /nologo
SYSLIBS = ws2_32.lib user32.lib

OUT = build
OBJ = $(OUT)\obj
LIBDIR = $(OUT)\lib
BIN = $(OUT)\bin

BSLIB_OBJS = \
	$(OBJ)\BSLib\BSDebug.obj \
	$(OBJ)\BSLib\SynchObject.obj

BSNET_OBJS = \
	$(OBJ)\BSNet\Msg.obj

LIBS = $(LIBDIR)\BSLib.lib $(LIBDIR)\BSNet.lib
TESTS = $(BIN)\MsgTest.exe $(BIN)\SynchTest.exe $(BIN)\DebugTest.exe

all: $(LIBS) $(TESTS)

$(OBJ)\BSLib $(OBJ)\BSNet $(OBJ)\test $(LIBDIR) $(BIN):
	@if not exist $@ mkdir $@

{JMX_Library\BSLib}.cpp{$(OBJ)\BSLib}.obj:
	cl $(CFLAGS) /Fo$@ /Fd$(OBJ)\BSLib\ $<

{JMX_Library\BSLib\thread}.cpp{$(OBJ)\BSLib}.obj:
	cl $(CFLAGS) /Fo$@ /Fd$(OBJ)\BSLib\ $<

{JMX_Library\BSNet}.cpp{$(OBJ)\BSNet}.obj:
	cl $(CFLAGS) /Fo$@ /Fd$(OBJ)\BSNet\ $<

{test}.cpp{$(OBJ)\test}.obj:
	cl $(CFLAGS) /Fo$@ /Fd$(OBJ)\test\ $<

$(LIBDIR)\BSLib.lib: $(OBJ)\BSLib $(LIBDIR) $(BSLIB_OBJS)
	lib $(LIBFLAGS) /out:$@ $(BSLIB_OBJS)

$(LIBDIR)\BSNet.lib: $(OBJ)\BSNet $(LIBDIR) $(BSNET_OBJS)
	lib $(LIBFLAGS) /out:$@ $(BSNET_OBJS)

$(BIN)\MsgTest.exe: $(OBJ)\test $(BIN) $(OBJ)\test\MsgTest.obj $(LIBS)
	link /nologo /DEBUG /out:$@ $(OBJ)\test\MsgTest.obj $(LIBS) $(SYSLIBS)

$(BIN)\SynchTest.exe: $(OBJ)\test $(BIN) $(OBJ)\test\SynchTest.obj $(LIBS)
	link /nologo /DEBUG /out:$@ $(OBJ)\test\SynchTest.obj $(LIBS) $(SYSLIBS)

$(BIN)\DebugTest.exe: $(OBJ)\test $(BIN) $(OBJ)\test\DebugTest.obj $(LIBS)
	link /nologo /DEBUG /out:$@ $(OBJ)\test\DebugTest.obj $(LIBS) $(SYSLIBS)

test: all
	$(BIN)\MsgTest.exe
	$(BIN)\SynchTest.exe
	$(BIN)\DebugTest.exe

clean:
	@if exist $(OUT) rmdir /s /q $(OUT)
