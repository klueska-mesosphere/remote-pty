PROGS = client server

all: $(PROGS)

$(PROGS): % : %.cpp common.h
	g++ -std=gnu++11 -o $(@) $(^) -lutil

clean:
	rm -rf $(PROGS)
