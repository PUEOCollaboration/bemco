


all: cpu-temps hsk-bemco gpu-temps

cpu-temps: cpu-temps.c
	gcc -g -o $@ $< -lsensors -lpq
hsk-bemco: hsk-bemco.c
	gcc -g -o $@ $< -lpq -lm

gpu-temps: gpu-temps.c
	gcc -g -o $@ $< -lnvidia-ml -lpq
write-disks: write-disks.c
	gcc -g -o $@ $< -pthread
