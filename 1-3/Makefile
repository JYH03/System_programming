CC = gcc
CFLAGS = -Wall		#오류 검사용

TARGET1 = cli
TARGET2 = srv

all: $(TARGET1) $(TARGET2)

$(TARGET1): cli.c
	$(CC) $(CFLAGS) -o $(TARGET1) cli.c
	
$(TARGET2): srv.c
	$(CC) $(CFLAGS) -o $(TARGET2) srv.c
	
clean:
	rm -rf $(TARGET1) $(TARGET2) cli.out
