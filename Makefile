CXX = $(CROSS_COMPILE)c++
CXXFLAGS = -std=c++17 -O2 -fPIC -shared

WFLAGS += -Wall
WFLAGS += -Wextra
WFLAGS += -Wpedantic
WFLAGS += -Wcast-align
WFLAGS += -Wctor-dtor-privacy
WFLAGS += -Wdisabled-optimization
WFLAGS += -Winit-self
WFLAGS += -Wlogical-op
WFLAGS += -Wmissing-declarations
WFLAGS += -Wnoexcept
WFLAGS += -Woverloaded-virtual
WFLAGS += -Wredundant-decls
WFLAGS += -Wshadow
WFLAGS += -Wstrict-null-sentinel
WFLAGS += -Wstrict-overflow=5
WFLAGS += -Wundef
WFLAGS += -Weffc++
WFLAGS += -Wcast-qual
WFLAGS += -Wsign-promo
WFLAGS += -Wformat=1
WFLAGS += -Wswitch-default
WFLAGS += -Wold-style-cast
WFLAGS += -Wno-uninitialized
WFLAGS += -Wno-strict-overflow
WFLAGS += -Wno-cast-align
WFLAGS += -Wno-attributes
#WFLAGS += -Wsign-conversion
#WFLAGS += -Werror

SOURCES += shm.cpp
HEADERS += shm.h
TARGET = libshm.so
LIBFLAGS = -lrt

RM = rm -f
CP = cp -f
MKDIR = mkdir -p

all:
	$(CXX) $(CXXFLAGS) $(WFLAGS) $(SOURCES) -o $(TARGET) $(LIBFLAGS)
clean:
	$(RM) *.so
