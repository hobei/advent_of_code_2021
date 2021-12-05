# Day 1

## Question : Sonar Sweep - Part 2

Considering every single measurement isn't as useful as you expected: there's just too much noise in the data.

Instead, consider sums of a three-measurement sliding window. Again considering the above example:

```
199  A      
200  A B    
208  A B C  
210    B C D
200  E   C D
207  E F   D
240  E F G  
269    F G H
260      G H
263        H
```

Start by comparing the first and second three-measurement windows. The measurements in the first window are marked A (199, 200, 208); their sum is 199 + 200 + 208 = 607. The second window is marked B (200, 208, 210); its sum is 618. The sum of measurements in the second window is larger than the sum of the first, so this first comparison increased.

Your goal now is to count the number of times the sum of measurements in this sliding window increases from the previous sum. So, compare A with B, then compare B with C, then C with D, and so on. Stop when there aren't enough measurements left to create a new three-measurement sum.

In the above example, the sum of each three-measurement window is as follows:

```
A: 607 (N/A - no previous sum)
B: 618 (increased)
C: 618 (no change)
D: 617 (decreased)
E: 647 (increased)
F: 716 (increased)
G: 769 (increased)
H: 792 (increased)
```

In this example, there are 5 sums that are larger than the previous sum.

Consider sums of a three-measurement sliding window. How many sums are larger than the previous sum?
## Approach

For this question I am going to read in the input data (data.txt) and then map it across the tiles of the ipu of column 1 of a matrix of shape {numValue, 3}.

Then I will copy the first column to the the 2nd and 3rd column and shifting the values by 1. So I will end up with the following matrix

```
A B C
B C D
C D E
D E 0
E 0 0
```

Then I will reduce the matrix to a vector by summing the rows. Then I will take that vector and create another vector offset by 1

I am then going to subtract one vector from the other and determine which values are greater than 0. Then sum the values to work out the number of increasing measurements

Using the approach above

```
Original Data : 199 200 208 210 200 207
Offset Data   : 199 199 200 208 210 200
Subtract      : 0   1   8   2   -10 7
Greater than 0: 0   1   1   1   0   1
```

The sum the `1`'s in the final vector.

Note : When offsetting the second vector, I set the first element to the same as the original data, so the result will be 0.

## To Run

1. You will need to have activate the Poplar SDK
2. Compile using `day1$ g++ --std=c++11 day1.cpp -lpoplar -lpopops -lpoputil -o day1`
3. Run `./day1`
4. To run with profiling `POPLAR_ENGINE_OPTIONS='{"autoReport.all":"true"}' ./day1`

