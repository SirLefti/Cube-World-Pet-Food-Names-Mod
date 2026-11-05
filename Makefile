CXX = g++

CXXFLAGS = -O2 -masm=intel -Wall
LDFLAGS = -shared -static -static-libgcc -static-libstdc++ -s

PetFoodNamesMod.dll: main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)
