# TP-Informatique-Concurente-Et-Repartie

COMPILE

```bash
gcc app.c -o app.out -lpthread -Wall
```

USAGE

```bash
./app <ID1> <remoteID2> <remoteID3> ... <remoteIDn>
```

EXEMPLE

```bash
./app 1 2 3
./app 2 1 3
./app 3 1 2
```