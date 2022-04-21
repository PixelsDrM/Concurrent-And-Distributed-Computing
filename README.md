# TP-Informatique-Concurente-Et-Repartie

## COMPILE

```bash
gcc app.c -o app.out -lpthread -Wall
```

## USAGE

```bash
./app.out <ID1> <remoteID2> <remoteID3> ... <remoteIDn>
```

## EXEMPLE

With three different terminal :

```bash
./app.out 1 2 3
./app.out 2 1 3
./app.out 3 1 2
```
