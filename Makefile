# c2p-68k — host-side test runner. There is nothing to "build": c2p32.h and
# c2p4st.h are header-only, and src/c2p_amiga.c is compiled by whatever
# project consumes it. The tests compile their own C harnesses with the host
# compiler and check the output against a naive per-pixel reference.
.PHONY: test lint clean
test:
	python3 -m pytest tests/ -q
# Compile-only sanity for the sources a consumer will actually build.
lint:
	$(CC) -std=c99 -Wall -Wextra -O2 -Iinclude -c src/c2p_amiga.c -o /dev/null
	$(CC) -std=c99 -Wall -Wextra -O2 -Iinclude -fsyntax-only -xc include/c2p4st.h
	$(CC) -std=c99 -Wall -Wextra -O2 -Iinclude -fsyntax-only -xc include/c2p32.h
clean:
	rm -rf tests/__pycache__ .pytest_cache
