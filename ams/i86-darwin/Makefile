INCL = ../include
API = ../library
TEST = ../test
UTILS = ../utils
RAMS = ../rams

OPT = -g -Wall -Dunix -Ddarwin -DUDPTS -DTCPTS -DDGRTS
CC = gcc $(OPT) -I$(API) -I$(INCL) -I$(RAMS) -I$(ROOT)/include
LDFLAGS = -dynamiclib -undefined dynamic_lookup
LD = gcc $(LDFLAGS)

LIBAMSOBJS = \
	libams.o \
	amscommon.o \
	loadmib.o \
	nullcrypt.o \
	dgrts.o \
	udpts.o \
	tcpts.o

AMSDOBJS = \
	amsd.o \
	libams.o \
	amscommon.o \
	loadmib.o \
	nullcrypt.o \
	dgrts.o \
	udpts.o \
	tcpts.o

RAMSTESTOBJS = \
	librams.o \
	ramscommon.o \
	ramsTest.o

PUBINCLS = \
	$(INCL)/ams.h

LBP = -lbp -lbpP

RUNTIMES = amsd amshello amsshell amslog amslogprt amsbenchs amsbenchr ramstest

ALL = check libams.so $(RUNTIMES)

all:		$(ALL)

check:		$(PUBINCLS)
		rm -f *.o
		touch check

clean:
		rm -f *.o
		rm -f $(ALL)
		rm -f ./lib/*
		rm -f ./bin/*

install:
		cp ../include/* $(ROOT)/include
		cp lib/* $(ROOT)/lib
		cp bin/* $(ROOT)/bin

#	-	-	Daemon executable	-	-	-	-

amsd:		$(AMSDOBJS) $(API)/amscommon.h
		$(CC) -o amsd $(AMSDOBJS) -L./lib -L$(ROOT)/lib -ldgr -lici -lpthread -lexpat
		cp amsd ./bin

#	-	-	Test executables	-	-	-	-

amshello:	amshello.o libams.so $(INCL)/ams.h
		$(CC) -o amshello amshello.o -L./lib -L$(ROOT)/lib -lams -ldgr -lici -lpthread -lexpat
		cp amshello ./bin

amsbenchs:	amsbenchs.o libams.so $(INCL)/ams.h
		$(CC) -o amsbenchs amsbenchs.o -L./lib -L$(ROOT)/lib -lams -ldgr -lici -lpthread -lexpat
		cp amsbenchs ./bin

amsbenchr:	amsbenchr.o libams.so $(INCL)/ams.h
		$(CC) -o amsbenchr amsbenchr.o -L./lib -L$(ROOT)/lib -lams -ldgr -lici -lpthread -lexpat
		cp amsbenchr ./bin

#	-	-	Utility executables	-	-	-	-

amsshell:	amsshell.o libams.so $(INCL)/ams.h
		$(CC) -o amsshell amsshell.o -L./lib -L$(ROOT)/lib -lams -ldgr -lici -lpthread -lexpat
		cp amsshell ./bin

amslog:		amslog.o libams.so $(INCL)/ams.h
		$(CC) -o amslog amslog.o -L./lib -L$(ROOT)/lib -lams -ldgr -lici -lpthread -lexpat
		cp amslog ./bin

amslogprt:	amslogprt.o
		$(CC) -o amslogprt amslogprt.o -L./lib -L$(ROOT)/lib
		cp amslogprt ./bin

#	-	-	RAMS executable		-	-	-	-

ramstest:	$(RAMSTESTOBJS) $(RAMS)/rams.h $(RAMS)/ramscommon.h libams.so $(INCL)/ams.h
		$(CC) -o ramstest $(RAMSTESTOBJS) -L./lib -L$(ROOT)/lib $(LBP) -lams -ldgr -lici -lpthread -lexpat
		cp ramstest ./bin

#	-	-	Libraries	-	-	-	-	-

libams.so:	$(LIBAMSOBJS) $(API)/amsP.h
		$(LD) -o libams.so $(LIBAMSOBJS)
		cp libams.so ./lib

#	-	-	Object modules	-	-	-	-	-

%.o:		$(API)/%.c
		$(CC) -c $<

%.o:		$(TEST)/%.c
		$(CC) -c $<

%.o:		$(UTILS)/%.c
		$(CC) -c $<

%.o:		$(RAMS)/%.c
		$(CC) -c $<
