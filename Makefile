PROGS = client server

all: $(PROGS)

$(PROGS): % : %.cpp
	g++ -std=gnu++11 -o $(@) $(^) -lutil

clean:
	rm -rf $(PROGS)
