
**input**
use the filesize as a guide, split the file based on chunks of bytes
one less pass over file as we dont have to get linecounts before processing, concurrent read on Teach cluster allows all procs to open the file at the same time
Use hashmaps local to each processor to find all unique ints from the file chunk
Determine which ints are frequent, creating a local hashmap of frequent integers and their counts. Use a support threshold which is tuned for each processor.
	local_support = global_support * ((byteend-bytestart) / totalbytes)


**SON algorithm on HPC cluster**
next need to tackle frequent pairs. 
Create a triangular matrix local to each processor
	This helps control memory and allows this code to scale to very high input filesizes
	allows each pair to use only 4bytes of ram
Proc parses the same chunk of the file as it did it part one
	This time, each line (basket) is filtered, so that only items from the *frequent* itemsets are read (this is where the hashtable is nice, o(1) lookup times for this)
	Once a basket has been filtered, all the ints which have been read (which are all frequent) are processed, generating all possible pairs of frequent ints for each basket (each line)
	When a pair is generated, it's count in the triangle matrix is incremented
		There is a cool formula at the top of apriori.h that resolves integer pairs into a location within the triangle matrix
	After all baskets are read, the triangle matrix is parsed, and any pairs which do not meet the support threshold are pruned.
	This leaves only *frequent* pairs left
		*At this point we switch to a generic struct for storing frequent itemsets larger than 2*
		We only used triangle matrix for pairs bc there are so many pairs, and exponentially *fewer* triples, quads etc.
		A simple struct that contains the ints, counts and support is used for triples and onwards.
	Now, we generate all possible size 3 itemsets, check the baskets for them, prune the low support, and repeat for size 4 and onwards.
	Stop when an itemset size yields no valid itemsets.
	Now, all local procs send their frequent itemsets to master proc. 
	Master then splits the frequent itemsets array into equal parts based on the number of procs, and the sends the equal parts off
	each proc then reads the *whole* file, calculating support for the itemsets it was assigned. Procs all know the global_support threshold, and prune failing itemsets. 

**Association rules**
Each proc now has a chunk of itemsets which are frequent across the entire file
Each proc goes through each of its itemsets (of size > 1), and generates rules
generate all possible rules for an itemset, then discard any which do not meet or exceed the confidence threshold (this is different than the support threshold from earlier)
After all is done, procs send association rules that they have confidence in back to main for output
