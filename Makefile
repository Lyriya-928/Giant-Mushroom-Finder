# Giant Mushroom Island Finder — native + Java build (Windows / MinGW)
#
#   mingw32-make -f Makefile CC=gcc all
#   mingw32-make -f Makefile CC=gcc native
#   mingw32-make -f Makefile CC=gcc jar
#   mingw32-make -f Makefile CC=gcc selftest

CC      = gcc
CFLAGS  = -O3 -Wall -Wextra -fwrapv -D_WIN32 -fopenmp
LDFLAGS = -lm -fopenmp
AR      = ar
ARFLAGS = cr

# Set this if javac is not on PATH in make's shell
JAVA_HOME = C:/Program Files/Java/jdk-26.0.1
JAVAC   = javac
JAR     = jar

CUBIOMES_DIR = cubiomes
NATIVE_DIR   = native
OUT_DIR      = build
JAVA_SRC     = src/main/java
JAVA_PKG     = dev/sakuhime/mushroomfinder
JAVA_OUT     = $(OUT_DIR)/classes

CUBIOMES_SRCS = \
	$(CUBIOMES_DIR)/noise.c \
	$(CUBIOMES_DIR)/biomes.c \
	$(CUBIOMES_DIR)/layers.c \
	$(CUBIOMES_DIR)/biomenoise.c \
	$(CUBIOMES_DIR)/generator.c \
	$(CUBIOMES_DIR)/finders.c \
	$(CUBIOMES_DIR)/util.c \
	$(CUBIOMES_DIR)/quadbase.c

CUBIOMES_OBJS = $(patsubst $(CUBIOMES_DIR)/%.c,$(OUT_DIR)/cubiomes_%.o,$(CUBIOMES_SRCS))

INCLUDES = -I$(CUBIOMES_DIR) -I$(NATIVE_DIR)
JNI_INC  = -I"$(JAVA_HOME)/include" -I"$(JAVA_HOME)/include/win32"

.PHONY: all cli native jar selftest dist clean

all: cli

cli: $(OUT_DIR)/gmif_cli.exe

$(OUT_DIR)/gmif_cli.exe: $(OUT_DIR)/mushroom_finder.o $(OUT_DIR)/tile_search.o $(OUT_DIR)/release_audit.o $(OUT_DIR)/mushroom_cli.o $(OUT_DIR)/libcubiomes.a
	$(CC) -o $@ $(OUT_DIR)/mushroom_finder.o $(OUT_DIR)/tile_search.o $(OUT_DIR)/release_audit.o $(OUT_DIR)/mushroom_cli.o $(OUT_DIR)/libcubiomes.a $(LDFLAGS)

$(OUT_DIR)/libcubiomes.a: $(CUBIOMES_OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(OUT_DIR)/cubiomes_%.o: $(CUBIOMES_DIR)/%.c | $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT_DIR)/mushroom_finder.o: $(NATIVE_DIR)/mushroom_finder.c $(NATIVE_DIR)/mushroom_finder.h | $(OUT_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OUT_DIR)/tile_search.o: $(NATIVE_DIR)/tile_search.c $(NATIVE_DIR)/mushroom_finder.h | $(OUT_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OUT_DIR)/release_audit.o: $(NATIVE_DIR)/release_audit.c $(NATIVE_DIR)/mushroom_finder.h | $(OUT_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OUT_DIR)/mushroom_cli.o: $(NATIVE_DIR)/mushroom_cli.c $(NATIVE_DIR)/mushroom_finder.h | $(OUT_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OUT_DIR)/mushroomfinder.dll: $(OUT_DIR)/mushroom_finder.o $(OUT_DIR)/tile_search.o $(OUT_DIR)/mushroom_finder_JNI.o $(OUT_DIR)/libcubiomes.a
	$(CC) -shared -o $@ $(OUT_DIR)/mushroom_finder.o $(OUT_DIR)/tile_search.o $(OUT_DIR)/mushroom_finder_JNI.o $(OUT_DIR)/libcubiomes.a $(LDFLAGS)

# audit is CLI-only (not linked into DLL)

$(OUT_DIR)/mushroom_finder_JNI.o: $(NATIVE_DIR)/mushroom_finder_JNI.c $(NATIVE_DIR)/mushroom_finder.h | $(OUT_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(JNI_INC) -c $< -o $@

native: $(OUT_DIR)/mushroomfinder.dll

$(OUT_DIR):
	mkdir $(OUT_DIR)

$(JAVA_OUT): | $(OUT_DIR)
	mkdir $(JAVA_OUT)

jar: native $(JAVA_OUT)
	$(JAVAC) -encoding UTF-8 -d $(JAVA_OUT) $(JAVA_SRC)/$(JAVA_PKG)/Main.java $(JAVA_SRC)/$(JAVA_PKG)/MainFrame.java $(JAVA_SRC)/$(JAVA_PKG)/NativeBridge.java $(JAVA_SRC)/$(JAVA_PKG)/NativeLoader.java $(JAVA_SRC)/$(JAVA_PKG)/SearchRunner.java $(JAVA_SRC)/$(JAVA_PKG)/SearchSettings.java $(JAVA_SRC)/$(JAVA_PKG)/SearchResult.java $(JAVA_SRC)/$(JAVA_PKG)/SelfTest.java
	$(JAR) cfe $(OUT_DIR)/GiantMushroomFinder.jar dev.sakuhime.mushroomfinder.Main -C $(JAVA_OUT) .

selftest: jar
	java -cp $(OUT_DIR)/classes -Djava.library.path=$(OUT_DIR) dev.sakuhime.mushroomfinder.Main --self-test

dist: cli jar
	@echo Build complete

clean:
	rm -rf $(OUT_DIR)
