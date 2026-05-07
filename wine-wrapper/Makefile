CFLAGS ?= -O2 -Wall -arch x86_64 -arch arm64

wine: wine.c
	$(CC) $(CFLAGS) wine.c -o wine

install: wine
	@if [ -z "$(PREFIX)" ]; then echo "set PREFIX, e.g. PREFIX=$$HOME/Wine/bin make install"; exit 1; fi
	mkdir -p "$(PREFIX)"
	cp wine "$(PREFIX)/wine"
	ln -sf wine "$(PREFIX)/wineserver"

clean:
	rm -f wine

.PHONY: install clean
