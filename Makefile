PROGS = client server

all: $(PROGS)

$(PROGS): % : %.cpp common.h msgs.h
	g++ -std=gnu++11 -g -o $(@) $(<) -lutil

clean:
	rm -rf $(PROGS)
