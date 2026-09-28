#!/usr/bin/env python3
"""
Script to analyze performance metrics from the parallel association rule mining algorithm.
Reads performance_metrics.csv and generates visualizations.
"""

import os
import sys
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import glob
from matplotlib.ticker import PercentFormatter

def analyze_single_run(metrics_file):
    """Analyze a single run from a metrics CSV file"""
    if not os.path.exists(metrics_file):
        print(f"Error: Metrics file {metrics_file} not found")
        return

    # Read the metrics
    df = pd.read_csv(metrics_file)
    
    # Separate the time metrics and statistics
    time_metrics = df[~df['Metric'].isna()].copy()
    statistics = df[~df['Statistic'].isna()].copy()
    
    # Create output directory for plots
    output_dir = os.path.join(os.path.dirname(metrics_file), 'plots')
    os.makedirs(output_dir, exist_ok=True)
    
    # Plot the time breakdown
    time_data = time_metrics[time_metrics['Metric'] != 'Total execution time'].copy()
    
    # Bar chart of execution times
    plt.figure(figsize=(12, 6))
    bars = plt.bar(time_data['Metric'], time_data['Value'].astype(float))
    plt.title('Execution Time Breakdown')
    plt.xlabel('Phase')
    plt.ylabel('Time (seconds)')
    plt.xticks(rotation=45, ha='right')
    
    # Add the percentage on top of each bar
    for bar in bars:
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2., height,
                 f'{height:.2f}s',
                 ha='center', va='bottom', rotation=0)
    
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'time_breakdown.png'))
    
    # Pie chart of time percentages
    plt.figure(figsize=(10, 10))
    plt.pie(time_data['Percentage'].astype(float), 
            labels=time_data['Metric'], 
            autopct='%1.1f%%',
            startangle=90)
    plt.axis('equal')  # Equal aspect ratio ensures that pie is drawn as a circle.
    plt.title('Percentage of Total Execution Time')
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'time_percentages.png'))
    
    # Print summary
    total_time = time_metrics[time_metrics['Metric'] == 'Total execution time']['Value'].values[0]
    print(f"Performance Summary:")
    print(f"Total execution time: {total_time}s")
    print("\nPhase Breakdown:")
    for _, row in time_data.iterrows():
        print(f"{row['Metric']}: {float(row['Value']):.2f}s ({float(row['Percentage']):.1f}%)")
    
    print("\nStatistics:")
    for _, row in statistics.iterrows():
        print(f"{row['Statistic']}: {row['Value']}")
    
    return time_metrics, statistics

