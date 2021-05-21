IDIR=./include
CC=g++
CPPFLAGS=-I$(IDIR) -lprotobuf -pthread -std=c++11
SRCDIR=./src
CHATDIR=$(SRCDIR)/Chat

SERVERCPP= $(CHATDIR)/Server.cpp
CLIENTCPP= $(CHATDIR)/Client.cpp

PROTOCPPOUT=../lib
PROTOCFLAGS=-I=$(IDIR) --cpp_out=$(IDIR)
PROTOCC=protoc
MSGCC= new.pb.cc

server: $(SERVERCPP)
	 $(CC) $(CPPFLAGS) -o server $(SERVERCPP) $(IDIR)/$(MSGCC)

client: $(CLIENTCPP)
	 $(CC) $(CPPFLAGS) -o client  $(CLIENTCPP) $(IDIR)/$(MSGCC)

message: $(IDIR)/new.proto
	$(PROTOCC) $(PROTOCFLAGS) new.proto
