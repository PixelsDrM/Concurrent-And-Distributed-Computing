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

First terminal : 
```bash
./observer.out
```

Second terminal : 
```bash
./app.out 1 2 3
```

Third terminal : 
```bash
./app.out 2 1 3
```

Fourth terminal : 
```bash
./app.out 3 1 2
```