def analyze_scaling(scaling_dir):
    """Analyze scaling performance across different processor counts"""
    scaling_file = os.path.join(scaling_dir, 'scaling.csv')
    
    if not os.path.exists(scaling_file):
        print(f"Error: Scaling file {scaling_file} not found")
        return
    
    # Read the scaling data
    df = pd.read_csv(scaling_file)
    
    # Create output directory for plots
    output_dir = os.path.join(scaling_dir, 'plots')
    os.makedirs(output_dir, exist_ok=True)
    
    # Plot execution time vs processor count
    plt.figure(figsize=(10, 6))
    plt.plot(df['processor_count'], df['total_time'], 'o-', linewidth=2, markersize=8)
    plt.title('Execution Time vs. Number of Processors')
    plt.xlabel('Number of Processors')
    plt.ylabel('Execution Time (seconds)')
    plt.grid(True)
    plt.savefig(os.path.join(output_dir, 'execution_time_scaling.png'))
    
    # Plot speedup vs processor count
    plt.figure(figsize=(10, 6))
    plt.plot(df['processor_count'], df['speedup'], 'o-', linewidth=2, markersize=8)
    plt.plot([1, max(df['processor_count'])], [1, max(df['processor_count'])], 'r--', label='Ideal Speedup')
    plt.title('Speedup vs. Number of Processors')
    plt.xlabel('Number of Processors')
    plt.ylabel('Speedup')
    plt.grid(True)
    plt.legend()
    plt.savefig(os.path.join(output_dir, 'speedup_scaling.png'))
    
    # Plot efficiency vs processor count
    plt.figure(figsize=(10, 6))
    efficiency = df['speedup'] / df['processor_count']
    plt.plot(df['processor_count'], efficiency, 'o-', linewidth=2, markersize=8)
    plt.axhline(y=1.0, color='r', linestyle='--', label='Ideal Efficiency')
    plt.title('Parallel Efficiency vs. Number of Processors')
    plt.xlabel('Number of Processors')
    plt.ylabel('Efficiency (Speedup/Processors)')
    plt.grid(True)
    plt.legend()
    plt.savefig(os.path.join(output_dir, 'efficiency_scaling.png'))
    
    # Compare phase times across different processor counts
    phase_times = {}
    phase_proportions = {}
    
    # Get metrics for each processor count
    metrics_files = glob.glob(os.path.join(scaling_dir, 'metrics_*.csv'))
    proc_counts = sorted([int(os.path.basename(f).split('_')[1].split('.')[0]) for f in metrics_files])
    
    for proc in proc_counts:
        metrics_file = os.path.join(scaling_dir, f'metrics_{proc}.csv')
        if os.path.exists(metrics_file):
            df_metrics = pd.read_csv(metrics_file)
            time_data = df_metrics[~df_metrics['Metric'].isna()].copy()
            
            # Extract execution times for each phase
            for _, row in time_data.iterrows():
                phase = row['Metric']
                if phase not in phase_times:
                    phase_times[phase] = []
                    phase_proportions[phase] = []
                
                if phase != 'Total execution time':
                    phase_times[phase].append(float(row['Value']))
                    phase_proportions[phase].append(float(row['Percentage']))
    
    # Plot execution time by phase
    phases = [phase for phase in phase_times.keys() if phase != 'Total execution time']
    
    # Time breakdown by processor count
    plt.figure(figsize=(14, 8))
    bar_width = 0.8 / len(phases)
    index = np.arange(len(proc_counts))
    
    for i, phase in enumerate(phases):
        plt.bar(index + i * bar_width, phase_times[phase], bar_width, label=phase)
    
    plt.xlabel('Number of Processors')
    plt.ylabel('Time (seconds)')
    plt.title('Phase Execution Time by Processor Count')
    plt.xticks(index + bar_width * (len(phases) / 2), proc_counts)
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'phase_times_by_processor.png'))
    
    # Percentage breakdown by processor count
    plt.figure(figsize=(14, 8))
    bottom = np.zeros(len(proc_counts))
    
    for phase in phases:
        plt.bar(proc_counts, phase_proportions[phase], bottom=bottom, label=phase)
        bottom += phase_proportions[phase]
    
    plt.xlabel('Number of Processors')
    plt.ylabel('Percentage of Total Time')
    plt.title('Phase Time Percentage by Processor Count')
    plt.legend(loc='upper center', bbox_to_anchor=(0.5, -0.05), ncol=3)
    plt.gca().yaxis.set_major_formatter(PercentFormatter())
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'phase_percentages_by_processor.png'))
    
    # Print summary
    print("\nScaling Analysis:")
    print("Processor Count | Total Time | Speedup | Efficiency")
    print("-" * 50)
    
    for i, proc in enumerate(proc_counts):
        if i < len(df):
            time = df[df['processor_count'] == proc]['total_time'].values[0]
            speedup = df[df['processor_count'] == proc]['speedup'].values[0]
            eff = speedup / proc
            print(f"{proc:14d} | {float(time):10.2f}s | {float(speedup):7.2f} | {eff:9.2f}")

def main():
    """Main function to analyze performance metrics"""
    if len(sys.argv) < 2:
        print("Usage: python analyze_performance.py <metrics_file_or_results_dir>")
        return
    
    path = sys.argv[1]
    
    if os.path.isdir(path):
        # Check if this is a results directory
        metrics_file = os.path.join(path, 'performance_metrics.csv')
        scaling_dir = os.path.join(path, 'scaling')
        
        if os.path.exists(metrics_file):
            print(f"Analyzing single run from {metrics_file}")
            analyze_single_run(metrics_file)
        
        if os.path.exists(scaling_dir):
            print(f"\nAnalyzing scaling performance from {scaling_dir}")
            analyze_scaling(scaling_dir)
            
    elif os.path.isfile(path) and path.endswith('.csv'):
        print(f"Analyzing performance from {path}")
        analyze_single_run(path)
    else:
        print(f"Error: {path} is not a valid metrics file or results directory")

if __name__ == "__main__":
    main()