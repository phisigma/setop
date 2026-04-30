PROGNAME := setop
MANPAGE := $(PROGNAME).1.gz

.PHONY: all clean install uninstall documentation

CXXFLAGS += -std=c++23 -O3
LIBS += -lboost_program_options -lboost_regex
SOURCES = main.cpp FormulaParser.cpp SetCalculator.cpp TermParser.cpp
SOURCES_FULLNAME = $(addprefix src/,$(SOURCES))
OBJECTS = $(SOURCES:.cpp=.o)

# where to put executable and manpage on 'make install'
BIN ?= $(DESTDIR)/usr/bin
HELP ?= $(DESTDIR)/usr/share/man/man1
BASHCOMPLETION ?= $(DESTDIR)/usr/share/bash-completion/completions

# replace characters ' with \' so they are escaped and paths can be put within apostrophes later
bin_directory = $(subst ','\'',$(BIN))
help_directory = $(subst ','\'',$(HELP))
bashcompletion_directory = $(subst ','\'',$(BASHCOMPLETION))


all: $(PROGNAME) $(MANPAGE)

$(PROGNAME): $(OBJECTS)
	$(CXX) $(LDFLAGS) $(OBJECTS) $(LIBS) -o $(PROGNAME)

%.o: src/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	@echo "Clean."
	-rm -f $(PROGNAME)
	-rm -f $(OBJECTS)
	-rm -f $(MANPAGE)

install: $(PROGNAME) $(MANPAGE) src/setop.bash
	install -d '$(bin_directory)' '$(help_directory)' '$(bashcompletion_directory)'
	install $(PROGNAME) '$(bin_directory)'
	install $(MANPAGE) '$(help_directory)'
	install src/setop.bash '$(bashcompletion_directory)'

uninstall:
	rm '$(bin_directory)/$(PROGNAME)' || true
	rm '$(help_directory)/$(MANPAGE)' || true
	rm '$(bashcompletion_directory)/setop.bash' || true

$(MANPAGE): $(PROGNAME)
	help2man -n "make set of strings from input" -N -L en_US.UTF-8 ./$(PROGNAME) | gzip > $(MANPAGE)

documentation: $(SOURCES_FULLNAME) doxygen-config
	doxygen doxygen-config
