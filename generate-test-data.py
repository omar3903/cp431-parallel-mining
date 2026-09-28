#!/usr/bin/env python3
"""
Script to generate synthetic transaction datasets for testing the parallel association rule mining algorithm.
Creates datasets of different sizes with controlled patterns to ensure rules can be discovered.
"""

import os
import random
import numpy as np

def generate_dataset(num_transactions, num_items, pattern_probability=0.3, output_file='transactions.txt'):
    """
    Generate a synthetic transaction dataset with embedded patterns.
    
    Args:
        num_transactions: Number of transactions to generate
        num_items: Total number of possible items
        pattern_probability: Probability of including a pattern in a transaction
        output_file: File to write the transactions to
    """
    # Create output directory if it doesn't exist
    os.makedirs(os.path.dirname(output_file), exist_ok=True)
    
    # Define patterns (combinations of items that appear together)
    patterns = [
        [1, 2, 3],        # Pattern 1: Items 1, 2, and 3 appear together
        [4, 5],           # Pattern 2: Items 4 and 5 appear together
        [6, 7, 8, 9],     # Pattern 3: Items 6, 7, 8, and 9 appear together
        [10, 11, 12],     # Pattern 4: Items 10, 11, and 12 appear together
        [13, 14]          # Pattern 5: Items 13 and 14 appear together
    ]
    
    with open(output_file, 'w') as f:
        for _ in range(num_transactions):
            # Initialize an empty transaction
            transaction = set()
            
            # Add random items with base probability
            for item in range(1, num_items + 1):
                if random.random() < 0.1:  # 10% chance of adding each item
                    transaction.add(item)
            
            # Add patterns with specified probability
            for pattern in patterns:
                if random.random() < pattern_probability:
                    transaction.update(pattern)
            
            # Write the transaction to the file
            if transaction:  # Ensure transaction is not empty
                f.write(' '.join(map(str, sorted(transaction))) + '\n')
    
    print(f"Generated {num_transactions} transactions with {num_items} possible items")
    print(f"Dataset saved to {output_file}")

def main():
    """Generate test datasets of different sizes"""
    # Create test_data directory
    os.makedirs('test_data', exist_ok=True)
    
    # Generate small dataset (100 transactions, 20 items)
    generate_dataset(100, 20, 0.4, 'test_data/small.txt')
    
    # Generate medium dataset (1,000 transactions, 50 items)
    generate_dataset(1000, 50, 0.3, 'test_data/medium.txt')
    
    # Generate large dataset (10,000 transactions, 100 items)
    generate_dataset(10000, 100, 0.2, 'test_data/large.txt')
    
    # Generate huge dataset (100,000 transactions, 200 items)
    generate_dataset(100000, 200, 0.1, 'test_data/huge.txt')

if __name__ == "__main__":
    main()
