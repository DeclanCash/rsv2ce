OBJS = src/rsv2ce.o \
       src/intset.o \
       data/data.o
CFLAGS += -Wall -Isrc/
LDLIBS += -lreadline

rsv2ce: $(OBJS)
	$(CC) -o $@ $(LDFLAGS) $(OBJS) $(LDLIBS)

data/data.c: data/rsv2ce.tsv data/generate.awk src/data.h
	awk -f data/generate.awk $< > $@

.PHONY: clean
clean:
	rm -rf $(OBJS) rsv2ce
