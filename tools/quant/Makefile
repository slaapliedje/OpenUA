# Host-side quantiser analysis tools (#139). They include the ENGINE'S OWN
# platform/include/quantize.h, so their numbers describe the shipping path
# rather than a re-implementation — that is the whole point of them, and the
# reason they live here and not in a notebook.
CFLAGS ?= -O2 -Wall -Wextra -I../../platform/include
BINS    = qfid qpre qdither qstable qvp qrender qpin qband qprof

all: $(BINS)

# quantize.h IS the subject — without this dependency an edit to it leaves
# every tool reporting the OLD numbers, which is worse than no numbers.
$(BINS): ../../platform/include/quantize.h

%: %.c
	$(CC) $(CFLAGS) -o $@ $<
clean:
	rm -f $(BINS)
.PHONY: all clean
