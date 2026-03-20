CC = clang
CFLAGS = -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -O2
FRAMEWORKS = -framework IOKit -framework CoreFoundation
LIBS = -lm

.PHONY: all clean

all: chooser sensors space_impact flappy_bird space_invaders dino

chooser: chooser.c game_engine.h sensors.h
	$(CC) $(CFLAGS) -o $@ chooser.c $(FRAMEWORKS) $(LIBS)

sensors: sensors.c
	$(CC) $(CFLAGS) -o $@ $< $(FRAMEWORKS) $(LIBS)

space_impact: space_impact.c game_engine.h sensors.h
	$(CC) $(CFLAGS) -o $@ space_impact.c $(FRAMEWORKS) $(LIBS)

flappy_bird: flappy_bird.c game_engine.h sensors.h
	$(CC) $(CFLAGS) -o $@ flappy_bird.c $(FRAMEWORKS) $(LIBS)

space_invaders: space_invaders.c game_engine.h sensors.h
	$(CC) $(CFLAGS) -o $@ space_invaders.c $(FRAMEWORKS) $(LIBS)

dino: dino.c game_engine.h sensors.h
	$(CC) $(CFLAGS) -o $@ dino.c $(FRAMEWORKS) $(LIBS)

clean:
	rm -f chooser sensors space_impact flappy_bird space_invaders dino
