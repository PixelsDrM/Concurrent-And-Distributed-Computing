# TP-Informatique-Concurente-Et-Repartie

## COMPILE

```bash
gcc app.c -o app.out -lpthread -Wall && gcc observer.c -o observer.out -Wall
```

## USAGE

Start observer first :
```bash
./observer.out
```
Then the app :
```bash
./app.out <ID1> <remoteID2> <remoteID3> ... <remoteIDn>
```

To stop the observer just close every running app with an interuption signal (CTRL+C)

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