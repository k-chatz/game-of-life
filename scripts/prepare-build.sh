#!/bin/sh
rm -f /home/msi/projects/CLionProjects/game-of-life/mpi/generations/row/*
rm -f /home/msi/projects/CLionProjects/game-of-life/mpi/generations/boxes/*
python3 /home/msi/projects/CLionProjects/game-of-life/scripts/block.py 32 /home/msi/projects/CLionProjects/game-of-life/mpi/generations/row/input.txt
python3 /home/msi/projects/CLionProjects/game-of-life/scripts/boxes.py 32 /home/msi/projects/CLionProjects/game-of-life/mpi/generations/row/input.txt /home/msi/projects/CLionProjects/game-of-life/mpi/generations/boxes/input.txt
