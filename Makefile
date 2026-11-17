# nmake build for the static libraries and tests. Run toolchain\env.cmd first.
#
#   nmake            build all libraries and tests
#   nmake test       build and run the tests
#   nmake clean

# _SECURE_SCL_THROWS=1: the originals' checked iterators throw out_of_range / invalid_argument
# ("invalid vector<T> subscript") instead of calling _invalid_parameter.
CFLAGS  = /nologo /c /O2 /MT /EHsc /GS /W3 /Zi /DWIN32 /DNDEBUG /D_WINDOWS \
          /D_WIN32_WINNT=0x0500 /DWIN32_LEAN_AND_MEAN /D_SECURE_SCL_THROWS=1 \
          /IJMX_Library /IJMX_ServerFramework
LIBFLAGS = /nologo
SYSLIBS = ws2_32.lib user32.lib ole32.lib

# nmake SECURITY_CONSTANTS=1 compiles in the protocol tables generated from your own copy of
# the original binaries (see tools\gen_security_constants.py); without them the security layer
# only talks to other rebuilt peers.
!IFDEF SECURITY_CONSTANTS
CFLAGS = $(CFLAGS) /DBSNET_SECURITY_CONSTANTS
!ENDIF

OUT = build
OBJ = $(OUT)\obj
LIBDIR = $(OUT)\lib
BIN = $(OUT)\bin

BSLIB_OBJS = \
	$(OBJ)\BSLib\BSDebug.obj \
	$(OBJ)\BSLib\SynchObject.obj \
	$(OBJ)\BSLib\SimpleThread.obj \
	$(OBJ)\BSLib\ThreadManager.obj \
	$(OBJ)\BSLib\Task.obj \
	$(OBJ)\BSLib\System.obj

BSNET_OBJS = \
	$(OBJ)\BSNet\Msg.obj \
	$(OBJ)\BSNet\MsgPool.obj \
	$(OBJ)\BSNet\NetEngineLog.obj \
	$(OBJ)\BSNet\BlowFish.obj \
	$(OBJ)\BSNet\MD5.obj \
	$(OBJ)\BSNet\KeyExchange.obj \
	$(OBJ)\BSNet\Security.obj \
	$(OBJ)\BSNet\SecurityConstants.obj

SRVFW_OBJS = \
	$(OBJ)\ServerFramework\MassiveMsg.obj

LIBS = $(LIBDIR)\ServerFramework.lib $(LIBDIR)\BSNet.lib $(LIBDIR)\BSLib.lib
TESTS = $(BIN)\MsgTest.exe $(BIN)\SynchTest.exe $(BIN)\DebugTest.exe $(BIN)\PoolTest.exe \
        $(BIN)\QueTest.exe $(BIN)\MassiveMsgTest.exe $(BIN)\SecurityTest.exe \
        $(BIN)\ThreadTest.exe

all: $(LIBS) $(TESTS)

$(OBJ)\BSLib $(OBJ)\BSNet $(OBJ)\ServerFramework $(OBJ)\test $(LIBDIR) $(BIN):
	@if not exist $@ mkdir $@

{JMX_Library\BSLib}.cpp{$(OBJ)\BSLib}.obj:
	cl $(CFLAGS) /Fo$@ /Fd$(OBJ)\BSLib\ $<

{JMX_Library\BSLib\thread}.cpp{$(OBJ)\BSLib}.obj:
	cl $(CFLAGS) /Fo$@ /Fd$(OBJ)\BSLib\ $<

{JMX_Library\BSNet}.cpp{$(OBJ)\BSNet}.obj:
	cl $(CFLAGS) /Fo$@ /Fd$(OBJ)\BSNet\ $<

{JMX_Library\BSNet\security}.cpp{$(OBJ)\BSNet}.obj:
	cl $(CFLAGS) /Fo$@ /Fd$(OBJ)\BSNet\ $<

{JMX_ServerFramework\ServerFramework}.cpp{$(OBJ)\ServerFramework}.obj:
	cl $(CFLAGS) /Fo$@ /Fd$(OBJ)\ServerFramework\ $<

{test}.cpp{$(OBJ)\test}.obj:
	cl $(CFLAGS) /Fo$@ /Fd$(OBJ)\test\ $<

$(LIBDIR)\BSLib.lib: $(OBJ)\BSLib $(LIBDIR) $(BSLIB_OBJS)
	lib $(LIBFLAGS) /out:$@ $(BSLIB_OBJS)

$(LIBDIR)\BSNet.lib: $(OBJ)\BSNet $(LIBDIR) $(BSNET_OBJS)
	lib $(LIBFLAGS) /out:$@ $(BSNET_OBJS)

$(LIBDIR)\ServerFramework.lib: $(OBJ)\ServerFramework $(LIBDIR) $(SRVFW_OBJS)
	lib $(LIBFLAGS) /out:$@ $(SRVFW_OBJS)

{$(OBJ)\test}.obj{$(BIN)}.exe:
	link /nologo /DEBUG /out:$@ $< $(LIBS) $(SYSLIBS)

$(TESTS): $(OBJ)\test $(BIN) $(LIBS)

$(BIN)\MsgTest.exe: $(OBJ)\test\MsgTest.obj
$(BIN)\SynchTest.exe: $(OBJ)\test\SynchTest.obj
$(BIN)\DebugTest.exe: $(OBJ)\test\DebugTest.obj
$(BIN)\PoolTest.exe: $(OBJ)\test\PoolTest.obj
$(BIN)\QueTest.exe: $(OBJ)\test\QueTest.obj
$(BIN)\MassiveMsgTest.exe: $(OBJ)\test\MassiveMsgTest.obj
$(BIN)\SecurityTest.exe: $(OBJ)\test\SecurityTest.obj
$(BIN)\ThreadTest.exe: $(OBJ)\test\ThreadTest.obj

test: all
	$(BIN)\MsgTest.exe
	$(BIN)\SynchTest.exe
	$(BIN)\DebugTest.exe
	$(BIN)\PoolTest.exe
	$(BIN)\QueTest.exe
	$(BIN)\MassiveMsgTest.exe
	$(BIN)\SecurityTest.exe
	$(BIN)\ThreadTest.exe

clean:
	@if exist $(OUT) rmdir /s /q $(OUT)
