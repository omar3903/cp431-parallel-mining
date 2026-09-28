#!/bin/bash

# Default values
DEFAULT_PROCESSORS=4
DEFAULT_SUPPORT=0.1
DEFAULT_CONFIDENCE=0.7
OUTPUT_DIR="results"

# Parse command line arguments
if [ $# -lt 1 ]; then
    echo "Usage: $0 <input_file> [support_threshold] [confidence_threshold] [num_processors]"
    echo "Example: $0 transactions.txt 0.1 0.7 4"
    exit 1
fi

INPUT_FILE=$1
SUPPORT=${2:-$DEFAULT_SUPPORT}
CONFIDENCE=${3:-$DEFAULT_CONFIDENCE}
NUM_PROCESSORS=${4:-$DEFAULT_PROCESSORS}

# Create results directory if it doesn't exist
mkdir -p $OUTPUT_DIR

# Generate timestamp for this run
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RUN_DIR="$OUTPUT_DIR/run_${TIMESTAMP}"
mkdir -p $RUN_DIR

# Save run parameters
echo "Input file: $INPUT_FILE" > "$RUN_DIR/parameters.txt"
echo "Support threshold: $SUPPORT" >> "$RUN_DIR/parameters.txt"
echo "Confidence threshold: $CONFIDENCE" >> "$RUN_DIR/parameters.txt"
echo "Number of processors: $NUM_PROCESSORS" >> "$RUN_DIR/parameters.txt"

# Compile the program
echo "Compiling..."
mpicc -o find-rules find-rules.c Apriori.c dynamic_hash_table.c -lm

# Check if compilation was successful
if [ $? -ne 0 ]; then
    echo "Compilation failed. Exiting."
    exit 1
fi

echo "Compilation successful!"

# Run the program with basic resource monitoring
echo "Running with $NUM_PROCESSORS processors..."
echo "Support threshold: $SUPPORT"
echo "Confidence threshold: $CONFIDENCE"

# Start monitoring system resources
if command -v mpstat &> /dev/null; then
    mpstat 1 > "$RUN_DIR/cpu_usage.txt" &
    MPSTAT_PID=$!
fi

if command -v vmstat &> /dev/null; then
    vmstat 1 > "$RUN_DIR/memory_usage.txt" &
    VMSTAT_PID=$!
fi

# Run the program and time it
START_TIME=$(date +%s.%N)
mpirun -np $NUM_PROCESSORS ./find-rules $INPUT_FILE $SUPPORT $CONFIDENCE | tee "$RUN_DIR/output.log"
END_TIME=$(date +%s.%N)

# Stop resource monitoring
if [ ! -z "$MPSTAT_PID" ]; then
    kill $MPSTAT_PID 2>/dev/null
fi

if [ ! -z "$VMSTAT_PID" ]; then
    kill $VMSTAT_PID 2>/dev/null
fi

# Calculate elapsed time
ELAPSED=$(echo "$END_TIME - $START_TIME" | bc)
echo "Total wall clock time: $ELAPSED seconds" | tee -a "$RUN_DIR/output.log"

# Copy performance metrics to run directory
if [ -f "performance_metrics.csv" ]; then
    cp "performance_metrics.csv" "$RUN_DIR/"
    echo "Performance metrics saved to $RUN_DIR/performance_metrics.csv"
fi

# Generate scaling data if running multiple times with different processor counts
if [ $# -eq 1 ]; then
    echo "Would you like to run a scaling test with different processor counts? (y/n)"
    read RUN_SCALING
    
    if [ "$RUN_SCALING" = "y" ]; then
        SCALING_DIR="$RUN_DIR/scaling"
        mkdir -p $SCALING_DIR
        echo "processor_count,total_time,speedup" > "$SCALING_DIR/scaling.csv"
        
        # Run with 1 processor as baseline
        echo "Running with 1 processor for baseline..."
        mpirun -np 1 ./find-rules $INPUT_FILE $SUPPORT $CONFIDENCE > "$SCALING_DIR/output_1.log"
        
        # Extract time from performance_metrics.csv
        BASELINE_TIME=$(grep "Total execution time" performance_metrics.csv | cut -d, -f2)
        echo "1,$BASELINE_TIME,1.0" >> "$SCALING_DIR/scaling.csv"
        cp "performance_metrics.csv" "$SCALING_DIR/metrics_1.csv"
        
        # Run with increasing processor counts
        for PROCS in 2 4 8 16; do
            if [ $PROCS -le $(nproc) ]; then
                echo "Running with $PROCS processors..."
                mpirun -np $PROCS ./find-rules $INPUT_FILE $SUPPORT $CONFIDENCE > "$SCALING_DIR/output_${PROCS}.log"
                
                # Extract time and calculate speedup
                PROC_TIME=$(grep "Total execution time" performance_metrics.csv | cut -d, -f2)
                SPEEDUP=$(echo "scale=2; $BASELINE_TIME / $PROC_TIME" | bc)
                echo "$PROCS,$PROC_TIME,$SPEEDUP" >> "$SCALING_DIR/scaling.csv"
                cp "performance_metrics.csv" "$SCALING_DIR/metrics_${PROCS}.csv"
            fi
        done
        
        echo "Scaling test complete. Results saved to $SCALING_DIR/scaling.csv"
    fi
fi

echo "Run completed! All results saved to $RUN_DIR/"
