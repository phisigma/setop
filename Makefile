PROGNAME := setop

.PHONY: all, clean

CXXFLAGS += -std=c++23 -O3
LIBS += -lboost_program_options -lboost_regex
SOURCES = src/main.cpp

# where to put executable and manpage on 'make install'
BIN ?= $(DESTDIR)/usr/bin
HELP ?= $(DESTDIR)/usr/share/man/man1
BASHCOMPLETION ?= $(DESTDIR)/usr/share/bash-completion/completions


all: $(PROGNAME) man

$(PROGNAME): $(SOURCES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) $(LIBS) -o $(PROGNAME)

clean:
	@echo "Clean."
	-rm -f $(PROGNAME)
	-rm -f $(PROGNAME).1.gz

install: $(PROGNAME) man src/setop.bash
	install -d $(BIN) $(HELP) $(BASHCOMPLETION)
	install $(PROGNAME) $(BIN)
	install $(PROGNAME).1.gz $(HELP)
	install src/setop.bash $(BASHCOMPLETION)

uninstall:
	rm $(BIN)/$(PROGNAME) || true
	rm $(HELP)/$(PROGNAME).1.gz || true
	rm $(BASHCOMPLETION)/setop.bash || true

man: $(PROGNAME)
	help2man -n "make set of strings from input" -N -L en_US.UTF-8 ./$(PROGNAME) | gzip > $(PROGNAME).1.gz

documentation: $(SOURCES) doxygen-config
	doxygen doxygen-config
