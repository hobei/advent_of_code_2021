# Day 3 Part 1

## Question : Binary Diagnostic

The submarine has been making some odd creaking noises, so you ask it to produce a diagnostic report just in case.

The diagnostic report (your puzzle input) consists of a list of binary numbers which, when decoded properly, can tell you many useful things about the conditions of the submarine. The first parameter to check is the power consumption.

You need to use the binary numbers in the diagnostic report to generate two new binary numbers (called the gamma rate and the epsilon rate). The power consumption can then be found by multiplying the gamma rate by the epsilon rate.

Each bit in the gamma rate can be determined by finding the most common bit in the corresponding position of all numbers in the diagnostic report. For example, given the following diagnostic report:

00100
11110
10110
10111
10101
01111
00111
11100
10000
11001
00010
01010

Considering only the first bit of each number, there are five 0 bits and seven 1 bits. Since the most common bit is 1, the first bit of the gamma rate is 1.

The most common second bit of the numbers in the diagnostic report is 0, so the second bit of the gamma rate is 0.

The most common value of the third, fourth, and fifth bits are 1, 1, and 0, respectively, and so the final three bits of the gamma rate are 110.

So, the gamma rate is the binary number 10110, or 22 in decimal.

The epsilon rate is calculated in a similar way; rather than use the most common bit, the least common bit from each position is used. So, the epsilon rate is 01001, or 9 in decimal. Multiplying the gamma rate (22) by the epsilon rate (9) produces the power consumption, 198.

Use the binary numbers in your diagnostic report to calculate the gamma rate and epsilon rate, then multiply them together. What is the power consumption of the submarine? (Be sure to represent your answer in decimal, not binary.)

## Approach

For this question we will first read the data into a 2d matrix.

Then we will convert the input data of 0's and 1's into -1's and 1's

```
00100
11110
10110
```
to
```
-1 -1  1 -1 -1
 1  1  1  1 -1
 1 -1  1  1 -1
```

We can then reduce the columns to get a positive or negative value which we can then to a comparison with 0 to get a bitmap

One we have the bit map we can multiple it by powers of two and sum them for gamma

For epsilon we need to invert the bitmap and then multiple it by powers of two and sum them

## To Run


1. You will need to have activate the Poplar SDK
2. Compile using `make`
3. Run `./out`
4. To run with profiling `POPLAR_ENGINE_OPTIONS='{"autoReport.all":"true"}' ./out`

