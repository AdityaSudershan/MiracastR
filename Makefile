GST_CFLAGS = $(shell pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0)
GST_LIBS   = $(shell pkg-config --libs gstreamer-1.0 gstreamer-app-1.0)

CXX = g++

CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread $(GST_CFLAGS)

TARGET = startcast

SRC = src/main.cpp \
      src/rtsp_parser.cpp \
      src/rtsp_utils.cpp \
      src/rtp_receiver.cpp \
      src/rtcp_receiver.cpp \
      src/gstreamer_pipeline.cpp

OBJ = $(SRC:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $(TARGET) $(GST_LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean