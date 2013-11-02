CC         = gcc
CFLAGS     = -c -Wall
LDFLAGS    = 

SOURCES    = test2.c
OBJECTS    = $(SOURCES:.c=.o)

EXECUTABLE = bin/test2

#CFLAGS     += -D USE_WEBKIT2
#CFLAGS     += `pkg-config --cflags gtk+-3.0 webkit2gtk-3.0`
#LDFLAGS    += `pkg-config --libs gtk+-3.0 webkit2gtk-3.0`

CFLAGS     += `pkg-config --cflags gtk+-3.0 webkitgtk-3.0`
LDFLAGS    += `pkg-config --libs gtk+-3.0 webkitgtk-3.0`

all: $(OBJECTS) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	mkdir bin
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm $(OBJECTS) $(EXECUTABLE)

