CXXFLAGS += $(shell pkg-config --cflags opencv) -I. -g
LDFLAGS += $(shell pkg-config --libs opencv) -lCIEDE2000

macduff: macduff.cpp
	g++ $(CXXFLAGS) macduff.cpp $(LDFLAGS) -o macduff

clean:
		rm -f macduff
