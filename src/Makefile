CFLAGS = -W -Wall -fPIC -O3
#CFLAGS = -W -Wall -fPIC -g -DDEBUG

LDFLAGS = -ldl -lpthread -lpmem
TARGET = libnvmmio.so

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))

all : $(TARGET)

$(TARGET) : $(OBJECTS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)
	
clean :
	$(RM) $(OBJECTS) $(TARGET)
