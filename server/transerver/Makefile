# 简易版本
CXX := g++
CXXFLAGS := -std=c++17 -Wall -I.
TARGET := server

all: $(TARGET)

$(TARGET): main.o Socket/socket.o 
	$(CXX) $(CXXFLAGS) -o $@ $^ -lcurl

main.o: main.cpp Socket/socket.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

Socket/socket.o: Socket/socket.cpp Socket/socket.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) *.o Socket/*.o

.PHONY: all clean