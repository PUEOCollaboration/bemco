


all: cpu-temps hsk-bemco gpu-temps

cpu-temps: cpu-temps.c
	gcc -g -o $@ $< -lsensors -lpq -lnvidia-ml
hsk-bemco: hsk-bemco.c
	gcc -g -o $@ $< -lpq -lm

write-disks: write-disks.c
	gcc -g -o $@ $< -pthread
