OS = windows
ARCH = %Platform%
BIN_NAME = merton-nes.dll

OBJS = \
	src\cart.obj \
	src\apu.obj \
	src\cpu.obj \
	src\sys.obj \
	src\ppu.obj \
	src\retro.obj

FLAGS = \
	/W4 \
	/MT \
	/MP \
	/volatile:iso \
	/nologo

INCLUDES = \
	-I.

DEFS = \
	-DWIN32_LEAN_AND_MEAN \
	-DUNICODE

LINK_FLAGS = \
	/nodefaultlib \
	/nologo \
	/dll

LIBS = \
	libvcruntime.lib \
	libucrt.lib \
	libcmt.lib \
	kernel32.lib

!IFDEF DEBUG
FLAGS = $(FLAGS) /Oy- /Ob0 /Zi
LINK_FLAGS = $(LINK_FLAGS) /debug
!ELSE
FLAGS = $(FLAGS) /O2 /GS- /Gw /Gy /DL /GL
LINK_FLAGS = $(LINK_FLAGS) /LTCG
!ENDIF

CFLAGS = $(INCLUDES) $(DEFS) $(FLAGS)

all: clean clear $(OBJS)
	link /out:$(BIN_NAME) $(LINK_FLAGS) *.obj $(LIBS) $(RESOURCES)

merton: all
	copy $(BIN_NAME) ..\merton\merton-files\cores

clean:
	@del /q *.obj 2>nul
	@del /q *.lib 2>nul
	@del /q *.dll 2>nul
	@del /q *.ilk 2>nul
	@del /q *.pdb 2>nul
	@del /q *.exp 2>nul

clear:
	@cls
