ARCH = %%Platform%%
NAME = merton-nes

OBJS = \
	src\core.obj \
	src\cart.obj \
	src\apu.obj \
	src\cpu.obj \
	src\sys.obj \
	src\ppu.obj

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
	-DCORE_EXPORT

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
	link /out:$(NAME).dll $(LINK_FLAGS) *.obj $(LIBS)

merton: all
	copy $(NAME).dll ..\merton\merton-files\cores

upload: all
	python ..\merton\assets\upload-core.py upload $(NAME) windows x86_64 $(NAME).dll

clean:
	@-del /q *.obj 2>nul
	@-del /q *.lib 2>nul
	@-del /q *.dll 2>nul
	@-del /q *.ilk 2>nul
	@-del /q *.pdb 2>nul
	@-del /q *.exp 2>nul

clear:
	@cls
