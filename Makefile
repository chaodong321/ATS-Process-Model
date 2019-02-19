.PHONY: all clean

CC = g++
CXXFLAGS := -g -Wall
LDFLAGS :=  -lpthread
LIBS := 

TRAFFIC_COP := traffic_cop
TRAFFIC_MANAGER := traffic_manager
TRAFFIC_SERVER := traffic_server

COMM_SRCS = util_log.o lock_and_kill.o

TRAFFIC_COP_SRCS = traffic_cop.o
TRAFFIC_MANAGER_SRCS = traffic_manager.o
TRAFFIC_SERVER_SRCS = traffic_server.o

all: $(TRAFFIC_COP) $(TRAFFIC_MANAGER) $(TRAFFIC_SERVER)

$(TRAFFIC_COP): $(COMM_SRCS) $(TRAFFIC_COP_SRCS)
	$(CC) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -o $@ $^

$(TRAFFIC_MANAGER):$(COMM_SRCS) $(TRAFFIC_MANAGER_SRCS)
	$(CC) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -o $@ $^

$(TRAFFIC_SERVER): $(COMM_SRCS) $(TRAFFIC_SERVER_SRCS)
	$(CC) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -o $@ $^

%.o:%.cc
	$(CC) $(CXXFLAGS) -c $< -o $@

clean:
	rm $(COMM_SRCS) $(TRAFFIC_COP_SRCS) $(TRAFFIC_MANAGER_SRCS) $(TRAFFIC_SERVER_SRCS) $(TRAFFIC_COP) $(TRAFFIC_MANAGER) $(TRAFFIC_SERVER) -f
