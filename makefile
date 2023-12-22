ARCH = %%Platform%%
BIN = merton-nes.dll

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
	/wd4100 \
	/nologo

INCLUDES = \
	-I.

DEFS = \
	-DUNICODE \
	-DWIN32_LEAN_AND_MEAN

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
FLAGS = $(FLAGS) /Ob0 /Zi /Oy-
LINK_FLAGS = $(LINK_FLAGS) /debug
!ELSE
FLAGS = $(FLAGS) /O2 /GS- /Gw /GL
LINK_FLAGS = $(LINK_FLAGS) /LTCG
!ENDIF

CFLAGS = $(INCLUDES) $(DEFS) $(FLAGS)

all: clean clear $(OBJS)
	link /out:$(BIN) $(LINK_FLAGS) *.obj $(LIBS)

merton: all
	copy $(BIN) ..\merton\merton-files\cores

upload: all
	python ..\merton\assets\upload-core.py windows x86_64 $(BIN)

clean:
	@-del /q *.obj 2>nul
	@-del /q *.lib 2>nul
	@-del /q *.dll 2>nul
	@-del /q *.ilk 2>nul
	@-del /q *.pdb 2>nul
	@-del /q *.exp 2>nul

clear:
	@cls
