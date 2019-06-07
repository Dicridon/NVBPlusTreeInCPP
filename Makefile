TARGET = perf
CC = g++
SRCS = $(shell ls *.cpp)
OBJS = $(SRCS:.cpp=.o)
CFLAGS = -g
LIBS = -lpmemobj


TARGET: $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LIBS) $(CFLAGS)

%.o:%.cpp
	$(CC) -c $< -o $@ $(CFLAGS) $(LIBS)

clean:
	rm *.o $(TARGET) -f
