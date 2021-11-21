srcs = \
		  tree.cpp \
		  main.cpp

objects = $(srcs:.cpp=.o)

EMBREE_DIR = $(HOME)/embree-3.13.2.x86_64.linux

CXXFLAGS += -O3 -MMD -I../h -I/usr/local/include/Imath -I$(EMBREE_DIR)/include

LDFLAGS += -L$(EMBREE_DIR)/lib -lImath -lembree3 -ltbb -lboost_program_options -lrt

slrender: $(objects) Makefile
	$(CXX) $(objects) $(LDFLAGS) -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.d
	rm -f *.o
	rm -f slrender

-include $(srcs:.cpp=.d)
