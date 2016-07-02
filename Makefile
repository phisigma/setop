PROGNAME := setop

.PHONY: all, clean

CXXFLAGS += -std=c++11 -O3
LIBS += -lboost_program_options -lboost_regex
SOURCES = src/main.cpp

# where to put executable and manpage on 'make install'
BIN ?= $(DESTDIR)/usr/bin
HELP ?= $(DESTDIR)/usr/share/man/man1


all: $(PROGNAME) man

$(PROGNAME): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) $(LIBS) -o $(PROGNAME)

clean:
	@echo "Clean."
	-rm -f $(PROGNAME)
	-rm -f $(PROGNAME).1

install: $(PROGNAME) man
	install -d $(BIN) $(HELP)
	install $(PROGNAME) $(BIN)
	install $(PROGNAME).1 $(HELP)

man: $(PROGNAME)
	help2man -n "make set of strings from input" -N -L en_US.UTF-8 ./$(PROGNAME) | gzip > $(PROGNAME).1

# not needed:
#documentation: $(SOURCES) doxyconfig
#	doxygen doxyconfig
