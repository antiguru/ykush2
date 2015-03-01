DESTDIR?=/ 
INSTALL_LOCATION=$(DESTDIR)/usr/
CFLAGS+= -std=c99
LDFLAGS+= -lusb-1.0

#CFLAGS=-c -Wall -std=c99 -g
#LDFLAGS=-lusb-1.0 -g
SOURCES=main.c
DEPS=usbcom.h
EXECUTABLE=ykush2
INSTALL=/usr/bin/install

all: $(EXECUTABLE)
$(EXECUTABLE): $(SOURCES) $(DEPS)
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $@
	
.PHONY: clean

clean: 
	rm -f $(EXECUTABLE)

install: $(EXECUTABLE)
	mkdir -p $(INSTALL_LOCATION)/bin
	$(INSTALL) $(EXECUTABLE) $(INSTALL_LOCATION)/bin

