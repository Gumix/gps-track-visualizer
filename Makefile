CC = gcc
IDIR = /opt/local/include/libxml2/
LDIR = /opt/local/lib/
LIBS = -lc -lxml2 -lproj -lgd
CFLAGS = -march=native -Ofast -Wa,-q
NAME = gps-track-visualizer
SRC = $(NAME).c
OBJ = $(NAME).o

$(NAME): $(OBJ)
	$(CC) -o $@ $^ -L $(LDIR) $(LIBS)

$(OBJ): $(SRC)
	$(CC) -c -o $@ $^ -I $(IDIR) $(CFLAGS)

.PHONY: clean

clean:
	rm $(NAME) $(OBJ)
