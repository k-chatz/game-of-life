#include <mpi.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "mpi.h"
#include "game_of_life.h"

#define STEPS 10

int main(int argc, char **argv) {
    int s = 0, i = 0, j = 0, rank, size = 0, workers = 0, root = 0, sum = 0, inputFileNotExists = 0, array_of_sizes[2], array_of_subsizes[2], starts[2];
    double start_w_time = 0.0, end_w_time = 0.0, local_time = 0.0, max_time = 0.0;
    char **block = NULL, **old = NULL, **current = NULL, **temp = NULL, buffer[100];
    MPI_Datatype colType, rowType, subType;
    MPI_Request send_a_request[8], recv_a_request[8], send_b_request[8], recv_b_request[8];
    MPI_Status send_a_status[8], send_b_status[8], recv_a_status[8], recv_b_status[8];
    MPI_Status status, *readFileStatus;
    MPI_Request *readFileReq;
    MPI_File inputFile, outputFile;
    GridInfo grid;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    //TODO: Setup grid dimensions & local array dimensions based on blockDims[0], M, W parameters.
    setupGrid(&grid, TABLE_N, TABLE_M);

    // Allocate local blocks
    old = allocate2DArray(grid.localBlockDims[0] + 2, grid.localBlockDims[1] + 2);
    current = allocate2DArray(grid.localBlockDims[0] + 2, grid.localBlockDims[1] + 2);

    // Initialize local blocks
    for (i = 0; i < grid.localBlockDims[0] + 2; i++) {
        for (j = 0; j < grid.localBlockDims[1] + 2; j++) {
            old[i][j] = '0';
            current[i][j] = '0';
        }
    }

    // rowType
    MPI_Type_vector(1, grid.localBlockDims[1], 0, MPI_CHAR, &rowType);
    MPI_Type_commit(&rowType);

    // colType
    MPI_Type_vector(grid.localBlockDims[0], 1, grid.localBlockDims[1] + 2, MPI_CHAR, &colType);
    MPI_Type_commit(&colType);

    // Open input file
    inputFileNotExists = MPI_File_open(MPI_COMM_WORLD,
                                       "/home/msi/projects/CLionProjects/game-of-life/mpi/input/input.txt",
                                       MPI_MODE_RDONLY, MPI_INFO_NULL, &inputFile);

    if (grid.gridRank == root) {
        block = allocate2DArray(grid.blockDims[0], grid.blockDims[1]);
    }

    if (inputFileNotExists) {
        // No file, generate array
        if (rank == root) {
            initialize_block(block, false, grid.blockDims[0], grid.blockDims[1]);
            printf("block: (memory)\n");
            print_array(block, true, true,
                        grid.blockDims[0],
                        grid.blockDims[1],
                        grid.localBlockDims[0],
                        grid.localBlockDims[1]
            );
        }
        scatter2DArray(block, old, root, &grid);
    } else {
        // Read from file
        array_of_sizes[0] = grid.blockDims[0];
        array_of_sizes[1] = grid.blockDims[1];

        array_of_subsizes[0] = grid.localBlockDims[0];
        array_of_subsizes[1] = grid.localBlockDims[1];

        starts[0] = grid.gridCoords[0] * grid.localBlockDims[0];
        starts[1] = grid.gridCoords[1] * grid.localBlockDims[1];

        MPI_Type_create_subarray(2, array_of_sizes, array_of_subsizes, starts, MPI_ORDER_C, MPI_CHAR, &subType);
        MPI_Type_commit(&subType);

        MPI_File_set_view(inputFile, 0, MPI_CHAR, subType, "native", MPI_INFO_NULL);

        readFileReq = malloc(grid.localBlockDims[0] * sizeof(MPI_Request));
        readFileStatus = malloc(grid.localBlockDims[0] * sizeof(MPI_Status));
        for (i = 1; i <= grid.localBlockDims[0]; i++) {
            MPI_File_iread(inputFile, &old[i][1], grid.localBlockDims[1], MPI_CHAR, &readFileReq[i - 1]);
        }

        // Wait until reading is done
        MPI_Waitall(3, readFileReq, readFileStatus);

        MPI_File_close(&inputFile);

        free(readFileReq);
        free(readFileStatus);

        if (grid.gridRank == root) {
            initialize_block(block, true, grid.blockDims[0], grid.blockDims[1]);
        }

        gather2DArray(block, old, root, &grid);

        if (grid.gridRank == root) {
            printf("block: (file)\n");
            print_array(block, true, true,
                        grid.blockDims[0],
                        grid.blockDims[1],
                        grid.localBlockDims[0],
                        grid.localBlockDims[1]
            );
        }
    }

    // Initialize send/receive requests for old local block
    sendInit(old, grid, rowType, colType, send_a_request);
    recvInit(old, grid, rowType, colType, recv_a_request);

    // Initialize send/receive requests for current local block
    sendInit(current, grid, rowType, colType, send_b_request);
    recvInit(current, grid, rowType, colType, recv_b_request);

    // (συγχρονισμός διεργασιών πριν τις μετρήσεις)
    MPI_Barrier(grid.gridComm);

    // Start MPI_Wtime
    start_w_time = MPI_Wtime();

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Start loop
    for (s = 0; s < STEPS; s++) {

        // Create generation file for current step
        if (grid.gridRank == root) {
            sprintf(buffer, "/home/msi/projects/CLionProjects/game-of-life/mpi/generations/step-%d.txt", s);
            MPI_File_open(MPI_COMM_SELF, buffer, MPI_MODE_WRONLY | MPI_MODE_CREATE, MPI_INFO_NULL, &outputFile);
            MPI_File_close(&outputFile);
        }

        // Start receive/send requests
        if (s % 2 == 0) {
            MPI_Startall(8, recv_a_request);
            MPI_Startall(8, send_a_request);
        } else {
            MPI_Startall(8, recv_b_request);
            MPI_Startall(8, send_b_request);
        }

        grid.stepLocalChanges = 0;

        // Calculate internals
        for (i = 2; i < grid.localBlockDims[0]; i++) {
            for (j = 2; j < grid.localBlockDims[1]; j++) {
                calculate(old, current, i, j, &grid.stepLocalChanges);
            }
        }

        // Wait receive requests
        if (s % 2 == 0) {
            MPI_Waitall(8, recv_a_request, recv_a_status);
        } else {
            MPI_Waitall(8, recv_b_request, recv_b_status);
        }

        // Calculate up row
        for (i = 1; i < grid.localBlockDims[1] + 1; i++) {
            calculate(old, current, 1, i, &grid.stepLocalChanges);
        }

        // Calculate down row
        for (i = 1; i < grid.localBlockDims[1] + 1; i++) {
            calculate(old, current, grid.localBlockDims[0], i, &grid.stepLocalChanges);
        }

        // Calculate left Column
        for (i = 2; i < grid.localBlockDims[0]; i++) {
            calculate(old, current, i, 1, &grid.stepLocalChanges);
        }

        // Calculate right column
        for (i = 2; i < grid.localBlockDims[0]; i++) {
            calculate(old, current, i, grid.localBlockDims[1], &grid.stepLocalChanges);
        }



        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //print_step(s, &grid, old, current);
//        gather2DArray(block, current, root, &grid);
//        for (i = 0; i < grid.processes; i++) {
//            MPI_Barrier(grid.gridComm);
//            if (i == grid.gridRank) {
//                if (grid.gridRank == root) {
//                    printf("block: (step)\n");
//                    print_array(block, true, false,
//                                grid.blockDims[0],
//                                grid.blockDims[1],
//                                grid.localBlockDims[0],
//                                grid.localBlockDims[1]
//                    );
//                }
//            }
//        }
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////



        // Write current in file
        MPI_File_open(MPI_COMM_SELF, buffer, MPI_MODE_WRONLY, MPI_INFO_NULL, &outputFile);
        MPI_File_set_view(outputFile, 0, MPI_CHAR, subType, "native", MPI_INFO_NULL);
        for (i = 1; i <= grid.localBlockDims[0]; i++) {
            MPI_File_write_all(outputFile, &current[i][1], grid.localBlockDims[0], MPI_CHAR, &status);
        }
        MPI_File_close(&outputFile);

        // Swap local blocks
        temp = old;
        old = current;
        current = temp;

        // Summarize all local changes
        if (s % 10 == 0) {
            MPI_Allreduce(&grid.stepLocalChanges, &grid.stepGlobalChanges, 1, MPI_INT, MPI_SUM, grid.gridComm);
            if (grid.stepGlobalChanges == 0) {
                break;
            }
        }

        // Wait send requests
        if (s % 2 == 0) {
            MPI_Waitall(8, send_a_request, send_a_status);
        } else {
            MPI_Waitall(8, send_b_request, send_b_status);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // End MPI_Wtime
    end_w_time = MPI_Wtime();
    local_time = end_w_time - start_w_time;
    MPI_Reduce(&local_time, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, grid.gridComm);

    printf("Worker %d ==> Start Time = %.6f End Time = %.6f Duration = %.9f seconds\n",
           grid.gridRank, start_w_time, end_w_time, end_w_time - start_w_time);

    gather2DArray(block, old, root, &grid);

    if (grid.gridRank == root) {
        printf("Result block:\n");
        print_array(block, true, true,
                    grid.blockDims[0],
                    grid.blockDims[1],
                    grid.localBlockDims[0],
                    grid.localBlockDims[1]
        );
        printf("Steps: %d, Max time: %f\n", s, max_time);
        //free2DArray(block, grid.blockDims[0]);
    }

    free2DArray(old, grid.localBlockDims[0] + 2);
    free2DArray(current, grid.localBlockDims[0] + 2);

    MPI_Comm_free(&grid.gridComm);
    MPI_Finalize();
}
