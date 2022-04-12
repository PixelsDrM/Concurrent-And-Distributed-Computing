# TP-Informatique-Concurente-Et-Repartie

COMPILE

```bash
gcc app.c -o app.out -lpthread
```

USAGE

```bash
./app <ID> <remoteID1> <remoteID2> ...
```

EXEMPLE

```bash
./app 1 2 3
./app 2 1 3
./app 3 1 2
```